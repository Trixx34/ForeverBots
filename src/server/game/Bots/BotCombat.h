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

#ifndef TRINITY_BOT_COMBAT_H
#define TRINITY_BOT_COMBAT_H

// Basic combat of the native player bots (strategy "combat", Combat engine), see docs/playerbots/engine-design.md.
// The strategy, triggers and actions live in BotCombat.cpp and are registered by RegisterCombatBotObjects (BotEngine.h).

#include "Define.h"
#include <string>
#include <vector>

class Player;

// Bot.AI.Combat.PrePullManaPct: a mana user out of combat drinks when its mana is below this percent, so it does not pull
// with an almost empty pool (used by the rest strategy together with Bot.AI.Rest.DrinkBelowPct, the higher one wins).
TC_GAME_API bool BotCombatRotationEnabled();
TC_GAME_API uint32 BotCombatPrePullManaPct();

// Console aid (`bot spells <name>`): the spells the bot knows (id, name) and what the combat strategy resolved from them
// (role, highest known rank per table entry, why an entry is not usable). Not for per-tick use.
TC_GAME_API std::vector<std::string> BotCombatDescribeSpells(Player* bot);

#endif
