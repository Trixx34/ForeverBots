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

#include "BotControl.h"
#include <limits>
#include <string>

using namespace BotControl;

TEST_CASE("BotControl role parsing", "[BotControl]")
{
    Role r = Role::Auto;
    REQUIRE(ParseRole("Tank", r));
    CHECK(r == Role::Tank);
    REQUIRE(ParseRole(" healer ", r));
    CHECK(r == Role::Healer);
    REQUIRE(ParseRole("dd", r));
    CHECK(r == Role::Dps);
    REQUIRE(ParseRole("auto", r));
    CHECK(r == Role::Auto);
    r = Role::Tank;
    CHECK_FALSE(ParseRole("", r));
    CHECK_FALSE(ParseRole("tank now", r));
    CHECK_FALSE(ParseRole("bard", r));
    CHECK(r == Role::Tank);   // unchanged on failure
}

TEST_CASE("BotControl stance parsing", "[BotControl]")
{
    Stance s = Stance::Aggressive;
    REQUIRE(ParseStance("DEFENSIVE", s));
    CHECK(s == Stance::Defensive);
    REQUIRE(ParseStance("passive", s));
    CHECK(s == Stance::Passive);
    REQUIRE(ParseStance("aggressive", s));
    CHECK(s == Stance::Aggressive);
    CHECK_FALSE(ParseStance("", s));
    CHECK_FALSE(ParseStance("angry", s));
    CHECK(std::string(StanceName(Stance::Defensive)) == "defensive");
    CHECK(std::string(RoleName(Role::Dps)) == "dps");
}

