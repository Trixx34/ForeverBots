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

#ifndef TRINITY_BOT_POPULATION_H
#define TRINITY_BOT_POPULATION_H

// Dynamic bot population (Bot.Population.*, default off): keeps the number of pool bots online at a target that follows the number of
// real players and the hour of the day, logging bots in and out in small steps. World thread, from BotMgr::Update.
// See docs/playerbots/feature-bot-population-20261008.md.

#include "Define.h"

namespace BotPopulation
{
    TC_GAME_API void Update(uint32 diff);
}

#endif
