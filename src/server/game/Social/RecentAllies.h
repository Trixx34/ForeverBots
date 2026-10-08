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

#ifndef TRINITY_RECENT_ALLIES_H
#define TRINITY_RECENT_ALLIES_H

#include "Define.h"

class Creature;
class Group;
class Player;

// The Allies tab of the Classic 1.60 Social window: players you grouped, traded, whispered or fought with in the last 30 days
// (characters.character_recent_allies).
namespace RecentAllies
{
    TC_GAME_API void OnGroupJoin(Group const* group, Player const* newcomer);
    TC_GAME_API void OnTrade(Player const* player, Player const* trader);
    TC_GAME_API void OnWhisper(Player const* sender, Player const* receiver);
    TC_GAME_API void OnCreatureKill(Creature* creature);    // "fought together": every player fighting the creature when it died
}

#endif
