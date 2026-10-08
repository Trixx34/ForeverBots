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
#include "Config.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
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
        char const* end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(text.data() + pos, end, entry);
        if (ec != std::errc() || !entry || ptr == end || (*ptr != ':' && *ptr != '|'))
            continue;
        if (std::find(out.begin(), out.end(), entry) == out.end())
            out.push_back(entry);
    }
    return out;
}

void SortForListing(std::vector<Holding>& holdings)
{
    std::sort(holdings.begin(), holdings.end(), [](Holding const& a, Holding const& b)
    {
        if (a.Quality != b.Quality)
            return a.Quality > b.Quality;
        if (a.Count != b.Count)
            return a.Count > b.Count;
        return a.Entry < b.Entry;
    });
}

std::vector<std::string> PackLines(std::span<std::string const> pieces, size_t maxLen)
{
    std::vector<std::string> lines;
    for (std::string const& p : pieces)
    {
        if (!lines.empty() && lines.back().size() + 2 + p.size() <= maxLen)
            lines.back() += ", " + p;
        else
            lines.push_back(p);
    }
    return lines;
}

OfferPlan PlanOffer(std::span<uint32 const> wanted, std::span<Holding const> held, std::span<uint32 const> alreadyIn, uint32 freeSlots)
{
    OfferPlan plan;
    for (uint32 entry : wanted)
    {
        if (std::find(alreadyIn.begin(), alreadyIn.end(), entry) != alreadyIn.end())
            continue;
        if (std::none_of(held.begin(), held.end(), [entry](Holding const& h) { return h.Entry == entry && h.Count > 0; }))
            plan.Missing.push_back(entry);
        else if (plan.Add.size() >= freeSlots)
            plan.NoRoom.push_back(entry);
        else
            plan.Add.push_back(entry);
    }
    return plan;
}

namespace
{
bool Tradable(Item* item, Player* player)
{
    ItemTemplate const* proto = item->GetTemplate();
    return item->CanBeTraded(false, true) && !item->IsBindedNotWith(player) && !proto->HasFlag(ITEM_FLAG_HAS_LOOT)
        && proto->GetClass() != ITEM_CLASS_QUEST && !proto->GetStartQuest();
}

// Tradable stacks of the bot's bags (equipped items are never given away), summed per entry.
std::vector<Holding> Holdings(Player* bot, Player* player)
{
    std::map<uint32, Holding> byEntry;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (Tradable(item, player))
        {
            Holding& h = byEntry[item->GetEntry()];
            h.Entry = item->GetEntry();
            h.Quality = item->GetTemplate()->GetQuality();
            h.Count += item->GetCount();
        }
        return ItemSearchCallbackResult::Continue;
    });
    std::vector<Holding> out;
    for (auto const& [entry, h] : byEntry)
        out.push_back(h);
    return out;
}

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

std::string Link(uint32 entry)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
        return std::to_string(entry);
    uint32 const q = std::min<uint32>(proto->GetQuality(), MAX_ITEM_QUALITY - 1);
    return Trinity::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:0|h[{}]|h|r", ItemQualityColors[q], entry, proto->GetName(LOCALE_enUS));
}

void WhisperLines(Player* bot, Player* player, std::vector<std::string> const& lines)
{
    for (std::string const& l : lines)
        bot->Whisper(l, LANG_UNIVERSAL, player);
}

// the bot is in an open trade with this player
bool InTradeWith(Player* player, Player* bot)
{
    return bot->GetSession()->IsBot() && bot->GetTradeData() && bot->GetTradeData()->GetTrader() == player && player->GetTradeData();
}
}

bool Enabled()
{
    return sWorld->getBoolConfig(CONFIG_BOT_ENABLED) && sConfigMgr->GetBoolDefault("Bot.Trade.Link.Enabled", false);
}

void OnTradeOpened(Player* player, Player* bot)
{
    if (!Enabled() || !InTradeWith(player, bot))
        return;

    std::vector<Holding> held = Holdings(bot, player);
    if (held.empty())
    {
        WhisperLines(bot, player, { "I have nothing I can trade." });
        return;
    }
    SortForListing(held);
    uint32 const maxListed = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Trade.Link.MaxListed", 15), 1, 40));
    size_t const total = held.size();
    if (held.size() > maxListed)
        held.resize(maxListed);

    std::vector<std::string> pieces;
    for (Holding const& h : held)
        pieces.push_back(h.Count > 1 ? Trinity::StringFormat("{} x{}", Link(h.Entry), h.Count) : Link(h.Entry));
    std::vector<std::string> lines = PackLines(pieces, 200);
    lines.front() = "I can trade: " + lines.front();
    if (total > held.size())
        lines.push_back(Trinity::StringFormat("... and {} more. Whisper me the links of what you want.", total - held.size()));
    else
        lines.push_back("Whisper me the links of what you want.");
    WhisperLines(bot, player, lines);
}

void OnWhisper(Player* player, Player* receiver, std::string_view text)
{
    if (!Enabled() || !receiver || player->GetSession()->IsBot() || text.find("|Hitem:") == std::string_view::npos || !InTradeWith(player, receiver))
        return;
    Player* bot = receiver;

    TradeData* mine = bot->GetTradeData();
    std::vector<uint32> inWindow;
    uint8 firstFree = 0;
    uint32 freeSlots = 0;
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
    {
        if (Item* item = mine->GetItem(TradeSlots(i)))
            inWindow.push_back(item->GetEntry());
        else if (!freeSlots++)
            firstFree = i;
    }

    std::vector<uint32> const wanted = ParseItemLinks(text, TRADE_SLOT_TRADED_COUNT);
    std::vector<Holding> const held = Holdings(bot, player);
    OfferPlan plan = PlanOffer(wanted, held, inWindow, freeSlots);

    // free slots are not necessarily contiguous: walk the window again for each item
    for (uint32 entry : plan.Add)
    {
        Item* item = BestStack(bot, player, entry);
        uint8 slot = TRADE_SLOT_TRADED_COUNT;
        for (uint8 i = firstFree; i < TRADE_SLOT_TRADED_COUNT; ++i)
            if (!mine->GetItem(TradeSlots(i)))
            {
                slot = i;
                break;
            }
        if (!item || slot >= TRADE_SLOT_TRADED_COUNT)
        {
            plan.NoRoom.push_back(entry);
            continue;
        }
        WorldPackets::Trade::SetTradeItem set{WorldPacket(CMSG_SET_TRADE_ITEM)};
        set.TradeSlot = slot;
        set.PackSlot = item->GetBagSlot();
        set.ItemSlotInPack = item->GetSlot();
        bot->GetSession()->HandleSetTradeItemOpcode(set);
    }

    std::vector<std::string> notes;
    for (uint32 entry : plan.Missing)
        notes.push_back(Trinity::StringFormat("I have no {} to give.", Link(entry)));
    for (uint32 entry : plan.NoRoom)
        notes.push_back(Trinity::StringFormat("No room left in the window for {}.", Link(entry)));
    WhisperLines(bot, player, notes);
}
}
