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

#include "BotRotation.h"
#include <algorithm>

namespace BotRotation
{
bool Allowed(Rule const& rule, Facts const& f)
{
    if (rule.Condition == Cond::Always)
        return true;
    if (!f.Enabled)
        return false;
    switch (rule.Condition)
    {
        case Cond::TargetHpAbove:  return f.TargetHpPct > rule.Param;
        case Cond::TargetHpBelow:  return f.TargetHpPct < rule.Param;
        case Cond::SelfHpBelow:    return f.SelfHpPct < rule.Param;
        case Cond::SelfPowerBelow: return f.SelfPowerPct < rule.Param;
        case Cond::SelfPowerAbove: return f.SelfPowerPct >= rule.Param;
        case Cond::EnemiesAtLeast: return f.EnemiesOnBot >= uint32(std::max(rule.Param, 1));
        case Cond::TargetCasting:  return f.TargetCasting;
        case Cond::TargetFleeing:  return f.TargetFleeing;
        case Cond::Opener:         return f.FightMs < uint32(std::max(rule.Param, 0)) * 1000u;
        case Cond::TargetStrong:   return f.TargetElite || f.TargetLevelDiff >= std::max(rule.Param, 1);
        case Cond::LifeTapSafe:    return f.SelfHpPct > 60 && f.SelfPowerPct < rule.Param;
        case Cond::Stealthed:      return f.Stealthed;
        default:                   return true;
    }
}

char const* CondName(Cond c)
{
    switch (c)
    {
        case Cond::Always:         return "always";
        case Cond::TargetHpAbove:  return "target_hp_above";
        case Cond::TargetHpBelow:  return "target_hp_below";
        case Cond::SelfHpBelow:    return "self_hp_below";
        case Cond::SelfPowerBelow: return "self_power_below";
        case Cond::SelfPowerAbove: return "self_power_above";
        case Cond::EnemiesAtLeast: return "enemies_at_least";
        case Cond::TargetCasting:  return "target_casting";
        case Cond::TargetFleeing:  return "target_fleeing";
        case Cond::Opener:         return "opener";
        case Cond::TargetStrong:   return "target_strong";
        case Cond::LifeTapSafe:    return "life_tap_safe";
        case Cond::Stealthed:      return "stealthed";
    }
    return "?";
}
}
