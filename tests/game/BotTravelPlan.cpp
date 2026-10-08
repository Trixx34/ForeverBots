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

#include "BotTravelPlan.h"
#include <set>

using namespace BotTravel;

namespace
{
Query Q(uint32 level, Team t, uint32 area)
{
    Query q;
    q.Level = level;
    q.BotTeam = t;
    q.CurrentArea = area;
    Zone const* z = FindZone(area);
    q.CurrentMap = z ? z->Map : 0;
    q.BotKey = 42;
    return q;
}

constexpr uint32 DUN_MOROGH = 1, ELWYNN = 12, WESTFALL = 40, DUSKWOOD = 10, REDRIDGE = 44, TELDRASSIL = 141, DARKSHORE = 148, ASHENVALE = 331,
    DUROTAR = 14, TIRISFAL = 85, BARRENS = 17, WETLANDS = 11, STRANGLETHORN = 33, SILITHUS = 1377,
    EPLAGUE = 139;
}

TEST_CASE("Zone table: sane bands, unique ids", "[BotTravel]")
{
    std::set<uint32> seen;
    for (Zone const& z : Zones())
    {
        INFO(z.Name);
        CHECK(seen.insert(z.Area).second);
        CHECK(z.MinLevel >= 1);
        CHECK(z.MinLevel < z.MaxLevel);
        CHECK(z.MaxLevel <= 60);
        CHECK((z.Teams & 3) != 0);
    }
}

TEST_CASE("Zone table: every level 1 to 60 has a zone for both factions", "[BotTravel]")
{
    for (Team t : { Team::Alliance, Team::Horde })
        for (uint32 lvl = 1; lvl <= 60; ++lvl)
        {
            bool any = false;
            for (Zone const& z : Zones())
                any |= (z.Teams & uint8(t)) && lvl >= z.MinLevel && lvl <= z.MaxLevel;
            INFO("team " << int(t) << " level " << lvl);
            CHECK(any);
        }
}

TEST_CASE("Edges: every edge references known zones and has a mirror", "[BotTravel]")
{
    auto const& edges = StaticEdges();
    for (Edge const& e : edges)
    {
        CHECK(FindZone(e.From) != nullptr);
        CHECK(FindZone(e.To) != nullptr);
        CHECK(e.Sec > 0.0f);
        bool mirrored = false;
        for (Edge const& o : edges)
            mirrored |= o.From == e.To && o.To == e.From && o.M == e.M && o.Teams == e.Teams;
        CHECK(mirrored);
    }
}

TEST_CASE("Every zone is reachable from the starting zone of its faction", "[BotTravel]")
{
    for (Zone const& z : Zones())
        for (Team t : { Team::Alliance, Team::Horde })
        {
            if (!(z.Teams & uint8(t)))
                continue;
            uint32 const start = t == Team::Alliance ? ELWYNN : DUROTAR;
            INFO(z.Name << " for team " << int(t));
            CHECK(PlanRoute(start, z.Area, t, MODE_ALL, {}, 12).Found);
        }
}

TEST_CASE("PlanRoute: same zone, walk, mode filter and team filter", "[BotTravel]")
{
    SECTION("same zone is a found route without steps")
    {
        Route r = PlanRoute(ELWYNN, ELWYNN, Team::Alliance, MODE_ALL);
        CHECK(r.Found);
        CHECK(r.Steps.empty());
        CHECK(r.TotalSec == 0.0f);
    }
    SECTION("neighbouring zones walk")
    {
        Route r = PlanRoute(ELWYNN, WESTFALL, Team::Alliance, MODE_ALL);
        REQUIRE(r.Found);
        REQUIRE(r.Steps.size() == 1);
        CHECK(r.Steps[0].M == MODE_WALK);
    }
    SECTION("Teldrassil is an island: a boat is the only way off")
    {
        CHECK(PlanRoute(TELDRASSIL, DARKSHORE, Team::Alliance, MODE_WALK).Found == false);
        Route r = PlanRoute(TELDRASSIL, DARKSHORE, Team::Alliance, MODE_ALL);
        REQUIRE(r.Found);
        CHECK(r.Steps.front().M == MODE_BOAT);
    }
    SECTION("a Horde zeppelin is closed to the Alliance")
    {
        Route h = PlanRoute(DUROTAR, TIRISFAL, Team::Horde, MODE_ALL);
        REQUIRE(h.Found);
        CHECK(h.Steps.front().M == MODE_ZEPPELIN);
        Route a = PlanRoute(DUROTAR, TIRISFAL, Team::Alliance, MODE_ALL);
        if (a.Found)
            for (Edge const& e : a.Steps)
                CHECK((e.Teams & uint8(Team::Alliance)) != 0);
    }
    SECTION("the step limit is honoured")
    {
        CHECK(PlanRoute(ELWYNN, SILITHUS, Team::Alliance, MODE_ALL, {}, 2).Found == false);
        CHECK(PlanRoute(ELWYNN, SILITHUS, Team::Alliance, MODE_ALL, {}, 12).Found == true);
    }
    SECTION("route steps are connected end to end")
    {
        Route r = PlanRoute(ELWYNN, EPLAGUE, Team::Alliance, MODE_ALL, {}, 12);
        REQUIRE(r.Found);
        uint32 at = ELWYNN;
        float sum = 0.0f;
        for (Edge const& e : r.Steps)
        {
            CHECK(e.From == at);
            at = e.To;
            sum += e.Sec;
        }
        CHECK(at == EPLAGUE);
        CHECK(sum == Catch::Approx(r.TotalSec));
    }
}

