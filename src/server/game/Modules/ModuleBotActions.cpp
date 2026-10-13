#include "ModuleBotActions.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DB2Structure.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"

#include <algorithm>
#include <list>

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
    if (!bot.IsHostileTo(target))
    {
        reason = "target is not hostile to the bot";
        return nullptr;
    }
    return target;
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

// Learns every spell the trainer in range teaches this bot now: lowest level first, so each rank comes before the next. The
// core's own teach checks decide money, class and prerequisites, so nothing here is granted that the trainer would refuse.
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
            if (!bot.Attack(target, true))
            {
                reason = "attack was refused";
                return BotActionStatus::kRefused;
            }
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
            SpellCastResult const result = bot.CastSpell(target, castId, false);
            if (result != SPELL_CAST_OK)
                reason = "cast of spell " + std::to_string(castId) + " returned result " + std::to_string(int32(result));
            return StatusForCast(result);
        }
    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}
