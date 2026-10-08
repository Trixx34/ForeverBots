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

#ifndef TRINITY_BOT_PET_H
#define TRINITY_BOT_PET_H

// Hunter bot pets (strategy "pet", NonCombat and Combat engines; Bot.AI.Pet.*, default off). The decisions are in BotPetLogic.cpp, this
// file reads the game state, carries the decisions out, and owns the taming run. See docs/playerbots/feature-bot-pets-movement-loot-20261008.md.

#include "Define.h"

class BotAI;
class Player;

namespace BotPet
{
    struct Config
    {
        bool Enabled = false;        // Bot.AI.Pet.Enabled: master switch
        bool Feed = true;            // Bot.AI.Pet.Feed
        bool Abilities = true;       // Bot.AI.Pet.Abilities: autocast set of the pet
        bool Taming = true;          // Bot.AI.Pet.Taming: Taming the Beast quests
        bool TameFirstPet = true;    // Bot.AI.Pet.TameFirstPet: a hunter without any pet tames one with Tame Beast
        uint32 MinLevel = 10;        // Bot.AI.Pet.MinLevel
        uint32 FeedBelowPct = 66;    // Bot.AI.Pet.FeedBelowPct
        uint32 MendBelowPct = 60;    // Bot.AI.Pet.MendBelowPct
        uint32 RetrySec = 8;         // Bot.AI.Pet.RetrySec
        uint32 TameCooldownSec = 300; // Bot.AI.Pet.TameCooldownSec: pause after a tame attempt that failed
    };

    TC_GAME_API Config const& Cfg();

    // World thread, once, before bot AIs exist (BotMgr, next to BotQuest::EnsureIndex): the spawn points of the beasts of the taming
    // quests, so a bot knows where to walk. Reads the ObjectMgr caches only. No-op while the pet feature is off.
    TC_GAME_API void EnsureIndex();

    // BotAI::OnLogout (map thread): dismisses the pet (saved as the current pet) and drops the taming run, so nothing outlives the bot.
    TC_GAME_API void OnLogout(BotAI* ai, Player* bot);
    // BotAI::Tick when the map changed: a pet on the old map is dismissed (instances, battlegrounds), taming is abandoned, timers reset.
    TC_GAME_API void OnMapChange(BotAI* ai, Player* bot);
    // True while a taming run holds the bot (walking to the beast, channelling). The quest layer yields while it is true.
    TC_GAME_API bool Busy(BotAI* ai);
    // The quest classifier asks this for quests it would block as NEEDS_EVENT: true when the pet module completes them.
    TC_GAME_API bool SupportsEventQuest(uint32 questId);
}

#endif
