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

#ifndef TRINITY_BOT_TRADE_LINK_H
#define TRINITY_BOT_TRADE_LINK_H

// Item links in party chat start trades with bots (Bot.Trade.Link.*, default off). A party member links one or more items; for every
// linked item the bot of the group holding the most of it (tradable stacks only) opens a trade with that player and puts the item in
// the window. The player confirms the trade and the bot accepts through BotSocial::OnTradePlayerAccepted as for any trade.
// Pure parsing and planning here, the game side (group scan, trade packets, queue) in BotTradeLink.cpp.
// See docs/playerbots/feature-bot-trade-links-20261008.md.

#include "Define.h"
#include <span>
#include <string_view>
#include <vector>

class Player;

namespace BotTradeLink
{
    // Distinct item entries of the |Hitem:<entry>:...|h[name]|h links in `text`, in order of appearance, at most `maxItems`.
    TC_GAME_API std::vector<uint32> ParseItemLinks(std::string_view text, uint32 maxItems);

    struct Holding { uint32 Entry = 0; uint32 Count = 0; };   // tradable items of that entry the bot carries

    struct BotStock
    {
        uint64 Bot = 0;                   // opaque id (guid counter)
        bool InRange = true;              // close enough to open a trade window
        std::vector<Holding> Items;
    };

    struct Trade { uint64 Bot = 0; std::vector<uint32> Items; };   // one trade window: the entries the bot puts in

    enum class Skip : uint8 { NoneHave, TooMany };
    struct SkippedItem { uint32 Entry = 0; Skip Why = Skip::NoneHave; };

    struct Plan
    {
        std::vector<Trade> Trades;
        std::vector<SkippedItem> Skipped;
    };

    // Each linked item goes to the in-range bot with the highest count (ties: the first bot of the span). The items of one bot are
    // grouped in the order they were linked and split into windows of at most `maxSlots` items; more than `maxTrades` windows in total
    // drop the later ones (TooMany). Items nobody holds are skipped (NoneHave).
    TC_GAME_API Plan PlanTrades(std::span<uint32 const> wanted, std::span<BotStock const> bots, uint32 maxSlots, uint32 maxTrades);

    // ChatHandler.cpp, CHAT_MSG_PARTY: `issuer` said `text` in party chat. Queues trades when it contains item links. World thread.
    TC_GAME_API void OnPartyChat(Player* issuer, std::string_view text);

    // TradeHandler.cpp, HandleBeginTradeOpcode: `player` opened the window of a trade `bot` proposed; the bot puts its items in.
    TC_GAME_API void OnTradeBegun(Player* player, Player* bot);

    // BotSocial::OnTradeExecuting: the trade between `player` and `bot` is about to complete.
    TC_GAME_API void OnTradeExecuting(Player* player, Player* bot);

    // World thread, BotSocial::Update: starts the queued trades one at a time and drops the ones left open.
    TC_GAME_API void Update(uint32 diff);

    // A player logs out: forgets their queue.
    TC_GAME_API void OnPlayerLogout(uint64 guidCounter);
}

#endif
