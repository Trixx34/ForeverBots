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

#ifndef TRINITY_BOT_REPAIR_H
#define TRINITY_BOT_REPAIR_H

// Paid repairs and potion restocking glue (Bot.AI.Repair.*, default off), called by the dungeon run before a group goes in and when a
// member's gear is nearly broken. The decisions are in BotRepairPlan.cpp. There is no walk to a vendor: the visit is abstract, like the
// free repair it replaces. See docs/playerbots/feature-bot-revive-repair-20261008.md.

#include "BotRepairPlan.h"

class BotAI;
class Player;

namespace BotRepair
{
    // Bot.AI.Repair.*, read once.
    TC_GAME_API Config const& Cfg();

    // Repairs the bot's gear (paid or free by the settings) and restocks potions. force = repair whatever the durability. Logs
    // REPAIR_PAID / REPAIR_FREE / REPAIR_TOO_POOR and SUPPLY_BOUGHT. Returns true when anything was done.
    TC_GAME_API bool Prepare(BotAI* ai, Player* bot, char const* context, bool force);
}

#endif
