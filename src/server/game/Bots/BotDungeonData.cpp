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

#include "BotDungeonData.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string>

namespace BotDungeon
{
namespace
{
// Map ids, level ranges and final bosses of Classic. A wrong boss entry only means the run ends when every pack is dead.
constexpr DungeonInfo DUNGEONS[] =
{
    { "rfc", "ragefire ragefirechasm",                "Ragefire Chasm",       389, 13, 18, 11519 },
    { "dm",  "deadmines thedeadmines",                "The Deadmines",         36, 15, 24, 639 },
    { "wc",  "wailingcaverns",                        "Wailing Caverns",       43, 15, 25, 3654 },
    { "sfk", "shadowfang shadowfangkeep",             "Shadowfang Keep",       33, 14, 24, 4275 },
    { "bfd", "blackfathom blackfathomdeeps",          "Blackfathom Deeps",     48, 20, 30, 4829 },
    { "stockade", "stormwindstockade thestockade",    "The Stockade",          34, 22, 30, 1716 },
    { "gnomer", "gnomeregan",                         "Gnomeregan",            90, 24, 33, 7800 },
    { "rfk", "razorfenkraul",                         "Razorfen Kraul",        47, 24, 34, 4421 },
    { "sm",  "scarletmonastery monastery",            "Scarlet Monastery",    189, 28, 38, 0 },
    { "rfd", "razorfendowns",                         "Razorfen Downs",       129, 33, 43, 7358 },
    { "ulda", "uldaman",                              "Uldaman",               70, 36, 46, 2748 },
    { "zf",  "zulfarrak",                             "Zul'Farrak",           209, 42, 52, 7267 },
    { "mara", "maraudon",                             "Maraudon",             349, 40, 52, 12201 },
    { "st",  "sunkentemple templeofatalhakkar",       "Sunken Temple",        109, 47, 57, 5709 },
};

std::string Normalize(std::string_view s)
{
    std::string out;
    for (char c : s)
        if (c != ' ' && c != '\'' && c != '-' && c != '_')
            out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}

std::span<DungeonInfo const> Dungeons() { return DUNGEONS; }

DungeonInfo const* FindDungeon(std::string_view text)
{
    std::string const key = Normalize(text);
    if (key.empty())
        return nullptr;
    for (DungeonInfo const& d : DUNGEONS)
    {
        if (key == d.Key || key == std::to_string(d.MapId) || key == Normalize(d.Name))
            return &d;
        std::string aliases = d.Aliases;
        size_t pos = 0;
        while (pos < aliases.size())
        {
            size_t end = aliases.find(' ', pos);
            if (end == std::string::npos)
                end = aliases.size();
            if (key == aliases.substr(pos, end - pos))
                return &d;
            pos = end + 1;
        }
    }
    return nullptr;
}

std::vector<DungeonInfo const*> DungeonsForLevel(int32 level, int32 slackLow)
{
    std::vector<DungeonInfo const*> out;
    for (DungeonInfo const& d : DUNGEONS)
        if (level + slackLow >= d.MinLevel && level <= d.MaxLevel)
            out.push_back(&d);
    std::stable_sort(out.begin(), out.end(), [&](DungeonInfo const* a, DungeonInfo const* b)
    {
        int32 const da = std::abs(2 * level - (a->MinLevel + a->MaxLevel));
        int32 const db = std::abs(2 * level - (b->MinLevel + b->MaxLevel));
        return da < db;
    });
    return out;
}

namespace
{
float Dist(SpawnPoint const& a, SpawnPoint const& b)
{
    float const dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int Find(std::vector<int>& parent, int i)
{
    while (parent[size_t(i)] != i)
    {
        parent[size_t(i)] = parent[size_t(parent[size_t(i)])];
        i = parent[size_t(i)];
    }
    return i;
}

PackSpec Build(std::vector<SpawnPoint> const& pts, uint32 finalBossEntry)
{
    PackSpec p;
    p.Points = pts;
    for (SpawnPoint const& s : pts)
    {
        p.X += s.X; p.Y += s.Y; p.Z += s.Z;
        ++p.Mobs;
        p.Elites += s.Elite ? 1 : 0;
        p.MaxLevel = std::max(p.MaxLevel, s.Level);
        p.Boss = p.Boss || s.Boss || (finalBossEntry && s.Entry == finalBossEntry);
        p.Patrols = p.Patrols || s.Patrol;
        p.HasFinalBoss = p.HasFinalBoss || (finalBossEntry && s.Entry == finalBossEntry);
    }
    if (p.Mobs)
    {
        p.X /= float(p.Mobs); p.Y /= float(p.Mobs); p.Z /= float(p.Mobs);
    }
    return p;
}
}

std::vector<PackSpec> ClusterPacks(std::span<SpawnPoint const> spawns, float linkRadius, uint32 maxPack, uint32 finalBossEntry)
{
    size_t const n = spawns.size();
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (Dist(spawns[i], spawns[j]) <= linkRadius)
                parent[size_t(Find(parent, int(j)))] = Find(parent, int(i));

    std::vector<std::vector<SpawnPoint>> groups;
    {
        std::vector<int> slot(n, -1);
        for (size_t i = 0; i < n; ++i)
        {
            int const root = Find(parent, int(i));
            if (slot[size_t(root)] < 0)
            {
                slot[size_t(root)] = int(groups.size());
                groups.emplace_back();
            }
            groups[size_t(slot[size_t(root)])].push_back(spawns[i]);
        }
    }

    std::vector<std::vector<SpawnPoint>> finalGroups;
    for (std::vector<SpawnPoint>& g : groups)
    {
        bool hasBoss = false;
        for (SpawnPoint const& s : g)
            hasBoss = hasBoss || s.Boss || (finalBossEntry && s.Entry == finalBossEntry);
        if (hasBoss || !maxPack || g.size() <= maxPack)
        {
            finalGroups.push_back(std::move(g));
            continue;
        }
        // too many for one pull: cut into packs around seeds
        std::vector<SpawnPoint> rest = std::move(g);
        while (!rest.empty())
        {
            auto seedIt = std::min_element(rest.begin(), rest.end(), [](SpawnPoint const& a, SpawnPoint const& b)
            {
                return a.X != b.X ? a.X < b.X : a.Y != b.Y ? a.Y < b.Y : a.SpawnId < b.SpawnId;
            });
            SpawnPoint const seed = *seedIt;
            std::sort(rest.begin(), rest.end(), [&](SpawnPoint const& a, SpawnPoint const& b)
            {
                float const da = Dist(a, seed), db = Dist(b, seed);
                return da != db ? da < db : a.SpawnId < b.SpawnId;
            });
            std::vector<SpawnPoint> taken, left;
            for (SpawnPoint const& s : rest)
                (taken.size() < maxPack && Dist(s, seed) <= linkRadius ? taken : left).push_back(s);
            finalGroups.push_back(std::move(taken));
            rest = std::move(left);
        }
    }

    std::vector<PackSpec> out;
    out.reserve(finalGroups.size());
    for (std::vector<SpawnPoint>& g : finalGroups)
    {
        std::sort(g.begin(), g.end(), [](SpawnPoint const& a, SpawnPoint const& b) { return a.SpawnId < b.SpawnId; });
        out.push_back(Build(g, finalBossEntry));
    }
    std::sort(out.begin(), out.end(), [](PackSpec const& a, PackSpec const& b) { return a.Points.front().SpawnId < b.Points.front().SpawnId; });
    for (size_t i = 0; i < out.size(); ++i)
        out[i].Id = uint32(i + 1);
    return out;
}
}
