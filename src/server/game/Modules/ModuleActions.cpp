/*
 * Module -> core actions. See ModuleActions.h.
 */

#include "ModuleActions.h"
#include "Chat.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ModuleStore.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>

namespace
{

// Longest text a module may send a player. Chat lines are short; this only guards against abuse.
constexpr size_t MaxMessageBytes = 255;

// Quest group loot: the range a party member may be from the looter when the module sets none, and the most it may set.
constexpr uint32 DefaultShareRangeYards = 100;
constexpr uint32 MaxShareRangeYards = 200;

// Gives a copy of a quest item the looter just looted to each party member who still needs it. The looter keeps their own. A member
// is skipped when on another map, out of range, with no quest that needs the item, or with no room in the bags; none of that is an
// error. The module names the item, the core finds who gets it and gives it, so a module cannot give out items to anyone it likes.
void ShareQuestItem(Player* looter, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(request.ItemEntry);
    if (!proto || proto->GetClass() != ITEM_CLASS_QUEST)
    {
        reply.Reason = "item is not a quest item";
        return;
    }
    if (request.Count == 0 || request.Count > proto->GetMaxStackSize())
    {
        reply.Reason = "count must be from 1 to the item's stack size";
        return;
    }

    Group* group = looter->GetGroup();
    if (!group)
    {
        reply.Reason = "player is not in a group";
        return;
    }

    uint32 const range = request.RangeYards ? std::min(request.RangeYards, MaxShareRangeYards) : DefaultShareRangeYards;
    uint32 given = 0;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* member = ref.GetSource();
        if (!member || member == looter || !member->IsInWorld() || member->GetMap() != looter->GetMap())
            continue;
        if (!looter->IsWithinDist(member, float(range)) || !member->HasQuestForItem(request.ItemEntry))
            continue;

        ItemPosCountVec dest;
        if (member->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, request.ItemEntry, request.Count) != EQUIP_ERR_OK)
            continue;
        Item* item = member->StoreNewItem(dest, request.ItemEntry, true);
        if (!item)
            continue;

        member->SendNewItem(item, request.Count, true, false);
        ++given;
    }

    reply.Recipients = given;
    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "shared quest item {} x{} from player {} with {} party member(s)",
                 request.ItemEntry, request.Count, looter->GetGUID().ToString(), given);
}

void SellItem(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    Item* item = player->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(request.ItemGuid));
    if (!item)
    {
        reply.Reason = "item is no longer in the player's bags";
        return;
    }

    if (item->GetEntry() != request.ItemEntry)
    {
        reply.Reason = "item entry does not match the request";
        return;
    }

    if (request.Count == 0 || player->GetItemCount(request.ItemEntry) < request.Count)
    {
        reply.Reason = "not enough of the item to sell";
        return;
    }

    ItemTemplate const* proto = item->GetTemplate();
    uint32 const price = proto ? proto->GetSellPrice() : 0;
    if (price == 0)
    {
        reply.Reason = "item has no vendor value";
        return;
    }

    // Refuse before removing anything: once the items are gone the money has to be credited.
    uint64 const total = uint64(price) * request.Count;
    if (player->GetMoney() > MAX_MONEY_AMOUNT - total)
    {
        reply.Reason = "gold cap would be exceeded";
        return;
    }

    // Count what was really removed, rather than trusting the return value of DestroyItemCount.
    uint32 const before = player->GetItemCount(request.ItemEntry);
    player->DestroyItemCount(request.ItemEntry, request.Count, true);
    uint32 const removed = before - player->GetItemCount(request.ItemEntry);

    uint64 const credit = uint64(price) * removed;
    if (credit && !player->ModifyMoney(static_cast<int64>(credit)))
    {
        TC_LOG_ERROR("modules", "sold {} x item {} for player {} but could not credit {} copper",
                     removed, request.ItemEntry, player->GetGUID().ToString(), credit);
        reply.Reason = "items were removed but the money could not be credited";
        return;
    }

    reply.CopperGained = credit;
    if (removed != request.Count)
    {
        reply.Reason = "only part of the requested count was removed";
        return;
    }

    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "sold {} x item {} [{}] for {} copper, player {}",
                 removed, request.ItemEntry, request.ItemGuid, credit, player->GetGUID().ToString());
}

} // namespace

namespace
{

// Module store actions (doc 15). They need the module, not a player.
void ExecuteStoreAction(std::string const& moduleName, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    std::string error;
    switch (request.Kind)
    {
        case ModuleWire::ActionKind::StoreGet:
        {
            bool found = false;
            std::string value;
            error = ModuleStore::Get(moduleName, request.Key, found, value);
            reply.Found = found;
            reply.Value = std::move(value);
            break;
        }
        case ModuleWire::ActionKind::StoreSet:
            error = ModuleStore::Set(moduleName, request.Key, request.Value);
            break;
        case ModuleWire::ActionKind::StoreDelete:
        {
            bool existed = false;
            error = ModuleStore::Delete(moduleName, request.Key, existed);
            reply.Found = existed;
            break;
        }
        case ModuleWire::ActionKind::StoreList:
            error = ModuleStore::List(moduleName, request.Key, reply.Keys);
            break;
        default:
            error = "not a store action";
            break;
    }

    if (!error.empty())
    {
        reply.Reason = error;
        return;
    }
    reply.Success = true;
}

} // namespace

void ExecuteModuleAction(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply, std::string const& moduleName)
{
    reply.MessageId = request.MessageId;

    switch (request.Kind)
    {
        case ModuleWire::ActionKind::StoreGet:
        case ModuleWire::ActionKind::StoreSet:
        case ModuleWire::ActionKind::StoreDelete:
        case ModuleWire::ActionKind::StoreList:
            if (moduleName.empty())
                reply.Reason = "module is no longer loaded";
            else
                ExecuteStoreAction(moduleName, request, reply);
            return;

        default:
            break;
    }

    Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(request.PlayerGuid));
    if (!player || !player->GetSession())
    {
        reply.Reason = "player is not online";
        return;
    }

    switch (request.Kind)
    {
        case ModuleWire::ActionKind::SellItem:
            SellItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::ShareQuestItem:
            ShareQuestItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::SendPlayerMessage:
            if (request.Text.empty() || request.Text.size() > MaxMessageBytes)
            {
                reply.Reason = "message must be 1 to 255 bytes";
                return;
            }
            ChatHandler(player->GetSession()).SendSysMessage(request.Text);
            reply.Success = true;
            return;

        case ModuleWire::ActionKind::None:
            break;
    }

    reply.Reason = "unknown action";
}
