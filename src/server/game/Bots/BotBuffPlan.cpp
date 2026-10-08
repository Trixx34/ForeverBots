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

#include "BotBuffPlan.h"
#include <algorithm>

namespace BotBuff
{
namespace
{
constexpr size_t MAX_RECORDS = 64;
}

bool Applies(Targets t, uint8 cls)
{
    switch (t)
    {
        case Targets::All:
            return true;
        case Targets::ManaUsers:
            // priest 5, shaman 7, mage 8, warlock 9, druid 11, paladin 2, hunter 3 (hunters and paladins do use mana)
            return cls == 2 || cls == 3 || cls == 5 || cls == 7 || cls == 8 || cls == 9 || cls == 11;
        case Targets::Melee:
            return cls == 1 || cls == 2 || cls == 3 || cls == 4;
    }
    return false;
}

bool Throttle::Ready(uint64 targetKey, uint32 buffRoot, uint32 nowMs, uint32 waitMs) const
{
    for (Rec const& r : _v)
        if (r.Key == targetKey && r.Root == buffRoot)
            return nowMs - r.Ms >= waitMs;
    return true;
}

void Throttle::Note(uint64 targetKey, uint32 buffRoot, uint32 nowMs)
{
    for (Rec& r : _v)
        if (r.Key == targetKey && r.Root == buffRoot)
        {
            r.Ms = nowMs;
            return;
        }
    if (_v.size() >= MAX_RECORDS)
        _v.erase(_v.begin());
    _v.push_back({ targetKey, buffRoot, nowMs });
}
}