TEST_CASE("BotControl follow distance parsing", "[BotControl]")
{
    DistanceArgs d;
    REQUIRE(ParseDistance("15", 3.0f, 40.0f, d) == nullptr);
    CHECK_FALSE(d.Reset);
    CHECK(d.Yards == 15.0f);
    REQUIRE(ParseDistance("7.5y", 3.0f, 40.0f, d) == nullptr);
    CHECK(d.Yards == 7.5f);
    REQUIRE(ParseDistance("3", 3.0f, 40.0f, d) == nullptr);   // bounds are inclusive
    REQUIRE(ParseDistance("40", 3.0f, 40.0f, d) == nullptr);
    REQUIRE(ParseDistance("Default", 3.0f, 40.0f, d) == nullptr);
    CHECK(d.Reset);

    d = DistanceArgs();
    CHECK(ParseDistance("", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("2.9", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("41", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("-5", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("abc", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("10 20", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("nan", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("inf", 3.0f, 40.0f, d) != nullptr);
    CHECK(ParseDistance("1e99", 3.0f, 40.0f, d) != nullptr);
    CHECK_FALSE(d.Reset);
    CHECK(d.Yards == 0.0f);
}

TEST_CASE("BotControl follow thresholds", "[BotControl]")
{
    float start = 0, arrive = 0;
    FollowThresholds(0.0f, start, arrive);
    CHECK(start == 10.0f);
    CHECK(arrive == 5.0f);
    FollowThresholds(20.0f, start, arrive);
    CHECK(start == 20.0f);
    CHECK(arrive == 10.0f);
    FollowThresholds(3.0f, start, arrive);
    CHECK(start == 3.0f);
    CHECK(arrive == 2.0f);   // never below 2 yards
    FollowThresholds(-4.0f, start, arrive);   // garbage falls back to the defaults
    CHECK(start == 10.0f);
}

TEST_CASE("BotControl stance gate", "[BotControl]")
{
    FoeFacts onSelf{ true, false }, onGroup{ false, true }, onNobody{ false, false };
    CHECK(StanceAllows(Stance::Aggressive, onNobody));
    CHECK(StanceAllows(Stance::Aggressive, onSelf));

    CHECK(StanceAllows(Stance::Defensive, onSelf));
    CHECK(StanceAllows(Stance::Defensive, onGroup));
    CHECK_FALSE(StanceAllows(Stance::Defensive, onNobody));

    CHECK(StanceAllows(Stance::Passive, onSelf));
    CHECK_FALSE(StanceAllows(Stance::Passive, onGroup));
    CHECK_FALSE(StanceAllows(Stance::Passive, onNobody));
}

TEST_CASE("BotControl tank priority", "[BotControl]")
{
    CHECK(TankPriority(Role::Tank, false) == 0);
    CHECK(TankPriority(Role::Tank, true) == 0);
    CHECK(TankPriority(Role::Auto, true) == 1);
    CHECK(TankPriority(Role::Auto, false) == -1);
    CHECK(TankPriority(Role::Dps, true) == -1);      // a warrior told to do damage does not tank
    CHECK(TankPriority(Role::Healer, true) == -1);
    CHECK(TankPriority(Role::Tank, false) < TankPriority(Role::Auto, true));
}

TEST_CASE("BotControl activity phrases", "[BotControl]")
{
    ReportFacts f;
    f.Name = "Thrall";
    CHECK(DescribeDoing(f) == "idle");

    f.Following = true;
    CHECK(DescribeDoing(f) == "following you");
    f.Mounted = true;
    CHECK(DescribeDoing(f) == "following you, mounted");
    f.Mounted = false;

    f.Staying = true;
    f.Following = false;
    CHECK(DescribeDoing(f) == "holding position");

    f.HasGoal = true;
    f.GoalTag = "goto";
    CHECK(DescribeDoing(f) == "heading to a spot you sent me to");
    f.GoalTag = "quest";
    CHECK(DescribeDoing(f) == "working on a quest");
    f.GoalTag = "grind";
    CHECK(DescribeDoing(f) == "travelling (grind)");

    f.GoalTag = "follow";
    f.Staying = false;
    CHECK(DescribeDoing(f) == "following you");   // a follow leg is following, not travelling

    f.Resting = true;
    CHECK(DescribeDoing(f) == "resting");
    f.InCombat = true;
    CHECK(DescribeDoing(f) == "fighting");
    f.FightTarget = "Defias Thug";
    CHECK(DescribeDoing(f) == "fighting Defias Thug");

    f.Alive = false;
    CHECK(DescribeDoing(f) == "dead");
    f.Ghost = true;
    CHECK(DescribeDoing(f).find("ghost") != std::string::npos);
}

TEST_CASE("BotControl report line", "[BotControl]")
{
    ReportFacts f;
    f.Name = "Thrall";
    f.Following = true;
    f.HealthPct = 80;
    CHECK(DescribeReport(f) == "Thrall: following you; hp 80%");   // defaults are not listed

    f.UsesMana = true;
    f.ManaPct = 40;
    f.RoleSet = Role::Tank;
    f.StanceSet = Stance::Defensive;
    f.FollowYards = 15.0f;
    f.Focus = "Defias Thug";
    f.InCombat = true;
    f.FightTarget = "Defias Thug";
    CHECK(DescribeReport(f) == "Thrall: fighting Defias Thug; role tank, stance defensive, follow 15y, focus Defias Thug; hp 80%, mana 40%");

    f.Alive = false;
    CHECK(DescribeReport(f) == "Thrall: dead; role tank, stance defensive, follow 15y, focus Defias Thug");   // no vitals for the dead
}

TEST_CASE("BotControl status question", "[BotControl]")
{
    CHECK(IsStatusQuestion("what are you doing"));
    CHECK(IsStatusQuestion("What are you doing?"));
    CHECK(IsStatusQuestion("  what   are you  doing ?! "));
    CHECK(IsStatusQuestion("what are you up to"));
    CHECK(IsStatusQuestion("what's up?"));
    CHECK(IsStatusQuestion("REPORT"));
    CHECK_FALSE(IsStatusQuestion(""));
    CHECK_FALSE(IsStatusQuestion("?"));
    CHECK_FALSE(IsStatusQuestion("what are you doing tonight"));
    CHECK_FALSE(IsStatusQuestion("report to the tank"));
    CHECK_FALSE(IsStatusQuestion(std::string(200, 'a')));
}
