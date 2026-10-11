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

#include "BotSpellLearn.h"
#include <algorithm>

namespace BotSpellLearn
{
std::vector<uint32> Select(std::vector<Candidate> const& candidates, uint32 botLevel)
{
    std::vector<Candidate const*> picked;
    for (Candidate const& c : candidates)
        if (c.SpellId && c.Available && c.InfoValid && c.ReqLevel <= botLevel)
            picked.push_back(&c);
    std::stable_sort(picked.begin(), picked.end(), [](Candidate const* a, Candidate const* b)
    {
        return a->ReqLevel != b->ReqLevel ? a->ReqLevel < b->ReqLevel : a->SpellId < b->SpellId;
    });
    std::vector<uint32> out;
    for (Candidate const* c : picked)
        if (std::find(out.begin(), out.end(), c->SpellId) == out.end())
            out.push_back(c->SpellId);
    return out;
}

bool Due(bool enabled, uint32 botLevel, uint32 lastLevel)
{
    return enabled && botLevel >= 1 && botLevel != lastLevel;
}
}
