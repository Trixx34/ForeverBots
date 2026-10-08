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

#include "BotMovePlan.h"
#include <cmath>
#include <set>

using namespace BotMove;

namespace
{
bool AlwaysClear(Vec3 const&, Vec3 const&) { return true; }

// a wall along x = 10 from y = -12 to y = 6 blocks every straight line that crosses it inside that range
bool WallBlocks(Vec3 const& a, Vec3 const& b)
{
    if ((a.x - 10.0f) * (b.x - 10.0f) >= 0.0f)
        return true;
    float const t = (10.0f - a.x) / (b.x - a.x);
    float const y = a.y + t * (b.y - a.y);
    return !(y > -12.0f && y < 6.0f);
}
}

TEST_CASE("BotMove: path shortcut", "[BotMove]")
{
    LosFn open = AlwaysClear;

    SECTION("a zigzag collapses to one leg when everything is in sight")
    {
        std::vector<Vec3> path = { {0, 0, 0}, {5, 3, 0}, {10, -2, 0}, {15, 3, 0}, {20, 0, 0} };
        std::vector<Vec3> out = ShortcutPath(path, open);
        REQUIRE(out.size() == 2);
        CHECK(out.front().x == 0.0f);
        CHECK(out.back().x == 20.0f);
    }

    SECTION("a wall keeps the detour around it")
    {
        std::vector<Vec3> path = { {0, 0, 0}, {5, 8, 0}, {15, 8, 0}, {20, 0, 0} };
        std::vector<Vec3> out = ShortcutPath(path, WallBlocks);
        REQUIRE(out.size() == 4);   // every shortcut crosses the wall, so the detour over its end stays
        CHECK(PathLength(out) == Catch::Approx(PathLength(path)));
    }

    SECTION("a shortcut never makes the path longer")
    {
        std::vector<Vec3> path = { {0, 0, 0}, {10, 10, 0}, {20, 0, 0}, {30, 10, 0}, {40, 0, 0} };
        CHECK(PathLength(ShortcutPath(path, open)) <= PathLength(path));
    }

    SECTION("legs longer than the limit are not created")
    {
        std::vector<Vec3> path = { {0, 0, 0}, {30, 4, 0}, {60, 0, 0}, {90, 4, 0} };
        ShortcutConfig cfg;
        cfg.MaxSegment = 45.0f;
        std::vector<Vec3> out = ShortcutPath(path, open, cfg);
        for (size_t i = 1; i < out.size(); ++i)
            CHECK(Dist3D(out[i - 1], out[i]) <= 45.0f + 0.01f);
    }

    SECTION("short paths come back as they are")
    {
        CHECK(ShortcutPath({}, open).empty());
        std::vector<Vec3> two = { {0, 0, 0}, {5, 5, 0} };
        CHECK(ShortcutPath(two, open).size() == 2);
    }
}

TEST_CASE("BotMove: corner rounding", "[BotMove]")
{
    std::vector<Vec3> corner = { {0, 0, 0}, {20, 0, 0}, {20, 20, 0} };

    SECTION("a right angle becomes two gentle turns")
    {
        std::vector<Vec3> out = RoundCorners(corner, AlwaysClear);
        REQUIRE(out.size() == 4);
        float worst = 0.0f;
        for (size_t i = 1; i + 1 < out.size(); ++i)
            worst = std::max(worst, TurnAngleDeg(out[i - 1], out[i], out[i + 1]));
        CHECK(worst < 90.0f);
        CHECK(worst > 0.0f);
        CHECK(out.front().x == 0.0f);
        CHECK(out.back().y == 20.0f);
    }

    SECTION("a blocked chamfer keeps the original corner")
    {
        auto blocked = [](Vec3 const&, Vec3 const&) { return false; };
        CHECK(RoundCorners(corner, blocked).size() == 3);
    }

    SECTION("a nearly straight path is left alone")
    {
        std::vector<Vec3> flat = { {0, 0, 0}, {20, 1, 0}, {40, 0, 0} };
        CHECK(RoundCorners(flat, AlwaysClear).size() == 3);
    }

    SECTION("tiny legs are not chamfered")
    {
        std::vector<Vec3> tiny = { {0, 0, 0}, {1, 0, 0}, {1, 1, 0} };
        CHECK(RoundCorners(tiny, AlwaysClear).size() == 3);
    }

    SECTION("turn angle")
    {
        CHECK(TurnAngleDeg({0, 0, 0}, {1, 0, 0}, {2, 0, 0}) == Catch::Approx(0.0f).margin(0.01f));
        CHECK(TurnAngleDeg({0, 0, 0}, {1, 0, 0}, {1, 1, 0}) == Catch::Approx(90.0f).margin(0.01f));
        CHECK(TurnAngleDeg({0, 0, 0}, {1, 0, 0}, {0, 0.001f, 0}) > 170.0f);
    }
}

