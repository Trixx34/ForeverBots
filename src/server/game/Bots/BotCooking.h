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

#ifndef TRINITY_BOT_COOKING_H
#define TRINITY_BOT_COOKING_H

// Game side of BotCookPlan.h: reads the bags, classifies food, describes cooking recipes and answers the "food" chat command.
// Settings: Bot.AI.Cooking.* and Bot.Chat.Food.Enabled (botserver.conf).

#include "BotCookPlan.h"
#include "Define.h"
#include <vector>

class ItemTemplate;
class Player;
class SpellInfo;

namespace BotCooking
{
    // Bot.AI.Cooking.Enabled (needs Bot.Enabled): cook for the stock, not only for skill-ups.
    TC_GAME_API bool Enabled();
    TC_GAME_API BotCook::Config const& Cfg();

    // Bot.Chat.Food.Enabled: the `food` chat command is accepted.
    TC_GAME_API bool ChatEnabled();

    // Food, buff food or drink by what its on-use spell does; None for anything else. Eating and drinking auras only restore health or
    // power, any other aura (or a spell the food triggers) makes it buff food.
    TC_GAME_API BotCook::Kind Classify(ItemTemplate const* proto);

    // Tradable food and drink in the bags of `bot`, summed per entry. Items bound to someone else are left out when `to` is given.
    TC_GAME_API std::vector<BotCook::Food> Holdings(Player* bot, Player* to);

    TC_GAME_API BotCook::Stock CountStock(Player* bot);

    // Fills the item kind, yield and the number of casts the bags allow for a cooking spell. False when the spell makes no food.
    TC_GAME_API bool Describe(Player* bot, SpellInfo const* spell, BotCook::Recipe& out);

    // `food` chat command: the bot whispers `to` what it can hand over, as item links. Returns the refusal code logged, "OK" when it did.
    TC_GAME_API char const* OfferFood(Player* bot, Player* to);
}

#endif
