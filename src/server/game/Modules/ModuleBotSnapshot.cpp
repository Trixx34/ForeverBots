#include "ModuleBotSnapshot.h"
#include "Cell.h"
#include "DB2Structure.h"
#include "CellImpl.h"
#include "CreatureData.h"
#include "Group.h"
#include "DB2Stores.h"
#include "Item.h"
#include "Log.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "SpawnData.h"
#include "SharedDefines.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <unordered_map>
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

// The bot's quest log (layout 8): the quest ids in its slots, the status, and the progress of the first objectives.
void FillQuestLog(Player& bot, forever_module::BotSnapshot& entry)
{
    entry.quest_count = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE && entry.quest_count < forever_module::kMaxQuestLog; ++slot)
    {
        uint32 const questId = bot.GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        forever_module::QuestEntry& out = entry.quests[entry.quest_count];
        out = forever_module::QuestEntry{};
        out.quest_id = questId;
        switch (bot.GetQuestStatus(questId))
        {
            case QUEST_STATUS_COMPLETE: out.status = 2; break;
            case QUEST_STATUS_FAILED: out.status = 3; break;
            default: out.status = 1; break;
        }
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        {
            size_t index = 0;
            for (QuestObjective const& objective : quest->Objectives)
            {
                if (index >= forever_module::kMaxQuestObjectives)
                    break;
                out.counts[index++] = uint16(std::clamp<int32>(bot.GetQuestObjectiveData(objective), 0, 65535));
            }
        }
        ++entry.quest_count;
    }
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

// The flags that make a unit impossible for a player to attack, whatever its faction: the ones WorldObject::IsValidAttackTarget
// rejects first. The full function is not used here: its visibility and reputation (at war) checks rejected the creatures the bots
// are meant to fight in the fourth 500-bot sim run, which saw no fights at all.
bool CannotBeAttackedByPlayers(Unit const& unit)
{
    return unit.IsImmuneToPC() || unit.IsUninteractible() ||
           unit.HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_ON_TAXI);
}

// Whether the bot may pick this unit as a target (the snapshot's is_hostile). Hostile units, and neutral creatures ("yellow"): a
// neutral creature can be attacked but does not attack first, and most starter-zone creatures are neutral, so a bot that waited for
// hostile units would never start a fight. Neutral units that are not for fighting are left out: critters, and creatures with an
// NPC role (vendor, trainer, quest giver and the like). Units a player cannot attack are never included.
bool CanBeFought(Player const& bot, Unit const& unit)
{
    if (CannotBeAttackedByPlayers(unit))
        return false;
    if (bot.IsHostileTo(&unit))
        return true;
    if (!unit.IsCreature() || bot.IsFriendlyTo(&unit))
        return false;
    Creature const* creature = unit.ToCreature();
    return creature->GetCreatureTemplate()->type != CREATURE_TYPE_CRITTER && creature->GetNpcFlags() == UNIT_NPC_FLAG_NONE;
}

// Where the vendors of each map stand: the spawn position of every creature whose template has a vendor flag. Built on the
// first call, since the spawn table is loaded at world start. Only the world thread reads it.
struct VendorSpot
{
    float X;
    float Y;
    float Z;
    bool Repair;                           // the creature repairs gear (UNIT_NPC_FLAG_REPAIR)
    FactionTemplateEntry const* Faction;   // the creature's faction, so a bot does not walk to a town that kills it
};

std::unordered_map<uint32, std::vector<VendorSpot>> const& VendorsByMap()
{
    static std::unordered_map<uint32, std::vector<VendorSpot>> vendors;
    static bool built = false;
    if (!built)
    {
        built = true;
        auto const started = std::chrono::steady_clock::now();
        size_t spawns = 0;
        for (auto const& spawnEntry : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& spawn = spawnEntry.second;
            ++spawns;
            CreatureTemplate const* templ = sObjectMgr->GetCreatureTemplate(spawn.id);
            if (!templ || (templ->npcflag & (UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_REPAIR)) == 0)
                continue;
            uint32 const factionId = templ->faction;
            vendors[spawn.mapId].push_back({ spawn.spawnPoint.GetPositionX(), spawn.spawnPoint.GetPositionY(),
                                             spawn.spawnPoint.GetPositionZ(), (templ->npcflag & UNIT_NPC_FLAG_REPAIR) != 0,
                                             sFactionTemplateStore.LookupEntry(factionId) });
        }

        // One scan of every creature spawn, inside the first snapshot: its cost is logged, since it is a stall of one world update.
        size_t vendorCount = 0;
        for (auto const& onMap : vendors)
            vendorCount += onMap.second.size();
        TC_LOG_INFO("server.worldserver", "Bots: vendor index built from {} creature spawns in {} ms: {} vendors and repairmen on {} maps.",
            spawns, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
            vendorCount, vendors.size());
    }
    return vendors;
}

