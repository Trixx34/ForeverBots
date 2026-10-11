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

#include "BotQuestUsePlan.h"
#include <algorithm>

namespace BotQuestUse
{
bool IsTargetItem(SpellFacts const& s)
{
    return s.OnUse && s.NeedsUnitTarget;
}

float CastRange(SpellFacts const& s, Config const& cfg)
{
    float const raw = s.MaxRange > 0.0f ? s.MaxRange * 0.9f : cfg.MaxRange * 0.5f;
    return std::clamp(raw, cfg.MinRange, cfg.MaxRange);
}

Decision Decide(Facts const& f, Config const& cfg)
{
    if (f.CreditGained)
        return { Step::Done, "credit" };
    if (!f.TargetAlive)
        return { Step::Abort, "target_gone" };
    if (f.ElapsedMs >= cfg.TargetTimeoutMs)
        return { Step::Abort, "timeout" };
    if (f.Casting)
        return { Step::Wait, "casting" };
    bool const recent = f.SinceCastMs < cfg.RetryMs;
    if (f.Casts >= cfg.MaxCasts && !recent)
        return { Step::Abort, "no_credit" };
    if (f.BotInCombat)
        return { Step::Wait, "in_combat" };
    if (f.Distance > f.Range || !f.LineOfSight)
        return { Step::Approach, f.LineOfSight ? "far" : "no_los" };
    if (recent)
        return { Step::Wait, "cooldown" };
    return { Step::Cast, "in_range" };
}

char const* StepName(Step s)
{
    switch (s)
    {
        case Step::Approach: return "approach";
        case Step::Cast: return "cast";
        case Step::Done: return "done";
        case Step::Abort: return "abort";
        default: return "wait";
    }
}
}
