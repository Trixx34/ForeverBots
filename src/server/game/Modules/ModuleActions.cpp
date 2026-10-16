/*
 * Module -> core actions. See ModuleActions.h.
 */

#include "ModuleActions.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Chat.h"
#include "Creature.h"
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

// Buys `count` of `item_entry` from the vendor named by `vendor_guid`. Checks range, sell-list
// membership, price, and player money before removing anything.
void BuyItem(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    // Find the vendor NPC and verify interaction range.
    ObjectGuid vendorGuid;
    vendorGuid.SetRawValue(0, request.VendorGuid);
    Creature* vendor = ObjectAccessor::GetCreature(*player, vendorGuid);
    if (!vendor || !vendor->IsInWorld())
    {
        reply.Reason = "vendor is not in the world";
        return;
    }
    if (!player->IsWithinDist(vendor, INTERACTION_DISTANCE))
    {
        reply.Reason = "vendor is out of interaction range";
        return;
    }
    if (!player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR, UNIT_NPC_FLAG_2_NONE))
    {
        reply.Reason = "cannot interact with that vendor";
        return;
    }

    // Look up the item on the vendor's sell list.
    VendorItemData const* vItems = vendor->GetVendorItems();
    if (!vItems || vItems->Empty())
    {
        reply.Reason = "vendor has nothing for sale";
        return;
    }
    VendorItem const* vItem = nullptr;
    for (uint32 slot = 0; slot < vItems->GetItemCount(); ++slot)
    {
        VendorItem const* cand = vItems->GetItem(slot);
        if (cand && cand->item == request.ItemEntry)
        {
            vItem = cand;
            break;
        }
    }
    if (!vItem)
    {
        reply.Reason = "item is not on this vendor's sell list";
        return;
    }

    // Price: buy price from the item template, extended cost overrides it.
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(request.ItemEntry);
    if (!proto)
    {
        reply.Reason = "item template not found";
        return;
    }
    uint32 price = proto->GetBuyPrice();
    if (vItem->ExtendedCost)
    {
        ItemExtendedCostEntry const* iece = sItemExtendedCostStore.LookupEntry(vItem->ExtendedCost);
        if (iece && iece->Money)
            price = iece->Money;
    }
    if (price == 0)
    {
        reply.Reason = "item has no buy price";
        return;
    }

    // Reputation discount, same as the player-facing vendor path.
    price = uint32(std::floor(price * player->GetReputationPriceDiscount(vendor)));

    uint64 const total = uint64(price) * request.Count;
    if (total > MAX_MONEY_AMOUNT || player->GetMoney() < total)
    {
        reply.Reason = "not enough money";
        return;
    }

    // Bag space check before taking money.
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, request.ItemEntry, request.Count) != EQUIP_ERR_OK)
    {
        reply.Reason = "not enough bag space";
        return;
    }

    // Deduct money, add item.
    if (!player->ModifyMoney(static_cast<int64>(-total)))
    {
        reply.Reason = "could not deduct money";
        return;
    }
    Item* item = player->StoreNewItem(dest, request.ItemEntry, true);
    if (!item)
    {
        // Refund on failure.
        player->ModifyMoney(static_cast<int64>(total));
        reply.Reason = "could not add item to bags";
        return;
    }
    player->SendNewItem(item, request.Count, true, false);

    reply.CopperSpent = total;
    reply.Success = true;
    TC_LOG_DEBUG("modules.actions", "bought {} x item {} from vendor {} for {} copper, player {}",
                 request.Count, request.ItemEntry, request.VendorGuid, total, player->GetGUID().ToString());
}