// The nearest vendor to a bot on its map, looked up again only when the bot has moved this far from the last lookup.
constexpr float kVendorLookupMoveYards = 100.0f;

struct VendorLookup
{
    bool Made = false;      // a lookup has been made for this bot
    uint32 MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    bool FoundVendor = false;   // the map has a vendor friendly to the bot
    VendorSpot Vendor{};
    bool FoundRepair = false;   // ... and a repairman
    VendorSpot Repairman{};
};

// Fills the nearest-vendor fields (layout 6, M6) and the nearest-repairman fields (layout 9). The position is 2D for the distance:
// vendors sit at different heights in the same town, and the walk is on the ground. Only vendors the bot's faction is not hostile to
// count: a Horde bot must not be sent to Stormwind.
void FillNearestVendor(Player& bot, BotSnapshot& entry)
{
    static std::unordered_map<uint64, VendorLookup> lookups; // bot counter -> its last lookup
    VendorLookup& last = lookups[bot.GetGUID().GetCounter()];

    float const x = bot.GetPositionX();
    float const y = bot.GetPositionY();
    uint32 const mapId = bot.GetMapId();
    float const dx = x - last.X;
    float const dy = y - last.Y;
    bool const fresh = last.Made && last.MapId == mapId && dx * dx + dy * dy <= kVendorLookupMoveYards * kVendorLookupMoveYards;
    if (!fresh)
    {
        last = VendorLookup{};
        last.Made = true;
        last.MapId = mapId;
        last.X = x;
        last.Y = y;
        FactionTemplateEntry const* mine = bot.GetFactionTemplateEntry();
        auto const& vendors = VendorsByMap();
        auto const onMap = vendors.find(mapId);
        if (onMap != vendors.end())
        {
            float bestVendorSq = std::numeric_limits<float>::max();
            float bestRepairSq = std::numeric_limits<float>::max();
            for (VendorSpot const& spot : onMap->second)
            {
                if (mine && spot.Faction && mine->IsHostileTo(spot.Faction))
                    continue;
                float const sx = spot.X - x;
                float const sy = spot.Y - y;
                float const distSq = sx * sx + sy * sy;
                if (!spot.Repair && distSq < bestVendorSq)
                {
                    bestVendorSq = distSq;
                    last.Vendor = spot;
                    last.FoundVendor = true;
                }
                if (spot.Repair && distSq < bestRepairSq)
                {
                    bestRepairSq = distSq;
                    last.Repairman = spot;
                    last.FoundRepair = true;
                }
            }
        }
    }

    if (last.FoundVendor)
    {
        entry.vendor_x = last.Vendor.X;
        entry.vendor_y = last.Vendor.Y;
        entry.vendor_z = last.Vendor.Z;
        entry.vendor_valid = 1;
    }
    if (last.FoundRepair)
    {
        entry.repair_x = last.Repairman.X;
        entry.repair_y = last.Repairman.Y;
        entry.repair_z = last.Repairman.Z;
        entry.repair_valid = 1;
    }
}

// The lowest durability among the equipped items that wear out, in percent (100 when none does).
uint8 LowestDurabilityPct(Player& bot)
{
    uint32 lowest = 100;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        uint32 const maxDurability = *item->m_itemData->MaxDurability;
        if (!maxDurability)
            continue;
        lowest = std::min<uint32>(lowest, uint32(uint64(*item->m_itemData->Durability) * 100 / maxDurability));
    }
    return uint8(lowest);
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
    entry.map_id = bot.GetMapId(); // layout 7: the map, for a module's hubs (M7)

    FillCooldowns(bot, entry.spell_cooldowns);
    FillQuestLog(bot, entry);

    entry.in_combat = bot.IsInCombat() ? 1 : 0;
    if (Unit* victim = bot.GetVictim())
    {
        if (!EncodeUnitRef(*victim, entry.threat_target_guid))
            entry.threat_target_guid = 0;
        entry.target_distance = bot.GetDistance(victim);
    }

    FillParty(bot, entry);
    FillNearestVendor(bot, entry);
    entry.durability_pct = LowestDurabilityPct(bot);

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
        // is_hostile means the bot may fight it: hostile by faction or a neutral creature, and not one a player cannot attack (a
        // trainer immune to player characters is hostile by faction, but a bot that picked it would never land a hit).
        near.is_hostile = CanBeFought(bot, *unit);
        near.level = unit->GetLevel();
        near.elite = unit->IsCreature() && unit->ToCreature()->IsElite() ? 1 : 0;
        near.casting = unit->IsNonMeleeSpellCast(false) ? 1 : 0;
        result.Nearby.push_back(near);
    }

    cachedNearby = result.Nearby;
    return result;
}

bool BotMayFight(Player const& bot, Unit const& unit)
{
    return CanBeFought(bot, unit);
}
