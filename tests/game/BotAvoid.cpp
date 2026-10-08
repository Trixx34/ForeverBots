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

#include "BotAvoid.h"

TEST_CASE("BotAvoid::ParseEntryList", "[bots]")
{
    CHECK(BotAvoid::ParseEntryList("116, 822;250927 79") == std::vector<uint32_t>{ 116, 822, 250927, 79 });
    CHECK(BotAvoid::ParseEntryList("").empty());
    CHECK(BotAvoid::ParseEntryList("abc,0, 12x ,5,5") == std::vector<uint32_t>{ 5 });
}

TEST_CASE("BotAvoid::ShouldAvoid", "[bots]")
{
    std::vector<uint32_t> const list{ 116, 79 };
    CHECK(BotAvoid::ShouldAvoid(list, 116, 5, 3, 1));      // +2 on the list
    CHECK_FALSE(BotAvoid::ShouldAvoid(list, 116, 4, 3, 1)); // +1 is within one level
    CHECK_FALSE(BotAvoid::ShouldAvoid(list, 117, 9, 3, 1)); // not on the list
    CHECK_FALSE(BotAvoid::ShouldAvoid({}, 116, 9, 3, 1));
    CHECK_FALSE(BotAvoid::ShouldAvoid(list, 116, 9, 3, -1)); // negative gap = feature off
}

TEST_CASE("BotAvoid::FleeModeFor", "[bots]")
{
    CHECK(BotAvoid::FleeModeFor(12345, 0, 1, 0) == 0);
    CHECK(BotAvoid::FleeModeFor(1, 0, 1, 50) == 1);
    CHECK(BotAvoid::FleeModeFor(51, 0, 1, 50) == 0);
    CHECK(BotAvoid::FleeModeFor(199, 0, 1, 100) == 1);
    int armed = 0;
    for (uint64_t g = 1; g <= 1000; ++g)
        armed += BotAvoid::FleeModeFor(g, 0, 1, 30);
    CHECK(armed == 300);
}
