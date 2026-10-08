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
    for (uint64 seed = 0; seed < 12; ++seed)
    {
        std::vector<uint32> plan = Plan(seed);
        REQUIRE(plan.size() == 5);
        CHECK(plan[4] == SKILL_FISHING);
        CHECK(plan[0] == SKILL_FIRST_AID);
        CHECK(plan[1] == SKILL_COOKING);
        CHECK(Find(plan[2])->Gathering);
        CHECK_FALSE(Find(plan[3])->Gathering);
    }
}

TEST_CASE("BotProfession plan is deterministic and uses all three pairs", "[BotProfession]")
{
    CHECK(Plan(7) == Plan(7));
    CHECK(Plan(0) != Plan(1));
    CHECK(Plan(1) != Plan(2));
    CHECK(Plan(2) != Plan(3));
    CHECK(Plan(0) == Plan(4));
}

TEST_CASE("BotProfession next skill respects level and known skills", "[BotProfession]")
{
    std::vector<uint32> plan = Plan(0);
    CHECK(NextToLearn(plan, {}, 4) == 0);                              // too low to spend copper
    CHECK(NextToLearn(plan, {}, 5) == SKILL_FIRST_AID);
    CHECK(NextToLearn(plan, { SKILL_FIRST_AID }, 5) == SKILL_COOKING);
    CHECK(NextToLearn(plan, { SKILL_FIRST_AID, SKILL_COOKING, SKILL_MINING }, 20) == SKILL_BLACKSMITHING);
    CHECK(NextToLearn(plan, { SKILL_FIRST_AID, SKILL_COOKING, SKILL_MINING }, 7) == SKILL_FISHING);
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

TEST_CASE("BotProfession skinning requirement follows the core formula", "[BotProfession]")
{
    CHECK(SkinReqSkill(1) == 1);
    CHECK(SkinReqSkill(10) == 1);
    CHECK(SkinReqSkill(11) == 10);
    CHECK(SkinReqSkill(19) == 90);
    CHECK(SkinReqSkill(20) == 100);
    CHECK(SkinReqSkill(60) == 300);
}

TEST_CASE("BotProfession recipe pick prefers certain skill-ups and skips grey", "[BotProfession]")
{
    std::vector<Recipe> r = { { 1, 40, 60 }, { 2, 100, 130 }, { 3, 20, 30 } };
    CHECK(PickRecipe(1, r) == 2);     // all certain, lowest grey threshold wins
    CHECK(PickRecipe(35, r) == 0);    // recipe 3 is grey, recipe 1 is chance-based, recipe 2 still certain
    CHECK(PickRecipe(45, r) == 1);    // recipe 2 certain beats recipe 1 chance
    CHECK(PickRecipe(130, r) == -1);  // everything grey
    CHECK(PickRecipe(5, {}) == -1);
}

namespace
{
struct Lake { float X0, X1, Y0, Y1; };   // axis aligned pond, everything else is land
bool InLake(void* c, float x, float y) { Lake const& l = *static_cast<Lake*>(c); return x >= l.X0 && x <= l.X1 && y >= l.Y0 && y <= l.Y1; }
bool OnLand(void* c, float x, float y) { return !InLake(c, x, y); }
bool Knows(void* c, uint32 spell) { return *static_cast<uint32*>(c) == spell; }
}

TEST_CASE("BotProfession finds a shore next to water", "[BotProfession]")
{
    Lake lake{ 10.0f, 30.0f, -10.0f, 10.0f };
    Shore s;
    CHECK(FindShore(0.0f, 0.0f, InLake, OnLand, &lake, s));
    CHECK(OnLand(&lake, s.StandX, s.StandY));
    CHECK(InLake(&lake, s.WaterX, s.WaterY));
    Lake far{ 500.0f, 520.0f, 500.0f, 520.0f };
    CHECK_FALSE(FindShore(0.0f, 0.0f, InLake, OnLand, &far, s));
}

TEST_CASE("BotProfession picks the best known fishing rank", "[BotProfession]")
{
    uint32 known = 7731;
    CHECK(FishingSpell(Knows, &known) == 7731);
    known = 18248;
    CHECK(FishingSpell(Knows, &known) == 18248);
    known = 1;
    CHECK(FishingSpell(Knows, &known) == 0);
}
