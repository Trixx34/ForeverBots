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

#ifndef TRINITY_BOT_QUEST_H
#define TRINITY_BOT_QUEST_H

// Bot questing ("quest" strategy, NonCombat engine): pick a quest by level and position, walk to the giver, accept, work the
// objectives (kill / loot / talk), turn in with a reward choice. Every blocked case is logged as quest_blocked with a stable
// reason code (docs/playerbots/quest-design.md section 6.2). See docs/playerbots/progress.md "Quest pipeline".
//
// The static data (starter grid, spawn points, kill-credit map, quest-item drop sources) is built once on the world thread by
// EnsureIndex() and is read-only afterwards, so map threads never query the database.

#include "Define.h"
#include <string>

class BotAI;
class Player;

namespace BotQuest
{
    // World thread, before any bot AI exists (called from BotMgr). Idempotent. One synchronous query for the quest-item loot
    // tables, everything else comes from the ObjectMgr caches.
    TC_GAME_API void EnsureIndex();
    TC_GAME_API bool IsReady();
    // One-line summary of the bot's current quest task (console / diagnostics). Map thread or a quiet world thread.
    TC_GAME_API std::string DescribeTask(BotAI* ai);
}

#endif
