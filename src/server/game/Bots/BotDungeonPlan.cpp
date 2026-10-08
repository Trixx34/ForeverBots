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

#include "BotDungeonPlan.h"
#include <algorithm>

namespace BotDungeon
{
uint32 RolesOfClass(uint8 classId)
{
    switch (classId)
    {
        case CLS_WARRIOR: return RoleBit(Role::Tank) | RoleBit(Role::Dps);
        case CLS_PALADIN: return RoleBit(Role::Tank) | RoleBit(Role::Healer) | RoleBit(Role::Dps);
        case CLS_DRUID:   return RoleBit(Role::Tank) | RoleBit(Role::Healer) | RoleBit(Role::Dps);
        case CLS_PRIEST:  return RoleBit(Role::Healer) | RoleBit(Role::Dps);
        case CLS_SHAMAN:  return RoleBit(Role::Healer) | RoleBit(Role::Dps);
        case CLS_HUNTER:
        case CLS_ROGUE:
        case CLS_MAGE:
        case CLS_WARLOCK: return RoleBit(Role::Dps);
        default:          return 0;
    }
}

Role PreferredRole(uint8 classId)
{
    switch (classId)
    {
        case CLS_WARRIOR: return Role::Tank;
        case CLS_PRIEST:  return Role::Healer;
        default:          return Role::Dps;
    }
}

char const* RoleName(Role r)
{
    switch (r)
    {
        case Role::Tank:   return "tank";
        case Role::Healer: return "healer";
        default:           return "dps";
    }
}

char const* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Gather:  return "gather";
        case Phase::Travel:  return "travel";
        case Phase::Clear:   return "clear";
        case Phase::Loot:    return "loot";
        case Phase::Recover: return "recover";
        case Phase::Done:    return "done";
        default:             return "aborted";
    }
}

namespace
{
// Is this class a "pure" filler of the role: a hybrid is used for a scarce role only when no pure class is there, so a priest heals
// before a paladin does and a warrior tanks before a druid does.
bool IsPreferred(uint8 classId, Role r) { return PreferredRole(classId) == r; }
}

