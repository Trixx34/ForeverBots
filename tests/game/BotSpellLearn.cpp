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

#include "BotSpellLearn.h"

using namespace BotSpellLearn;

static Candidate Cand(uint32 id, uint32 req, bool avail = true, bool valid = true)
{
    Candidate c;
    c.SpellId = id;
    c.ReqLevel = req;
    c.Available = avail;
    c.InfoValid = valid;
    return c;
}

TEST_CASE("BotSpellLearn Select orders by required level then spell id", "[BotSpellLearn]")
{
    std::vector<uint32> const out = Select({ Cand(30, 10), Cand(20, 4), Cand(10, 4), Cand(5, 1) }, 10);
    REQUIRE(out.size() == 4);
    CHECK(out == std::vector<uint32>{ 5, 10, 20, 30 });
}

TEST_CASE("BotSpellLearn Select drops spells above the bot level, unavailable and invalid ones", "[BotSpellLearn]")
{
    std::vector<uint32> const out = Select({ Cand(1, 5), Cand(2, 6), Cand(3, 1, false), Cand(4, 1, true, false), Cand(0, 1) }, 5);
    CHECK(out == std::vector<uint32>{ 1 });
}

TEST_CASE("BotSpellLearn Select lists a spell offered by several trainers once", "[BotSpellLearn]")
{
    std::vector<uint32> const out = Select({ Cand(7, 2), Cand(7, 2), Cand(8, 2), Cand(7, 2) }, 20);
    CHECK(out == std::vector<uint32>{ 7, 8 });
}

TEST_CASE("BotSpellLearn Select of nothing is empty", "[BotSpellLearn]")
{
    CHECK(Select({}, 60).empty());
}

TEST_CASE("BotSpellLearn Due fires on login and on each new level only when enabled", "[BotSpellLearn]")
{
    CHECK(Due(true, 12, 0));      // login catch-up
    CHECK(Due(true, 13, 12));     // level up
    CHECK_FALSE(Due(true, 12, 12));
    CHECK_FALSE(Due(false, 13, 12));
    CHECK_FALSE(Due(true, 0, 0));
}
