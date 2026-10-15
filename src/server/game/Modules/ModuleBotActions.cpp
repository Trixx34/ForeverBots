#include "ModuleBotActions.h"
#include "BotGearRules.h"
#include "ModuleBotSnapshot.h"
#include <cmath>
#include "BotMgr.h"
#include "Cell.h"
#include "Corpse.h"
#include "GameObject.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "MiscPackets.h"
#include "MapManager.h"
#include "Config.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DB2Structure.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <list>
#include <cctype>
#include <limits>
#include <map>
#include <vector>

using forever_module::ActionType;
using forever_module::BotAction;
using forever_module::BotActionStatus;

namespace
{

// Resolves a unit reference (forever_module::MakeUnitRef) on the bot's map. Players are found anywhere online.
// Creatures are found only on the bot's map, since a creature guid is meaningless elsewhere.
Unit* ResolveUnit(Player& bot, uint64 ref)
{
    switch (static_cast<HighGuid>(forever_module::UnitRefType(ref)))
    {
        case HighGuid::Player:
            return ObjectAccessor::FindPlayerByLowGUID(forever_module::UnitRefCounter(ref));

        case HighGuid::Creature:
        {
            Map* map = bot.GetMap();
            if (!map)
                return nullptr;
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Creature>(uint16(map->GetId()),
                                                                          forever_module::UnitRefEntry(ref),
                                                                          forever_module::UnitRefCounter(ref));
            return map->GetCreature(guid);
        }

        case HighGuid::GameObject:
        {
            Map* map = bot.GetMap();
            if (!map)
                return nullptr;
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::GameObject>(forever_module::UnitRefEntry(ref),
                                                                           forever_module::UnitRefCounter(ref));
            return map->GetGameObject(guid);
        }

        default:
            return nullptr;
    }
}

// A target the bot may attack: alive, in the world, and hostile to the bot.
Unit* ResolveHostileTarget(Player& bot, uint64 ref, std::string& reason)
{
    Unit* target = ResolveUnit(bot, ref);
    if (!target || target == &bot || !target->IsInWorld() || !target->IsAlive())
    {
        reason = "target is not a live unit on this bot's map";
        return nullptr;
    }
    if (!BotMayFight(bot, *target))
    {
        reason = "target is not one the bot may attack (friendly, a critter, an NPC, or not attackable)";
        return nullptr;
    }
    return target;
}

// Why Unit::Attack refused an attack, for the log. The checks follow the ones Unit::Attack makes before it starts an attack.
std::string AttackRefusalReason(Player& bot, Unit* target)
{
    if (!bot.IsAlive())
        return "bot is dead";
    if (!target->IsInWorld() || !target->IsAlive())
        return "target is dead or not in the world";
    if (bot.IsMounted())
        return "bot is mounted";
    if (target->IsPlayer() && target->ToPlayer()->IsGameMaster())
        return "target is a game master";
    if (target->IsCreature() && target->ToCreature()->IsEvadingAttacks())
        return "target is evading attacks";
    return "the core refused the attack for no known reason";
}

// Maps a cast result to the status a module sees in the next snapshot.
BotActionStatus StatusForCast(SpellCastResult result)
{
    switch (result)
    {
        case SPELL_CAST_OK:
            return BotActionStatus::kApplied;
        case SPELL_FAILED_OUT_OF_RANGE:
            return BotActionStatus::kOutOfRange;
        case SPELL_FAILED_NOT_READY:
            return BotActionStatus::kOnCooldown;
        case SPELL_FAILED_NO_POWER:
            return BotActionStatus::kNotEnoughPower;
        case SPELL_FAILED_LINE_OF_SIGHT:
            return BotActionStatus::kNoLineOfSight;
        case SPELL_FAILED_BAD_TARGETS:
        case SPELL_FAILED_BAD_IMPLICIT_TARGETS:
            return BotActionStatus::kInvalidTarget;
        case SPELL_FAILED_NOT_KNOWN:
            return BotActionStatus::kNotKnown;
        case SPELL_FAILED_MOVING:
            return BotActionStatus::kMoving;
        case SPELL_FAILED_SPELL_IN_PROGRESS:
            return BotActionStatus::kBusy;
        default:
            return BotActionStatus::kRefused;
    }
}

} // namespace

// The highest rank of a spell the bot knows, walking the rank chain from the id the module sent (its rank 1). 0 if the bot
// knows no rank of it. A lower rank that was replaced by a higher one is still in the chain, so this finds the higher one.
uint32 HighestKnownRank(Player& bot, uint32 spellId)
{
    uint32 best = bot.HasSpell(spellId) ? spellId : 0;
    uint32 id = spellId;
    for (uint32 guard = 0; guard < 16; ++guard)
    {
        uint32 const next = sSpellMgr->GetNextSpellInChain(id);
        if (!next)
            break;
        if (bot.HasSpell(next))
            best = next;
        id = next;
    }
    return best;
}

// Services at NPCs (M3, M4). The NPC must be within interaction range, the distance the core's own NPC checks use, so the
// bot only looks at NPCs the core would serve. Walking to a service is travel (M6), not done here.
constexpr uint32 kServiceSellBit = 1;   // spell_id bit: sell grey items to a vendor
constexpr uint32 kServiceRepairBit = 2; // spell_id bit: repair at a repairman
constexpr uint32 kServiceEquipBit = 4;  // spell_id bit: equip the best gear in the bags
constexpr uint32 kQualityPoor = 0;      // grey
constexpr uint32 kQualityCommon = 1;    // white: the lowest quality the bot equips

// The closest NPC in interaction range that has one of `flags` and that the bot can interact with, or nullptr.
Creature* NearestServiceNpc(Player& bot, NPCFlags flags)
{
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, INTERACTION_DISTANCE);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, INTERACTION_DISTANCE);

    Creature* best = nullptr;
    float bestDist = INTERACTION_DISTANCE;
    for (Unit* unit : inRange)
    {
        Creature* npc = unit->ToCreature();
        if (!npc || !npc->HasNpcFlag(flags))
            continue;
        float const dist = bot.GetDistance(npc);
        if (dist >= bestDist || !bot.GetNPCIfCanInteractWith(npc->GetGUID(), flags, UNIT_NPC_FLAG_2_NONE))
            continue;
        best = npc;
        bestDist = dist;
    }
    return best;
}

// Sells every grey item in the bags to the vendor. Returns how many stacks were sold.
uint32 SellGreyItems(Player& bot)
{
    // Collected first: selling moves the items out of the bags, which the search must not be walking while it does.
    std::vector<Item*> grey;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&grey](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && proto->GetQuality() == kQualityPoor && proto->GetSellPrice() > 0)
            grey.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });

    uint32 sold = 0;
    for (Item* item : grey)
        if (!bot.SellItemToVendor(item, item->GetCount())) // no value means it was sold
            ++sold;
    return sold;
}

// The equipment slot an item of this inventory type goes in (the first of a pair, for rings and trinkets), or NULL_SLOT.
uint8 EquipSlotFor(InventoryType type)
{
    switch (type)
    {
        case INVTYPE_HEAD: return EQUIPMENT_SLOT_HEAD;
        case INVTYPE_NECK: return EQUIPMENT_SLOT_NECK;
        case INVTYPE_SHOULDERS: return EQUIPMENT_SLOT_SHOULDERS;
        case INVTYPE_BODY: return EQUIPMENT_SLOT_BODY;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: return EQUIPMENT_SLOT_CHEST;
        case INVTYPE_WAIST: return EQUIPMENT_SLOT_WAIST;
        case INVTYPE_LEGS: return EQUIPMENT_SLOT_LEGS;
        case INVTYPE_FEET: return EQUIPMENT_SLOT_FEET;
        case INVTYPE_WRISTS: return EQUIPMENT_SLOT_WRISTS;
        case INVTYPE_HANDS: return EQUIPMENT_SLOT_HANDS;
        case INVTYPE_FINGER: return EQUIPMENT_SLOT_FINGER1;
        case INVTYPE_TRINKET: return EQUIPMENT_SLOT_TRINKET1;
        case INVTYPE_CLOAK: return EQUIPMENT_SLOT_BACK;
        case INVTYPE_WEAPON:
        case INVTYPE_2HWEAPON:
        case INVTYPE_WEAPONMAINHAND: return EQUIPMENT_SLOT_MAINHAND;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_HOLDABLE: return EQUIPMENT_SLOT_OFFHAND;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN: return EQUIPMENT_SLOT_RANGED;
        default: return NULL_SLOT;
    }
}

