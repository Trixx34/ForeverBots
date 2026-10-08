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

#include "BotConsumablePlan.h"

namespace BotConsumable
{
namespace
{
int32 Best(std::span<Candidate const> c, Kind k, uint32 missing)
{
    int32 cover = -1, biggest = -1;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (c[i].K != k || !c[i].Count || !c[i].Restore)
            continue;
        if (biggest < 0 || c[i].Restore > c[size_t(biggest)].Restore)
            biggest = int32(i);
        if (c[i].Restore >= missing && (cover < 0 || c[i].Restore < c[size_t(cover)].Restore))
            cover = int32(i);
    }
    return cover >= 0 ? cover : biggest;
}
}

Pick Choose(std::span<Candidate const> candidates, Facts const& f, Config const& cfg)
{
    Pick p;
    if (!f.InCombat)
        return p;
    if (f.HealthReady && f.HpPct < cfg.HealthBelowPct)
    {
        uint32 const missing = uint32(uint64(f.HpMax) * uint32(100 - f.HpPct) / 100);
        if (int32 i = Best(candidates, Kind::Health, missing); i >= 0)
        {
            p.Index = i;
            p.K = Kind::Health;
            return p;
        }
    }
    if (f.ManaReady && f.UsesMana && f.ManaPct < cfg.ManaBelowPct)
    {
        uint32 const missing = uint32(uint64(f.ManaMax) * uint32(100 - f.ManaPct) / 100);
        if (int32 i = Best(candidates, Kind::Mana, missing); i >= 0)
        {
            p.Index = i;
            p.K = Kind::Mana;
        }
    }
    return p;
}
}
