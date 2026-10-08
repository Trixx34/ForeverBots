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

#include "BotPopulationPlan.h"
#include <algorithm>
#include <charconv>
#include <vector>

namespace BotPopulation
{
bool ParseHours(std::string_view text, std::array<uint16, 24>& out)
{
    std::vector<uint16> values;
    size_t pos = 0;
    while (pos <= text.size() && values.size() < 24)
    {
        size_t end = text.find(',', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view tok = text.substr(pos, end - pos);
        while (!tok.empty() && tok.front() == ' ')
            tok.remove_prefix(1);
        while (!tok.empty() && tok.back() == ' ')
            tok.remove_suffix(1);
        if (tok.empty())
            break;
        uint32 v = 0;
        auto [p, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v);
        if (ec != std::errc() || p != tok.data() + tok.size())
            return false;
        values.push_back(uint16(std::min<uint32>(v, 300)));
        pos = end + 1;
    }
    if (values.empty())
        return false;
    for (size_t i = 0; i < 24; ++i)
        out[i] = values[std::min(i, values.size() - 1)];
    return true;
}

uint32 Target(Config const& cfg, uint32 players, uint32 hour)
{
    uint64 const raw = uint64(cfg.Base) + uint64(cfg.PerPlayerTenths) * players / 10;
    uint64 const scaled = raw * cfg.HourPct[std::min<uint32>(hour, 23)] / 100;
    uint32 const lo = std::min(cfg.Min, cfg.Max);
    return uint32(std::clamp<uint64>(scaled, lo, cfg.Max));
}

int32 Step(Config const& cfg, uint32 current, uint32 target)
{
    if (current + cfg.Hysteresis < target)
        return int32(std::min(target - current, cfg.StepUp));
    if (current > target + cfg.Hysteresis)
        return -int32(std::min(current - target, cfg.StepDown));
    return 0;
}
}
