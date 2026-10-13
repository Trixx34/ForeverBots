#include "ModuleBotActions.h"
#include "BotMgr.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DB2Structure.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <list>
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
    if (!bot.IsHostileTo(target))
    {
        reason = "target is not hostile to the bot";
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
            SpellCastResult const result = bot.CastSpell(target, castId, false);
            if (result != SPELL_CAST_OK)
                reason = "cast of spell " + std::to_string(castId) + " returned result " + std::to_string(int32(result));
            return StatusForCast(result);
        }
    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}
