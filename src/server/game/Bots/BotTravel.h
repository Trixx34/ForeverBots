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


#ifndef TRINITY_BOT_TRAVEL_H
#define TRINITY_BOT_TRAVEL_H

// Travel and leveling progression glue (strategy "travel", NonCombat engine; Bot.AI.Travel.*, default off). The decisions are in
// BotTravelPlan.cpp; this file reads the game state and carries them out: leave a zone the bot has outgrown, walk or fly to the next one,
// learn flight masters on the way, use the hearthstone and bind at inns. See docs/playerbots/feature-bot-travel-20261008.md.

#include "Define.h"

class BotAI;
class Player;

namespace BotTravel
{
    // BotAI::OnLogout (map thread): drops a running trip.
    TC_GAME_API void OnLogout(BotAI* ai, Player* bot);
    // True while a trip holds the bot (walking to the next zone, flying, hearthing). The quest layer yields while it is true.
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
