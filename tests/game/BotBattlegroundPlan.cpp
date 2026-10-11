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

#include "tc_catch2.h"

#include "BotBattlegroundPlan.h"

#include <algorithm>
#include <cmath>

using namespace BotBattleground;

namespace
{
QueueFacts OneHuman()
{
    QueueFacts f;
    f.HumansWaiting[0] = 1;
    f.LongestHumanWaitSec = 30;
    f.MinPerTeam = 5;
    f.MaxPerTeam = 10;
    return f;
}

Facts Running()
{
    Facts f;
    f.InProgress = true;
    f.OwnFlag = Flag::InBase;
    f.EnemyFlag = Flag::InBase;
    return f;
}
}

TEST_CASE("No bots queue without a waiting player", "[BotBattlegroundPlan]")
{
    Config cfg;
    QueueFacts f;
    f.BotsQueued[0] = 2;
    QueueNeed n = PlanQueue(f, cfg);
    CHECK(n.Add[0] == 0);
    CHECK(n.Add[1] == 0);
    CHECK_FALSE(n.DropBots);   // not yet: the player may be about to come back

    f.NoHumanSec = cfg.IdleDropSec;
    CHECK(PlanQueue(f, cfg).DropBots);
}

TEST_CASE("A player waits alone for JoinDelaySec", "[BotBattlegroundPlan]")
{
    Config cfg;
    QueueFacts f = OneHuman();
    f.LongestHumanWaitSec = cfg.JoinDelaySec - 1;
    CHECK(PlanQueue(f, cfg).Add[0] + PlanQueue(f, cfg).Add[1] == 0);
    f.LongestHumanWaitSec = cfg.JoinDelaySec;
    QueueNeed n = PlanQueue(f, cfg);
    CHECK(n.Add[0] + n.Add[1] > 0);
}

TEST_CASE("Both sides are filled to the minimum", "[BotBattlegroundPlan]")
{
    Config cfg;
    cfg.JoinPerStepMax = 20;
    cfg.MaxBotsPerQueue = 20;
    QueueNeed n = PlanQueue(OneHuman(), cfg);
    CHECK(n.Add[0] == 4);   // 1 player + 4 bots
    CHECK(n.Add[1] == 5);

    QueueFacts f = OneHuman();
    f.BotsQueued[0] = 4;
    f.BotsQueued[1] = 5;
    n = PlanQueue(f, cfg);
    CHECK(n.Add[0] == 0);
    CHECK(n.Add[1] == 0);
}

TEST_CASE("The step and the queue cap limit the bots", "[BotBattlegroundPlan]")
{
    Config cfg;
    QueueNeed n = PlanQueue(OneHuman(), cfg);
    CHECK(n.Add[0] + n.Add[1] == cfg.JoinPerStepMax);
    CHECK(std::abs(int(n.Add[0]) - int(n.Add[1])) <= 1);   // both sides fill together

    cfg.MaxBotsPerQueue = 2;
    cfg.JoinPerStepMax = 10;
    QueueFacts f = OneHuman();
    f.BotsQueued[1] = 1;
    n = PlanQueue(f, cfg);
    CHECK(n.Add[0] + n.Add[1] == 1);
}

TEST_CASE("FillTo is clamped to the battleground", "[BotBattlegroundPlan]")
{
    Config cfg;
    cfg.JoinPerStepMax = 40;
    cfg.MaxBotsPerQueue = 80;
    cfg.FillTo = 40;
    QueueNeed n = PlanQueue(OneHuman(), cfg);
    CHECK(n.Add[0] == 9);
    CHECK(n.Add[1] == 10);

    cfg.FillTo = 2;   // below the minimum: the match could never start
    n = PlanQueue(OneHuman(), cfg);
    CHECK(n.Add[0] == 4);
    CHECK(n.Add[1] == 5);
}

