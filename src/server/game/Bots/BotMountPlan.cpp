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

#include "BotMountPlan.h"
#include <cmath>

namespace BotMount
{
Decision Decide(Facts const& f, Config const& cfg)
{
    if (!f.BotAlive)
        return {};

    if (f.BotMounted)
    {
        // a fight ends every ride, whoever ordered it
        if (f.BotInCombat || f.LeaderInCombat)
            return { Act::Dismount, "combat" };
        if (f.Ordered || f.BotBusy || f.SinceActMs < cfg.ActCooldownMs)
            return {};
        if (!f.Follows)
            return { Act::Dismount, "no_leader" };
        if (!f.LeaderMounted && f.LeaderOnFootMs >= cfg.DismountDelayMs && f.Distance <= cfg.DismountYards)
            return { Act::Dismount, "leader_on_foot" };
        return {};
    }

    if (!f.Follows || !f.HasMount || !f.CanMountHere || f.BotBusy || f.BotInCombat || f.LeaderInCombat)
        return {};
    if (f.SinceActMs < cfg.ActCooldownMs || f.SinceFailMs < cfg.FailBackoffMs)
        return {};
    if (f.LeaderMounted && f.LeaderMountedMs >= cfg.MountDelayMs)
        return { Act::Mount, "leader_mounted" };
    if (!f.LeaderMounted && f.Distance > cfg.CatchUpYards && f.FarForMs >= cfg.CatchUpMs)
        return { Act::Mount, "catch_up" };
    return {};
}

int32 PickMatching(std::span<MountOption const> options, int32 wantSpeed)
{
    int32 enough = -1, fastest = -1;
    for (size_t i = 0; i < options.size(); ++i)
    {
        MountOption const& o = options[i];
        if (o.Flying || !o.SpellId)
            continue;
        auto better = [&](int32 cur, bool lowerSpeedWins)
        {
            if (cur < 0)
                return true;
            MountOption const& c = options[size_t(cur)];
            if (o.Speed != c.Speed)
                return lowerSpeedWins ? o.Speed < c.Speed : o.Speed > c.Speed;
            return o.SpellId < c.SpellId;
        };
        if (better(fastest, false))
            fastest = int32(i);
        if (wantSpeed > 0 && o.Speed >= wantSpeed && better(enough, true))
            enough = int32(i);
    }
    return enough >= 0 ? enough : fastest;
}

int32 SpeedPctFromRate(float rate)
{
    if (!std::isfinite(rate) || rate <= 1.0f)
        return 0;
    return int32(std::lround((rate - 1.0f) * 100.0f));
}
}
