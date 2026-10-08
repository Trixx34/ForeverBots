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

#include "BotTradeLink.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "GroupReference.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <charconv>
#include <map>

namespace BotTradeLink
{
std::vector<uint32> ParseItemLinks(std::string_view text, uint32 maxItems)
{
    std::vector<uint32> out;
    constexpr std::string_view tag = "|Hitem:";
    for (size_t pos = text.find(tag); pos != std::string_view::npos && out.size() < maxItems; pos = text.find(tag, pos))
    {
        pos += tag.size();
        uint32 entry = 0;
        char const* begin = text.data() + pos;
        char const* end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, entry);
        if (ec != std::errc() || !entry || ptr == end || (*ptr != ':' && *ptr != '|'))
            continue;
        if (std::find(out.begin(), out.end(), entry) == out.end())
            out.push_back(entry);
    }
    return out;
}

Plan PlanTrades(std::span<uint32 const> wanted, std::span<BotStock const> bots, uint32 maxSlots, uint32 maxTrades)
{
    Plan plan;
    maxSlots = std::max<uint32>(maxSlots, 1);

    std::vector<uint64> order;                       // bots in order of their first item
    std::map<uint64, std::vector<uint32>> perBot;
    for (uint32 entry : wanted)
    {
        BotStock const* best = nullptr;
        uint32 bestCount = 0;
        for (BotStock const& b : bots)
        {
            if (!b.InRange)
                continue;
            for (Holding const& h : b.Items)
                if (h.Entry == entry && h.Count > bestCount)
                {
                    best = &b;
                    bestCount = h.Count;
                }
        }
        if (!best)
        {
            plan.Skipped.push_back({ entry, Skip::NoneHave });
            continue;
        }
        if (perBot.find(best->Bot) == perBot.end())
            order.push_back(best->Bot);
        perBot[best->Bot].push_back(entry);
    }

    for (uint64 bot : order)
    {
        std::vector<uint32> const& items = perBot[bot];
        for (size_t i = 0; i < items.size(); i += maxSlots)
        {
            size_t const n = std::min<size_t>(maxSlots, items.size() - i);
            if (plan.Trades.size() >= maxTrades)
            {
                for (size_t k = 0; k < n; ++k)
                    plan.Skipped.push_back({ items[i + k], Skip::TooMany });
                continue;
            }
            plan.Trades.push_back({ bot, std::vector<uint32>(items.begin() + i, items.begin() + i + n) });
        }
    }
    return plan;
}

namespace
{
struct Session
{
    std::vector<Trade> Queue;       // front = current
    ObjectGuid ActiveBot;           // bot whose proposal is open (empty = none open)
    uint32 LeftMs = 0;
    bool Completed = false;         // the open trade went through
};
std::map<ObjectGuid, Session> Sessions;   // by requesting player

bool Enabled()
{
    return sWorld->getBoolConfig(CONFIG_BOT_ENABLED) && sConfigMgr->GetBoolDefault("Bot.Trade.Link.Enabled", false);
}

uint32 TimeoutMs()
{
    return uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Trade.Link.TimeoutSeconds", 60), 10, 600)) * 1000;
}

void Log(Player* bot, bool accepted, char const* reason, std::string summary)
{
    BotAI* ai = bot && bot->GetSession() ? bot->GetSession()->GetBotAI() : nullptr;
    if (!ai)
        return;
    BotEvent e = ai->MakeEvent(bot, "trade", accepted ? BOTLOG_INFO : BOTLOG_WARN, reason, std::move(summary));
    e.Details = Trinity::StringFormat("{{\"outcome\":\"{}\"}}", accepted ? "accepted" : "refused");
    sBotMgr->LogEvent(std::move(e));
}

// A bot may give to this player: alt bots only to their owner account (as for every alt bot trade), world bots to their group.
bool MayTrade(Player* player, Player* bot)
{
    if (!bot->GetSession()->IsBot() || player->GetSession()->IsBot() || !bot->IsAlive() || bot->IsInCombat() || bot->GetTradeData())
        return false;
    if (bot->GetSession()->IsAltBot() && bot->GetSession()->GetAccountId() != player->GetSession()->GetAccountId())
        return false;
    return true;
}

bool Tradable(Item* item, Player* player)
{
    return item->CanBeTraded(false, true) && !item->IsBindedNotWith(player) && !item->GetTemplate()->HasFlag(ITEM_FLAG_HAS_LOOT)
        && item->GetTemplate()->GetClass() != ITEM_CLASS_QUEST && !item->GetTemplate()->GetStartQuest();
}

// The largest tradable stack of `entry` the bot carries (equipped items are never given away).
Item* BestStack(Player* bot, Player* player, uint32 entry)
{
    Item* best = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (item->GetEntry() == entry && Tradable(item, player) && (!best || item->GetCount() > best->GetCount()))
            best = item;
        return ItemSearchCallbackResult::Continue;
    });
    return best;
}

uint32 TradableCount(Player* bot, Player* player, uint32 entry)
{
    uint32 n = 0;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (item->GetEntry() == entry && Tradable(item, player))
            n += item->GetCount();
        return ItemSearchCallbackResult::Continue;
    });
    return n;
}

void Tell(Player* player, std::string const& line)
{
    if (player->GetSession() && !player->GetSession()->IsBot())
        ChatHandler(player->GetSession()).SendSysMessage(line);
}