// Equips each bag item that is a weapon or armour piece better than what the slot holds now. Returns how many were equipped.
uint32 EquipBestGear(Player& bot)
{
    std::vector<Item*> candidates;
    bot.ForEachItem(ItemSearchLocation::Inventory, [&candidates](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && proto->GetQuality() >= kQualityCommon &&
            (proto->GetClass() == ITEM_CLASS_WEAPON || proto->GetClass() == ITEM_CLASS_ARMOR) &&
            EquipSlotFor(proto->GetInventoryType()) != NULL_SLOT)
            candidates.push_back(item);
        return ItemSearchCallbackResult::Continue;
    });

    uint32 equipped = 0;
    for (Item* item : candidates)
    {
        // Checked against the slot as it is now: an earlier swap in this pass may have changed it.
        uint8 const slot = EquipSlotFor(item->GetTemplate()->GetInventoryType());
        Item* current = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (current && current->GetTemplate()->GetBaseItemLevel() >= item->GetTemplate()->GetBaseItemLevel())
            continue;

        uint16 const source = uint16(item->GetBagSlot()) << 8 | item->GetSlot();
        uint16 const target = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
        uint16 destination = target;
        if (bot.CanEquipItem(slot, destination, item, true) != EQUIP_ERR_OK)
            continue;

        bot.SwapItem(source, target);
        if (bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item)
            ++equipped;
    }
    return equipped;
}

// ---- kAutoGear (scenarios, doc 19): dress a bot for its level ----

// The gear the core may hand out: weapons and armour of common to Bot.AutoGear.MaxQuality (default 2, uncommon), with a slot, no
// quest-bound items and no unfinished-looking names (test, deprecated or placeholder items). Built once.
struct GearPoolStats
{
    uint32 WeaponsAndArmor = 0, QualityTooLow = 0, QualityOverCap = 0, QuestBound = 0, NoSlot = 0, Placeholder = 0, Kept = 0;
};

std::vector<ItemTemplate const*> const& GearCandidates()
{
    static std::vector<ItemTemplate const*> const candidates = []
    {
        std::vector<ItemTemplate const*> out;
        GearPoolStats stats;
        uint32 const maxQuality = uint32(std::max(1, sConfigMgr->GetIntDefault("Bot.AutoGear.MaxQuality", 2)));
        for (auto const& [id, proto] : sObjectMgr->GetItemTemplateStore())
        {
            if (proto.GetClass() != ITEM_CLASS_WEAPON && proto.GetClass() != ITEM_CLASS_ARMOR)
                continue;
            ++stats.WeaponsAndArmor;
            if (proto.GetQuality() < kQualityCommon)
            {
                ++stats.QualityTooLow;
                continue;
            }
            if (proto.GetQuality() > maxQuality)
            {
                ++stats.QualityOverCap;
                continue;
            }
            if (proto.GetBonding() == BIND_QUEST)
            {
                ++stats.QuestBound;
                continue;
            }
            if (proto.GetInventoryType() == INVTYPE_BODY || EquipSlotFor(proto.GetInventoryType()) == NULL_SLOT)
            {
                ++stats.NoSlot;
                continue;
            }
            std::string name = proto.GetName(DEFAULT_LOCALE) ? proto.GetName(DEFAULT_LOCALE) : "";
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.empty() || name.find("deprecated") != std::string::npos || name.find("test") == 0 ||
                name.find("unused") != std::string::npos || name.find("[ph]") != std::string::npos || name.rfind("zz", 0) == 0)
            {
                ++stats.Placeholder;
                continue;
            }
            out.push_back(&proto);
        }
        stats.Kept = uint32(out.size());
        TC_LOG_INFO("server.worldserver",
            "BotMgr: autogear pool maxquality={} weapons_armor={} quality_below_common={} quality_over_cap={} quest_bound={} no_slot={} placeholder={} candidates={}",
            maxQuality, stats.WeaponsAndArmor, stats.QualityTooLow, stats.QualityOverCap, stats.QuestBound, stats.NoSlot, stats.Placeholder,
            stats.Kept);
        return out;
    }();
    return candidates;
}

// True for armour in the body slots that is heavier than the class wears (rules in BotGearRules.h).
bool ArmorTooHeavy(Player const& bot, ItemTemplate const& item)
{
    return BotGearRules::ArmorTooHeavy(bot.GetClass(), bot.GetLevel(), item.GetClass(), item.GetSubClass(), item.GetInventoryType());
}

// What a class wants from its gear, as item-mod stats: the stats it uses, in no order.
std::vector<int32> PreferredStats(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN: return { ITEM_MOD_STRENGTH, ITEM_MOD_STAMINA };
        case CLASS_HUNTER: return { ITEM_MOD_AGILITY, ITEM_MOD_STAMINA };
        case CLASS_ROGUE: return { ITEM_MOD_AGILITY, ITEM_MOD_STRENGTH, ITEM_MOD_STAMINA };
        default: return { ITEM_MOD_INTELLECT, ITEM_MOD_SPIRIT, ITEM_MOD_STAMINA }; // priest, shaman, mage, warlock, druid
    }
}

// Higher is better: the item level first, then how well its stats suit the class (a stat the class does not use counts against it).
int32 GearScore(std::vector<int32> const& preferred, ItemTemplate const& item)
{
    int32 stats = 0;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 const stat = item.GetStatModifierBonusStat(i);
        int32 const amount = item.GetStatPercentEditor(i);
        if (amount <= 0)
            continue;
        bool const wanted = std::find(preferred.begin(), preferred.end(), stat) != preferred.end();
        bool const primary = stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPIRIT;
        stats += wanted ? amount : (primary ? -3 * amount : 0);
    }
    return int32(item.GetBaseItemLevel()) * 10 + stats;
}

struct GearPick
{
    ItemTemplate const* Item = nullptr;
    int32 Score = std::numeric_limits<int32>::min();
};

void Consider(GearPick& pick, ItemTemplate const& item, int32 score)
{
    if (!pick.Item || score > pick.Score)
        pick = { &item, score };
}

enum class GearResult { Equipped, KeptBetter, NoBagSpace, CannotEquip };

char const* GearResultName(GearResult result)
{
    switch (result)
    {
        case GearResult::Equipped: return "equipped";
        case GearResult::KeptBetter: return "kept_worn_item_level_not_lower";
        case GearResult::NoBagSpace: return "no_bag_space";
        case GearResult::CannotEquip: return "cannot_equip_removed";
    }
    return "unknown";
}

