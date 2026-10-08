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

#include "BotLfgPlan.h"
#include <string>
#include <vector>

using namespace BotLfg;
using BotDungeon::Role;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    c.MinLevel = 1;
    return c;
}

Candidate Bot(uint64 guid, uint8 cls, uint32 level, float dist = 10.0f, uint8 team = 0, bool sameMap = true)
{
    Candidate c;
    c.Guid = guid;
    c.ClassId = cls;
    c.Level = level;
    c.Distance = dist;
    c.Team = team;
    c.SameMap = sameMap;
    return c;
}

Wanted Want(Role mine, uint32 level = 30)
{
    Wanted w;
    w.Level = level;
    w.Have = { mine };
    return w;
}

std::vector<uint64> Guids(FillResult const& r)
{
    std::vector<uint64> out;
    for (Pick const& p : r.Picks)
        out.push_back(p.Guid);
    return out;
}

enum : uint8 { WAR = 1, PAL = 2, HUN = 3, ROG = 4, PRI = 5, SHA = 7, MAG = 8, WLK = 9, DRU = 11 };
}

TEST_CASE("BotLfg: parsing the request", "[BotLfg]")
{
    CHECK(ParseRequest("lfg").What == Action::Join);
    CHECK_FALSE(ParseRequest("lfg").HasRole);
    CHECK(ParseRequest("  LFG  ").What == Action::Join);

    Request r = ParseRequest("lfg tank");
    CHECK(r.What == Action::Join);
    CHECK(r.HasRole);
    CHECK(r.AsRole == Role::Tank);
    CHECK(ParseRequest("LFG Healer").AsRole == Role::Healer);
    CHECK(ParseRequest("lfg heal").AsRole == Role::Healer);
    CHECK(ParseRequest("lfg dps").AsRole == Role::Dps);
    CHECK(ParseRequest("lfg damage").AsRole == Role::Dps);

    CHECK(ParseRequest("lfg off").What == Action::Leave);
    CHECK(ParseRequest("lfg cancel").What == Action::Leave);
    CHECK(ParseRequest("lfg status").What == Action::Status);

    r = ParseRequest("lfg banana");
    CHECK(r.What == Action::Join);
    CHECK(r.BadArgs);

    CHECK(ParseRequest("").What == Action::None);
    CHECK(ParseRequest("lf").What == Action::None);
    CHECK(ParseRequest("lfgx").What == Action::None);
    CHECK(ParseRequest("hello lfg").What == Action::None);
    CHECK(ParseRequest("follow").What == Action::None);
}

TEST_CASE("BotLfg: open places", "[BotLfg]")
{
    Config cfg = On();
    std::vector<Role> have{ Role::Dps };
    FillResult r = OpenPlaces(have, cfg);
    CHECK(r.MissingTanks == 1);
    CHECK(r.MissingHealers == 1);
    CHECK(r.MissingDps == 2);

    have = { Role::Tank };
    r = OpenPlaces(have, cfg);
    CHECK(r.MissingTanks == 0);
    CHECK(r.MissingHealers == 1);
    CHECK(r.MissingDps == 3);

    have = { Role::Tank, Role::Healer, Role::Dps, Role::Dps, Role::Dps };
    r = OpenPlaces(have, cfg);
    CHECK(r.Complete());

    have = { Role::Healer, Role::Healer, Role::Healer };   // extra healers take damage places
    r = OpenPlaces(have, cfg);
    CHECK(r.MissingTanks == 1);
    CHECK(r.MissingHealers == 0);
    CHECK(r.MissingDps == 1);

    cfg.GroupSize = 3;
    have = { Role::Dps };
    r = OpenPlaces(have, cfg);
    CHECK(r.MissingTanks == 1);
    CHECK(r.MissingHealers == 1);
    CHECK(r.MissingDps == 0);

    cfg.GroupSize = 2;
    r = OpenPlaces(have, cfg);
    CHECK(r.MissingTanks == 1);
    CHECK(r.MissingHealers == 0);
}

