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

#ifndef TRINITY_BOT_WATCHDOG_H
#define TRINITY_BOT_WATCHDOG_H

// Bot watchdog glue (strategy "watchdog", NonCombat engine; Bot.AI.Watchdog.*, default off). Reads a progress snapshot of the bot, asks
// BotWatchdogPlan.h whether it is stalled and carries out the recovery: clear the goal, hearthstone, teleport to the home bind.
// See docs/playerbots/feature-bot-watchdog-20261008.md.

#include "Define.h"

namespace BotWatchdog
{
    // True when the watchdog is switched on (Bot.AI.Watchdog.Enabled).
    TC_GAME_API bool Enabled();
}

#endif