TEST_CASE("BotMove: approach offsets spread bots", "[BotMove]")
{
    ApproachParams p;
    p.TargetKey = 4242;
    p.Radius = 3.0f;
    p.BearingToBot = 1.0f;

    SECTION("the same inputs give the same offset")
    {
        p.BotKey = 7;
        Vec3 a = ApproachOffset(p), b = ApproachOffset(p);
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }

    SECTION("200 bots at one target do not share a point")
    {
        std::set<std::pair<int, int>> cells;
        for (uint64 bot = 1; bot <= 200; ++bot)
        {
            p.BotKey = bot;
            Vec3 o = ApproachOffset(p);
            CHECK(std::hypot(o.x, o.y) <= p.Radius + 0.001f);
            cells.insert({ int(std::floor(o.x * 2.0f)), int(std::floor(o.y * 2.0f)) });
        }
        CHECK(cells.size() > 18);   // half-yard cells in a small arc: a spread, not one or two points
    }

    SECTION("offsets stay on the side the bot comes from")
    {
        for (uint64 bot = 1; bot <= 100; ++bot)
        {
            p.BotKey = bot;
            Vec3 o = ApproachOffset(p);
            float const ang = std::atan2(o.y, o.x);
            CHECK(std::fabs(ang - p.BearingToBot) <= p.ArcRad * 0.5f + 0.001f);
        }
    }

    SECTION("a new attempt moves the point")
    {
        p.BotKey = 9;
        Vec3 first = ApproachOffset(p);
        p.Attempt = 1;
        Vec3 second = ApproachOffset(p);
        CHECK((first.x != second.x || first.y != second.y));
    }
}

TEST_CASE("BotMove: pacing", "[BotMove]")
{
    SECTION("pauses stay in their ranges and differ between bots")
    {
        std::set<uint32> seen;
        for (uint64 bot = 1; bot <= 100; ++bot)
        {
            uint32 g = PauseMs(Pause::QuestGiver, bot, 0);
            CHECK(g >= 600);
            CHECK(g <= 1800);
            uint32 l = PauseMs(Pause::Loot, bot, 0);
            CHECK(l >= 400);
            CHECK(l <= 1200);
            uint32 c = PauseMs(Pause::PostCombat, bot, 0);
            CHECK(c >= 800);
            CHECK(c <= 2500);
            CHECK(PauseMs(Pause::StartDelay, bot, 3) <= 900);
            seen.insert(g / 25);
        }
        CHECK(seen.size() > 20);
    }

    SECTION("repeated pauses of one bot differ")
    {
        CHECK(PauseMs(Pause::Arrive, 5, 1) != PauseMs(Pause::Arrive, 5, 2));
    }

    SECTION("speed varies a little and drifts without jumps")
    {
        float lo = 10.0f, hi = 0.0f, maxStep = 0.0f, prev = SpeedFactor(11, 0, 0.04f);
        for (uint32 t = 0; t < 600000; t += 1000)
        {
            float v = SpeedFactor(11, t, 0.04f);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            maxStep = std::max(maxStep, std::fabs(v - prev));
            prev = v;
        }
        CHECK(lo >= 0.96f - 0.0001f);
        CHECK(hi <= 1.04f + 0.0001f);
        CHECK(hi - lo > 0.02f);
        CHECK(maxStep < 0.01f);   // one second never changes speed by more than one percent
        CHECK(SpeedFactor(11, 1234, 0.0f) == 1.0f);
        CHECK(SpeedFactor(11, 500000, 0.04f) != SpeedFactor(12, 500000, 0.04f));
    }
}

