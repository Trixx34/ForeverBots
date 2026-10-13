#include "ModuleBotSnapshot.h"
#include "Cell.h"
#include "DB2Structure.h"
#include "CellImpl.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <limits>
#include <vector>

using forever_module::BotSnapshot;
using forever_module::NearbyUnit;
using forever_module::SpellCooldown;

namespace
{

// How far around a bot the snapshot looks for units, in yards.
constexpr float NearbyRange = 40.0f;

uint32 ClampToUint32(uint64 value)
{
    return static_cast<uint32>(std::min<uint64>(value, std::numeric_limits<uint32>::max()));
}

uint32 ClampToUint32(int64 value)
{
    return value <= 0 ? 0 : ClampToUint32(static_cast<uint64>(value));
}

// The bot's active cooldowns, closest to expiring first, up to the slots in the snapshot. Each is a known spell
// (not removed) with a remaining cooldown above zero.
void FillCooldowns(Player& bot, SpellCooldown (&slots)[forever_module::kMaxTrackedCooldowns])
{
    struct Active
    {
        uint32 SpellId;
        uint32 Remaining;
    };

    std::vector<Active> active;
    SpellHistory const* history = bot.GetSpellHistory();
    for (auto const& [spellId, spell] : bot.GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info)
            continue;

        auto const remaining = history->GetRemainingCooldown(info).count();
        if (remaining <= 0)
            continue;
        active.push_back({ spellId, uint32(std::min<int64>(remaining, std::numeric_limits<uint32>::max())) });
    }

    std::sort(active.begin(), active.end(), [](Active const& a, Active const& b) { return a.Remaining < b.Remaining; });
    for (std::size_t i = 0; i < active.size() && i < forever_module::kMaxTrackedCooldowns; ++i)
        slots[i] = { active[i].SpellId, active[i].Remaining };
}

// A player's unit reference, for a party leader. The leader need not be in the bot's view for the reference to be valid.
uint64 PlayerRef(ObjectGuid guid)
{
    if (guid.GetCounter() > forever_module::kUnitRefCounterMask)
        return 0;
    return forever_module::MakeUnitRef(uint8(HighGuid::Player), 0, guid.GetCounter());
}

// The bot's party and any invite it has not answered (layout 5, M8).
void FillParty(Player& bot, BotSnapshot& entry)
{
    if (Group* group = bot.GetGroup())
    {
        entry.group_role = group->IsLeader(bot.GetGUID()) ? 2 : 1;
        entry.group_size = group->GetMembersCount();
        entry.group_leader_guid = PlayerRef(group->GetLeaderGUID());
    }
    if (Group* invite = bot.GetGroupInvite())
        entry.group_invite_from = PlayerRef(invite->GetLeaderGUID());
}

} // namespace

bool EncodeUnitRef(Unit const& unit, uint64& ref)
{
    ObjectGuid const guid = unit.GetGUID();
    ObjectGuid::LowType const counter = guid.GetCounter();
    if (counter > forever_module::kUnitRefCounterMask)
        return false;

    switch (guid.GetHigh())
    {
        case HighGuid::Player:
            ref = forever_module::MakeUnitRef(uint8(HighGuid::Player), 0, counter);
            return true;

        case HighGuid::Creature:
            if (guid.GetEntry() > forever_module::kUnitRefEntryMask)
                return false;
            ref = forever_module::MakeUnitRef(uint8(HighGuid::Creature), guid.GetEntry(), counter);
            return true;

        default:
            return false;
    }
}

ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby, bool refreshNearby, std::vector<NearbyUnit>& cachedNearby)
{
    ModuleSnapshotBot result;

    BotSnapshot& entry = result.Bot;
    entry.guid = bot.GetGUID().GetCounter();
    entry.pos_x = bot.GetPositionX();
    entry.pos_y = bot.GetPositionY();
    entry.pos_z = bot.GetPositionZ();
    entry.orientation = bot.GetOrientation();
    entry.health = ClampToUint32(bot.GetHealth());
    entry.max_health = ClampToUint32(bot.GetMaxHealth());
    // The main resource of the bot's class: mana, rage or energy (layout 4).
    Powers const powerType = bot.GetPowerType();
    entry.power_type = uint8(powerType);
    entry.power = ClampToUint32(int64(bot.GetPower(powerType)));
    entry.max_power = ClampToUint32(int64(bot.GetMaxPower(powerType)));
    entry.level = bot.GetLevel();

    FillCooldowns(bot, entry.spell_cooldowns);

    entry.in_combat = bot.IsInCombat() ? 1 : 0;
    if (Unit* victim = bot.GetVictim())
    {
        if (!EncodeUnitRef(*victim, entry.threat_target_guid))
            entry.threat_target_guid = 0;
        entry.target_distance = bot.GetDistance(victim);
    }

    FillParty(bot, entry);

    if (maxNearby == 0)
        return result;

    // Between refreshes the bot's own entry is current, and its nearby units are the ones from the last refresh.
    if (!refreshNearby && !cachedNearby.empty())
    {
        result.Nearby = cachedNearby;
        if (result.Nearby.size() > maxNearby)
            result.Nearby.resize(maxNearby);
        return result;
    }

    // Every unit in range, split into players and creatures, each closest first.
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, NearbyRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, NearbyRange);

    std::vector<Unit*> players;
    std::vector<Unit*> creatures;
    for (Unit* unit : inRange)
    {
        if (unit == &bot)
            continue;
        (unit->IsPlayer() ? players : creatures).push_back(unit);
    }
    auto closerFirst = [&bot](Unit* a, Unit* b)
    {
        return bot.GetExactDist2dSq(a) < bot.GetExactDist2dSq(b);
    };
    std::sort(players.begin(), players.end(), closerFirst);
    std::sort(creatures.begin(), creatures.end(), closerFirst);

    // The closest few players, then creatures, then any players that are left if there is still room.
    std::vector<Unit*> chosen;
    size_t const playerSlots = std::min<size_t>(kNearbyPlayerSlots, maxNearby);
    for (Unit* player : players)
    {
        if (chosen.size() >= playerSlots)
            break;
        chosen.push_back(player);
    }
    size_t const leadingPlayers = chosen.size();
    for (Unit* creature : creatures)
    {
        if (chosen.size() >= maxNearby)
            break;
        chosen.push_back(creature);
    }
    for (size_t i = leadingPlayers; i < players.size() && chosen.size() < maxNearby; ++i)
        chosen.push_back(players[i]);
    std::sort(chosen.begin(), chosen.end(), closerFirst);

    for (Unit* unit : chosen)
    {
        NearbyUnit near;
        if (!EncodeUnitRef(*unit, near.guid))
            continue;
        near.pos_x = unit->GetPositionX();
        near.pos_y = unit->GetPositionY();
        near.pos_z = unit->GetPositionZ();
        near.health = ClampToUint32(unit->GetHealth());
        near.max_health = ClampToUint32(unit->GetMaxHealth());
        near.is_hostile = bot.IsHostileTo(unit);
        near.level = unit->GetLevel();
        near.elite = unit->IsCreature() && unit->ToCreature()->IsElite() ? 1 : 0;
        near.casting = unit->IsNonMeleeSpellCast(false) ? 1 : 0;
        result.Nearby.push_back(near);
    }

    cachedNearby = result.Nearby;
    return result;
}