TEST_CASE("PlanRoute: a known flight path beats walking", "[BotTravel]")
{
    Route walk = PlanRoute(ELWYNN, SILITHUS, Team::Alliance, MODE_WALK | MODE_BOAT | MODE_TRAM, {}, 12);
    std::vector<Edge> taxi = { { ELWYNN, SILITHUS, MODE_TAXI, TaxiSeconds(4000.0f), 3 } };
    Route fly = PlanRoute(ELWYNN, SILITHUS, Team::Alliance, MODE_ALL, taxi, 12);
    REQUIRE(fly.Found);
    CHECK(fly.Steps.size() == 1);
    CHECK(fly.Steps[0].M == MODE_TAXI);
    if (walk.Found)
        CHECK(fly.TotalSec < walk.TotalSec);
    // the taxi mode can be switched off
    Route noTaxi = PlanRoute(ELWYNN, SILITHUS, Team::Alliance, MODE_WALK | MODE_BOAT | MODE_TRAM, taxi, 12);
    for (Edge const& e : noTaxi.Steps)
        CHECK(e.M != MODE_TAXI);
}

TEST_CASE("PickZone: a bot stays while the zone fits", "[BotTravel]")
{
    for (uint32 lvl = 1; lvl <= 10; ++lvl)
    {
        Choice c = PickZone(Q(lvl, Team::Alliance, ELWYNN));
        CHECK(c.Reason == Why::Stay);
        CHECK(c.Area == ELWYNN);
        CHECK(c.R.Steps.empty());
    }
}

TEST_CASE("PickZone: an outgrown zone leads to a zone that fits", "[BotTravel]")
{
    Choice c = PickZone(Q(11, Team::Alliance, ELWYNN));
    CHECK(c.Reason == Why::Outgrown);
    REQUIRE(c.Area != ELWYNN);
    Zone const* z = FindZone(c.Area);
    REQUIRE(z);
    CHECK(11 >= z->MinLevel);
    CHECK(11 <= z->MaxLevel);
    CHECK((z->Teams & uint8(Team::Alliance)) != 0);
    CHECK(c.R.Found);
}

TEST_CASE("PickZone: the destination always suits level and faction, 1 to 60", "[BotTravel]")
{
    for (Team t : { Team::Alliance, Team::Horde })
        for (Zone const& from : Zones())
        {
            if (!(from.Teams & uint8(t)))
                continue;
            for (uint32 lvl = from.MaxLevel + 1; lvl <= 60; lvl += 3)
            {
                Query q = Q(lvl, t, from.Area);
                q.MaxSteps = 12;
                Choice c = PickZone(q);
                INFO(from.Name << " team " << int(t) << " level " << lvl);
                REQUIRE(c.Reason != Why::Stay);
                if (c.Reason == Why::NoCandidate)
                    continue;
                Zone const* z = FindZone(c.Area);
                REQUIRE(z);
                CHECK((z->Teams & uint8(t)) != 0);
                CHECK(lvl + 3 >= z->MinLevel);
                CHECK(lvl <= z->MaxLevel + 3u);
                CHECK(c.R.Found);
            }
        }
}

TEST_CASE("PickZone: Horde and Alliance end up in different starter-to-mid zones", "[BotTravel]")
{
    Choice h = PickZone(Q(12, Team::Horde, DUROTAR));
    Choice a = PickZone(Q(12, Team::Alliance, ELWYNN));
    REQUIRE(h.Area != 0);
    REQUIRE(a.Area != 0);
    CHECK((FindZone(h.Area)->Teams & uint8(Team::Horde)) != 0);
    CHECK((FindZone(a.Area)->Teams & uint8(Team::Alliance)) != 0);
}