// Human-readable name for an InventoryResult (equip error code).
char const* InventoryResultName(uint32 code)
{
    switch (static_cast<InventoryResult>(code))
    {
        case EQUIP_ERR_OK: return "OK";
        case EQUIP_ERR_INV_FULL: return "inventory_full";
        case EQUIP_ERR_CANT_EQUIP: return "cannot_equip";
        case EQUIP_ERR_CANT_BE_EQUIPPED: return "cannot_be_equipped";
        case EQUIP_ERR_ITEM_DOESNT_GO_INTO_SLOT: return "wrong_slot_type";
        case EQUIP_ERR_WRONG_SLOT: return "wrong_slot";
        case EQUIP_ERR_NOT_EQUIPABLE: return "not_equippable";
        case EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER: return "bag_over_other_bag";
        case EQUIP_ERR_NOT_IN_COMBAT: return "not_in_combat";
        case EQUIP_ERR_NO_EQUIP_LEVEL: return "level_too_low";
        case EQUIP_ERR_WRONG_WEATHER: return "wrong_weather";
        case EQUIP_ERR_CANT_CARRY_MORE_OF_THIS: return "unique_limit";
        case EQUIP_ERR_CANT_DUAL_WIELD: return "cannot_dual_wield";
        case EQUIP_ERR_RACE_CANT_EQUIP: return "race_cannot_equip";
        case EQUIP_ERR_CANT_BE_EQUIPPED_HERE: return "cannot_equip_here";
        case EQUIP_ERR_NOT_DURING_ARENA_MATCH: return "not_during_arena";
        case EQUIP_ERR_ITEM_BOUND: return "item_bound";
        case EQUIP_ERR_RESTRICTED_PVP_ITEM: return "restricted_pvp";
        case EQUIP_ERR_ITEM_UNIQUE: return "unique_item";
        case EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE: return "unique_equippable";
        case EQUIP_ERR_ITEM_UNIQUE_OWNER: return "unique_owner";
        case EQUIP_ERR_ITEM_LIMIT_CATEGORY_EXCEEDED: return "limit_category_exceeded";
        case EQUIP_ERR_ITEM_LEVEL_TOO_LOW: return "item_level_too_low";
        case EQUIP_ERR_ITEM_LEVEL_TOO_HIGH: return "item_level_too_high";
        case EQUIP_ERR_TOO_FEW_TO_SPLIT: return "too_few_to_split";
        case EQUIP_ERR_ITEM_MAX_COUNT_EXCEEDED: return "max_count_exceeded";
        case EQUIP_ERR_NOT_A_BAG: return "not_a_bag";
        case EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE: return "only_ammo_here";
        case EQUIP_ERR_CANT_STACK: return "cannot_stack";
        case EQUIP_ERR_CANT_SWAP: return "cannot_swap";
        case EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG: return "doesnt_go_into_bag";
        case EQUIP_ERR_BAG_FULL: return "bag_full";
        case EQUIP_ERR_LOOT_CANT_LOOT_THAT: return "cannot_loot_that";
        case EQUIP_ERR_LOOT_DIDNT_KILL: return "didnt_kill";
        case EQUIP_ERR_LOOT_MASTER_INST: return "master_loot";
        case EQUIP_ERR_LOOT_PLAYER: return "player_loot";
        case EQUIP_ERR_LOOT_FREE_FOR_ALL: return "free_for_all_loot";
        case EQUIP_ERR_LOOT_ROLL: return "loot_roll";
        case EQUIP_ERR_LOOT_FFA_ROLL: return "ffa_roll";
        case EQUIP_ERR_LOOT_FFA_ROLL_ANOTHER: return "ffa_roll_another";
        case EQUIP_ERR_LOOT_MASTER_INST_ANOTHER: return "master_loot_another";
        case EQUIP_ERR_LOOT_PLAYER_ANOTHER: return "player_loot_another";
        case EQUIP_ERR_LOOT_ROLL_ANOTHER: return "roll_another";
        case EQUIP_ERR_LOOT_ROLL_NEED: return "roll_need";
        case EQUIP_ERR_LOOT_ROLL_GREED: return "roll_greed";
        case EQUIP_ERR_LOOT_ROLL_ANOTHER_NEED: return "roll_another_need";
        case EQUIP_ERR_LOOT_ROLL_ANOTHER_GREED: return "roll_another_greed";
        case EQUIP_ERR_LOOT_ROLL_ANOTHER_NEED_GREED: return "roll_another_need_greed";
        case EQUIP_ERR_LOOT_MASTER_INST_ANOTHER_NEED: return "master_loot_another_need";
        case EQUIP_ERR_LOOT_PLAYER_ANOTHER_NEED: return "player_loot_another_need";
        case EQUIP_ERR_LOOT_ROLL_ANOTHER_NEED_GREED: return "roll_another_need_greed";
        case EQUIP_ERR_CANT_SPLIT: return "cannot_split";
        case EQUIP_ERR_AUTO_EQUIP_ITEM_SLOT: return "auto_equip_slot";
        case EQUIP_ERR_LEGACY_RAID: return "legacy_raid";
        case EQUIP_ERR_CANT_USE_ITEM: return "cannot_use_item";
        case EQUIP_ERR_ITEM_MAX_COUNT_SOCKETED: return "max_socketed_exceeded";
        case EQUIP_ERR_MUST_BE_IN_COMBAT: return "must_be_in_combat";
        case EQUIP_ERR_CANT_EQUIP_NEED_TALENT: return "need_talent";
        case EQUIP_ERR_CANT_EQUIP_NEED_SKILL: return "need_skill";
        case EQUIP_ERR_CANT_EQUIP_NEED_REPUTATION: return "need_reputation";
        case EQUIP_ERR_CANT_EQUIP_NEED_ARENA_RATING: return "need_arena_rating";
        case EQUIP_ERR_ITEM_NOT_FOR_YOUR_RACE: return "race_mismatch";
        case EQUIP_ERR_ITEM_REQUIRES_PROFESSION: return "requires_profession";
        case EQUIP_ERR_ITEM_REQUIRES_LEVEL: return "requires_level";
        case EQUIP_ERR_CANT_EQUIP_CLASSIC: return "classic_cannot_equip";
        case EQUIP_ERR_CANT_EQUIP_ITEM_SLOT: return "item_slot_mismatch";
        case EQUIP_ERR_CANT_EQUIP_RANK: return "rank_too_low";
        case EQUIP_ERR_ITEM_MOD_APPEARANCE_GROUP_LIMITED: return "appearance_limit";
        case EQUIP_ERR_ITEM_MAX_COUNT_EQUIPPED_SOCKETED: return "max_equipped_socketed";
        default: return "unknown(" + std::to_string(code) + ")";
    }
}

// Creates the item in the bags and moves it into `slot`, unless the slot already holds an item of at least this item level.
GearResult GiveAndEquip(Player& bot, ItemTemplate const& item, uint8 slot, uint32& equipError)
{
    equipError = 0;
    Item* current = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (current && current->GetTemplate()->GetBaseItemLevel() >= item.GetBaseItemLevel())
        return GearResult::KeptBetter;

    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item.GetId(), 1) != EQUIP_ERR_OK)
        return GearResult::NoBagSpace;
    Item* created = bot.StoreNewItem(destination, item.GetId(), true);
    if (!created)
        return GearResult::NoBagSpace;

    uint16 target = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
    uint16 equipDestination = target;
    InventoryResult const canEquip = bot.CanEquipItem(slot, equipDestination, created, true);
    if (canEquip != EQUIP_ERR_OK)
    {
        equipError = uint32(canEquip);
        // Not left in the bags: the same pick comes back at every gear pass and would fill the bags with copies.
        bot.DestroyItem(created->GetBagSlot(), created->GetSlot(), true);
        return GearResult::CannotEquip;
    }
    bot.SwapItem(uint16(created->GetBagSlot()) << 8 | created->GetSlot(), target);
    return bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == created ? GearResult::Equipped : GearResult::CannotEquip;
}

