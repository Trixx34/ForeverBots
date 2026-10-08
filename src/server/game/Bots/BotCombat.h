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

class Creature;
class Player;
class Unit;

// Bot.AI.Combat.PrePullManaPct: a mana user out of combat drinks when its mana is below this percent, so it does not pull
// with an almost empty pool (used by the rest strategy together with Bot.AI.Rest.DrinkBelowPct, the higher one wins).
TC_GAME_API uint32 BotCombatPrePullManaPct();

// Death avoidance (Bot.AI.Avoid.*, docs/playerbots/death-avoidance-20261008.md): true when `mob` is on the configured avoid list and
// more than Bot.AI.Avoid.MaxLevelGap levels above the bot. Quest/grind target choice skips such mobs, the combat target choice fights
// them last.
TC_GAME_API bool BotCombatAvoids(Player const* bot, Creature const* mob);
// Flee mode of this bot: Bot.AI.Flee.Mode, or Bot.AI.Flee.AbMode for the share of bots picked by Bot.AI.Flee.AbPct (A/B runs).
TC_GAME_API int32 BotCombatFleeMode(Player const* bot);

// Bot.AI.Roles.*: true when a group member within 45 yards is fighting a mob this bot could attack and the bot is not in combat itself yet.
// BotAI::DesiredState puts such a bot into the Combat engine so a healer or damage dealer joins the fight of the group.
TC_GAME_API bool BotCombatGroupEngaged(Player* bot);

// Bot.Chat.Orders (`heal`): the bot casts its best known heal of the combat spell table at `target` right now (no fight context needed).
// Returns "OK" or a refusal code: NO_HEAL_SPELL, OUT_OF_RANGE, NO_LOS, NO_POWER, COOLDOWN, BUSY (casting or moving), CAST_FAILED.
TC_GAME_API char const* BotCombatHealUnit(Player* bot, Unit* target);

// Console aid (`bot spells <name>`): the spells the bot knows (id, name) and what the combat strategy resolved from them
// (role, highest known rank per table entry, why an entry is not usable). Not for per-tick use.
TC_GAME_API std::vector<std::string> BotCombatDescribeSpells(Player* bot);

#endif