// Plans and queues the trades; separate from the chat hook so the scan runs once per message.
void Request(Player* player, std::vector<uint32> const& wanted)
{
    Group* group = player->GetGroup();
    if (!group)
        return;
    float const yards = std::clamp(sConfigMgr->GetFloatDefault("Bot.Trade.Link.MaxYards", 10.0f), 1.0f, TRADE_DISTANCE - 0.5f);

    std::vector<BotStock> stock;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* bot = ref.GetSource();
        if (!bot || bot == player || !MayTrade(player, bot))
            continue;
        BotStock s;
        s.Bot = bot->GetGUID().GetCounter();
        s.InRange = bot->IsWithinDistInMap(player, yards, false);
        for (uint32 entry : wanted)
            if (uint32 n = TradableCount(bot, player, entry))
                s.Items.push_back({ entry, n });
        stock.push_back(std::move(s));
    }

    uint32 const maxItems = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Trade.Link.MaxTrades", 3), 1, 10));
    Plan plan = PlanTrades(wanted, stock, TRADE_SLOT_TRADED_COUNT, maxItems);

    for (SkippedItem const& s : plan.Skipped)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(s.Entry);
        Tell(player, Trinity::StringFormat("Bots: no item {}: {}", proto ? proto->GetName(LOCALE_enUS) : std::to_string(s.Entry),
            s.Why == Skip::TooMany ? "too many trades for one request" : "no bot of your party near you carries it (tradable)"));
    }
    if (plan.Trades.empty())
        return;
    Session& session = Sessions[player->GetGUID()];
    session.Queue = std::move(plan.Trades);
    session.ActiveBot = ObjectGuid::Empty;
    session.Completed = false;
}

// Opens the proposal of the front trade; false when it cannot be opened (the trade is dropped).
bool StartFront(Player* player, Session& session)
{
    Trade const& t = session.Queue.front();
    Player* bot = nullptr;
    if (Group* group = player->GetGroup())
        for (GroupReference const& ref : group->GetMembers())
            if (ref.GetSource() && ref.GetSource()->GetGUID().GetCounter() == t.Bot)
                bot = ref.GetSource();
    if (!bot || player->GetTradeData() || !MayTrade(player, bot) || !bot->IsWithinDistInMap(player, TRADE_DISTANCE - 0.5f, false))
        return false;

    WorldPackets::Trade::InitiateTrade init{WorldPacket(CMSG_INITIATE_TRADE)};
    init.Guid = player->GetGUID();
    bot->GetSession()->HandleInitiateTradeOpcode(init);
    if (!bot->GetTradeData())
        return false;
    session.ActiveBot = bot->GetGUID();
    session.LeftMs = TimeoutMs();
    session.Completed = false;
    return true;
}
}

void OnPartyChat(Player* issuer, std::string_view text)
{
    if (!Enabled() || !issuer || issuer->GetSession()->IsBot() || text.find("|Hitem:") == std::string_view::npos)
        return;
    std::vector<uint32> const wanted = ParseItemLinks(text, uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Trade.Link.MaxItems", 12), 1, 30)));
    if (!wanted.empty())
        Request(issuer, wanted);
}

void OnTradeBegun(Player* player, Player* bot)
{
    auto it = Sessions.find(player->GetGUID());
    if (it == Sessions.end() || it->second.ActiveBot != bot->GetGUID() || it->second.Queue.empty() || !bot->GetTradeData())
        return;

    uint8 slot = 0;
    for (uint32 entry : it->second.Queue.front().Items)
    {
        Item* item = BestStack(bot, player, entry);
        if (!item || slot >= TRADE_SLOT_TRADED_COUNT)
            continue;
        WorldPackets::Trade::SetTradeItem set{WorldPacket(CMSG_SET_TRADE_ITEM)};
        set.TradeSlot = slot++;
        set.PackSlot = item->GetBagSlot();
        set.ItemSlotInPack = item->GetSlot();
        bot->GetSession()->HandleSetTradeItemOpcode(set);
    }
    if (!slot)
        bot->TradeCancel(true, TRADE_STATUS_CANCELLED);
}

void OnTradeExecuting(Player* player, Player* bot)
{
    auto it = Sessions.find(player->GetGUID());
    if (it != Sessions.end() && it->second.ActiveBot == bot->GetGUID())
        it->second.Completed = true;
}

void Update(uint32 diff)
{
    for (auto it = Sessions.begin(); it != Sessions.end();)
    {
        Session& s = it->second;
        Player* player = ObjectAccessor::FindPlayer(it->first);
        if (!player || s.Queue.empty() || !Enabled())
        {
            it = Sessions.erase(it);
            continue;
        }

        if (!s.ActiveBot.IsEmpty())
        {
            Player* bot = ObjectAccessor::FindPlayer(s.ActiveBot);
            if (bot && bot->GetTradeData())
            {
                if (s.LeftMs > diff)
                    s.LeftMs -= diff;
                else
                {
                    Log(bot, false, "TRADE_LINK_TIMEOUT", Trinity::StringFormat("item trade with {} left open, cancelled", player->GetName()));
                    bot->TradeCancel(true, TRADE_STATUS_CANCELLED);
                    s.Queue.clear();
                }
                ++it;
                continue;
            }
            // the window is gone: a completed trade moves on to the next one, a cancelled one ends the request
            if (!s.Completed)
                s.Queue.clear();
            else
                s.Queue.erase(s.Queue.begin());
            s.ActiveBot = ObjectGuid::Empty;
            if (s.Queue.empty())
            {
                it = Sessions.erase(it);
                continue;
            }
        }

        if (!StartFront(player, s))
        {
            Tell(player, "Bots: could not open the next item trade (bot too far away or busy).");
            it = Sessions.erase(it);
            continue;
        }
        ++it;
    }
}

void OnPlayerLogout(uint64 guidCounter)
{
    for (auto it = Sessions.begin(); it != Sessions.end();)
        it = it->first.GetCounter() == guidCounter ? Sessions.erase(it) : std::next(it);
}
}
