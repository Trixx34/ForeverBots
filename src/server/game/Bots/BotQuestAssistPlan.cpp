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

#include "BotQuestAssistPlan.h"
#include <algorithm>
#include <cmath>

namespace BotQuestAssist
{
namespace
{
float Dist2D(float x1, float y1, float x2, float y2)
{
    float const dx = x1 - x2, dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

uint32 AbsDiff(uint32 a, uint32 b) { return a > b ? a - b : b - a; }

constexpr uint32 PARTY_SIZE = 5;
}

Kind Classify(QuestFacts const& q)
{
    if (q.Excluded)
        return Kind::None;
    if (q.Escort)
        return Kind::Escort;
    if (q.GroupInfo || q.SuggestedPlayers >= 2)
        return Kind::Group;
    return Kind::None;
}

uint32 HelpersWanted(Kind kind, uint32 suggestedPlayers, uint32 groupSize, Config const& cfg)
{
    if (!cfg.Enabled || kind == Kind::None)
        return 0;
    uint32 const size = std::max<uint32>(1, groupSize);
    if (size >= PARTY_SIZE)
        return 0;
    uint32 const maxHelpers = std::clamp<uint32>(cfg.MaxHelpers, 1, PARTY_SIZE - 1);
    uint32 target = maxHelpers;                       // companions the player should have besides themselves
    if (kind == Kind::Group)
        target = std::min(maxHelpers, std::max<uint32>(1, suggestedPlayers) - 1);
    if (target == 0)
        target = 1;                                   // a group quest tagged "group" without a suggested number
    uint32 const have = size - 1;
    uint32 const missing = target > have ? target - have : 0;
    return std::min(missing, PARTY_SIZE - size);
}

std::vector<uint64> PickHelpers(PlayerFacts const& player, std::vector<Candidate> const& candidates, uint32 wanted, Config const& cfg)
{
    struct Ranked { uint64 Guid; uint32 LevelGap; float Dist; };
    std::vector<Ranked> ranked;
    for (Candidate const& c : candidates)
    {
        if (c.MapId != player.MapId || c.Level < cfg.MinHelperLevel || AbsDiff(c.Level, player.Level) > cfg.MaxLevelGap)
            continue;
        float const d = Dist2D(c.X, c.Y, player.X, player.Y);
        if (d > cfg.RecruitRadius)
            continue;
        ranked.push_back({ c.Guid, AbsDiff(c.Level, player.Level), d });
    }
    std::sort(ranked.begin(), ranked.end(), [](Ranked const& a, Ranked const& b)
    {
        if (a.LevelGap != b.LevelGap)
            return a.LevelGap < b.LevelGap;
        if (a.Dist != b.Dist)
            return a.Dist < b.Dist;
        return a.Guid < b.Guid;
    });
    std::vector<uint64> out;
    for (Ranked const& r : ranked)
    {
        if (out.size() >= wanted)
            break;
        if (std::find(out.begin(), out.end(), r.Guid) == out.end())
            out.push_back(r.Guid);
    }
    return out;
}

Verdict Evaluate(SessionState const& st, Config const& cfg)
{
    Verdict v;
    if (!st.PlayerPresent || st.PlayerGoneSec >= cfg.PlayerGoneSec)
    {
        v.End = true; v.Reason = "player_gone";
        return v;
    }
    if (!st.QuestOpen && st.QuestClosedSec >= cfg.DoneGraceSec)
    {
        v.End = true; v.Reason = "quest_done";
        return v;
    }
    if (st.AgeSec >= st.LifetimeSec)
    {
        v.End = true; v.Reason = "lifetime";
        return v;
    }
    size_t kept = 0;
    for (HelperState const& h : st.Helpers)
    {
        char const* why = nullptr;
        if (!h.Present)
            why = "gone";
        else if (!h.Alive && h.DeadSec >= cfg.DeadSec)
            why = "dead";
        else if (h.FarSec >= cfg.LeashSec)
            why = "lost";
        if (why)
        {
            v.Drop.push_back(h.Guid);
            v.DropReasons.emplace_back(why);
        }
        else
            ++kept;
    }
    if (kept == 0)
    {
        v.End = true; v.Reason = "no_helpers";
        v.Drop.clear();
        v.DropReasons.clear();
    }
    return v;
}

std::vector<Assignment> AssignGuards(std::vector<Attacker> attackers, std::vector<Guard> guards, Config const& cfg)
{
    std::vector<Assignment> out;
    if (attackers.empty() || guards.empty())
        return out;
    // most urgent attacker first
    std::sort(attackers.begin(), attackers.end(), [](Attacker const& a, Attacker const& b)
    {
        if (a.OnEscort != b.OnEscort)
            return a.OnEscort;
        if (a.EscortDist != b.EscortDist)
            return a.EscortDist < b.EscortDist;
        return a.Guid < b.Guid;
    });
    std::sort(guards.begin(), guards.end(), [](Guard const& a, Guard const& b) { return a.Guid < b.Guid; });

    std::vector<uint32> taken(attackers.size(), 0);
    for (Guard const& g : guards)
    {
        if (!g.Free)
            continue;
        // among the close attackers the least covered one, ties to the more urgent (earlier in the list)
        int best = -1;
        for (size_t i = 0; i < attackers.size(); ++i)
        {
            float const d = Dist2D(g.X, g.Y, attackers[i].X, attackers[i].Y);
            if (d > cfg.GuardRadius)
                continue;
            if (best < 0 || taken[i] < taken[size_t(best)])
                best = int(i);
        }
        if (best < 0)
            continue;
        ++taken[size_t(best)];
        out.push_back({ g.Guid, attackers[size_t(best)].Guid });
    }
    return out;
}

char const* KindName(Kind k)
{
    switch (k)
    {
        case Kind::Group: return "group";
        case Kind::Escort: return "escort";
        default: return "none";
    }
}
}
