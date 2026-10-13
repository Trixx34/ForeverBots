#include "ModuleBotActions.h"
#include "ModuleBotSnapshot.h"
#include "BotMgr.h"
#include "Cell.h"
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
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
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
std::vector<ItemTemplate const*> const& GearCandidates()
{
    static std::vector<ItemTemplate const*> const candidates = []
    {
        std::vector<ItemTemplate const*> out;
        uint32 const maxQuality = uint32(std::max(1, sConfigMgr->GetIntDefault("Bot.AutoGear.MaxQuality", 2)));
        for (auto const& [id, proto] : sObjectMgr->GetItemTemplateStore())
        {
            if (proto.GetClass() != ITEM_CLASS_WEAPON && proto.GetClass() != ITEM_CLASS_ARMOR)
                continue;
            if (proto.GetQuality() < kQualityCommon || proto.GetQuality() > maxQuality || proto.GetBonding() == BIND_QUEST)
                continue;
            if (proto.GetInventoryType() == INVTYPE_BODY || EquipSlotFor(proto.GetInventoryType()) == NULL_SLOT)
                continue;
            std::string name = proto.GetName(DEFAULT_LOCALE) ? proto.GetName(DEFAULT_LOCALE) : "";
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.empty() || name.find("deprecated") != std::string::npos || name.find("test") == 0 ||
                name.find("unused") != std::string::npos || name.find("[ph]") != std::string::npos || name.rfind("zz", 0) == 0)
                continue;
            out.push_back(&proto);
        }
        return out;
    }();
    return candidates;
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

// Creates the item in the bags and moves it into `slot`, unless the slot already holds an item of at least this item level.
bool GiveAndEquip(Player& bot, ItemTemplate const& item, uint8 slot)
{
    Item* current = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (current && current->GetTemplate()->GetBaseItemLevel() >= item.GetBaseItemLevel())
        return false;

    ItemPosCountVec destination;
    if (bot.CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item.GetId(), 1) != EQUIP_ERR_OK)
        return false;
    Item* created = bot.StoreNewItem(destination, item.GetId(), true);
    if (!created)
        return false;

    uint16 target = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
    uint16 equipDestination = target;
    if (bot.CanEquipItem(slot, equipDestination, created, true) != EQUIP_ERR_OK)
        return false; // stays in the bags
    bot.SwapItem(uint16(created->GetBagSlot()) << 8 | created->GetSlot(), target);
    return bot.GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == created;
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

    bool const tank = (flags & 1) != 0;
    std::vector<int32> const preferred = PreferredStats(bot.GetClass());
    std::map<uint8, GearPick> bySlot;          // armour and ranged, by equipment slot
    GearPick twoHand, shield, holdable;
    std::vector<std::pair<int32, ItemTemplate const*>> oneHand;   // main-hand weapons (one-hand, main hand)
    std::vector<std::pair<int32, ItemTemplate const*>> offHand;    // off-hand weapons (off hand only, and one-hand for dual wield)

    for (ItemTemplate const* item : GearCandidates())
    {
        if (item->GetBaseRequiredLevel() > int32(bot.GetLevel()) || bot.CanUseItem(item) != EQUIP_ERR_OK)
            continue;
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

    uint32 equipped = 0;
    if (main.Item && GiveAndEquip(bot, *main.Item, EQUIPMENT_SLOT_MAINHAND))
        ++equipped;
    if (off.Item && (!main.Item || main.Item->GetInventoryType() != INVTYPE_2HWEAPON) && GiveAndEquip(bot, *off.Item, EQUIPMENT_SLOT_OFFHAND))
        ++equipped;
    for (auto const& [slot, pick] : bySlot)
        if (pick.Item && GiveAndEquip(bot, *pick.Item, slot))
            ++equipped;

    TC_LOG_INFO("server.worldserver", "BotMgr: autogear {} (class {}, level {}): {} item(s) equipped.", bot.GetName(), uint32(bot.GetClass()),
        uint32(bot.GetLevel()), equipped);
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
    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}
