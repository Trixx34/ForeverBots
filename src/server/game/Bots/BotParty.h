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

#ifndef TRINITY_BOT_PARTY_H
#define TRINITY_BOT_PARTY_H

// Glue for the party decisions in BotPartyPlan.h (Bot.AI.Party.*, default off): bots that are close, of similar level and carry the same
// unfinished quest form a real Group out in the world; the members follow the leader (who keeps questing) and fight with their normal
// combat AI, so kills count for everybody. World thread only (BotMgr::Update).
// See docs/playerbots/feature-bot-parties-20261008.md.

#include "Define.h"

class BotAI;

namespace BotParty
{
    // Called by BotMgr::Update every world tick.
    TC_GAME_API void Update(uint32 diff);

    // True while the bot is a follower (not the leader) of a bot party: the quest AI stands back so the follow movement is not
    // overridden. Thread-safe (map threads call it).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
