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
#include <cmath>

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
        { SKILL_FISHING,   "Fishing",    true,  5 },
        { SKILL_BLACKSMITHING,  "Blacksmithing",  false, 8 },
        { SKILL_LEATHERWORKING, "Leatherworking", false, 8 },
        { SKILL_ALCHEMY,        "Alchemy",        false, 8 },
        { SKILL_TAILORING,      "Tailoring",      false, 8 },
        { SKILL_ENGINEERING,    "Engineering",    false, 8 },
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
    static uint32 const pairs[4][2] =
    {
        { SKILL_MINING, SKILL_BLACKSMITHING }, { SKILL_HERBALISM, SKILL_ALCHEMY },
        { SKILL_SKINNING, SKILL_LEATHERWORKING }, { SKILL_MINING, SKILL_ENGINEERING }
    };
    auto const& p = pairs[seed % 4];
    return { SKILL_FIRST_AID, SKILL_COOKING, p[0], p[1], SKILL_FISHING };
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

uint32 FishingSpell(bool (*hasSpell)(void*, uint32), void* ctx)
{
    for (uint32 spell : { 18248u, 7732u, 7731u, 7620u })
        if (hasSpell(ctx, spell))
            return spell;
    return 0;
}

bool FindShore(float x, float y, bool (*isWater)(void*, float, float), bool (*isLand)(void*, float, float), void* ctx, Shore& out)
{
    constexpr int DIRS = 16;
    constexpr float PI2 = 6.2831853f;
    for (float standDist = 0.0f; standDist <= 24.0f; standDist += 4.0f)
        for (int i = 0; i < DIRS; ++i)
        {
            float const a = PI2 * float(i) / DIRS;
            float const dx = std::cos(a), dy = std::sin(a);
            float const sx = x + dx * standDist, sy = y + dy * standDist;
            if (!isLand(ctx, sx, sy))
                continue;
            for (float w = 4.0f; w <= 10.0f; w += 3.0f)
                if (isWater(ctx, sx + dx * w, sy + dy * w))
                {
                    out = { sx, sy, sx + dx * w, sy + dy * w };
                    return true;
                }
        }
    return false;
}

int PickRecipe(int32 skillValue, std::vector<Recipe> const& craftable)
{
    int best = -1;
    int bestTier = 0;
    for (size_t i = 0; i < craftable.size(); ++i)
    {
        Recipe const& r = craftable[i];
        if (skillValue >= r.Grey)
            continue;
        int const tier = skillValue < r.Yellow ? 2 : 1;
        if (best < 0 || tier > bestTier || (tier == bestTier && r.Grey < craftable[best].Grey))
        {
            best = int(i);
            bestTier = tier;
        }
    }
    return best;
}

uint32 SkinReqSkill(uint32 level)
{
    if (level <= 10)
        return 1;
    if (level < 20)
        return (level - 10) * 10;
    return level * 5;   // classic levels end at 60
}

bool NodeWorthIt(uint32 skillValue, uint32 reqSkill)
{
    // nodes more than 25 points above the bot cannot be gathered; the rest can at least be looted
    return reqSkill <= skillValue + 25;
}
}
