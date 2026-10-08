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

#include "BotTownIdlePlan.h"
#include "BotMovePlan.h"

namespace BotTownIdle
{
using BotMove::Mix;
using BotMove::Range;
using BotMove::Unit;

namespace
{
constexpr float PI_F = 3.14159265f;
}

uint32 SpotWeight(SpotKind kind)
{
    switch (kind)
    {
        case SpotKind::Inn:     return 4;
        case SpotKind::Bank:    return 4;
        case SpotKind::Auction: return 3;
        case SpotKind::Flight:  return 2;
        case SpotKind::Mail:    return 3;
        case SpotKind::Vendor:  return 1;
    }
    return 1;
}

int32 PickSpot(std::vector<Spot> const& spots, int32 current, uint64 botKey, uint32 nowMs)
{
    if (spots.empty())
        return -1;
    if (spots.size() == 1)
        return 0;

    uint32 total = 0;
    for (size_t i = 0; i < spots.size(); ++i)
        if (int32(i) != current)
            total += SpotWeight(spots[i].Kind);
    uint32 pick = uint32(Unit(Mix(botKey, nowMs / 1000, 77)) * float(total));
    for (size_t i = 0; i < spots.size(); ++i)
    {
        if (int32(i) == current)
            continue;
        uint32 const w = SpotWeight(spots[i].Kind);
        if (pick < w)
            return int32(i);
        pick -= w;
    }
    return current == 0 ? 1 : 0;   // rounding guard, unreachable with total > 0
}

uint32 LingerMs(Config const& cfg, uint64 botKey)
{
    uint32 const lo = cfg.LingerMinSec * 1000, hi = cfg.LingerMaxSec > cfg.LingerMinSec ? cfg.LingerMaxSec * 1000 : lo;
    return lo + uint32(Unit(Mix(botKey, 53)) * float(hi - lo));
}

Plan PlanStep(Facts const& f, Config const& cfg, std::vector<Spot> const& spots, uint64 botKey, uint32 nowMs)
{
    Plan plan;
    uint64 const h = Mix(botKey, nowMs / 1000, f.StillMs / 1000);

    if (f.Sitting)
    {
        uint32 const hold = cfg.SitMinMs + uint32(Unit(Mix(botKey, 91)) * float(cfg.SitMaxMs > cfg.SitMinMs ? cfg.SitMaxMs - cfg.SitMinMs : 0));
        if (f.Threat || f.SittingMs >= hold)
            plan.What = Act::StandUp;
        return plan;
    }
    if (f.Threat)
        return plan;   // the aggro behavior owns that

    bool const linger = f.StillMs >= LingerMs(cfg, botKey);
    // a bot that has not reached a spot yet (fresh in town, or the last walk was cut short) heads for one first
    bool const needSpot = !spots.empty() && !f.AtSpot && f.StillMs >= cfg.MinGapMs / 2;
    if (!linger && !needSpot && (f.StillMs < cfg.MinGapMs || f.SinceActionMs < cfg.MinGapMs))
        return plan;

    auto goSomewhere = [&]()
    {
        int32 const s = PickSpot(spots, f.CurrentSpot, botKey, nowMs);
        if (s >= 0)
        {
            plan.What = Act::GoSpot;
            plan.Spot = s;
            plan.StandOff = Range(Mix(h, 5), cfg.StandOffMin, cfg.StandOffMax);
            plan.Bearing = Range(Mix(h, 6), 0.0f, 2.0f * PI_F);
        }
        else
        {
            plan.What = Act::Wander;
            plan.StandOff = Range(Mix(h, 1), 4.0f, 14.0f);
            plan.Bearing = Range(Mix(h, 2), 0.0f, 2.0f * PI_F);
        }
    };

    if (linger || needSpot)
    {
        goSomewhere();
        return plan;
    }

    // lingering at a spot: socialize
    float const roll = Unit(h) * 100.0f;
    float const sitPct = f.HurtOrDrained ? float(cfg.SitPct) * 4.0f : float(cfg.SitPct);
    if (roll < float(cfg.EmotePct))
    {
        plan.What = Act::Emote;
        uint64 const e = Mix(h, 7);
        // dancing is rare: one in sixteen emotes
        plan.Emote = (e & 15) == 0 ? EmoteKind::Dance : EmoteKind(uint32(Unit(Mix(h, 8)) * 5.0f) % 5);
    }
    else if (roll < float(cfg.EmotePct) + sitPct)
    {
        plan.What = Act::Sit;
    }
    else
    {
        plan.What = Act::Look;
        plan.TurnRad = (Unit(Mix(h, 3)) < 0.5f ? -1.0f : 1.0f) * Range(Mix(h, 4), 0.5f, 2.4f);
    }
    return plan;
}
}
