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

#include "BotProfession.h"
#include <algorithm>

using namespace BotProfession;

TEST_CASE("BotProfession plan covers first aid, cooking and two distinct gathering skills", "[BotProfession]")
{
    for (uint64 seed = 0; seed < 9; ++seed)
    {
        std::vector<uint32> plan = Plan(seed);
        REQUIRE(plan.size() == 4);
        CHECK(plan[0] == SKILL_FIRST_AID);
        CHECK(plan[1] == SKILL_COOKING);
        CHECK(plan[2] != plan[3]);
        CHECK(Find(plan[2])->Gathering);
        CHECK(Find(plan[3])->Gathering);
    }
}

TEST_CASE("BotProfession plan is deterministic and uses all three pairs", "[BotProfession]")
{
    CHECK(Plan(7) == Plan(7));
    CHECK(Plan(0) != Plan(1));
    CHECK(Plan(1) != Plan(2));
    CHECK(Plan(0) == Plan(3));
}

TEST_CASE("BotProfession next skill respects level and known skills", "[BotProfession]")
{
    std::vector<uint32> plan = Plan(0);
    CHECK(NextToLearn(plan, {}, 4) == 0);                              // too low to spend copper
    CHECK(NextToLearn(plan, {}, 5) == SKILL_FIRST_AID);
    CHECK(NextToLearn(plan, { SKILL_FIRST_AID }, 5) == SKILL_COOKING);
    CHECK(NextToLearn(plan, { SKILL_FIRST_AID, SKILL_COOKING, SKILL_MINING }, 20) == SKILL_SKINNING);
    CHECK(NextToLearn(plan, plan, 60) == 0);
    CHECK(Find(9999) == nullptr);
}

TEST_CASE("BotProfession node range", "[BotProfession]")
{
    CHECK(NodeWorthIt(1, 1));
    CHECK(NodeWorthIt(1, 26));
    CHECK_FALSE(NodeWorthIt(1, 27));
    CHECK(NodeWorthIt(100, 50));
}
