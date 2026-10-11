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

#ifndef TRINITY_BOT_DUEL_H
#define TRINITY_BOT_DUEL_H

// Glue of the duel feature (Bot.Duel.*): the core hooks and the "duel" strategy. The decisions are in BotDuelPlan.h.
// Bot sessions have no client, so the bot "presses" accept or cancel itself through the session handlers, like BotSocial does for invites.

#include "Define.h"
#include "ObjectGuid.h"
#include <string_view>

class BotAI;
class Player;
enum DuelCompleteType : uint8;

namespace BotDuel
{
    // SpellEffects.cpp, end of EffectDuel (map thread of the caster): `challenger` asked `target` for a duel at the flag `arbiter`.
    // When `target` is a bot it schedules the answer.
    TC_GAME_API void OnChallenged(Player* target, Player* challenger, ObjectGuid arbiter);

    // Player.cpp, DuelComplete: the duel ended. `loser` is the player the method ran on, `winner` the opponent (also for an interrupted duel).
    TC_GAME_API void OnDuelEnd(Player* winner, Player* loser, DuelCompleteType type);

    // ChatHandler.cpp, whisper: `sender` whispered `receiver`. A bot asked for a duel walks up and challenges.
    TC_GAME_API void OnWhisper(Player* sender, Player* receiver, std::string_view text);

    // The opponent of a bot whose duel is running (not in the countdown), else nullptr. The combat engine fights it.
    TC_GAME_API Player* ActiveOpponent(Player* bot);

    // True while the duel module holds the bot (reply pending, countdown, walking up to a player to challenge).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