TEST_CASE("PickZone: an exhausted zone is left past the middle of the band only", "[BotTravel]")
{
    Query low = Q(2, Team::Alliance, ELWYNN);
    low.LocalExhausted = true;
    CHECK(PickZone(low).Reason == Why::Stay);
    Query high = Q(8, Team::Alliance, ELWYNN);
    high.LocalExhausted = true;
    Choice c = PickZone(high);
    CHECK(c.Reason == Why::Exhausted);
    CHECK(c.Area != ELWYNN);
}

TEST_CASE("PickZone: a bot that is far too low for its zone moves", "[BotTravel]")
{
    Choice c = PickZone(Q(1, Team::Alliance, STRANGLETHORN));
    CHECK(c.Reason == Why::TooLow);
    CHECK(c.Area != STRANGLETHORN);
}

TEST_CASE("PickZone: the leave margin delays leaving", "[BotTravel]")
{
    Query q = Q(11, Team::Alliance, ELWYNN);
    q.LeaveMargin = 2;
    CHECK(PickZone(q).Reason == Why::Stay);
    q.Level = 13;
    CHECK(PickZone(q).Reason != Why::Stay);
}

TEST_CASE("PickZone: recent zones are avoided, picks are reproducible and spread", "[BotTravel]")
{
    Query q = Q(15, Team::Alliance, ELWYNN);
    Choice first = PickZone(q);
    Choice again = PickZone(q);
    CHECK(first.Area == again.Area);

    q.Recent = { first.Area };
    Choice other = PickZone(q);
    CHECK(other.Area != first.Area);

    std::set<uint32> picked;
    for (uint64 bot = 1; bot <= 200; ++bot)
    {
        Query b = Q(15, Team::Alliance, ELWYNN);
        b.BotKey = bot;
        picked.insert(PickZone(b).Area);
    }
    CHECK(picked.size() >= 2);
}

TEST_CASE("PickZone: an unknown position (a city) still gets a destination", "[BotTravel]")
{
    Query q = Q(25, Team::Horde, 0);
    q.CurrentMap = 1;
    Choice c = PickZone(q);
    CHECK(c.Area != 0);
    CHECK(c.Reason != Why::Stay);
}

TEST_CASE("ShouldHearth: only when the bind is clearly closer to the goal", "[BotTravel]")
{
    HearthQuery q;
    q.Current = SILITHUS;
    q.Target = ELWYNN;
    q.Bind = ELWYNN;
    q.HearthReady = true;
    q.BotTeam = Team::Alliance;
    float saved = 0;
    CHECK(ShouldHearth(q, &saved));
    CHECK(saved > 600.0f);

    SECTION("not ready") { q.HearthReady = false; CHECK_FALSE(ShouldHearth(q)); }
    SECTION("no bind") { q.Bind = 0; CHECK_FALSE(ShouldHearth(q)); }
    SECTION("bind is the current zone") { q.Bind = SILITHUS; CHECK_FALSE(ShouldHearth(q)); }
    SECTION("target one zone away")
    {
        q.Current = ELWYNN;
        q.Target = WESTFALL;
        q.Bind = DUSKWOOD;
        CHECK_FALSE(ShouldHearth(q));
    }
    SECTION("bind farther from the goal than the current spot")
    {
        q.Current = WESTFALL;
        q.Target = ELWYNN;
        q.Bind = SILITHUS;
        CHECK_FALSE(ShouldHearth(q));
    }
}

TEST_CASE("ShouldRebind: binds when it shortens the way back", "[BotTravel]")
{
    RebindQuery q;
    q.BotTeam = Team::Alliance;
    q.Bind = ELWYNN;
    q.InnArea = REDRIDGE;
    q.Next = DUSKWOOD;
    CHECK_FALSE(ShouldRebind(q));      // one zone either way: not worth it

    q.Bind = DUN_MOROGH;
    q.Next = STRANGLETHORN;
    q.InnArea = DUSKWOOD;
    CHECK(ShouldRebind(q));

    q.InnArea = DUN_MOROGH;
    CHECK_FALSE(ShouldRebind(q));      // already bound here

    q.Bind = 0;
    q.InnArea = REDRIDGE;
    CHECK(ShouldRebind(q));            // no bind at all
}

TEST_CASE("Names", "[BotTravel]")
{
    CHECK(std::string(ModeName(MODE_BOAT)) == "boat");
    CHECK(std::string(WhyName(Why::Exhausted)) == "EXHAUSTED");
    CHECK(Zones().size() > 30);
    (void)WETLANDS; (void)ASHENVALE; (void)BARRENS; (void)TIRISFAL;
}