TEST_CASE("BotMove: failed legs lead to another route, never the same query", "[BotMove]")
{
    SECTION("advice escalates")
    {
        CHECK(AdviceFor(0) == Reroute::Retry);
        CHECK(AdviceFor(1) == Reroute::Retry);
        CHECK(AdviceFor(2) == Reroute::SideRoute);
        CHECK(AdviceFor(3) == Reroute::Wait);
        CHECK(AdviceFor(4) == Reroute::Wait);
        CHECK(AdviceFor(5) == Reroute::GiveUp);
        CHECK(AdviceFor(50) == Reroute::GiveUp);
    }

    SECTION("backoff grows, is capped and carries jitter")
    {
        uint32 prev = 0;
        for (uint32 fails = 1; fails <= 4; ++fails)
        {
            uint32 w = BackoffMs(fails, 3, 99);
            CHECK(w > prev * 3 / 4);
            prev = w;
        }
        for (uint32 fails = 1; fails < 40; ++fails)
            CHECK(BackoffMs(fails, 3, 99) <= 30000u * 5 / 4);
        CHECK(BackoffMs(1, 3, 99) >= 1500u * 3 / 4);
        CHECK(BackoffMs(1, 3, 99) != BackoffMs(1, 4, 99));   // two bots do not retry in lockstep
    }

    SECTION("a failed goal is blocked until the backoff passed")
    {
        FailTable t;
        uint64 key = LegKey({0, 0, 0}, {100, 100, 0});
        CHECK_FALSE(t.Blocked(key, 1000));
        uint32 wait = t.Fail(key, 1000, 5);
        CHECK(wait >= 1000);
        CHECK(t.Blocked(key, 1000 + wait - 1));
        CHECK_FALSE(t.Blocked(key, 1000 + wait + 1));
        CHECK(t.Fails(key) == 1);
        uint32 wait2 = t.Fail(key, 5000, 5);
        CHECK(wait2 > wait * 3 / 4);
        CHECK(t.Fails(key) == 2);
        t.Success(key);
        CHECK(t.Fails(key) == 0);
        CHECK_FALSE(t.Blocked(key, 5001));
    }

    SECTION("the table is bounded")
    {
        FailTable t(4);
        for (uint64 k = 1; k <= 20; ++k)
            t.Fail(k, uint32(k * 100), 1);
        CHECK(t.Size() == 4);
        CHECK(t.Fails(20) == 1);   // the newest survive
    }

    SECTION("the same leg issued again and again is counted")
    {
        LegTracker lt;
        uint64 key = LegKey({0, 0, 0}, {50, 0, 0});
        CHECK(lt.Issue(key, 1000) == 0);
        CHECK(lt.Issue(key, 1400) == 1);
        CHECK(lt.Issue(key, 1800) == 2);
        CHECK(lt.Repeats() == 2);
        CHECK(lt.Issue(key + 1, 1900) == 0);
        CHECK(lt.Issue(key, 20000) == 0);   // outside the window it is a new leg
    }

    SECTION("leg keys ignore sub-cell jitter and tell legs apart")
    {
        CHECK(LegKey({0.2f, 0.3f, 0}, {50.1f, 50.2f, 0}) == LegKey({0.5f, 0.1f, 0}, {50.4f, 50.0f, 0}));
        CHECK(LegKey({0, 0, 0}, {50, 50, 0}) != LegKey({0, 0, 0}, {50, 80, 0}));
    }
}

TEST_CASE("BotMove: stuck response", "[BotMove]")
{
    StuckPlan first = PlanStuck(1, 5, 10000);
    CHECK(first.Act == StuckAct::StepBack);
    CHECK(first.LookPauseMs >= 700);
    CHECK(first.StepYards >= 2.5f);
    CHECK(first.StepYards <= 5.0f);
    CHECK(first.TurnRad != 0.0f);

    StuckPlan second = PlanStuck(2, 5, 20000);
    CHECK(second.Act == StuckAct::SideRoute);
    CHECK(std::fabs(second.SideYards) >= 6.0f);

    CHECK(PlanStuck(3, 5, 30000).Act == StuckAct::GiveUp);
    CHECK(PlanStuck(9, 5, 30000).Act == StuckAct::GiveUp);

    // the bot never answers a stuck episode by pushing on: no episode asks for the same move
    CHECK(first.Act != second.Act);
}

