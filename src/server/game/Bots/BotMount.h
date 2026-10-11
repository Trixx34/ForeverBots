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

#ifndef TRINITY_BOT_MOUNT_H
#define TRINITY_BOT_MOUNT_H

// Mount glue (strategy "mount", NonCombat and Combat engines; Bot.AI.Mount.*, default off). Reads the facts of a bot that follows a
// leader, asks BotMountPlan.h whether to mount or dismount and carries it out. The chat orders `mount` and `dismount` (BotChat.cpp) use
// the same mount choice and tell this module which rides a player ordered.
// See docs/playerbots/feature-bot-mounts-20261008.md.

#include "Define.h"

class BotAI;
class Player;

namespace BotMount
{
    // True when the automatic mounting is switched on (Bot.AI.Mount.Enabled).
    TC_GAME_API bool Enabled();

    // Mounts the bot on its best ground mount (the fastest one when wantSpeed <= 0, else see PickMatching). Stops the follow leg first,
    // movement interrupts the cast. Returns "OK", "NO_MOUNT" (no ground mount spell known) or "CANT_MOUNT" (the cast was refused).
    TC_GAME_API char const* MountUp(BotAI* ai, Player* bot, int32 wantSpeed);

    // A player ordered the bot to mount or dismount in chat: an ordered ride is not ended by the automatic dismount.
    TC_GAME_API void NoteOrder(BotAI* ai, bool mounted);
}

#endif
