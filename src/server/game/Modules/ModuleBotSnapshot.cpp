#include "ModuleBotSnapshot.h"
#include "Cell.h"
#include "DB2Structure.h"
#include "CellImpl.h"
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

ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby)
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
    entry.mana = ClampToUint32(int64(bot.GetPower(POWER_MANA)));
    entry.max_mana = ClampToUint32(int64(bot.GetMaxPower(POWER_MANA)));

    FillCooldowns(bot, entry.spell_cooldowns);

    if (Unit* victim = bot.GetVictim())
        if (!EncodeUnitRef(*victim, entry.threat_target_guid))
            entry.threat_target_guid = 0;

    if (maxNearby == 0)
        return result;

    // Every unit in range, then the closest maxNearby of them that can be packed.
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, NearbyRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, NearbyRange);

    std::vector<Unit*> units;
    for (Unit* unit : inRange)
        if (unit != &bot)
            units.push_back(unit);
    std::sort(units.begin(), units.end(), [&bot](Unit* a, Unit* b)
    {
        return bot.GetExactDist2dSq(a) < bot.GetExactDist2dSq(b);
    });

    for (Unit* unit : units)
    {
        if (result.Nearby.size() >= maxNearby)
            break;

        NearbyUnit near;
        if (!EncodeUnitRef(*unit, near.guid))
            continue;
        near.pos_x = unit->GetPositionX();
        near.pos_y = unit->GetPositionY();
        near.pos_z = unit->GetPositionZ();
        near.health = ClampToUint32(unit->GetHealth());
        near.max_health = ClampToUint32(unit->GetMaxHealth());
        near.is_hostile = bot.IsHostileTo(unit);
        result.Nearby.push_back(near);
    }
    return result;
}
