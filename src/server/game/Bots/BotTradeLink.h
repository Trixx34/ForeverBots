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

// Trading items with bots by whisper (Bot.Trade.Link.*, default off). The player opens a trade with a bot of their group; the bot
// whispers the items it can give (tradable stacks only, as item links) and the player whispers back the links of the ones they
// want. The bot puts each into the window. The player confirms, the bot accepts through BotSocial::OnTradePlayerAccepted.
// Pure parsing and planning here, the game side (inventory scan, whispers, trade packets) in BotTradeLink.cpp.
// See docs/playerbots/feature-bot-trade-links-20261008.md.

#include "Define.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace BotTradeLink
{
    // Distinct item entries of the |Hitem:<entry>:...|h[name]|h links in `text`, in order of appearance, at most `maxItems`.
    TC_GAME_API std::vector<uint32> ParseItemLinks(std::string_view text, uint32 maxItems);

    struct Holding { uint32 Entry = 0; uint32 Count = 0; uint32 Quality = 0; };   // tradable items of that entry the bot carries

    // Listing order: better quality first, then larger count, then lower entry.
    TC_GAME_API void SortForListing(std::vector<Holding>& holdings);

    // Packs the pieces into lines of at most `maxLen` characters, separated by ", ". A piece longer than maxLen gets a line of its own.
    TC_GAME_API std::vector<std::string> PackLines(std::span<std::string const> pieces, size_t maxLen);

    struct OfferPlan
    {
        std::vector<uint32> Add;          // entries to put in the window, in order
        std::vector<uint32> Missing;      // wanted but not held
        std::vector<uint32> NoRoom;       // wanted but the window has no free slot left
    };

    // `alreadyIn` are the entries already in the bot's side of the window (ignored when wanted again).
    TC_GAME_API OfferPlan PlanOffer(std::span<uint32 const> wanted, std::span<Holding const> held, std::span<uint32 const> alreadyIn, uint32 freeSlots);

    TC_GAME_API bool Enabled();

    // BotSocial::OnTradeInitiated, after the window opened towards `bot`: the bot whispers what it can give.
    TC_GAME_API void OnTradeOpened(Player* player, Player* bot);

    // ChatHandler.cpp, CHAT_MSG_WHISPER: `player` whispered `text` to `receiver`; when that is a bot in a trade with them, linked items go into the window.
    TC_GAME_API void OnWhisper(Player* player, Player* receiver, std::string_view text);
}

#endif
