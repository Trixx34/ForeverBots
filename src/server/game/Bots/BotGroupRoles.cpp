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

#include "BotGroupRoles.h"
#include <algorithm>

namespace BotGroupRoles
{
namespace
{
bool Needs(Ally const& a, Config const& cfg)
{
    if (!a.Alive || !a.MissingHealth)
        return false;
    int32 const limit = a.IsTank ? std::max(cfg.TankHealBelowPct, cfg.AllyHealBelowPct) : cfg.AllyHealBelowPct;
    return a.HealthPct < limit;
}
}

bool AnyoneNeedsHeal(std::span<Ally const> allies, Config const& cfg)
{
    return std::any_of(allies.begin(), allies.end(), [&](Ally const& a) { return Needs(a, cfg); });
}

int32 PickHealTarget(std::span<Ally const> allies, Config const& cfg)
{
    // rank: 0 emergency, 1 tank, 2 other
    int best = -1, bestRank = 3;
    for (size_t i = 0; i < allies.size(); ++i)
    {
        Ally const& a = allies[i];
        if (!Needs(a, cfg) || !a.InRange)
            continue;
        int const rank = a.HealthPct < cfg.EmergencyPct ? 0 : a.IsTank ? 1 : 2;
        if (best < 0 || rank < bestRank ||
            (rank == bestRank && (a.HealthPct < allies[size_t(best)].HealthPct ||
                (a.HealthPct == allies[size_t(best)].HealthPct && a.Guid < allies[size_t(best)].Guid))))
        {
            best = int(i);
            bestRank = rank;
        }
    }
    return best;
}

int32 PickHeal(std::span<HealOption const> options, uint32 missing, uint32 mana, bool emergency)
{
    int best = -1;
    if (emergency)
    {
        double bestRate = -1.0;
        for (size_t i = 0; i < options.size(); ++i)
        {
            HealOption const& o = options[i];
            if (!o.Ready || o.ManaCost > mana || !o.Amount)
                continue;
            double const rate = double(std::min(o.Amount, std::max(missing, 1u))) / double(std::max(o.CastMs, 500u));
            if (rate > bestRate)
            {
                bestRate = rate;
                best = int(i);
            }
        }
        return best;
    }

    // efficient: covers 80 percent of the gap, not more than 2.5 times too much; cheapest per point healed
    double bestCost = 0.0;
    for (size_t i = 0; i < options.size(); ++i)
    {
        HealOption const& o = options[i];
        if (!o.Ready || o.ManaCost > mana || !o.Amount)
            continue;
        if (uint64(o.Amount) * 10 < uint64(missing) * 8 || double(o.Amount) > 2.5 * double(std::max(missing, 1u)))
            continue;
        double const cost = double(o.ManaCost) / double(o.Amount);
        if (best < 0 || cost < bestCost)
        {
            bestCost = cost;
            best = int(i);
        }
    }
    if (best >= 0)
        return best;

    // nothing fits: the largest heal that does not overheal wildly, else the smallest that covers it
    int largestUnder = -1, smallestOver = -1;
    for (size_t i = 0; i < options.size(); ++i)
    {
        HealOption const& o = options[i];
        if (!o.Ready || o.ManaCost > mana || !o.Amount)
            continue;
        if (o.Amount < missing)
        {
            if (largestUnder < 0 || o.Amount > options[size_t(largestUnder)].Amount)
                largestUnder = int(i);
        }
        else if (smallestOver < 0 || o.Amount < options[size_t(smallestOver)].Amount)
            smallestOver = int(i);
    }
    return largestUnder >= 0 ? largestUnder : smallestOver;
}

int32 PickAssistTarget(std::span<Foe const> foes, uint64 tankTarget)
{
    int peel = -1, onTankTarget = -1, onTank = -1, lowest = -1;
    for (size_t i = 0; i < foes.size(); ++i)
    {
        Foe const& f = foes[i];
        if (!f.InRange)
            continue;
        if (f.OnHealer && !f.OnTank && (peel < 0 || f.HealthPct < foes[size_t(peel)].HealthPct))
            peel = int(i);
        if (tankTarget && f.Guid == tankTarget)
            onTankTarget = int(i);
        if (f.OnTank && (onTank < 0 || f.HealthPct < foes[size_t(onTank)].HealthPct))
            onTank = int(i);
        if (lowest < 0 || f.HealthPct < foes[size_t(lowest)].HealthPct ||
            (f.HealthPct == foes[size_t(lowest)].HealthPct && f.Guid < foes[size_t(lowest)].Guid))
            lowest = int(i);
    }
    // a mob on a non-tank member is peeled when it is the tank's target anyway, or when it is nearly dead, or when the tank has none
    if (peel >= 0 && (onTankTarget < 0 || foes[size_t(peel)].HealthPct < 25 || peel == onTankTarget))
        return peel;
    if (onTankTarget >= 0)
        return onTankTarget;
    if (peel >= 0)
        return peel;
    return onTank >= 0 ? onTank : lowest;
}
}