TEST_CASE("Bots are picked per side and stable", "[BotBattlegroundPlan]")
{
    std::vector<Candidate> cands;
    for (uint64 g = 1; g <= 12; ++g)
        cands.push_back({ g, uint8(g % 2), 20 });
    uint32 const need[TEAMS] = { 3, 2 };
    std::vector<uint64> a = PickBots(cands, need, 7), b = PickBots(cands, need, 7);
    REQUIRE(a.size() == 5);
    CHECK(a == b);
    CHECK(std::count_if(a.begin(), a.end(), [](uint64 g) { return g % 2 == 0; }) == 3);
    CHECK(std::count_if(a.begin(), a.end(), [](uint64 g) { return g % 2 == 1; }) == 2);

    uint32 const many[TEAMS] = { 99, 0 };
    CHECK(PickBots(cands, many, 1).size() == 6);   // only six bots of that side exist
}

TEST_CASE("Answer and leave delays stay in their range", "[BotBattlegroundPlan]")
{
    Config cfg;
    for (uint64 g = 1; g < 200; ++g)
    {
        uint32 const a = AcceptDelayMs(cfg, g), l = LeaveDelayMs(cfg, g);
        CHECK(a >= cfg.AcceptMinSec * 1000);
        CHECK(a <= cfg.AcceptMaxSec * 1000);
        CHECK(l >= cfg.LeaveMinSec * 1000);
        CHECK(l <= cfg.LeaveMaxSec * 1000);
        CHECK(a == AcceptDelayMs(cfg, g));
    }
}

TEST_CASE("Roles split a side", "[BotBattlegroundPlan]")
{
    Config cfg;
    auto count = [&](uint32 n, Role r)
    {
        uint32 c = 0;
        for (uint32 i = 0; i < n; ++i)
            c += RoleFor(i, n, cfg) == r;
        return c;
    };
    CHECK(count(10, Role::Offense) == 4);
    CHECK(count(10, Role::Defense) == 3);   // 2.5 rounds up
    CHECK(count(10, Role::Roam) == 3);
    CHECK(count(3, Role::Offense) >= 1);
    CHECK(count(3, Role::Defense) >= 1);
    CHECK(RoleFor(0, 0, cfg) == Role::Roam);
    CHECK(RoleFor(0, 1, cfg) == Role::Offense);   // 0.4 rounds to 0, but the first bot still has a job
}

TEST_CASE("Nothing happens before the gates open or when dead", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.InProgress = false;
    CHECK(PlanStep(f, cfg) == Order::Hold);
    f.InProgress = true;
    f.Alive = false;
    CHECK(PlanStep(f, cfg) == Order::Hold);
}

TEST_CASE("Attackers go for the enemy flag", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.MyRole = Role::Offense;
    CHECK(PlanStep(f, cfg) == Order::GetEnemyFlag);
    f.EnemyFlag = Flag::None;   // object not loaded yet counts as in base
    CHECK(PlanStep(f, cfg) == Order::GetEnemyFlag);
    f.EnemyFlag = Flag::Respawning;
    CHECK(PlanStep(f, cfg) == Order::Roam);
}

TEST_CASE("The carrier brings the flag home, or waits when its own flag is away", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.IAmCarrier = true;
    f.MyRole = Role::Defense;   // whatever its role was
    CHECK(PlanStep(f, cfg) == Order::CarryHome);
    f.OwnFlag = Flag::Taken;
    CHECK(PlanStep(f, cfg) == Order::WaitHome);
    f.OwnFlag = Flag::Dropped;
    f.DistOwnDropped = 5.0f;
    CHECK(PlanStep(f, cfg) == Order::WaitHome);   // the carrier does not stop to return it
}

TEST_CASE("A dropped own flag is returned from within reach", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.OwnFlag = Flag::Dropped;
    f.MyRole = Role::Offense;
    f.DistOwnDropped = cfg.ReturnYards - 1.0f;
    CHECK(PlanStep(f, cfg) == Order::ReturnOwnFlag);
    f.DistOwnDropped = cfg.ReturnYards + 1.0f;
    CHECK(PlanStep(f, cfg) != Order::ReturnOwnFlag);
    f.MyRole = Role::Defense;   // defenders come from three times as far
    f.DistOwnDropped = cfg.ReturnYards * 2.0f;
    CHECK(PlanStep(f, cfg) == Order::ReturnOwnFlag);
    f.DistOwnDropped = -1.0f;
    CHECK(PlanStep(f, cfg) != Order::ReturnOwnFlag);
}

