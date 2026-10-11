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

#include "BotBattlegroundPlan.h"
#include "BotMovePlan.h"
#include <algorithm>
#include <cmath>

namespace BotBattleground
{
using BotMove::Mix;
using BotMove::Range;
using BotMove::Unit;

namespace
{
constexpr float TWO_PI_F = 6.28318531f;
}

QueueNeed PlanQueue(QueueFacts const& f, Config const& cfg)
{
    QueueNeed need;
    uint32 const humans = f.HumansWaiting[0] + f.HumansWaiting[1];
    uint32 const bots = f.BotsQueued[0] + f.BotsQueued[1];
    if (!humans)
    {
        need.DropBots = bots > 0 && f.NoHumanSec >= cfg.IdleDropSec;
        return need;
    }
    if (f.LongestHumanWaitSec < cfg.JoinDelaySec)
        return need;

    uint32 const lo = std::max<uint32>(f.MinPerTeam, 1), hi = std::max(f.MaxPerTeam, lo);
    uint32 const target = cfg.FillTo ? std::clamp(cfg.FillTo, lo, hi) : lo;
    uint32 const room = cfg.MaxBotsPerQueue > bots ? cfg.MaxBotsPerQueue - bots : 0;
    uint32 left = std::min(cfg.JoinPerStepMax, room);

    uint32 want[TEAMS];
    for (uint8 t = 0; t < TEAMS; ++t)
    {
        uint32 const have = f.HumansWaiting[t] + f.BotsQueued[t];
        want[t] = target > have ? target - have : 0;
    }
    // one bot at a time to the side that is further from full, so both sides fill together
    while (left && (want[0] || want[1]))
    {
        uint8 const t = want[0] > want[1] ? 0 : want[1] > want[0] ? 1 : (f.BotsQueued[0] + need.Add[0] <= f.BotsQueued[1] + need.Add[1] ? 0 : 1);
        --want[t];
        ++need.Add[t];
        --left;
    }
    return need;
}

std::vector<uint64> PickBots(std::vector<Candidate> const& cands, uint32 const (&need)[TEAMS], uint64 seed)
{
    std::vector<uint64> out;
    for (uint8 t = 0; t < TEAMS; ++t)
    {
        if (!need[t])
            continue;
        std::vector<Candidate const*> side;
        for (Candidate const& c : cands)
            if (c.Team == t)
                side.push_back(&c);
        std::sort(side.begin(), side.end(), [seed](Candidate const* a, Candidate const* b)
        {
            uint64 const ka = Mix(seed, a->Guid, 11), kb = Mix(seed, b->Guid, 11);
            return ka != kb ? ka < kb : a->Guid < b->Guid;
        });
        for (size_t i = 0; i < side.size() && i < need[t]; ++i)
            out.push_back(side[i]->Guid);
    }
    return out;
}

uint32 AcceptDelayMs(Config const& cfg, uint64 botKey)
{
    uint32 const lo = cfg.AcceptMinSec * 1000, hi = std::max(cfg.AcceptMaxSec * 1000, lo);
    return lo + uint32(Unit(Mix(botKey, 91)) * float(hi - lo));
}

uint32 LeaveDelayMs(Config const& cfg, uint64 botKey)
{
    uint32 const lo = cfg.LeaveMinSec * 1000, hi = std::max(cfg.LeaveMaxSec * 1000, lo);
    return lo + uint32(Unit(Mix(botKey, 92)) * float(hi - lo));
}

Role RoleFor(uint32 rank, uint32 count, Config const& cfg)
{
    if (!count)
        return Role::Roam;
    uint32 off = (count * cfg.OffensePct + 50) / 100;
    uint32 def = (count * cfg.DefensePct + 50) / 100;
    off = std::max<uint32>(off, 1);   // somebody always goes for the flag
    if (count >= 3)
        def = std::max<uint32>(def, 1);
    off = std::min(off, count);
    def = std::min(def, count - off);
    if (rank < off)
        return Role::Offense;
    if (rank < off + def)
        return Role::Defense;
    return Role::Roam;
}

Order PlanStep(Facts const& f, Config const& cfg)
{
    if (!f.Alive || !f.InProgress)
        return Order::Hold;

    if (f.IAmCarrier)
        return f.OwnFlag == Flag::InBase || f.OwnFlag == Flag::None ? Order::CarryHome : Order::WaitHome;

    // the own flag lies on the ground: bring it back, defenders from further away
    if (f.OwnFlag == Flag::Dropped && f.DistOwnDropped >= 0.0f)
    {
        float const reach = f.MyRole == Role::Defense ? cfg.ReturnYards * 3.0f : cfg.ReturnYards;
        if (f.DistOwnDropped <= reach)
            return Order::ReturnOwnFlag;
    }

    // the enemy flag lies on the ground (our carrier died): attackers and roamers take it up
    if (f.EnemyFlag == Flag::Dropped && f.DistEnemyDropped >= 0.0f && f.MyRole != Role::Defense && f.DistEnemyDropped <= cfg.ReturnYards * 1.5f)
        return Order::PickUpFlag;

    if (f.OwnFlag == Flag::Taken)
    {
        if (f.MyRole == Role::Offense && f.TeamCarrier)
            return Order::Escort;
        if (f.EnemyCarrierSeen)
            return Order::HuntCarrier;
        // nobody knows where the carrier is: attackers go on with their job, the rest hold the flag room
        if (f.MyRole == Role::Offense && f.EnemyFlag != Flag::Taken && f.EnemyFlag != Flag::Respawning)
            return Order::GetEnemyFlag;
        return Order::Defend;
    }

    if (f.TeamCarrier)
        return f.MyRole == Role::Defense ? Order::Defend : Order::Escort;

    switch (f.MyRole)
    {
        case Role::Offense:
            return f.EnemyFlag == Flag::Taken || f.EnemyFlag == Flag::Respawning ? Order::Roam : Order::GetEnemyFlag;   // unknown counts as in base
        case Role::Defense:
            return Order::Defend;
        case Role::Roam:
            break;
    }
    return Order::Roam;
}

bool ShouldEngage(Order order, float enemyYards, Config const& cfg)
{
    if (enemyYards < 0.0f)
        return false;
    switch (order)
    {
        case Order::Hold:
            return false;
        case Order::CarryHome:
        case Order::WaitHome:
            return enemyYards <= 6.0f;
        case Order::ReturnOwnFlag:
        case Order::PickUpFlag:
            return enemyYards <= 8.0f;
        case Order::HuntCarrier:
            return enemyYards <= cfg.ReturnYards * 1.5f;
        default:
            return enemyYards <= cfg.EngageYards;
    }
}

Point RingPoint(float cx, float cy, float minYd, float maxYd, uint64 botKey, uint32 nowMs)
{
    uint32 const slot = nowMs / 20000;
    float const bearing = Unit(Mix(botKey, slot, 5)) * TWO_PI_F;
    float const radius = Range(Mix(botKey, slot, 6), minYd, std::max(minYd, maxYd));
    return { cx + std::cos(bearing) * radius, cy + std::sin(bearing) * radius };
}

char const* OrderName(Order order)
{
    switch (order)
    {
        case Order::Hold:          return "hold";
        case Order::GetEnemyFlag:  return "get_enemy_flag";
        case Order::PickUpFlag:    return "pick_up_flag";
        case Order::CarryHome:     return "carry_home";
        case Order::WaitHome:      return "wait_home";
        case Order::ReturnOwnFlag: return "return_own_flag";
        case Order::HuntCarrier:   return "hunt_carrier";
        case Order::Escort:        return "escort";
        case Order::Defend:        return "defend";
        case Order::Roam:          return "roam";
    }
    return "?";
}
}
