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

#ifndef TRINITY_BOT_BUFFS_H
#define TRINITY_BOT_BUFFS_H

// Group buffs glue (strategy "buffs", NonCombat engine; Bot.AI.Buffs.*, default off): buffing classes keep Power Word: Fortitude, Divine
// Spirit, Arcane Intellect, Mark of the Wild and the paladin blessings of Might and Wisdom up on themselves and on the group members
// near them. See docs/playerbots/feature-bot-group-buffs-20261008.md.

#include "Define.h"

namespace BotBuff
{
    // True when Bot.AI.Buffs.Enabled is set.
    TC_GAME_API bool Enabled();
}

#endif