TEST_CASE("A dropped enemy flag is picked up by attackers and roamers only", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.EnemyFlag = Flag::Dropped;
    f.DistEnemyDropped = 30.0f;
    f.MyRole = Role::Offense;
    CHECK(PlanStep(f, cfg) == Order::PickUpFlag);
    f.MyRole = Role::Roam;
    CHECK(PlanStep(f, cfg) == Order::PickUpFlag);
    f.MyRole = Role::Defense;
    CHECK(PlanStep(f, cfg) == Order::Defend);
}

TEST_CASE("When the enemy has our flag the carrier is hunted", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.OwnFlag = Flag::Taken;
    f.EnemyCarrierSeen = true;
    for (Role r : { Role::Offense, Role::Defense, Role::Roam })
    {
        f.MyRole = r;
        CHECK(PlanStep(f, cfg) == Order::HuntCarrier);
    }
    f.TeamCarrier = true;   // our own carrier needs escort more than a hunt does
    f.MyRole = Role::Offense;
    CHECK(PlanStep(f, cfg) == Order::Escort);

    f.TeamCarrier = false;
    f.EnemyCarrierSeen = false;   // nobody knows where he is
    CHECK(PlanStep(f, cfg) == Order::GetEnemyFlag);
    f.MyRole = Role::Roam;
    CHECK(PlanStep(f, cfg) == Order::Defend);
}

TEST_CASE("Escorts follow the side's carrier, defenders stay", "[BotBattlegroundPlan]")
{
    Config cfg;
    Facts f = Running();
    f.TeamCarrier = true;
    f.EnemyFlag = Flag::Taken;
    f.MyRole = Role::Offense;
    CHECK(PlanStep(f, cfg) == Order::Escort);
    f.MyRole = Role::Roam;
    CHECK(PlanStep(f, cfg) == Order::Escort);
    f.MyRole = Role::Defense;
    CHECK(PlanStep(f, cfg) == Order::Defend);
}

TEST_CASE("Engagement distances depend on the job", "[BotBattlegroundPlan]")
{
    Config cfg;
    CHECK_FALSE(ShouldEngage(Order::Defend, -1.0f, cfg));
    CHECK(ShouldEngage(Order::Defend, cfg.EngageYards, cfg));
    CHECK_FALSE(ShouldEngage(Order::Defend, cfg.EngageYards + 1.0f, cfg));
    CHECK_FALSE(ShouldEngage(Order::Hold, 1.0f, cfg));
    CHECK(ShouldEngage(Order::CarryHome, 5.0f, cfg));
    CHECK_FALSE(ShouldEngage(Order::CarryHome, 15.0f, cfg));
    CHECK_FALSE(ShouldEngage(Order::ReturnOwnFlag, 20.0f, cfg));
    CHECK(ShouldEngage(Order::HuntCarrier, 50.0f, cfg));
    CHECK_FALSE(ShouldEngage(Order::HuntCarrier, cfg.ReturnYards * 2.0f, cfg));
}

TEST_CASE("Standing points lie in the ring and hold for a while", "[BotBattlegroundPlan]")
{
    for (uint64 g = 1; g < 100; ++g)
    {
        Point p = RingPoint(100.0f, 200.0f, 4.0f, 15.0f, g, 5000);
        float const d = std::hypot(p.X - 100.0f, p.Y - 200.0f);
        CHECK(d >= 3.99f);
        CHECK(d <= 15.01f);
        Point q = RingPoint(100.0f, 200.0f, 4.0f, 15.0f, g, 19000);
        CHECK(p.X == q.X);
        CHECK(p.Y == q.Y);
    }
}

TEST_CASE("Every order has a name", "[BotBattlegroundPlan]")
{
    for (uint8 i = 0; i <= uint8(Order::Roam); ++i)
        CHECK(std::string(OrderName(Order(i))) != "?");
}
