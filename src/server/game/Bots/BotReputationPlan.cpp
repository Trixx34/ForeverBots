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

#include "BotReputationPlan.h"
#include <algorithm>
#include <cstdlib>

namespace BotReputation
{
float QuestWeight(std::span<Reward const> rewards, Config const& cfg)
{
    float const full = float(std::max<int32>(1, cfg.FullPoints));
    float gain = 0.0f;
    float lossFactor = 1.0f;

    for (Reward const& r : rewards)
    {
        if (r.OtherSide || r.Amount == 0)
            continue;

        float const share = std::min(1.0f, float(std::abs(r.Amount)) / full);
        if (r.Amount > 0)
        {
            if (r.Rank >= RankExalted || r.Standing >= StandingExalted)
                continue;
            if (r.CapRank > 0 && r.Rank >= r.CapRank)
                continue;
            gain += share * (r.Rank >= RankRevered ? 0.5f : 1.0f);
        }
        else
        {
            // a loss that turns the faction hostile (guards attack) is never worth it; a bot that is already there has nothing to lose
            if (r.Standing > StandingUnfriendly && r.Standing + r.Amount <= StandingUnfriendly)
                return 0.0f;
            if (r.Standing <= StandingUnfriendly)
                continue;
            lossFactor *= 1.0f - std::clamp(float(cfg.LossPenaltyPct) / 100.0f, 0.0f, 1.0f) * share;
        }
    }

    gain = std::min(gain, 1.0f);
    return (1.0f + float(std::max<int32>(0, cfg.BonusPct)) / 100.0f * gain) * lossFactor;
}
}
