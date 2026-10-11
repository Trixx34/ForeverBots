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

#ifndef TRINITY_BOT_BATTLEGROUND_H
#define TRINITY_BOT_BATTLEGROUND_H

// Glue for the battleground decisions in BotBattlegroundPlan.h (Bot.AI.Battleground.*, default off): bots join the Warsong Gulch queue
// next to the players who queue, accept the invitation after a human-like delay, play the flag objectives, wait for the spirit healer
// when they die and leave when the match is over. World thread only (BotMgr::Update). See
// docs/playerbots/feature-bot-battlegrounds-20261008.md.

#include "BotBattlegroundPlan.h"

class BotAI;

namespace BotBattleground
{
    // Bot.AI.Battleground.*, read once.
    TC_GAME_API Config const& Cfg();

    // Called by BotMgr::Update every world tick.
    TC_GAME_API void Update(uint32 diff);

    // True while the bot is inside a battleground: the quest AI and the group modules stand back. Thread-safe (map threads call it).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
