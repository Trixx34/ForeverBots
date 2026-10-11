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

#ifndef TRINITY_BOT_REVIVE_H
#define TRINITY_BOT_REVIVE_H

// Resurrection glue (strategies "revive" for priest, paladin and shaman bots and "revive_accept" for dead bots; Bot.AI.Revive.*,
// default off). The choice is in BotRevivePlan.cpp. See docs/playerbots/feature-bot-revive-repair-20261008.md.

#include "BotRevivePlan.h"

class BotAI;
class Player;

namespace BotRevive
{
    // Bot.AI.Revive.*, read once.
    TC_GAME_API Config const& Cfg();

    // True while a dead bot should keep its spirit because a group member is able to raise it (BotBehavior.cpp release_spirit).
    TC_GAME_API bool HoldRelease(BotAI* ai, Player* deadBot);

    // True for the classes that carry a resurrection spell here (priest, paladin, shaman).
    TC_GAME_API bool ClassCanRevive(uint8 playerClass);
}

#endif