ComposeResult Compose(std::span<Candidate const> candidates, Composition const& comp, uint32 dungeonMinLevel, uint32 dungeonMaxLevel,
    ComposeConfig const& cfg)
{
    ComposeResult res;
    uint32 const size = std::max<uint32>(comp.Size, 1);
    uint32 need[3] = { std::min(comp.Tanks, size), 0, 0 };
    need[1] = std::min(comp.Healers, size - need[0]);
    need[2] = size - need[0] - need[1];

    uint32 const minLevel = std::max(dungeonMinLevel, cfg.MinLevel);
    Candidate const* leader = nullptr;
    for (Candidate const& c : candidates)
        if (c.Leader) { leader = &c; break; }

    auto levelOk = [&](Candidate const& c, int32 lo, int32 hi)
    {
        return c.Level >= minLevel && c.Level <= dungeonMaxLevel && int32(c.Level) >= lo && int32(c.Level) <= hi;
    };

    // the level window is anchored on the leader (or on the first candidate without one)
    int32 anchor = leader ? leader->Level : (candidates.empty() ? 0 : candidates.front().Level);
    int32 lo = anchor - int32(cfg.MaxLevelSpread);
    int32 hi = anchor + int32(cfg.MaxLevelSpread);

    std::vector<Candidate const*> pool;
    for (Candidate const& c : candidates)
    {
        if (!c.Leader)
        {
            if (!c.Available || (c.Busy && !cfg.AllowBusy) || c.DistanceToLeader > cfg.MaxDistance)
                continue;
            if (!levelOk(c, lo, hi))
                continue;
        }
        else if (c.Level < minLevel || c.Level > dungeonMaxLevel)
            continue;      // a leader who does not fit the dungeon gets an empty group
        if (!RolesOfClass(c.ClassId))
            continue;
        pool.push_back(&c);
    }
    if (leader && (leader->Level < minLevel || leader->Level > dungeonMaxLevel))
    {
        res.MissingTanks = need[0];
        res.MissingHealers = need[1];
        res.MissingDps = need[2];
        return res;
    }

    auto better = [&](size_t a, size_t b, Role r)
    {
        bool const pa = IsPreferred(pool[a]->ClassId, r), pb = IsPreferred(pool[b]->ClassId, r);
        if (pa != pb)
            return pa;
        if (pool[a]->DistanceToLeader != pool[b]->DistanceToLeader)
            return pool[a]->DistanceToLeader < pool[b]->DistanceToLeader;
        return pool[a]->Guid < pool[b]->Guid;
    };

    // One full assignment with the leader forced into `leaderRole` (-1: no leader). Scarce roles are filled first, damage last.
    auto assign = [&](int leaderRole)
    {
        ComposeResult out;
        uint32 n[3] = { need[0], need[1], need[2] };
        std::vector<bool> used(pool.size(), false);
        for (size_t i = 0; i < pool.size() && leaderRole >= 0; ++i)
            if (pool[i]->Leader)
            {
                used[i] = true;
                out.Members.push_back({ pool[i]->Guid, Role(leaderRole) });
                --n[leaderRole];
                break;
            }
        for (Role r : { Role::Tank, Role::Healer, Role::Dps })
        {
            while (n[uint32(r)])
            {
                int best = -1;
                for (size_t i = 0; i < pool.size(); ++i)
                {
                    if (used[i] || pool[i]->Leader || !(RolesOfClass(pool[i]->ClassId) & RoleBit(r)))
                        continue;
                    if (best < 0 || better(i, size_t(best), r))
                        best = int(i);
                }
                if (best < 0)
                    break;
                used[size_t(best)] = true;
                out.Members.push_back({ pool[size_t(best)]->Guid, r });
                --n[uint32(r)];
            }
        }
        out.MissingTanks = n[0];
        out.MissingHealers = n[1];
        out.MissingDps = n[2];
        return out;
    };

    // The leader is always in. Try each role it can fill, its preferred one first, and keep the assignment that leaves the fewest
    // places open (so a paladin leader tanks only when nobody else can, and a priest leader heals when nobody else can).
    if (!leader)
        return assign(-1);
    uint32 const mask = RolesOfClass(leader->ClassId);
    Role const prefer = PreferredRole(leader->ClassId);
    std::vector<Role> options;
    if (mask & RoleBit(prefer))
        options.push_back(prefer);
    for (Role r : { Role::Tank, Role::Healer, Role::Dps })
        if ((mask & RoleBit(r)) && r != prefer && need[uint32(r)])
            options.push_back(r);
    ComposeResult best;
    bool have = false;
    for (Role r : options)
    {
        if (!need[uint32(r)])
            continue;
        ComposeResult cand = assign(int(r));
        auto missing = [](ComposeResult const& c) { return c.MissingTanks + c.MissingHealers + c.MissingDps; };
        if (!have || missing(cand) < missing(best))
        {
            best = std::move(cand);
            have = true;
        }
    }
    return have ? best : assign(-1);
}

Ready CheckReady(std::span<MemberState const> members, ReadyConfig const& cfg)
{
    bool anyMissing = false, anyRest = false, anyRepair = false;
    for (MemberState const& m : members)
    {
        if (m.InCombat)
            return Ready::InCombat;
        if (!m.Alive || !m.Present)
        {
            anyMissing = true;
            continue;
        }
        int32 const needMana = m.AsRole == Role::Healer ? cfg.MinHealerManaPct : cfg.MinManaPct;
        if (m.HealthPct < cfg.MinHealthPct || m.ManaPct < needMana)
            anyRest = true;
        if (m.DurabilityPct < cfg.MinDurabilityPct)
            anyRepair = true;
    }
    if (members.empty() || anyMissing)
        return Ready::WaitForMembers;
    if (anyRepair)
        return Ready::Repair;
    return anyRest ? Ready::Rest : Ready::Go;
}

