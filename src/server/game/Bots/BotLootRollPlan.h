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

#ifndef TRINITY_BOT_LOOT_ROLL_PLAN_H
#define TRINITY_BOT_LOOT_ROLL_PLAN_H

// Vote of a bot in a group loot roll (Bot.Loot.Roll.*, BotLootRoll.cpp). Pure over plain facts so it is unit tested without a map or a
// Player. See docs/playerbots/feature-bot-group-loot-20261008.md.

#include "Define.h"

namespace BotLootRoll
{
    enum class Vote : uint8 { Pass, Need, Greed };

    struct Config
    {
        bool GreedOnOther = true;     // greed on items the bot cannot or need not use (it sells them), else pass
    };

    struct Facts
    {
        bool NeedAllowed = true;      // the roll offers Need (not for greed-only items)
        bool Usable = false;          // the bot's class can wear or use the item at its level
        bool Upgrade = false;         // it beats what the bot wears in its best slot
        bool QuestItem = false;       // an active quest of the bot needs it
        bool BagFull = false;         // no room to take it
    };

    TC_GAME_API Vote Decide(Facts const& f, Config const& cfg);
}

#endif
