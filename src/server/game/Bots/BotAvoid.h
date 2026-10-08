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

#ifndef TRINITY_BOT_AVOID_H
#define TRINITY_BOT_AVOID_H

// Pure helpers of the death / level-gap avoidance (docs/playerbots/death-avoidance-20261008.md): the config-driven avoid list of
// creature entries and the per-bot flee mode A/B assignment. Header-only and free of server types so they can be unit tested.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace BotAvoid
{
    // "116, 822;250927 79" -> {116, 822, 250927, 79}. Separators: comma, semicolon, space. Junk tokens and 0 are ignored.
    inline std::vector<uint32_t> ParseEntryList(std::string const& s)
    {
        std::vector<uint32_t> out;
        std::string tok;
        auto flush = [&]()
        {
            if (!tok.empty())
            {
                char* end = nullptr;
                unsigned long const v = std::strtoul(tok.c_str(), &end, 10);
                if (end && *end == '\0' && v > 0 && v <= 0xFFFFFFFFul && std::find(out.begin(), out.end(), uint32_t(v)) == out.end())
                    out.push_back(uint32_t(v));
                tok.clear();
            }
        };
        for (char c : s)
        {
            if (c == ',' || c == ';' || c == ' ' || c == '\t')
                flush();
            else
                tok.push_back(c);
        }
        flush();
        return out;
    }

    // True when `entry` is on the avoid list and the mob is more than `maxGap` levels above the bot (plain level difference,
    // no elite bonus: "avoided unless within one level"). An empty list or a negative maxGap never avoids anything.
    inline bool ShouldAvoid(std::vector<uint32_t> const& list, uint32_t entry, int mobLevel, int botLevel, int maxGap)
    {
        if (list.empty() || maxGap < 0)
            return false;
        return std::find(list.begin(), list.end(), entry) != list.end() && mobLevel - botLevel > maxGap;
    }

    // Flee mode of one bot. With abPct 0 every bot runs `baseMode`. Otherwise the bots whose guid counter modulo 100 is below
    // abPct run `abMode` (the experiment arm) and the rest run `baseMode` (control); stable for the bot's whole life.
    inline int FleeModeFor(uint64_t guidCounter, int baseMode, int abMode, int abPct)
    {
        if (abPct <= 0)
            return baseMode;
        return int(guidCounter % 100) < std::min(abPct, 100) ? abMode : baseMode;
    }
}

#endif
