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

#include "BotPartyPlan.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace BotParty
{
namespace
{
uint64 Mix(uint64 a, uint64 b)
{
    uint64 z = a * 0x9E3779B97F4A7C15ull + b + 0x7F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float Dist2D(Candidate const& a, Candidate const& b)
{
    return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}

uint32 AbsDiff(uint32 a, uint32 b) { return a > b ? a - b : b - a; }
}

uint32 SharedCount(std::vector<uint32> const& a, std::vector<uint32> const& b)
{
    std::unordered_set<uint32> const sa(a.begin(), a.end());
    std::unordered_set<uint32> counted;
    for (uint32 q : b)
        if (sa.count(q))
            counted.insert(q);
    return uint32(counted.size());
}

std::vector<Formed> FormParties(std::vector<Candidate> cands, Config const& cfg, uint32 salt)
{
    std::vector<Formed> out;
    if (!cfg.Enabled)
        return out;
    uint32 const maxSize = std::clamp<uint32>(cfg.MaxSize, 2, 5);
    uint32 const minSize = std::clamp<uint32>(cfg.MinSize, 2, maxSize);

    std::sort(cands.begin(), cands.end(), [](Candidate const& a, Candidate const& b) { return a.Guid < b.Guid; });
    std::unordered_set<uint64> used;
    for (Candidate const& seed : cands)
    {
        if (used.count(seed.Guid))
            continue;
        // only some bots look for company at a given check
        if (Mix(seed.Guid, salt) % 100 >= std::min<uint32>(cfg.FormChancePct, 100))
            continue;

        struct Join { Candidate const* C; uint32 Shared; float Dist; };
        std::vector<Join> joiners;
        for (Candidate const& c : cands)
        {
            if (c.Guid == seed.Guid || used.count(c.Guid) || c.MapId != seed.MapId)
                continue;
            float const d = Dist2D(seed, c);
            if (d > cfg.JoinRadius)
                continue;
            uint32 const shared = SharedCount(seed.Quests, c.Quests);
            if (shared < std::max<uint32>(cfg.MinSharedQuests, 1))
                continue;
            joiners.push_back({ &c, shared, d });
        }
        std::sort(joiners.begin(), joiners.end(), [](Join const& a, Join const& b)
        {
            if (a.Shared != b.Shared)
                return a.Shared > b.Shared;
            if (a.Dist != b.Dist)
                return a.Dist < b.Dist;
            return a.C->Guid < b.C->Guid;
        });

        // take joiners in order while the level spread of the whole party holds
        std::vector<Candidate const*> party{ &seed };
        uint32 lo = seed.Level, hi = seed.Level;
        for (Join const& j : joiners)
        {
            if (party.size() >= maxSize)
                break;
            uint32 const nlo = std::min(lo, j.C->Level), nhi = std::max(hi, j.C->Level);
            if (nhi - nlo > cfg.MaxLevelSpread)
                continue;
            party.push_back(j.C);
            lo = nlo;
            hi = nhi;
        }
        if (party.size() < minSize)
            continue;

        Candidate const* leader = party.front();
        for (Candidate const* c : party)
            if (c->Level > leader->Level || (c->Level == leader->Level && c->Guid < leader->Guid))
                leader = c;

        Formed f;
        f.Leader = leader->Guid;
        for (Candidate const* c : party)
        {
            used.insert(c->Guid);
            if (c != leader)
                f.Members.push_back(c->Guid);
        }
        std::sort(f.Members.begin(), f.Members.end());

        // the quest most members hold with the leader
        std::vector<uint32> qs = leader->Quests;
        std::sort(qs.begin(), qs.end());
        qs.erase(std::unique(qs.begin(), qs.end()), qs.end());
        uint32 best = 0, bestCount = 0;
        for (uint32 q : qs)
        {
            uint32 n = 0;
            for (Candidate const* c : party)
                if (c != leader && std::find(c->Quests.begin(), c->Quests.end(), q) != c->Quests.end())
                    ++n;
            if (n > bestCount)
            {
                bestCount = n;
                best = q;
            }
        }
        f.SharedQuest = best;
        out.push_back(std::move(f));
    }
    return out;
}

uint32 LifetimeFor(Config const& cfg, uint64 leaderGuid, uint32 salt)
{
    uint32 const half = cfg.LifetimeSec / 2;
    return half + uint32(Mix(leaderGuid, salt ^ 0xA5A5u) % (uint64(cfg.LifetimeSec - half) + 1));
}

Verdict Evaluate(PartyState const& st, Config const& cfg)
{
    Verdict v;
    if (st.LifetimeSec && st.AgeSec >= st.LifetimeSec)
    {
        v.Disband = true;
        v.Reason = "lifetime";
        return v;
    }
    if (!st.LeaderPresent || st.LeaderGoneSec >= cfg.LeaderGoneSec)
    {
        v.Disband = true;
        v.Reason = "leader_gone";
        return v;
    }
    uint32 left = 0;
    for (MemberState const& m : st.Members)
    {
        char const* why = nullptr;
        if (!m.Present)
            why = "gone";
        else if (!m.Alive)
            why = "dead";
        else if (!m.SharesQuest)
            why = "quest_done";
        else if (!m.SameMap || m.FarSec >= cfg.LeashSec)
            why = "lost";
        else if (AbsDiff(m.Level, st.LeaderLevel) > cfg.MaxLevelSpread + 2)
            why = "level";
        if (why)
        {
            v.Drop.push_back(m.Guid);
            v.DropReasons.emplace_back(why);
        }
        else
            ++left;
    }
    if (left + 1 < std::clamp<uint32>(cfg.MinSize, 2, 5))
    {
        v.Disband = true;
        v.Reason = "too_small";
        v.Drop.clear();
        v.DropReasons.clear();
    }
    return v;
}
}
