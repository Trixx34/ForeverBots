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

#ifndef TRINITY_BOT_PLAYER_LED_H
#define TRINITY_BOT_PLAYER_LED_H

// Glue for the player-led dungeon decisions in BotPlayerLedPlan.h (Bot.AI.Dungeon.PlayerLed.*, default off): groups whose leader is a real
// player and whose bots follow that player. World thread only (BotMgr::Update).
// See docs/playerbots/feature-bot-player-led-dungeon-20261008.md.

#include "Define.h"

class BotAI;

namespace BotPlayerLed
{
    // Called by BotMgr::Update every world tick.
    TC_GAME_API void Update(uint32 diff);

    // True while the bot follows a player into or inside a dungeon: the quest AI stands back. Thread-safe (map threads call it).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
