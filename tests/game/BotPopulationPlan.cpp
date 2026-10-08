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

#include "BotPopulationPlan.h"

using namespace BotPopulation;

TEST_CASE("population: target follows players and clamps", "[BotPopulation]")
{
    Config c;
    c.Base = 50;
    c.PerPlayerTenths = 5;      // one bot per two players
    c.Min = 20;
    c.Max = 80;
    CHECK(Target(c, 0, 12) == 50);
    CHECK(Target(c, 20, 12) == 60);
    CHECK(Target(c, 1000, 12) == 80);
    c.Base = 5;
    CHECK(Target(c, 0, 12) == 20);
}

TEST_CASE("population: the hour curve scales the target", "[BotPopulation]")
{
    Config c;
    c.Base = 100;
    c.Max = 200;
    c.HourPct[3] = 40;
    c.HourPct[20] = 150;
    CHECK(Target(c, 0, 3) == 40);
    CHECK(Target(c, 0, 20) == 150);
    CHECK(Target(c, 0, 99) == 100);   // out-of-range hours read hour 23
}

TEST_CASE("population: min above max is treated as max", "[BotPopulation]")
{
    Config c;
    c.Base = 10;
    c.Min = 90;
    c.Max = 30;
    CHECK(Target(c, 0, 0) == 30);
}

TEST_CASE("population: steps are bounded and ignore small gaps", "[BotPopulation]")
{
    Config c;
    c.StepUp = 5;
    c.StepDown = 3;
    c.Hysteresis = 2;
    CHECK(Step(c, 50, 50) == 0);
    CHECK(Step(c, 48, 50) == 0);
    CHECK(Step(c, 47, 50) == 3);
    CHECK(Step(c, 0, 50) == 5);
    CHECK(Step(c, 52, 50) == 0);
    CHECK(Step(c, 53, 50) == -3);
    CHECK(Step(c, 100, 50) == -3);
}

TEST_CASE("population: hour list parsing", "[BotPopulation]")
{
    std::array<uint16, 24> h{};
    h.fill(100);
    CHECK(ParseHours("50, 60,70", h));
    CHECK(h[0] == 50);
    CHECK(h[1] == 60);
    CHECK(h[2] == 70);
    CHECK(h[23] == 70);
    CHECK(ParseHours("999", h));
    CHECK(h[0] == 300);
    std::array<uint16, 24> before = h;
    CHECK_FALSE(ParseHours("", h));
    CHECK_FALSE(ParseHours("abc", h));
    CHECK_FALSE(ParseHours("10,x", h));
    CHECK(h == before);
}
