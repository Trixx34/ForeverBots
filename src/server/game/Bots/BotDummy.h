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

#ifndef TRINITY_BOT_DUMMY_H
#define TRINITY_BOT_DUMMY_H

// Training dummy mode (strategy "dummy", chat verb "dummy"; Bot.AI.Dummy.*, default off): a bot fights a summoned, immortal,
// harmless dummy for a fixed time with its normal combat code, and logs DUMMY_SUMMARY (damage per spell, dps, time to first hit,
// longest gap). For tuning the class rotations without a sim run. See docs/playerbots/feature-bot-dummy-20261008.md.

#include "Define.h"
#include "SharedDefines.h"

class BotAI;
class Player;
class SpellInfo;
class Unit;

namespace BotDummy
{
    struct Config
    {
        bool Enabled = false;        // Bot.AI.Dummy.Enabled
        uint32 Entry = 9900001;      // Bot.AI.Dummy.Entry: creature_template entry of the dummy (sql/custom/world/2026_10_08_04_world_forever_bot_dummy.sql)
        uint32 DefaultSec = 60;      // Bot.AI.Dummy.DefaultSec: run length without an argument
        uint32 MaxSec = 600;         // Bot.AI.Dummy.MaxSec: longest run a command may ask for
        bool FullPower = true;       // Bot.AI.Dummy.FullPower: mana and energy are refilled every second, health too (rotation test, not an economy test)
    };

    TC_GAME_API Config const& Cfg();

    // Chat verb: starts a run of `sec` seconds (0 = default). Returns the code logged by the chat layer ("OK" or the refusal).
    TC_GAME_API char const* Start(BotAI* ai, Player* bot, uint32 sec);
    // Chat verb "dummy off": ends the run now and logs its summary.
    TC_GAME_API char const* Stop(BotAI* ai, Player* bot);
    TC_GAME_API bool Active(BotAI* ai);
    // BotAI::OnLogout: drops the run and despawns the dummy.
    TC_GAME_API void OnLogout(BotAI* ai, Player* bot);
    // Hook of the dummy creature script (npc_training_dummy::DamageTaken), called before the script zeroes the damage.
    TC_GAME_API void NoteDamage(Unit* dummy, Unit* attacker, uint32 damage, SpellInfo const* spell);
}

#endif