TEST_CASE("BotLfg: filling a party", "[BotLfg]")
{
    Config cfg = On();
    std::vector<Candidate> c{
        Bot(1, WAR, 30), Bot(2, PRI, 30), Bot(3, MAG, 30), Bot(4, ROG, 30), Bot(5, HUN, 30), Bot(6, WLK, 30)
    };
    FillResult r = PlanFill(c, Want(Role::Dps), cfg);
    REQUIRE(r.Picks.size() == 4);
    CHECK(r.Complete());
    CHECK(r.Picks[0].Guid == 1);
    CHECK(r.Picks[0].AsRole == Role::Tank);
    CHECK(r.Picks[1].Guid == 2);
    CHECK(r.Picks[1].AsRole == Role::Healer);
    CHECK(r.Picks[2].AsRole == Role::Dps);
    CHECK(r.Picks[3].AsRole == Role::Dps);

    SECTION("a player tank needs no tank")
    {
        r = PlanFill(c, Want(Role::Tank), cfg);
        CHECK(r.Picks.size() == 4);
        for (Pick const& p : r.Picks)
            CHECK(p.AsRole != Role::Tank);
        // the warrior is not wasted as tank: it deals damage this time
        CHECK(r.Complete());
    }

    SECTION("a bot is never used twice")
    {
        std::vector<Candidate> few{ Bot(7, PAL, 30), Bot(8, DRU, 30) };
        r = PlanFill(few, Want(Role::Dps), cfg);
        REQUIRE(r.Picks.size() == 2);
        CHECK(r.Picks[0].Guid != r.Picks[1].Guid);
        CHECK(r.MissingTanks + r.MissingHealers + r.MissingDps == 2);
        CHECK_FALSE(r.Complete());
    }

    SECTION("deterministic")
    {
        FillResult again = PlanFill(c, Want(Role::Dps), cfg);
        CHECK(Guids(r) == Guids(again));
    }

    SECTION("empty pool")
    {
        r = PlanFill({}, Want(Role::Dps), cfg);
        CHECK(r.Picks.empty());
        CHECK(r.MissingTanks == 1);
        CHECK(r.MissingHealers == 1);
        CHECK(r.MissingDps == 2);
    }
}

TEST_CASE("BotLfg: who is eligible", "[BotLfg]")
{
    Config cfg = On();

    SECTION("level window around the player")
    {
        std::vector<Candidate> c{ Bot(1, WAR, 25), Bot(2, WAR, 26), Bot(3, WAR, 34), Bot(4, WAR, 35) };
        FillResult r = PlanFill(c, Want(Role::Healer, 30), cfg);
        // tanks wanted: 26 and 34 are in, 25 and 35 are out
        REQUIRE(r.Picks.size() >= 1);
        for (Pick const& p : r.Picks)
            CHECK((p.Guid == 2 || p.Guid == 3));
    }

    SECTION("minimum level")
    {
        cfg.MinLevel = 20;
        std::vector<Candidate> c{ Bot(1, WAR, 15), Bot(2, PRI, 22) };
        FillResult r = PlanFill(c, Want(Role::Dps, 18), cfg);
        REQUIRE(r.Picks.size() == 1);
        CHECK(r.Picks[0].Guid == 2);
    }

    SECTION("level window does not underflow at level 1")
    {
        cfg.MaxLevelSpread = 10;
        std::vector<Candidate> c{ Bot(1, WAR, 1) };
        CHECK(PlanFill(c, Want(Role::Dps, 3), cfg).Picks.size() == 1);
    }

    SECTION("only the team of the player")
    {
        std::vector<Candidate> c{ Bot(1, WAR, 30, 10, 1), Bot(2, WAR, 30, 10, 0) };
        FillResult r = PlanFill(c, Want(Role::Dps), cfg);
        REQUIRE(r.Picks.size() == 1);
        CHECK(r.Picks[0].Guid == 2);
    }

    SECTION("a class without a role is skipped")
    {
        std::vector<Candidate> c{ Bot(1, 6, 30), Bot(2, 0, 30) };
        CHECK(PlanFill(c, Want(Role::Dps), cfg).Picks.empty());
    }

    SECTION("without teleport only near bots on the same map")
    {
        cfg.Teleport = false;
        cfg.MaxDistance = 500.0f;
        std::vector<Candidate> c{ Bot(1, WAR, 30, 100.0f), Bot(2, WAR, 30, 600.0f), Bot(3, WAR, 30, 10.0f, 0, false) };
        FillResult r = PlanFill(c, Want(Role::Dps), cfg);
        REQUIRE(r.Picks.size() == 1);
        CHECK(r.Picks[0].Guid == 1);
    }

    SECTION("with teleport distance and map do not matter")
    {
        std::vector<Candidate> c{ Bot(1, WAR, 30, 99999.0f, 0, false) };
        CHECK(PlanFill(c, Want(Role::Dps), cfg).Picks.size() == 1);
    }
}

