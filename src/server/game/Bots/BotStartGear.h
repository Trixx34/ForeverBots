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

#ifndef TRINITY_BOT_START_GEAR_H
#define TRINITY_BOT_START_GEAR_H

// Starter gear for bots (Bot.StartGear.*). At login, and again every LevelStep levels, every EMPTY equipment slot of a bot is filled
// with the best item of the bot's class and level from the item templates: the best armor type the class can wear, the best weapon
// (two-hander against one-hander plus off hand by BotGear score), rings, neck and cloak. Slots that already hold an item are never
// touched, so the call is idempotent and never takes anything away from a bot that has gear. Works for any bot, including the ones the
// simulation creates, because it runs from the bot's own first tick.

#include "Define.h"
#include <string>
#include <vector>

class Player;

namespace BotStartGear
{
    struct Granted
    {
        uint8 Slot = 0;
        uint32 Entry = 0;
        uint32 ReqLevel = 0;
        uint32 Quality = 0;
        std::string Name;
    };

    struct Result
    {
        bool Ran = false;
        uint32 EmptyBefore = 0;      // equipment slots empty when the pass started (among the slots the pass handles)
        uint32 Candidates = 0;       // templates that passed the class/level prefilter
        std::vector<Granted> Items;
    };

    TC_GAME_API bool Enabled();
    TC_GAME_API uint32 LevelStep();
    TC_GAME_API uint32 MinLevel();

    // Fills the empty slots of `bot`. Never replaces a worn item.
    TC_GAME_API Result Grant(Player* bot);
}

#endif