// spell_id bit 1: a tank (one-hand weapon and shield).
BotActionStatus AutoGearAction(Player& bot, uint32 flags, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.AutoGear", true);
    if (!allowed)
    {
        reason = "Bot.Actions.AutoGear is off";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (sBotMgr->IsActiveAlt(bot.GetGUID().GetCounter()))
    {
        reason = "an alt is a player's own character and is not dressed";
        return BotActionStatus::kRefused;
    }

    bool const tank = (flags & 1) != 0;
    std::vector<int32> const preferred = PreferredStats(bot.GetClass());
    std::map<uint8, GearPick> bySlot;          // armour and ranged, by equipment slot
    GearPick twoHand, shield, holdable;
    std::vector<std::pair<int32, ItemTemplate const*>> oneHand;   // main-hand weapons (one-hand, main hand)
    std::vector<std::pair<int32, ItemTemplate const*>> offHand;    // off-hand weapons (off hand only, and one-hand for dual wield)

    uint32 const candidates = uint32(GearCandidates().size());
    uint32 levelTooHigh = 0, cannotUse = 0;
    for (ItemTemplate const* item : GearCandidates())
    {
        if (item->GetBaseRequiredLevel() > int32(bot.GetLevel()))
        {
            ++levelTooHigh;
            continue;
        }
        if (bot.CanUseItem(item) != EQUIP_ERR_OK || ArmorTooHeavy(bot, *item))
        {
            ++cannotUse;
            continue;
        }
        int32 const score = GearScore(preferred, *item);
        switch (item->GetInventoryType())
        {
            case INVTYPE_WEAPON:
                oneHand.emplace_back(score, item);
                offHand.emplace_back(score, item);
                break;
            case INVTYPE_WEAPONMAINHAND: oneHand.emplace_back(score, item); break;
            case INVTYPE_WEAPONOFFHAND: offHand.emplace_back(score, item); break;
            case INVTYPE_2HWEAPON: Consider(twoHand, *item, score); break;
            case INVTYPE_SHIELD: Consider(shield, *item, score); break;
            case INVTYPE_HOLDABLE: Consider(holdable, *item, score); break;
            default: Consider(bySlot[EquipSlotFor(item->GetInventoryType())], *item, score); break;
        }
    }

    auto const best = [](std::vector<std::pair<int32, ItemTemplate const*>> const& list, ItemTemplate const* not_this) -> GearPick
    {
        GearPick pick;
        for (auto const& [score, item] : list)
            if (item != not_this)
                Consider(pick, *item, score);
        return pick;
    };

    // The weapons: a tank takes one hand and a shield; a dual-wielder two one-handers; otherwise a two-hander when it is at least as
    // good as the best one-hander, else one hand and a shield or a held item.
    GearPick main, off;
    GearPick const mainOne = best(oneHand, nullptr);
    if (tank && mainOne.Item && shield.Item)
    {
        main = mainOne;
        off = shield;
    }
    else if (bot.CanDualWield() && mainOne.Item && best(offHand, mainOne.Item).Item)
    {
        main = mainOne;
        off = best(offHand, mainOne.Item);
    }
    else if (twoHand.Item && (!mainOne.Item || twoHand.Score >= mainOne.Score))
        main = twoHand;
    else
    {
        main = mainOne;
        off = shield.Item ? shield : holdable;
    }

    uint32 equipped = 0, keptBetter = 0, failed = 0, tried = 0;
    auto const apply = [&](GearPick const& pick, uint8 slot)
    {
        ++tried;
        uint32 equipError = 0;
        GearResult const result = GiveAndEquip(bot, *pick.Item, slot, equipError);
        equipped += result == GearResult::Equipped;
        keptBetter += result == GearResult::KeptBetter;
        failed += result == GearResult::NoBagSpace || result == GearResult::CannotEquip;
        std::string failureDetail;
        if (result == GearResult::CannotEquip)
        {
            failureDetail = " equip_error=" + std::to_string(equipError) + " (" + InventoryResultName(equipError) + ")";
        }
        else if (result == GearResult::NoBagSpace)
        {
            failureDetail = " no_bag_space";
        }
        else if (result == GearResult::KeptBetter)
        {
            failureDetail = " kept_better_item";
        }
        TC_LOG_INFO("server.worldserver", "BotMgr: autogear pick bot={} slot={} item={} quality={} ilvl={} reqlevel={} armor_subclass={} result={}{}", bot.GetName(), uint32(slot),
            pick.Item->GetId(), uint32(pick.Item->GetQuality()), pick.Item->GetBaseItemLevel(), pick.Item->GetBaseRequiredLevel(),
            pick.Item->GetClass() == ITEM_CLASS_ARMOR ? int32(pick.Item->GetSubClass()) : -1, GearResultName(result), failureDetail);
    };
    if (main.Item)
        apply(main, EQUIPMENT_SLOT_MAINHAND);
    if (off.Item && (!main.Item || main.Item->GetInventoryType() != INVTYPE_2HWEAPON))
        apply(off, EQUIPMENT_SLOT_OFFHAND);
    for (auto const& [slot, pick] : bySlot)
        if (pick.Item)
            apply(pick, slot);

    // What the bot wears now, from the loaded item template store, each against the quality cap.
    uint32 const maxQuality = uint32(std::max(1, sConfigMgr->GetIntDefault("Bot.AutoGear.MaxQuality", 2)));
    uint32 wornItems = 0, overCap = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* worn = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!worn)
            continue;
        ItemTemplate const* proto = worn->GetTemplate();
        ++wornItems;
        bool const withinCap = proto->GetQuality() <= maxQuality;
        overCap += !withinCap;
        TC_LOG_INFO("server.worldserver", "BotMgr: gear bot={} slot={} item={} quality={} ilvl={} reqlevel={} within_cap={}", bot.GetName(), uint32(slot),
            proto->GetId(), uint32(proto->GetQuality()), proto->GetBaseItemLevel(), proto->GetBaseRequiredLevel(), withinCap ? 1 : 0);
    }

    TC_LOG_INFO("server.worldserver",
        "BotMgr: autogear {} (class {}, level {}): {} item(s) equipped. summary bot={} class={} level={} considered={} level_too_high={} cannot_use={} picks={} equipped={} kept_better={} failed={} worn={} over_cap={}",
        bot.GetName(), uint32(bot.GetClass()), uint32(bot.GetLevel()), equipped, bot.GetName(), uint32(bot.GetClass()), uint32(bot.GetLevel()), candidates,
        levelTooHigh, cannotUse, tried, equipped, keptBetter, failed, wornItems, overCap);
    return BotActionStatus::kApplied;
}

// Learns every spell the trainer in range teaches this bot now: lowest level first, so each rank comes before the next. The
// core's own teach checks decide money, class and prerequisites, so nothing here is granted that the trainer would refuse.
// The closest creature in interaction range that starts (`ender` false) or ends the quest, or nullptr.
Creature* NearestQuestNpc(Player& bot, uint32 questId, bool ender)
{
    std::list<Unit*> inRange;
    Trinity::AnyUnitInObjectRangeCheck check(&bot, INTERACTION_DISTANCE);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(&bot, inRange, check);
    Cell::VisitAllObjects(&bot, searcher, INTERACTION_DISTANCE);

    Creature* best = nullptr;
    float bestDist = INTERACTION_DISTANCE;
    for (Unit* unit : inRange)
    {
        Creature* npc = unit->ToCreature();
        if (!npc || !(ender ? npc->hasInvolvedQuest(questId) : npc->hasQuest(questId)))
            continue;
        float const dist = bot.GetDistance(npc);
        if (dist >= bestDist || !bot.CanInteractWithQuestGiver(npc))
            continue;
        best = npc;
        bestDist = dist;
    }
    return best;
}

// The reward a bot picks from a quest's choices: the item it can use with the highest sell price, else the highest sell price. 0 when
// the quest offers no choice.
uint32 PickQuestReward(Player& bot, Quest const& quest)
{
    uint32 best = 0;
    bool bestUsable = false;
    uint64 bestPrice = 0;
    for (uint32 i = 0; i < quest.GetRewChoiceItemsCount(); ++i)
    {
        if (!quest.RewardChoiceItemId[i] || quest.RewardChoiceItemType[i] != LootItemType::Item)
            continue;
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest.RewardChoiceItemId[i]);
        if (!item)
            continue;
        bool const usable = bot.CanUseItem(item) == EQUIP_ERR_OK;
        uint64 const price = item->GetSellPrice();
        if (!best || (usable && !bestUsable) || (usable == bestUsable && price > bestPrice))
        {
            best = item->GetId();
            bestUsable = usable;
            bestPrice = price;
        }
    }
    return best;
}

