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

#ifndef TRINITY_BOT_COOK_PLAN_H
#define TRINITY_BOT_COOK_PLAN_H

// Cooking for other people (Bot.AI.Cooking.*, Bot.Chat.Food.*, BotCooking.cpp). A bot with the cooking skill keeps a stock of food and of
// buff food ("Well Fed") for the players it travels with: it cooks what it caught or carries until the stock targets are met, even when
// the recipes no longer raise its skill, and it tells a player what it can hand over when asked. Pure over plain data so it is unit
// tested without a map or a Player. See docs/playerbots/feature-bot-fishing-cooking-20261008.md.

#include "Define.h"
#include <span>
#include <vector>

namespace BotCook
{
    enum class Kind : uint8 { None, Plain, Buff, Drink };

    struct Config
    {
        uint32 StockPlain = 20;      // plain food (health only) the bot cooks up to
        uint32 StockBuff = 10;       // buff food the bot cooks up to
        uint32 MinFreeSlots = 4;     // cooking stops when fewer bag slots are free
    };

    struct Stock
    {
        uint32 Plain = 0;
        uint32 Buff = 0;
    };

    struct Recipe
    {
        uint32 SpellId = 0;
        Kind Produces = Kind::None;
        int32 Yellow = 0;            // skill below this: the skill-up is certain
        int32 Grey = 0;              // skill at or above this: never skills up (the food is still made)
        uint32 Batches = 0;          // casts the reagents in the bags allow
        uint32 Yield = 1;            // items per cast
    };

    // Which kind the bot should cook next: buff food while below its target, then plain food; None when both are stocked.
    TC_GAME_API Kind Needed(Stock const& stock, Config const& cfg);

    // Index of the recipe to cook for the stock, -1 when nothing is needed or nothing can be made. Recipes of the needed kind that still raise
    // the skill come first, then the one that makes the better food (higher grey threshold), then the lower spell id. When the needed kind has
    // no recipe the other unmet kind is tried.
    TC_GAME_API int PickStock(int32 skillValue, Stock const& stock, Config const& cfg, std::span<Recipe const> recipes);

    // True when cooking should wait for room in the bags.
    TC_GAME_API bool BagsTooFull(uint32 freeSlots, Config const& cfg);

    struct Food
    {
        uint32 Entry = 0;
        uint32 Count = 0;
        Kind K = Kind::Plain;
        uint32 ReqLevel = 0;
    };

    // Indexes of the foods to offer a player of `playerLevel`, at most `maxListed`: only what the player can use, buff food first, then plain
    // food, then drink; inside a kind the higher required level (better food), then the larger stack, then the lower entry.
    TC_GAME_API std::vector<size_t> OfferOrder(std::span<Food const> held, uint32 playerLevel, uint32 maxListed);

    // Total of one kind in a list of holdings.
    TC_GAME_API uint32 CountOf(std::span<Food const> held, Kind kind);
}

#endif
