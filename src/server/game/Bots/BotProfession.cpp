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

#include "BotProfession.h"
#include <algorithm>

namespace BotProfession
{
std::vector<Info> const& All()
{
    static std::vector<Info> const table =
    {
        { SKILL_FIRST_AID, "First Aid",  false, 5 },
        { SKILL_COOKING,   "Cooking",    false, 5 },
        { SKILL_MINING,    "Mining",     true,  5 },
        { SKILL_HERBALISM, "Herbalism",  true,  5 },
        { SKILL_SKINNING,  "Skinning",   true,  5 },
    };
    return table;
}

Info const* Find(uint32 skill)
{
    for (Info const& i : All())
        if (i.Skill == skill)
            return &i;
    return nullptr;
}

std::vector<uint32> Plan(uint64 seed)
{
    static uint32 const pairs[3][2] = { { SKILL_MINING, SKILL_SKINNING }, { SKILL_HERBALISM, SKILL_SKINNING }, { SKILL_MINING, SKILL_HERBALISM } };
    auto const& p = pairs[seed % 3];
    return { SKILL_FIRST_AID, SKILL_COOKING, p[0], p[1] };
}

uint32 NextToLearn(std::vector<uint32> const& plan, std::vector<uint32> const& known, uint8 level)
{
    for (uint32 skill : plan)
    {
        if (std::find(known.begin(), known.end(), skill) != known.end())
            continue;
        Info const* info = Find(skill);
        if (info && level >= info->MinLevel)
            return skill;
    }
    return 0;
}

bool NodeWorthIt(uint32 skillValue, uint32 reqSkill)
{
    // nodes more than 25 points above the bot cannot be gathered; the rest can at least be looted
    return reqSkill <= skillValue + 25;
}
}
