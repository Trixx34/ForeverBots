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

#include "BotRepairPlan.h"
#include <algorithm>

namespace BotRepair
{
uint64 SpendCap(uint64 gold, Config const& cfg)
{
    int32 const pct = std::clamp(cfg.MaxSpendPct, 0, 100);
    return gold / 100 * uint64(pct) + (gold % 100) * uint64(pct) / 100;
}

RepairPick DecideRepair(RepairFacts const& f, Config const& cfg)
{
    RepairPick out;
    if (!cfg.Enabled || f.Cost == 0 || f.LowestPct >= cfg.BelowPct)
        return out;
    if (!cfg.Paid)
    {
        out.Kind = RepairKind::Free;
        return out;
    }
    if (f.Cost <= f.Gold)
    {
        out.Kind = RepairKind::Paid;
        out.Spend = f.Cost;
        return out;
    }
    out.Kind = cfg.FreeWhenBroke ? RepairKind::Free : RepairKind::TooPoor;
    return out;
}

std::vector<Purchase> PlanSupplies(std::span<Product const> products, SupplyFacts const& f, Config const& cfg)
{
    std::vector<Purchase> out;
    if (!cfg.Enabled || !cfg.Supplies || cfg.PotionsPerKind == 0)
        return out;
    uint64 budget = f.Budget;
    for (PotionKind kind : { PotionKind::Health, PotionKind::Mana })
    {
        if (kind == PotionKind::Mana && !f.UsesMana)
            continue;
        uint32 const owned = kind == PotionKind::Health ? f.OwnedHealth : f.OwnedMana;
        if (owned >= cfg.PotionsPerKind)
            continue;
        Product const* best = nullptr;
        for (Product const& p : products)
            if (p.Kind == kind && p.ReqLevel <= f.Level && p.Price > 0 && (!best || p.ReqLevel > best->ReqLevel))
                best = &p;
        if (!best)
            continue;
        uint32 count = cfg.PotionsPerKind - owned;
        count = uint32(std::min<uint64>(count, budget / best->Price));
        if (!count)
            continue;
        Purchase buy;
        buy.Entry = best->Entry;
        buy.Count = count;
        buy.Cost = uint64(count) * best->Price;
        budget -= buy.Cost;
        out.push_back(buy);
    }
    return out;
}
}
