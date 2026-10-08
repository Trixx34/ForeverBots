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

#ifndef TRINITY_BOT_DUNGEON_H
#define TRINITY_BOT_DUNGEON_H

// Configuration of the dungeon groups of bots (Bot.AI.Dungeon.*). The decisions are in BotDungeonPlan.h; this file reads the options
// once and hands the plan structs to the glue. Everything is off unless Bot.AI.Dungeon.Enabled is set.

#include "BotDungeonPlan.h"

namespace BotDungeon
{
    struct Settings
    {
        bool Enabled = false;                 // Bot.AI.Dungeon.Enabled: master switch
        uint32 GroupSize = 5;                 // Bot.AI.Dungeon.GroupSize
        Composition Comp;                     // Tanks/Healers: Bot.AI.Dungeon.Tanks / Bot.AI.Dungeon.Healers
        ComposeConfig Compose;                // Bot.AI.Dungeon.MaxLevelSpread / MinLevel
        ReadyConfig Ready;                    // Bot.AI.Dungeon.MinHealthPct / MinManaPct / MinHealerManaPct / MinDurabilityPct
        PullConfig Pull;                      // Bot.AI.Dungeon.MaxMobsPerPull / MaxLevelOverGroup / SkipPatrols
        RunConfig Run;                        // Bot.AI.Dungeon.MaxWipes / RunTimeoutMin
    };

    // Reads the options on first use (world thread) and returns the settings; clamped to sane values.
    TC_GAME_API Settings const& Cfg();
}

#endif
