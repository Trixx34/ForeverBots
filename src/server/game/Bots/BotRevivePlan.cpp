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

#include "BotRevivePlan.h"

namespace BotRevive
{
int32 PickResurrector(std::span<Candidate const> candidates, Config const& cfg)
{
    int32 best = -1;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        Candidate const& c = candidates[i];
        if (!c.Ready || c.InCombat || c.Busy || c.ManaPct < cfg.MinManaPct || c.Dist > cfg.MaxYards)
            continue;
        if (best < 0 || c.Dist < candidates[size_t(best)].Dist || (c.Dist == candidates[size_t(best)].Dist && c.Key < candidates[size_t(best)].Key))
            best = int32(i);
    }
    return best;
}

Act PlanStep(Facts const& f, Config const& cfg)
{
    if (!f.TargetDead || f.OfferPending || f.Attempts >= cfg.MaxAttempts)
        return Act::None;
    if (f.Dist > f.CastRange - 2.0f)
        return Act::Approach;
    if (f.Casting || f.SinceCastMs < cfg.RetryMs)
        return Act::Wait;
    return Act::Cast;
}

bool HoldRelease(bool rezzerAvailable, uint32 deadMs, Config const& cfg)
{
    return cfg.Enabled && cfg.WaitBeforeReleaseMs && rezzerAvailable && deadMs < cfg.WaitBeforeReleaseMs;
}

bool AcceptOffer(uint32 offerAgeMs, Config const& cfg)
{
    return offerAgeMs >= cfg.AcceptDelayMs;
}
}