PullChoice ChoosePull(std::span<Pack const> packs, int32 avgLevel, Ready ready, uint32 alive, uint32 groupSize, PullConfig const& cfg)
{
    PullChoice out;
    bool anyLeft = false, trashLeft = false;
    for (Pack const& p : packs)
    {
        if (p.Done)
            continue;
        anyLeft = true;
        if (!p.Boss)
            trashLeft = true;
    }
    if (!anyLeft)
    {
        out.Kind = PullKind::Finished;
        out.Why = "no packs left";
        return out;
    }
    if (ready != Ready::Go)
    {
        out.Kind = PullKind::Rest;
        out.Why = ready == Ready::InCombat ? "in combat" : ready == Ready::Rest ? "resting"
            : ready == Ready::Repair ? "repair" : "waiting for members";
        return out;
    }

    Pack const* best = nullptr;
    bool bossBlocked = false;
    for (Pack const& p : packs)
    {
        if (p.Done)
            continue;
        if (p.MaxMobLevel > avgLevel + cfg.MaxLevelOverGroup)
            continue;
        if (!p.Boss && p.Mobs > cfg.MaxMobsPerPull)
            continue;
        if (p.Patrols && cfg.SkipPatrols)
            continue;
        if (p.Boss && trashLeft)
        {
            bossBlocked = true;     // trash first; a boss is taken only when every other pack is done or skipped
            continue;
        }
        if (p.Boss && cfg.BossNeedsFullGroup && alive < groupSize)
            continue;
        if (!best || p.Distance < best->Distance || (p.Distance == best->Distance && p.Id < best->Id))
            best = &p;
    }
    // trash is skipped when every trash pack is too strong: then the boss may go anyway, if the group can take it
    if (!best && bossBlocked)
    {
        for (Pack const& p : packs)
        {
            if (p.Done || !p.Boss || p.MaxMobLevel > avgLevel + cfg.MaxLevelOverGroup)
                continue;
            if (cfg.BossNeedsFullGroup && alive < groupSize)
                continue;
            if (!best || p.Distance < best->Distance)
                best = &p;
        }
    }
    if (!best)
    {
        out.Kind = PullKind::SkipAll;
        out.Why = "every pack left is too strong, patrolling or the group is short";
        return out;
    }
    out.Kind = PullKind::Pull;
    out.PackId = best->Id;
    out.Why = best->Boss ? "boss" : "nearest pack";
    return out;
}

RunResult AdvanceRun(RunFacts const& f, RunConfig const& cfg)
{
    auto stay = [&](char const* why) { return RunResult{ f.Current, why }; };
    auto go = [](Phase p, char const* why) { return RunResult{ p, why }; };

    if (f.Current == Phase::Done || f.Current == Phase::Aborted)
        return stay("finished");

    uint32 const inPhase = f.NowMs - f.PhaseSinceMs;
    if (f.StartedMs && !f.LeaderIsPlayer && f.NowMs - f.StartedMs > cfg.RunTimeoutMs)
        return go(Phase::Aborted, "run timeout");

    // a wipe: everybody dead inside the dungeon
    if (f.Inside && f.Alive == 0 && f.Current != Phase::Recover)
    {
        if (f.Wipes + 1 > cfg.MaxWipes)
            return go(Phase::Aborted, "too many wipes");
        return go(Phase::Recover, "wipe");
    }

    switch (f.Current)
    {
        case Phase::Gather:
            if (f.Present >= f.Size)
                return go(Phase::Travel, "group complete");
            if (inPhase > cfg.GatherTimeoutMs)
                return go(Phase::Aborted, "gather timeout");
            return stay("gathering");
        case Phase::Travel:
            if (f.Inside)
                return go(Phase::Clear, "entered");
            if (inPhase > cfg.TravelTimeoutMs)
                return go(Phase::Aborted, "travel timeout");
            return stay("travelling");
        case Phase::Clear:
            if (!f.Inside)
                return go(Phase::Travel, "left the dungeon");
            if (f.BossKilled)
                return go(f.LootPending ? Phase::Loot : Phase::Done, "last boss dead");
            if (!f.PacksLeft)
                return go(Phase::Done, "no packs left");
            return stay("clearing");
        case Phase::Loot:
            if (!f.LootPending || inPhase > 60000)
                return go(Phase::Done, "looted");
            return stay("looting");
        case Phase::Recover:
            if (f.Alive >= f.Size && f.Present >= f.Size)
                return go(f.Inside ? Phase::Clear : Phase::Travel, "regrouped");
            if (inPhase > cfg.RecoverTimeoutMs)
                return go(Phase::Aborted, "recover timeout");
            return stay("recovering");
        default:
            return stay("");
    }
}
}
