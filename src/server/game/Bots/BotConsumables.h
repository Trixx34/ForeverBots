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

#ifndef TRINITY_BOT_CONSUMABLES_H
#define TRINITY_BOT_CONSUMABLES_H

// Consumables glue (strategy "consumables", Combat engine; Bot.AI.Consumables.*, default off): bots drink healing and mana potions from
// their bags in a fight when the class heal cannot help. The choice is in BotConsumablePlan.cpp.
// See docs/playerbots/feature-bot-consumables-20261008.md.

#include "Define.h"

namespace BotConsumable
{
    // True when Bot.AI.Consumables.Enabled is set.
    TC_GAME_API bool Enabled();
}

#endif
