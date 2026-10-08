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

#ifndef TRINITY_BOT_DUNGEON_RUN_H
#define TRINITY_BOT_DUNGEON_RUN_H

// Glue for the dungeon decisions in BotDungeonPlan.h (Bot.AI.Dungeon.*, default off): forms one group of online bots, gathers it,
// walks the leader to the dungeon entrance, teleports the group in and clears the packs one by one. World thread only (BotMgr::Update).
// One run at a time in this version. See docs/playerbots/feature-bot-dungeon-groups-20261008.md.

#include "Define.h"

class BotAI;

namespace BotDungeonRun
{
    // Called by BotMgr::Update every world tick.
    TC_GAME_API void Update(uint32 diff);

    // True while the bot belongs to the running dungeon group: the quest AI stands back so the group's movement is not overridden.
    // Thread-safe (map threads call it).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
