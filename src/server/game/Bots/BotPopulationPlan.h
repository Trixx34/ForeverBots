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

#ifndef TRINITY_BOT_POPULATION_PLAN_H
#define TRINITY_BOT_POPULATION_PLAN_H

// Target size and step of the dynamic bot population (Bot.Population.*, BotPopulation.cpp). Pure over plain data so it is unit tested
// without a world. See docs/playerbots/feature-bot-population-20261008.md.

#include "Define.h"
#include <array>
#include <string_view>

namespace BotPopulation
{
    struct Config
    {
        bool Enabled = false;
        uint32 Base = 100;            // bots wanted with no real player online
        uint32 PerPlayerTenths = 0;   // extra bots per real player, in tenths (10 = one bot more per player)
        uint32 Min = 0;
        uint32 Max = 200;
        uint32 StepUp = 5;            // most bots logged in per check
        uint32 StepDown = 3;          // most bots logged out per check
        uint32 Hysteresis = 2;        // no change while the population is within this many bots of the target
        std::array<uint16, 24> HourPct{};   // percent of the target per hour of the day (server local time)
        Config() { HourPct.fill(100); }
    };

    // Parses "100,100,...": up to 24 percentages (0-300). Missing hours keep the rest of the list's last value; an empty or unparsable
    // text leaves `out` unchanged and returns false.
    TC_GAME_API bool ParseHours(std::string_view text, std::array<uint16, 24>& out);

    // Wanted number of bots for `players` real players at `hour` (0-23).
    TC_GAME_API uint32 Target(Config const& cfg, uint32 players, uint32 hour);

    // Bots to log in (positive) or out (negative) now, given the bots online or on their way.
    TC_GAME_API int32 Step(Config const& cfg, uint32 current, uint32 target);
}

#endif