TEST_CASE("BotLfg: preference between candidates", "[BotLfg]")
{
    Config cfg = On();

    SECTION("a class that prefers the role comes before one that only can")
    {
        std::vector<Candidate> c{ Bot(1, PAL, 30, 5.0f), Bot(2, WAR, 30, 500.0f) };
        FillResult r = PlanFill(c, Want(Role::Dps), cfg);
        REQUIRE(!r.Picks.empty());
        CHECK(r.Picks[0].Guid == 2);
        CHECK(r.Picks[0].AsRole == Role::Tank);
    }

    SECTION("then the nearer bot, then the lower guid")
    {
        std::vector<Candidate> c{ Bot(9, WAR, 30, 50.0f), Bot(4, WAR, 30, 80.0f), Bot(3, WAR, 30, 50.0f) };
        FillResult r = PlanFill(c, Want(Role::Dps), cfg);
        REQUIRE(!r.Picks.empty());
        CHECK(r.Picks[0].Guid == 3);
    }

    SECTION("a hybrid fills a healer place when no priest is there")
    {
        std::vector<Candidate> c{ Bot(1, WAR, 30), Bot(2, SHA, 30) };
        FillResult r = PlanFill(c, Want(Role::Dps), cfg);
        bool healer = false;
        for (Pick const& p : r.Picks)
            if (p.Guid == 2 && p.AsRole == Role::Healer)
                healer = true;
        CHECK(healer);
    }
}

TEST_CASE("BotLfg: life of a search", "[BotLfg]")
{
    Config cfg = On();
    cfg.TimeoutSec = 100;
    cfg.ReleaseSec = 30;

    EntryFacts f;
    f.Missing = 2;
    f.AgeSec = 10;
    CHECK(Evaluate(f, cfg).What == Verdict::Keep);

    f.AgeSec = 100;
    Outcome o = Evaluate(f, cfg);
    CHECK(o.What == Verdict::Expire);
    CHECK(std::string(o.Why) == "timeout");

    f.AgeSec = 10;
    f.Missing = 0;
    o = Evaluate(f, cfg);
    CHECK(o.What == Verdict::Complete);
    CHECK(std::string(o.Why) == "full");

    f.Searching = false;   // finished searches only watch the player
    CHECK(Evaluate(f, cfg).What == Verdict::Keep);
    f.AgeSec = 100000;
    CHECK(Evaluate(f, cfg).What == Verdict::Keep);

    f.PlayerOnline = false;
    f.GoneSec = 29;
    CHECK(Evaluate(f, cfg).What == Verdict::Keep);
    f.GoneSec = 30;
    o = Evaluate(f, cfg);
    CHECK(o.What == Verdict::Release);
    CHECK(std::string(o.Why) == "offline");

    f.PlayerOnline = true;
    f.PlayerMayLead = false;
    o = Evaluate(f, cfg);
    CHECK(o.What == Verdict::Release);
    CHECK(std::string(o.Why) == "not_leader");

    EntryFacts s;   // a searching entry whose player joined another group
    s.PlayerMayLead = false;
    CHECK(Evaluate(s, cfg).What == Verdict::Release);
}

TEST_CASE("BotLfg: texts", "[BotLfg]")
{
    CHECK(DescribeMissing(0, 0, 0).empty());
    CHECK(DescribeMissing(1, 0, 0) == "1 tank");
    CHECK(DescribeMissing(0, 1, 0) == "1 healer");
    CHECK(DescribeMissing(0, 0, 3) == "3 damage dealers");
    CHECK(DescribeMissing(1, 1, 0) == "1 tank and 1 healer");
    CHECK(DescribeMissing(1, 1, 2) == "1 tank, 1 healer and 2 damage dealers");
    CHECK(std::string(RoleText(Role::Healer)) == "healer");
}