TEST_CASE("BotMove: idle behavior", "[BotMove]")
{
    IdleConfig cfg;
    IdleFacts f;

    SECTION("a bot that just stopped waits")
    {
        f.StillMs = 500;
        f.SinceActionMs = 500;
        CHECK(PlanIdle(f, cfg, 1, 10000).Act == IdleAct::Stay);
    }

    SECTION("standing too long forces a wander")
    {
        f.StillMs = cfg.MaxStillMs;
        f.SinceActionMs = 100;
        IdlePlan p = PlanIdle(f, cfg, 1, 40000);
        CHECK(p.Act == IdleAct::Wander);
        CHECK(p.WanderYards >= cfg.WanderMin);
        CHECK(p.WanderYards <= cfg.WanderMax);
    }

    SECTION("nothing starts with a threat around, and a sitting bot stands up")
    {
        f.StillMs = 60000;
        f.Threat = true;
        CHECK(PlanIdle(f, cfg, 1, 10000).Act == IdleAct::Stay);
        f.Sitting = true;
        CHECK(PlanIdle(f, cfg, 1, 10000).Act == IdleAct::StandUp);
    }

    SECTION("a sitting bot stands up after its time")
    {
        f.Sitting = true;
        f.SittingMs = 1000;
        CHECK(PlanIdle(f, cfg, 1, 10000).Act == IdleAct::Stay);
        f.SittingMs = cfg.SitMaxMs + 1;
        CHECK(PlanIdle(f, cfg, 1, 10000).Act == IdleAct::StandUp);
    }

    SECTION("over many idle moments a bot looks, wanders and sits; hurt bots sit more")
    {
        uint32 look = 0, wander = 0, sit = 0, hurtSit = 0;
        for (uint32 t = 0; t < 400; ++t)
        {
            f = IdleFacts();
            f.StillMs = 6000 + t * 37;
            f.SinceActionMs = 6000;
            IdlePlan p = PlanIdle(f, cfg, 3 + t, 20000 + t * 1000);
            look += p.Act == IdleAct::Look;
            wander += p.Act == IdleAct::Wander;
            sit += p.Act == IdleAct::Sit;
            f.HurtOrDrained = true;
            hurtSit += PlanIdle(f, cfg, 3 + t, 20000 + t * 1000).Act == IdleAct::Sit;
        }
        CHECK(look > 100);
        CHECK(wander > 40);
        CHECK(sit > 5);
        CHECK(hurtSit > sit);
    }
}

TEST_CASE("BotMove: metrics", "[BotMove]")
{
    SECTION("a straight walk has no turning, no idle")
    {
        Metrics m;
        for (uint32 i = 0; i < 40; ++i)
            m.Sample(i * 500, float(i) * 3.5f, 0.0f, true);
        MetricsSnapshot s = m.Take(40 * 500);
        CHECK(s.TurnRateDegPerSec == Catch::Approx(0.0f).margin(0.01f));
        CHECK(s.SharpTurns == 0);
        CHECK(s.IdleRatio == Catch::Approx(0.0f).margin(0.001f));
        CHECK(s.DistanceYd > 130.0f);
    }

    SECTION("a zigzag has a high turn rate and sharp turns")
    {
        Metrics m;
        for (uint32 i = 0; i < 40; ++i)
            m.Sample(i * 500, float(i % 2) * 3.0f, float(i) * 1.0f, true);
        MetricsSnapshot s = m.Take(40 * 500);
        CHECK(s.TurnRateDegPerSec > 60.0f);
        CHECK(s.SharpTurns > 20);
    }

    SECTION("idle ratio and the longest standstill")
    {
        Metrics m;
        uint32 t = 0;
        for (uint32 i = 0; i < 10; ++i, t += 500)
            m.Sample(t, float(i) * 3.0f, 0.0f, true);
        for (uint32 i = 0; i < 30; ++i, t += 500)
            m.Sample(t, 30.0f, 0.0f, false);
        MetricsSnapshot s = m.Take(t);
        CHECK(s.IdleRatio == Catch::Approx(0.75f).margin(0.05f));
        CHECK(s.LongestIdleMs >= 14000);
    }

    SECTION("counters and window restart")
    {
        Metrics m;
        m.Sample(1000, 0, 0, true);
        m.NoteStuck();
        m.NoteStuck();
        m.NotePathFail();
        m.NoteRepeatedLeg();
        CHECK_FALSE(m.Due(30000, 60000));
        CHECK(m.Due(61000, 60000));
        MetricsSnapshot s = m.Take(61000);
        CHECK(s.StuckEvents == 2);
        CHECK(s.PathFails == 1);
        CHECK(s.RepeatedLegs == 1);
        MetricsSnapshot s2 = m.Take(62000);
        CHECK(s2.StuckEvents == 0);
        CHECK(s2.PathFails == 0);
    }
}
