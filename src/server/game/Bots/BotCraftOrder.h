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

#ifndef TRINITY_BOT_CRAFT_ORDER_H
#define TRINITY_BOT_CRAFT_ORDER_H

// Crafting for players (Bot.Craft.*, default off). A player whispers a bot of their group "craft <item link or name> [x5]" (or "craft list");
// the bot crafts it with the recipes and reagents it has, then mails the result to the player, optionally as cash on delivery.
// Pure parsing, matching and planning here, the game side (recipe scan, casts, mail) in BotCraftOrder.cpp.
// See docs/playerbots/feature-bot-craft-orders-20261008.md.

#include "Define.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace BotCraftOrder
{
    enum class Kind : uint8 { None, List, Craft };

    struct Request
    {
        Kind Type = Kind::None;
        uint32 Count = 1;        // items wanted (not casts), 1..maxCount
        uint32 ItemEntry = 0;    // from an item link, else 0
        std::string Name;        // lower case free text, empty for a link or a list
    };

    // "craft list" | "craft <|Hitem:..|h[..]|h link>" | "craft <name words>", each with an optional count as "x5", "5x" or "5" (first or last word).
    // Kind::None when the text is not a craft order (first word is not "craft", or nothing was asked for).
    TC_GAME_API Request ParseRequest(std::string_view text, uint32 maxCount);

    struct Option
    {
        uint32 Spell = 0;
        uint32 Item = 0;
        uint32 Yield = 1;        // items one cast makes
        std::string Name;        // lower case item name
    };

    // Index of the recipe that answers the request, -1 when none. A link matches by item entry; a name matches exactly, then as a
    // prefix, then as a substring (ties: shorter name, then lower spell id).
    TC_GAME_API int Match(Request const& request, std::span<Option const> options);

    struct Facts
    {
        uint32 Wanted = 0;       // items asked for
        uint32 Yield = 1;        // items per cast
        uint32 MaxItems = 0;     // Bot.Craft.MaxItems
        uint32 MaxStack = 1;     // item stack size
        uint32 CastsPossible = 0;   // casts the bot's reagents allow right now
    };

    struct Plan
    {
        uint32 Casts = 0;
        uint32 Items = 0;        // Casts * Yield
        char const* Refusal = nullptr;   // NO_REAGENTS | TOO_MANY when nothing can be made
        bool Trimmed = false;    // fewer than wanted
    };

    // Casts needed for the wanted items (rounded up), limited by the reagents and by the per order cap (the item cap and the mail's attachment slots).
    TC_GAME_API Plan PlanOrder(Facts const& facts);

    // Stack sizes the items are mailed in (full stacks, the rest last).
    TC_GAME_API std::vector<uint32> MailStacks(uint32 items, uint32 maxStack);

    // Cash on delivery for the order: vendor sell price * items * percent / 100, saturating at 2^32-1 copper (the mail limit). 0 when free.
    TC_GAME_API uint32 Fee(uint32 sellPrice, uint32 items, uint32 percent);

    // "2 gold 5 silver 3 copper" style text of an amount.
    TC_GAME_API std::string FormatMoney(uint32 copper);

    TC_GAME_API bool Enabled();

    // ChatHandler.cpp, CHAT_MSG_WHISPER: `player` whispered `text` to `receiver`; a craft order to a bot of their group is queued.
    TC_GAME_API void OnWhisper(Player* player, Player* receiver, std::string_view text);

    // BotMgr::Update: casts the queued crafts one by one and mails the result.
    TC_GAME_API void Update(uint32 diff);
}

#endif