// kQuest: spell_id = (operation << 24) | quest id. 1 accepts the quest from a giver in range, 2 hands it in at an ender in range
// (picking the reward), 3 abandons it.
BotActionStatus QuestAction(Player& bot, uint32 code, std::string& reason)
{
    uint32 const operation = code >> 24;
    uint32 const questId = code & 0xFFFFFF;
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
    {
        reason = "unknown quest";
        return BotActionStatus::kRefused;
    }

    if (operation == 3)
    {
        if (bot.GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            reason = "quest is not in the log";
            return BotActionStatus::kRefused;
        }
        bot.RemoveActiveQuest(questId);
        return BotActionStatus::kApplied;
    }

    if (operation != 1 && operation != 2)
    {
        reason = "unknown quest operation";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }

    Creature* npc = NearestQuestNpc(bot, questId, operation == 2);
    if (!npc)
    {
        reason = operation == 1 ? "no giver of the quest within interaction range" : "no ender of the quest within interaction range";
        return BotActionStatus::kRefused;
    }

    if (operation == 1)
    {
        if (!bot.CanTakeQuest(quest, false))
        {
            reason = "bot cannot take the quest";
            return BotActionStatus::kRefused;
        }
        if (!bot.CanAddQuest(quest, false))
        {
            reason = "bot cannot add the quest (log or bags full)";
            return BotActionStatus::kRefused;
        }
        bot.AddQuestAndCheckCompletion(quest, npc);
        return BotActionStatus::kApplied;
    }

    if (bot.GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || !bot.CanRewardQuest(quest, false))
    {
        reason = "quest is not complete";
        return BotActionStatus::kRefused;
    }
    uint32 const reward = PickQuestReward(bot, *quest);
    if (!bot.CanRewardQuest(quest, LootItemType::Item, reward, false))
    {
        reason = "bot cannot receive the reward (bags full)";
        return BotActionStatus::kRefused;
    }
    bot.RewardQuest(quest, LootItemType::Item, reward, npc);
    return BotActionStatus::kApplied;
}

// kTeleport (scenarios): spell_id is the map, dest the position. Off unless Bot.Actions.Teleport is on; refused in combat.
BotActionStatus TeleportAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.Teleport", false);
    if (!allowed)
    {
        reason = "Bot.Actions.Teleport is off";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    uint32 const mapId = action.spell_id;
    if (!MapManager::IsValidMapCoord(mapId, action.dest_x, action.dest_y, action.dest_z))
    {
        reason = "not a valid map position";
        return BotActionStatus::kRefused;
    }
    if (!bot.TeleportTo(mapId, action.dest_x, action.dest_y, action.dest_z, bot.GetOrientation()))
    {
        reason = "the teleport was refused (instance full, locked or not allowed)";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

// kGrant (scenarios): spell_id is an item entry, dest_x the count. Off unless Bot.Actions.Grant is on.
BotActionStatus GrantAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Actions.Grant", false);
    if (!allowed)
    {
        reason = "Bot.Actions.Grant is off";
        return BotActionStatus::kRefused;
    }
    uint32 const entry = action.spell_id;
    if (!sObjectMgr->GetItemTemplate(entry))
    {
        reason = "unknown item";
        return BotActionStatus::kRefused;
    }
    uint32 const count = action.dest_x >= 1.0f ? uint32(action.dest_x) : 1u;
    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, count) != EQUIP_ERR_OK)
    {
        reason = "the bot's bags cannot hold the item";
        return BotActionStatus::kRefused;
    }
    if (!bot.StoreNewItem(destination, entry, true))
    {
        reason = "the item could not be created";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

BotActionStatus LearnFromTrainer(Player& bot, std::string& reason)
{
    Creature* trainer = NearestServiceNpc(bot, UNIT_NPC_FLAG_TRAINER);
    if (!trainer)
    {
        reason = "no trainer within interaction range";
        return BotActionStatus::kRefused;
    }

    Trainer::Trainer const* teaches = sObjectMgr->GetTrainer(trainer->GetTrainerId());
    if (!teaches)
    {
        reason = "trainer teaches nothing";
        return BotActionStatus::kRefused;
    }

    std::vector<Trainer::Spell> spells = teaches->Spells();
    std::stable_sort(spells.begin(), spells.end(), [](Trainer::Spell const& a, Trainer::Spell const& b)
    {
        return a.ReqLevel != b.ReqLevel ? a.ReqLevel < b.ReqLevel : a.SpellId < b.SpellId;
    });

    for (Trainer::Spell const& spell : spells)
        if (spell.ReqLevel <= bot.GetLevel() && !bot.HasSpell(spell.SpellId))
            teaches->TeachSpell(trainer, &bot, spell.SpellId);
    return BotActionStatus::kApplied;
}

// Party actions (M8). A bot invites only other bots the module controls, so the module's parties are its own; an invite
// never reaches a player on a real account. The steps follow the core's own invite and accept handlers (GroupHandler.cpp).
BotActionStatus InviteToParty(Player& bot, uint64 targetRef, std::string& reason)
{
    if (forever_module::UnitRefType(targetRef) != uint8(HighGuid::Player))
    {
        reason = "party invite target is not a player";
        return BotActionStatus::kInvalidTarget;
    }
    uint32 const counter = forever_module::UnitRefCounter(targetRef);
    Player* target = ObjectAccessor::FindPlayerByLowGUID(counter);
    if (!target || target == &bot || !target->IsInWorld() || !sBotMgr->Exists(counter))
    {
        reason = "party invite target is not a bot on this realm";
        return BotActionStatus::kInvalidTarget;
    }
    if (target->GetGroup() || target->GetGroupInvite())
    {
        reason = "party invite target is already in a party or invited";
        return BotActionStatus::kRefused;
    }
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP) && bot.GetTeam() != target->GetTeam())
    {
        reason = "party invite target is on the other team";
        return BotActionStatus::kRefused;
    }

    // A bot already in a party, or leading a pending one, invites into it. Only its leader does.
    Group* group = bot.GetGroup();
    if (!group)
        group = bot.GetGroupInvite();
    if (group && !group->IsLeader(bot.GetGUID()) && !group->IsAssistant(bot.GetGUID()))
    {
        reason = "bot is not the party leader";
        return BotActionStatus::kRefused;
    }
    if (group && group->IsFull())
    {
        reason = "party is full";
        return BotActionStatus::kRefused;
    }

    bool const fresh = group == nullptr;
    if (fresh)
    {
        // The party is not saved until a second member joins, so a failed invite just drops it.
        group = new Group();
        if (!group->AddLeaderInvite(&bot))
        {
            delete group;
            reason = "could not start a party";
            return BotActionStatus::kRefused;
        }
    }
    if (!group->AddInvite(target))
    {
        if (fresh)
        {
            group->RemoveAllInvites();
            delete group;
        }
        reason = "could not invite the bot";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

// Accepts the bot's pending party invite. The first accept forms the party, as the core's accept handler does.
BotActionStatus AcceptPartyInvite(Player& bot, std::string& reason)
{
    Group* group = bot.GetGroupInvite();
    if (!group)
    {
        reason = "bot has no pending party invite";
        return BotActionStatus::kRefused;
    }

    group->RemoveInvite(&bot);
    if (group->GetLeaderGUID() == bot.GetGUID())
    {
        reason = "bot leads the party it was invited to";
        return BotActionStatus::kRefused;
    }
    if (group->IsFull())
    {
        reason = "party is full";
        return BotActionStatus::kRefused;
    }

    if (!group->IsCreated())
    {
        // The leader may be zoning; the invite is dropped rather than left dangling.
        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (!leader)
        {
            group->RemoveAllInvites();
            reason = "party leader is offline";
            return BotActionStatus::kRefused;
        }
        group->RemoveInvite(leader);
        group->Create(leader);
        sGroupMgr->AddGroup(group);
    }
    if (!group->AddMember(&bot))
    {
        reason = "could not join the party";
        return BotActionStatus::kRefused;
    }
    group->BroadcastGroupUpdate();
    return BotActionStatus::kApplied;
}

// Leaves the bot's party, or declines its pending invite when it is in none (the core's own uninvite path).
BotActionStatus LeaveParty(Player& bot, std::string& reason)
{
    if (bot.GetGroup())
    {
        bot.RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
        return BotActionStatus::kApplied;
    }
    if (bot.GetGroupInvite())
    {
        bot.UninviteFromGroup();
        return BotActionStatus::kApplied;
    }
    reason = "bot is not in a party";
    return BotActionStatus::kRefused;
}

// Loots a corpse the way the client does: through the bot's own session handlers (open the loot, take each item, take the money,
// release it), so the loot rights, the group rules and the bag space are the core's, not a copy of them here. The distance is the one
// the loot handler uses for a corpse. A corpse that is empty, or whose items do not all fit, is not an error: the bot took what it could.
constexpr float kLootDistanceYards = 30.0f;

BotActionStatus LootCorpse(Player& bot, uint64 targetRef, std::string& reason)
{
    Unit* unit = ResolveUnit(bot, targetRef);
    Creature* corpse = unit ? unit->ToCreature() : nullptr;
    if (!corpse)
    {
        reason = "loot target is not a creature on this bot's map";
        return BotActionStatus::kInvalidTarget;
    }
    if (corpse->IsAlive())
    {
        reason = "loot target is alive";
        return BotActionStatus::kInvalidTarget;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsWithinDist(corpse, kLootDistanceYards))
    {
        reason = "corpse is more than 30 yards away";
        return BotActionStatus::kOutOfRange;
    }
    if (!bot.isAllowedToLoot(corpse))
    {
        reason = "bot has no right to loot this corpse";
        return BotActionStatus::kRefused;
    }

    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }

    // Open the loot. Every corpse that is lootable within range joins the bot's loot view, as for a player.
    WorldPackets::Loot::LootUnit open{ WorldPacket(CMSG_LOOT_UNIT) };
    open.Unit = corpse->GetGUID();
    session->HandleLootOpcode(open);
    if (bot.GetAELootView().empty())
        return BotActionStatus::kApplied; // nothing to take

    // Take every item of every loot in the view, then the money.
    WorldPackets::Loot::LootItem take{ WorldPacket(CMSG_LOOT_ITEM) };
    for (std::pair<ObjectGuid const, Loot*> const& view : bot.GetAELootView())
        for (::LootItem const& item : view.second->items)
            if (take.Loot.size() < 100)
                take.Loot.push_back({ view.first, uint8(item.LootListId) });
    if (!take.Loot.empty())
        session->HandleAutostoreLootItemOpcode(take);

    WorldPackets::Loot::LootMoney money{ WorldPacket(CMSG_LOOT_MONEY) };
    session->HandleLootMoneyOpcode(money);

    // Release each loot. The view changes as they are released, so the list is copied first.
    std::vector<Loot*> opened;
    for (std::pair<ObjectGuid const, Loot*> const& view : bot.GetAELootView())
        opened.push_back(view.second);
    for (Loot* loot : opened)
        session->DoLootRelease(loot);
    return BotActionStatus::kApplied;
}

