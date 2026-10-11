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

#include "BotCookPlan.h"
#include <algorithm>

namespace BotCook
{
Kind Needed(Stock const& stock, Config const& cfg)
{
    if (stock.Buff < cfg.StockBuff)
        return Kind::Buff;
    if (stock.Plain < cfg.StockPlain)
        return Kind::Plain;
    return Kind::None;
}

namespace
{
int PickOfKind(int32 skill, Kind kind, std::span<Recipe const> recipes)
{
    int best = -1;
    for (size_t i = 0; i < recipes.size(); ++i)
    {
        Recipe const& r = recipes[i];
        if (r.Produces != kind || !r.Batches)
            continue;
        if (best < 0)
        {
            best = int(i);
            continue;
        }
        Recipe const& b = recipes[size_t(best)];
        bool const rUp = skill < r.Grey, bUp = skill < b.Grey;
        if (rUp != bUp ? rUp : (r.Grey != b.Grey ? r.Grey > b.Grey : r.SpellId < b.SpellId))
            best = int(i);
    }
    return best;
}
}

int PickStock(int32 skillValue, Stock const& stock, Config const& cfg, std::span<Recipe const> recipes)
{
    Kind const first = Needed(stock, cfg);
    if (first == Kind::None)
        return -1;
    int const pick = PickOfKind(skillValue, first, recipes);
    if (pick >= 0)
        return pick;
    // nothing of the needed kind can be made: the other kind, when it is also below its target
    Kind const other = first == Kind::Buff ? Kind::Plain : Kind::Buff;
    bool const otherNeeded = other == Kind::Plain ? stock.Plain < cfg.StockPlain : stock.Buff < cfg.StockBuff;
    return otherNeeded ? PickOfKind(skillValue, other, recipes) : -1;
}

bool BagsTooFull(uint32 freeSlots, Config const& cfg)
{
    return freeSlots < cfg.MinFreeSlots;
}

std::vector<size_t> OfferOrder(std::span<Food const> held, uint32 playerLevel, uint32 maxListed)
{
    auto rank = [](Kind k) { return k == Kind::Buff ? 0 : (k == Kind::Plain ? 1 : 2); };
    std::vector<size_t> out;
    for (size_t i = 0; i < held.size(); ++i)
        if (held[i].Count && held[i].K != Kind::None && held[i].ReqLevel <= playerLevel)
            out.push_back(i);
    std::sort(out.begin(), out.end(), [&](size_t a, size_t b)
    {
        Food const& x = held[a];
        Food const& y = held[b];
        if (x.K != y.K)
            return rank(x.K) < rank(y.K);
        if (x.ReqLevel != y.ReqLevel)
            return x.ReqLevel > y.ReqLevel;
        if (x.Count != y.Count)
            return x.Count > y.Count;
        return x.Entry < y.Entry;
    });
    if (out.size() > maxListed)
        out.resize(maxListed);
    return out;
}

uint32 CountOf(std::span<Food const> held, Kind kind)
{
    uint32 n = 0;
    for (Food const& f : held)
        if (f.K == kind)
            n += f.Count;
    return n;
}
}
