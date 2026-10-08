/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BotAuction.h"
#include "AuctionHouseMgr.h"
#include "AuctionHousePackets.h"
#include "BotAuctionPlan.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Mail.h"
#include "MailPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"

namespace BotAuction
{
namespace
{
    BotAuctionPlan::Limits Lim()
    {
        BotAuctionPlan::Limits l;
        l.UndercutPct = uint32(std::max<int32>(0, std::min<int32>(50, sConfigMgr->GetIntDefault("Bot.AH.UndercutPct", 5))));
        l.MaxPerBot = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.AH.MaxListingsPerBot", 5)));
        l.MaxTotal = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.AH.MaxListingsTotal", 2000)));
        return l;
    }

    void Log(Player* bot, char const* code, std::string summary, uint32 entry = 0)
    {
        BotEvent ev;
        ev.BotGuid = bot->GetGUID().GetCounter();
        ev.Type = "decision";
        ev.Severity = BOTLOG_INFO;
        ev.Reason = code;
        ev.Summary = std::move(summary);
        ev.Level = bot->GetLevel();
        ev.MapId = uint16(bot->GetMapId());
        if (entry)
            ev.TargetEntry = entry;
        sBotMgr->LogEvent(std::move(ev));
    }
}

bool Enabled()
{
    return sConfigMgr->GetBoolDefault("Bot.AH.Enabled", false);
}

uint32 VisitCooldownMs()
{
    return uint32(std::max<int32>(60, sConfigMgr->GetIntDefault("Bot.AH.VisitCooldownSec", 900))) * 1000;
}

uint32 MaxPostsPerVisit()
{
    return 6;
}

Item* PickListing(Player* bot, std::function<bool(Item*)> const& keep)
{
    Item* pick = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->GetMaxStackSize() != 1 || !proto->GetSellPrice() || proto->GetQuality() < ITEM_QUALITY_UNCOMMON)
            return ItemSearchCallbackResult::Continue;
        if (proto->GetClass() == ITEM_CLASS_QUEST || proto->GetStartQuest() || bot->HasQuestForItem(item->GetEntry()))
            return ItemSearchCallbackResult::Continue;
        if (item->IsSoulBound() || item->IsNotEmptyBag() || !item->CanBeTraded() || proto->HasFlag(ITEM_FLAG_CONJURED) || keep(item))
            return ItemSearchCallbackResult::Continue;
        pick = item;
        return ItemSearchCallbackResult::Stop;
    });
    return pick;
}

bool HasMail(Player* bot)
{
    time_t const now = GameTime::GetGameTime();
    for (Mail const* m : bot->GetMails())
        if (m->state != MAIL_STATE_DELETED && m->deliver_time <= now && !m->COD && (m->money || !m->items.empty()))
            return true;
    return false;
}

void PostSell(ObjectGuid botGuid, ObjectGuid auctioneer, ObjectGuid itemGuid)
{
    sBotMgr->PostWorldTask([botGuid, auctioneer, itemGuid]()
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld() || !bot->GetSession())
            return;
        Creature* npc = bot->GetNPCIfCanInteractWith(auctioneer, UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE);
        Item* item = bot->GetItemByGuid(itemGuid);
        if (!npc || !item)
        {
            Log(bot, "AH_REFUSED", npc ? "item to list is gone" : "auctioneer not in reach");
            return;
        }
        ItemTemplate const* proto = item->GetTemplate();
        uint32 const entry = item->GetEntry();
        BotAuctionPlan::Limits const lim = Lim();

        uint32 mine = 0, total = 0;
        if (AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(npc->GetFaction()))
            for (auto it = house->GetAuctionsBegin(); it != house->GetAuctionsEnd(); ++it)
            {
                ++total;
                if (it->second.Owner == botGuid)
                    ++mine;
            }

        uint64 const deposit = AuctionHouseMgr::GetItemAuctionDeposit(bot, item, Minutes(BotAuctionPlan::RUN_TIME_MINUTES));
        BotAuctionPlan::Verdict const v = BotAuctionPlan::CanList(lim, mine, total, bot->GetMoney(), deposit);
        if (v != BotAuctionPlan::Verdict::Ok)
        {
            char const* code = v == BotAuctionPlan::Verdict::NoMoney || v == BotAuctionPlan::Verdict::DepositTooHigh ? "AH_NO_MONEY" : "AH_LIMIT";
            Log(bot, code, Trinity::StringFormat("not listing item {}: {} (deposit {} copper, bot has {} listings, house {})", entry, BotAuctionPlan::VerdictCode(v), deposit, mine, total), entry);
            return;
        }

        uint64 const price = BotAuctionPlan::ListingPrice(proto->GetSellPrice(), 0, lim.UndercutPct);
        WorldPackets::AuctionHouse::AuctionSellItem pkt{ WorldPacket(CMSG_AUCTION_SELL_ITEM) };
        pkt.Auctioneer = auctioneer;
        pkt.MinBid = price;
        pkt.BuyoutPrice = price;
        pkt.RunTime = BotAuctionPlan::RUN_TIME_MINUTES;
        pkt.Items.push_back({ itemGuid, 1 });
        bot->GetSession()->HandleAuctionSellItem(pkt);

        if (!bot->GetItemByGuid(itemGuid))
            Log(bot, "AH_LISTED", Trinity::StringFormat("listed item {} for {} copper (deposit {})", entry, price, deposit), entry);
        else
            Log(bot, "AH_REFUSED", Trinity::StringFormat("the house refused item {} (throttle, price or deposit)", entry), entry);
    });
}

void PostMail(ObjectGuid botGuid, ObjectGuid mailbox)
{
    sBotMgr->PostWorldTask([botGuid, mailbox]()
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld() || !bot->GetSession())
            return;

        struct Take { uint64 MailId; uint64 Money; std::vector<uint64> Items; };
        std::vector<Take> takes;
        time_t const now = GameTime::GetGameTime();
        for (Mail const* m : bot->GetMails())
        {
            if (m->state == MAIL_STATE_DELETED || m->deliver_time > now || m->COD || (!m->money && m->items.empty()))
                continue;
            Take t{ m->messageID, m->money, {} };
            for (MailItemInfo const& info : m->items)
                t.Items.push_back(info.item_guid);
            takes.push_back(std::move(t));
            if (takes.size() >= 5)
                break;
        }

        uint64 const money0 = bot->GetMoney();
        uint32 items = 0;
        for (Take const& t : takes)
        {
            if (t.Money)
            {
                WorldPackets::Mail::MailTakeMoney pkt{ WorldPacket(CMSG_MAIL_TAKE_MONEY) };
                pkt.Mailbox = mailbox;
                pkt.MailID = t.MailId;
                pkt.Money = t.Money;
                bot->GetSession()->HandleMailTakeMoney(pkt);
            }
            for (uint64 attach : t.Items)
            {
                WorldPackets::Mail::MailTakeItem pkt{ WorldPacket(CMSG_MAIL_TAKE_ITEM) };
                pkt.Mailbox = mailbox;
                pkt.MailID = t.MailId;
                pkt.AttachID = attach;
                bot->GetSession()->HandleMailTakeItem(pkt);
                ++items;
            }
        }
        if (!takes.empty())
            Log(bot, "AH_MAIL", Trinity::StringFormat("collected {} mails: {} copper, {} items requested", takes.size(), bot->GetMoney() - money0, items));
    });
}
}