// Release the spirit (layout 11): the checks of WorldSession::HandleRepopRequest, which returns silently, plus the combat refusal.
BotActionStatus ReleaseSpiritAction(Player& bot, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    if (bot.HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        reason = "spirit is already released";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsBeingTeleported())
    {
        reason = "bot is being teleported";
        return BotActionStatus::kRefused;
    }
    if (bot.HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
    {
        reason = "an aura prevents resurrection";
        return BotActionStatus::kRefused;
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    WorldPackets::Misc::RepopRequest request{ WorldPacket(CMSG_REPOP_REQUEST) };
    session->HandleRepopRequest(request);
    return BotActionStatus::kApplied;
}

// Answer a resurrect offer (layout 11): spell_id 0 accepts, 1 declines. The checks of WorldSession::HandleResurrectResponse.
BotActionStatus ResurrectResponseAction(Player& bot, BotAction const& action, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    Unit* caster = ResolveUnit(bot, action.target_guid);
    if (!caster || !bot.IsResurrectRequestedBy(caster->GetGUID()))
    {
        reason = "no resurrect pending from that player";
        return BotActionStatus::kInvalidTarget;
    }
    bool const decline = action.spell_id != 0;
    if (!decline)
    {
        if (Player* casterPlayer = caster->ToPlayer(); casterPlayer && casterPlayer->GetInstanceScript())
            if (InstanceScript* instance = casterPlayer->GetInstanceScript(); instance->IsEncounterInProgress() && !instance->GetCombatResurrectionCharges())
            {
                reason = "encounter in progress and no combat resurrection charge";
                return BotActionStatus::kRefused;
            }
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    WorldPackets::Misc::ResurrectResponse response{ WorldPacket(CMSG_RESURRECT_RESPONSE) };
    response.Resurrecter = caster->GetGUID();
    response.Response = decline ? 1 : 0;
    session->HandleResurrectResponse(response);
    return BotActionStatus::kApplied;
}

// Reclaim the corpse (layout 11): the checks of WorldSession::HandleReclaimCorpse, which returns silently.
BotActionStatus ReclaimCorpseAction(Player& bot, std::string& reason)
{
    if (bot.IsAlive())
    {
        reason = "bot is alive";
        return BotActionStatus::kRefused;
    }
    if (!bot.HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        reason = "spirit is not released";
        return BotActionStatus::kRefused;
    }
    if (bot.InArena())
    {
        reason = "no corpse reclaim in an arena";
        return BotActionStatus::kRefused;
    }
    Corpse* corpse = bot.GetCorpse();
    if (!corpse)
    {
        reason = "bot has no corpse in the world";
        return BotActionStatus::kRefused;
    }
    if (time_t(corpse->GetGhostTime() + bot.GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) >
        time_t(GameTime::GetGameTime()))
    {
        reason = "corpse reclaim delay not over";
        return BotActionStatus::kBusy;
    }
    if (!corpse->IsWithinDistInMap(&bot, CORPSE_RECLAIM_RADIUS, true))
    {
        reason = "corpse is out of reclaim range or on another map";
        return BotActionStatus::kOutOfRange;
    }
    if (sConfigMgr->GetBoolDefault("Classic.Hardcore", false))
    {
        reason = "hardcore realm: death is permanent";
        return BotActionStatus::kRefused;
    }
    WorldSession* session = bot.GetSession();
    if (!session)
    {
        reason = "bot has no session";
        return BotActionStatus::kRefused;
    }
    WorldPackets::Misc::ReclaimCorpse request{ WorldPacket(CMSG_RECLAIM_CORPSE) };
    session->HandleReclaimCorpse(request);
    return BotActionStatus::kApplied;
}

// ---- Chat orders (kSummon, kRevive, kMount) ----

// The player an order names, when it is in the bot's own group (a bot obeys only its group; the module checks the leader).
Player* GroupSpeaker(Player& bot, uint64 ref, std::string& reason)
{
    Unit* unit = ResolveUnit(bot, ref);
    Player* speaker = unit ? unit->ToPlayer() : nullptr;
    if (!speaker || speaker == &bot)
    {
        reason = "no such player";
        return nullptr;
    }
    if (!bot.IsInSameGroupWith(speaker))
    {
        reason = "not in the bot's group";
        return nullptr;
    }
    return speaker;
}

// Puts the bot next to the speaker, on a spiral by the index (so several bots do not share one point). The rules of the foreverbots summon.
BotActionStatus MoveToSpeaker(Player& bot, Player& speaker, uint32 index, std::string& reason)
{
    if (!speaker.IsAlive())
    {
        reason = "the speaker is dead";
        return BotActionStatus::kRefused;
    }
    if (speaker.IsInCombat())
    {
        reason = "the speaker is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    bool const sameMap = bot.GetMapId() == speaker.GetMapId() && bot.GetInstanceId() == speaker.GetInstanceId();
    if (!sameMap)
    {
        Map const* botMap = bot.GetMap();
        Map const* speakerMap = speaker.GetMap();
        if ((botMap && botMap->Instanceable()) || (speakerMap && speakerMap->Instanceable()))
        {
            reason = "an instance is involved on another map";
            return BotActionStatus::kRefused;
        }
    }
    else if (bot.GetExactDist2d(&speaker) < 5.0f)
    {
        reason = "already beside the speaker";
        return BotActionStatus::kRefused;
    }

    float const angle = float(index) * 2.4f;
    float const radius = 2.5f + 0.9f * float(index);
    float const x = speaker.GetPositionX() + std::cos(angle) * radius;
    float const y = speaker.GetPositionY() + std::sin(angle) * radius;
    float z = speaker.GetPositionZ();
    if (sameMap)
    {
        float const ground = speaker.GetMap()->GetHeight(speaker.GetPhaseShift(), x, y, z + 2.0f);
        if (ground > INVALID_HEIGHT)
            z = ground;
    }
    if (!bot.TeleportTo(speaker.GetMapId(), x, y, z, speaker.GetOrientation()))
    {
        reason = "the teleport was refused";
        return BotActionStatus::kRefused;
    }
    return BotActionStatus::kApplied;
}

BotActionStatus SummonAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    Player* speaker = GroupSpeaker(bot, action.target_guid, reason);
    if (!speaker)
        return BotActionStatus::kInvalidTarget;
    return MoveToSpeaker(bot, *speaker, uint32(std::max(0.0f, action.dest_x)), reason);
}

BotActionStatus ReviveAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    static bool const sickness = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.ReviveSickness", true);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (sConfigMgr->GetBoolDefault("Classic.Hardcore", false))
    {
        reason = "hardcore realm: death is permanent";
        return BotActionStatus::kRefused;
    }
    if (bot.IsAlive())
    {
        reason = "bot is not dead";
        return BotActionStatus::kRefused;
    }
    Player* speaker = GroupSpeaker(bot, action.target_guid, reason);
    if (!speaker)
        return BotActionStatus::kInvalidTarget;
    if (!speaker->IsAlive() || speaker->IsInCombat())
    {
        reason = "the speaker is dead or in combat";
        return BotActionStatus::kRefused;
    }
    bot.ResurrectPlayer(0.35f, sickness);
    bot.SpawnCorpseBones();
    std::string moveReason;
    BotActionStatus const moved = MoveToSpeaker(bot, *speaker, uint32(std::max(0.0f, action.dest_x)), moveReason);
    if (moved != BotActionStatus::kApplied)
        reason = "revived where it fell: " + moveReason; // the resurrect itself happened
    return BotActionStatus::kApplied;
}

BotActionStatus MountAction(Player& bot, BotAction const& action, std::string& reason)
{
    static bool const allowed = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
    if (!allowed)
    {
        reason = "Bot.Chat.Orders.Enabled is off";
        return BotActionStatus::kRefused;
    }
    if (!bot.IsAlive())
    {
        reason = "bot is dead";
        return BotActionStatus::kRefused;
    }
    if (action.spell_id == 0)
    {
        if (!bot.IsMounted())
        {
            reason = "bot is not mounted";
            return BotActionStatus::kRefused;
        }
        bot.RemoveAurasByType(SPELL_AURA_MOUNTED);
        return BotActionStatus::kApplied;
    }
    if (bot.IsInCombat())
    {
        reason = "bot is in combat";
        return BotActionStatus::kRefused;
    }
    if (bot.IsMounted())
    {
        reason = "bot is already mounted";
        return BotActionStatus::kRefused;
    }

    // The fastest learned ground mount: a spell with a mounted aura and a mounted speed effect, flying mounts left out.
    uint32 bestSpell = 0;
    int32 bestSpeed = -1;
    for (auto const& [spellId, playerSpell] : bot.GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED || playerSpell.disabled)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info || !info->HasAura(SPELL_AURA_MOUNTED) || info->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
            continue;
        int32 speed = 0;
        for (SpellEffectInfo const& effect : info->GetEffects())
            if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                speed = std::max(speed, int32(effect.CalcValue(&bot)));
        if (speed > bestSpeed || (speed == bestSpeed && spellId < bestSpell))
        {
            bestSpell = spellId;
            bestSpeed = speed;
        }
    }
    if (bestSpell == 0)
    {
        reason = "bot has no mount";
        return BotActionStatus::kRefused;
    }
    SpellCastResult const result = bot.CastSpell(&bot, bestSpell, false);
    if (result != SPELL_CAST_OK)
    {
        reason = "cast of mount " + std::to_string(bestSpell) + " returned result " + std::to_string(int32(result));
        return StatusForCast(result);
    }
    return BotActionStatus::kApplied;
}

BotActionStatus ApplyBotAction(Player& bot, BotAction const& action, std::string& reason)
{
    switch (action.type)
    {
        case ActionType::kStop:
            bot.StopMoving();
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveIdle();
            return BotActionStatus::kApplied;

        case ActionType::kMoveTo:
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MovePoint(0, action.dest_x, action.dest_y, action.dest_z);
            return BotActionStatus::kApplied;

        case ActionType::kFollow:
        {
            Unit* target = ResolveUnit(bot, action.target_guid);
            if (!target || target == &bot || !target->IsInWorld())
            {
                reason = "follow target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveFollow(target, 1.0f);
            return BotActionStatus::kApplied;
        }

        case ActionType::kAttackTarget:
        {
            Unit* target = ResolveHostileTarget(bot, action.target_guid, reason);
            if (!target)
                return BotActionStatus::kInvalidTarget;
            // Already meleeing this target: Unit::Attack refuses a repeat, but the bot is attacking as asked.
            if (bot.GetVictim() == target && bot.HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                return BotActionStatus::kApplied;
            if (!bot.Attack(target, true))
            {
                reason = "attack refused: " + AttackRefusalReason(bot, target);
                return BotActionStatus::kRefused;
            }
            bot.SetFacingToObject(target); // a swing and most spells need the target in front
            return BotActionStatus::kApplied;
        }

        case ActionType::kVendor:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }

            uint32 const services = action.spell_id;
            if (services & (kServiceSellBit | kServiceRepairBit))
            {
                Creature* npc = NearestServiceNpc(bot, UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_REPAIR);
                if (!npc)
                {
                    reason = "no vendor or repairman within interaction range";
                    return BotActionStatus::kRefused;
                }
                if ((services & kServiceSellBit) && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR_MASK))
                    SellGreyItems(bot);
                if ((services & kServiceRepairBit) && npc->HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
                    bot.DurabilityRepairAll(true, 0.0f, false);
            }
            if (services & kServiceEquipBit)
                EquipBestGear(bot);
            return BotActionStatus::kApplied;
        }

        case ActionType::kTrain:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }
            return LearnFromTrainer(bot, reason);
        }

        case ActionType::kRest:
        {
            // spell_id 1 sits down to rest, 0 stands up. A bot in combat does not sit.
            if (action.spell_id == 0)
            {
                bot.SetStandState(UNIT_STAND_STATE_STAND);
                return BotActionStatus::kApplied;
            }
            if (bot.IsInCombat())
            {
                reason = "bot is in combat and cannot sit";
                return BotActionStatus::kRefused;
            }
            bot.SetStandState(UNIT_STAND_STATE_SIT);
            return BotActionStatus::kApplied;
        }

        case ActionType::kLoot:
            return LootCorpse(bot, action.target_guid, reason);

        case ActionType::kReleaseSpirit:
            return ReleaseSpiritAction(bot, reason);

        case ActionType::kSummon:
            return SummonAction(bot, action, reason);

        case ActionType::kRevive:
            return ReviveAction(bot, action, reason);

        case ActionType::kMount:
            return MountAction(bot, action, reason);

        case ActionType::kResurrect:
            return ResurrectResponseAction(bot, action, reason);

        case ActionType::kReclaimCorpse:
            return ReclaimCorpseAction(bot, reason);

        case ActionType::kQuest:
            return QuestAction(bot, action.spell_id, reason);

        case ActionType::kTeleport:
            return TeleportAction(bot, action, reason);

        case ActionType::kGrant:
            return GrantAction(bot, action, reason);

        case ActionType::kAutoGear:
            return AutoGearAction(bot, action.spell_id, reason);

        case ActionType::kGroup:
        {
            // Forming and changing a party is out of combat only, the same as the trainer and vendor actions.
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }
            switch (action.spell_id)
            {
                case 1: return InviteToParty(bot, action.target_guid, reason);
                case 2: return AcceptPartyInvite(bot, reason);
                case 3: return LeaveParty(bot, reason);
            }
            reason = "unknown party action " + std::to_string(action.spell_id);
            return BotActionStatus::kRefused;
        }

        case ActionType::kCastSpell:
        {
            if (!sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE))
            {
                reason = "unknown spell " + std::to_string(action.spell_id);
                return BotActionStatus::kUnknownSpell;
            }
            // The module names a rank 1 spell; the bot casts the highest rank it knows.
            uint32 const castId = HighestKnownRank(bot, action.spell_id);
            if (!castId)
            {
                reason = "bot does not know spell " + std::to_string(action.spell_id);
                return BotActionStatus::kNotKnown;
            }
            // A zero target means the bot casts on itself.
            Unit* target = action.target_guid == 0 ? &bot : ResolveUnit(bot, action.target_guid);
            if (!target || !target->IsInWorld())
            {
                reason = "cast target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            // Known to fail: a cast while another is in progress, and a spell with a cast time while moving. The module is told which, and
            // waits or stops, instead of getting a generic refusal from the spell system.
            if (Spell const* current = bot.GetCurrentSpell(CURRENT_GENERIC_SPELL); current && current->getState() != SPELL_STATE_FINISHED)
            {
                reason = "bot is casting spell " + std::to_string(current->GetSpellInfo()->Id);
                return BotActionStatus::kBusy;
            }
            if (Spell const* channel = bot.GetCurrentSpell(CURRENT_CHANNELED_SPELL); channel && channel->getState() != SPELL_STATE_FINISHED)
            {
                reason = "bot is channeling spell " + std::to_string(channel->GetSpellInfo()->Id);
                return BotActionStatus::kBusy;
            }
            if (bot.isMoving())
            {
                SpellInfo const* castInfo = sSpellMgr->GetSpellInfo(castId, DIFFICULTY_NONE);
                if (castInfo && castInfo->CalcCastTime() > 0)
                {
                    reason = "spell " + std::to_string(castId) + " has a cast time and the bot is moving";
                    return BotActionStatus::kMoving;
                }
            }
            if (target != &bot && !bot.HasInArc(3.14159265f, target))
                bot.SetFacingToObject(target); // spells aimed at a unit need it in front; the client would turn the caster
            SpellCastResult const result = bot.CastSpell(target, castId, false);
            if (result != SPELL_CAST_OK)
                reason = "cast of spell " + std::to_string(castId) + " returned result " + std::to_string(int32(result));
            return StatusForCast(result);
        }

        case ActionType::kCraft:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }

            SpellInfo const* info = sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE);
            if (!info)
            {
                reason = "unknown spell " + std::to_string(action.spell_id);
                return BotActionStatus::kUnknownSpell;
            }
            if (!bot.HasSpell(action.spell_id))
            {
                reason = "bot does not know spell " + std::to_string(action.spell_id);
                return BotActionStatus::kNotKnown;
            }

            // Check reagents before consuming anything.
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                uint32 reagentEntry = info->Reagent[i];
                int16 reagentCount = info->ReagentCount[i];
                if (!reagentEntry || reagentCount <= 0)
                    continue;
                if (bot.GetItemCount(reagentEntry) < uint32(reagentCount))
                {
                    reason = "not enough reagents for spell " + std::to_string(action.spell_id);
                    return BotActionStatus::kRefused;
                }
            }
            // Consume reagents.
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                uint32 reagentEntry = info->Reagent[i];
                int16 reagentCount = info->ReagentCount[i];
                if (!reagentEntry || reagentCount <= 0)
                    continue;
                bot.DestroyItemCount(reagentEntry, uint32(reagentCount), true);
            }

            // Find the item the spell creates (SPELL_EFFECT_CREATE_ITEM).
            uint32 itemEntry = 0;
            uint32 itemCount = 1;
            for (uint32 i = 0; i < 3; ++i)
            {
                if (info->Effects[i].IsEffect(SPELL_EFFECT_CREATE_ITEM))
                {
                    itemEntry = info->Effects[i].ItemType;
                    itemCount = info->Effects[i].BasePoints > 0 ? uint32(info->Effects[i].BasePoints) : 1u;
                    break;
                }
            }
            if (!itemEntry)
            {
                reason = "spell " + std::to_string(action.spell_id) + " creates no item";
                return BotActionStatus::kRefused;
            }

            ItemPosCountVec dest;
            if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, itemCount) != EQUIP_ERR_OK)
            {
                reason = "not enough bag space for crafted item";
                return BotActionStatus::kRefused;
            }
            Item* item = bot.StoreNewItem(dest, itemEntry, true);
            if (!item)
            {
                reason = "could not add crafted item to bags";
                return BotActionStatus::kRefused;
            }
            bot.SendNewItem(item, itemCount, true, false);

            reason = "crafted item " + std::to_string(itemEntry);
            return BotActionStatus::kApplied;
        }

        case ActionType::kGather:
        {
            if (bot.IsInCombat())
            {
                reason = "bot is in combat";
                return BotActionStatus::kRefused;
            }

            Unit* target = ResolveUnit(bot, action.target_guid);
            if (!target || target->GetTypeId() != TYPEID_GAMEOBJECT)
            {
                reason = "gather target is not a game object";
                return BotActionStatus::kInvalidTarget;
            }
            GameObject* go = target->ToGameObject();

            if (!bot.IsWithinDist(go, INTERACTION_DISTANCE))
            {
                reason = "gather target is out of interaction range";
                return BotActionStatus::kOutOfRange;
            }

            // If a gathering spell is named, cast it on the node; otherwise use the game object's Use.
            if (action.spell_id)
            {
                SpellInfo const* gatherSpell = sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE);
                if (!gatherSpell || !bot.HasSpell(action.spell_id))
                {
                    reason = "bot does not know gathering spell " + std::to_string(action.spell_id);
                    return BotActionStatus::kNotKnown;
                }
                SpellCastResult const castResult = bot.CastSpell(go, action.spell_id, false);
                if (castResult != SPELL_CAST_OK)
                {
                    reason = "gather spell cast returned " + std::to_string(int32(castResult));
                    return StatusForCast(castResult);
                }
            }
            else
            {
                go->Use(&bot);
            }

            reason = "gathered from " + go->GetName();
            return BotActionStatus::kApplied;
        }

    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}