// Auction House actions: browse, bid, buyout, list, cancel.
void AuctionHouse(Player* player, ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply)
{
    Creature* auctioneer = ObjectAccessor::GetCreature(*player, ObjectGuid::Create<HighGuid::Creature>(request.AuctioneerGuid));
    if (!auctioneer || !auctioneer->IsInWorld())
    {
        reply.Reason = "auctioneer is not in the world";
        return;
    }
    if (!player->IsWithinDist(auctioneer, INTERACTION_DISTANCE))
    {
        reply.Reason = "auctioneer is out of interaction range";
        return;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
    if (!auctionHouse)
    {
        reply.Reason = "no auction house for this faction";
        return;
    }

    switch (static_cast<uint32>(request.AhOp))
    {
        case 0: // kBrowse
        {
            // Acknowledge; a real module would page through auctions via separate queries.
            reply.Success = true;
            return;
        }

        case 1: // kBid
        {
            if (!request.AuctionGuid || !request.Bid)
            {
                reply.Reason = "auction_guid and bid are required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner == player->GetGUID())
            {
                reply.Reason = "cannot bid on your own auction";
                return;
            }
            uint64 minBid = auction->BidAmount ? auction->BidAmount + auction->CalculateMinIncrement() : auction->MinBid;
            if (request.Bid < minBid)
            {
                reply.Reason = "bid too low";
                return;
            }
            if (auction->BuyoutOrUnitPrice && request.Bid >= auction->BuyoutOrUnitPrice)
            {
                // Treat a bid at or above buyout as a buyout.
                uint64 price = auction->BuyoutOrUnitPrice;
                if (!auction->IsCommodity())
                    price *= auction->GetTotalItemCount();
                if (player->GetMoney() < price)
                {
                    reply.Reason = "not enough money";
                    return;
                }
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                auctionHouse->RemoveAuction(trans, auction);
                auctionHouse->SendAuctionSold(auction, nullptr, trans);
                auctionHouse->SendAuctionWon(auction, player, trans);
                player->ModifyMoney(static_cast<int64>(-price));
                CharacterDatabase.CommitTransaction(trans);
                reply.CopperSpent = price;
                reply.Success = true;
                return;
            }
            uint64 priceToPay = request.Bid - (auction->Bidder == player->GetGUID() ? auction->BidAmount : 0);
            if (player->GetMoney() < priceToPay)
            {
                reply.Reason = "not enough money";
                return;
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            player->ModifyMoney(static_cast<int64>(-priceToPay));
            auction->Bidder = player->GetGUID();
            auction->BidAmount = request.Bid;
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = priceToPay;
            reply.Success = true;
            return;
        }

        case 2: // kBuyout
        {
            if (!request.AuctionGuid || !request.Buyout)
            {
                reply.Reason = "auction_guid and buyout are required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner == player->GetGUID())
            {
                reply.Reason = "cannot buy out your own auction";
                return;
            }
            uint64 price = auction->BuyoutOrUnitPrice;
            if (!auction->IsCommodity())
                price *= auction->GetTotalItemCount();
            if (request.Buyout < price)
            {
                reply.Reason = "buyout price too low";
                return;
            }
            if (player->GetMoney() < price)
            {
                reply.Reason = "not enough money";
                return;
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            auctionHouse->RemoveAuction(trans, auction);
            auctionHouse->SendAuctionSold(auction, nullptr, trans);
            auctionHouse->SendAuctionWon(auction, player, trans);
            player->ModifyMoney(static_cast<int64>(-price));
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = price;
            reply.Success = true;
            return;
        }

        case 3: // kList
        {
            // Listing requires splitting the item stack, computing the deposit, and creating a
            // full AuctionPosting with DB transaction. That is intentionally deferred; the
            // module can use kBuyout/kBid/kCancel on existing auctions instead.
            reply.Reason = "kList is not yet implemented for module actions";
            return;
        }

        case 4: // kCancel
        {
            if (!request.AuctionGuid)
            {
                reply.Reason = "auction_guid is required";
                return;
            }
            AuctionPosting* auction = auctionHouse->GetAuction(request.AuctionGuid);
            if (!auction)
            {
                reply.Reason = "auction not found";
                return;
            }
            if (auction->Owner != player->GetGUID())
            {
                reply.Reason = "not your auction";
                return;
            }
            uint64 cancelCost = 0;
            if (auction->BidAmount)
            {
                cancelCost = CalculatePct(auction->BidAmount, 5u);
                if (player->GetMoney() < cancelCost)
                {
                    reply.Reason = "not enough money for cancel fee";
                    return;
                }
            }
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            if (cancelCost)
                player->ModifyMoney(static_cast<int64>(-cancelCost));
            auctionHouse->RemoveAuction(trans, auction);
            if (cancelCost)
                auctionHouse->SendAuctionCancelledToBidder(auction, trans);
            CharacterDatabase.CommitTransaction(trans);
            reply.CopperSpent = cancelCost;
            reply.Success = true;
            return;
        }

        default:
            reply.Reason = "unknown auction house operation";
            return;
    }
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

        case ModuleWire::ActionKind::Log:
            if (request.Text.empty() || request.Text.size() > MaxMessageBytes)
            {
                reply.Reason = "log text must be 1 to 255 bytes";
                return;
            }
            switch (request.LogLevel)
            {
                case 0: TC_LOG_INFO("server.worldserver", "[{}] {}", moduleName, request.Text); break;
                case 1: TC_LOG_WARN("server.worldserver", "[{}] {}", moduleName, request.Text); break;
                default: TC_LOG_ERROR("server.worldserver", "[{}] {}", moduleName, request.Text); break;
            }
            reply.Success = true;
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

        case ModuleWire::ActionKind::BuyItem:
            BuyItem(player, request, reply);
            return;

        case ModuleWire::ActionKind::AuctionHouse:
            AuctionHouse(player, request, reply);
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

        case ModuleWire::ActionKind::Log:
        case ModuleWire::ActionKind::None:
            break;
    }

    reply.Reason = "unknown action";
}
