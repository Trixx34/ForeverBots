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

#include "BotChat.h"
#include "SharedDefines.h"
#include <cmath>
#include <limits>

using namespace BotChat;

namespace
{
constexpr uint32 Bit(uint8 classId) { return 1u << classId; }
RoleMasks Roles()
{
    return { Bit(CLASS_WARRIOR), Bit(CLASS_PRIEST), Bit(CLASS_ROGUE) | Bit(CLASS_MAGE) | Bit(CLASS_WARLOCK) | Bit(CLASS_HUNTER) };
}
}

TEST_CASE("BotChat selector: subgroups", "[BotChat]")
{
    Selector s;
    REQUIRE(ParseSelector("g1", Roles(), s));
    CHECK(s.Matches(0, CLASS_MAGE));
    CHECK_FALSE(s.Matches(1, CLASS_MAGE));

    REQUIRE(ParseSelector("g3-g4", Roles(), s));
    CHECK_FALSE(s.Matches(1, CLASS_MAGE));
    CHECK(s.Matches(2, CLASS_MAGE));
    CHECK(s.Matches(3, CLASS_MAGE));
    CHECK_FALSE(s.Matches(4, CLASS_MAGE));

    REQUIRE(ParseSelector("G4-2", Roles(), s));   // reversed range, second 'g' optional
    CHECK(s.Matches(1, CLASS_MAGE));
    CHECK(s.Matches(3, CLASS_MAGE));

    REQUIRE(ParseSelector("g1,g3", Roles(), s));
    CHECK(s.Matches(0, CLASS_MAGE));
    CHECK_FALSE(s.Matches(1, CLASS_MAGE));
    CHECK(s.Matches(2, CLASS_MAGE));
}

TEST_CASE("BotChat selector: roles and classes", "[BotChat]")
{
    Selector s;
    REQUIRE(ParseSelector("tank", Roles(), s));
    CHECK(s.Matches(0, CLASS_WARRIOR));
    CHECK_FALSE(s.Matches(0, CLASS_PRIEST));

    REQUIRE(ParseSelector("dps", Roles(), s));
    CHECK(s.Matches(5, CLASS_ROGUE));
    CHECK_FALSE(s.Matches(5, CLASS_WARRIOR));

    REQUIRE(ParseSelector("tank,dps", Roles(), s));   // union among roles
    CHECK(s.Matches(0, CLASS_WARRIOR));
    CHECK(s.Matches(0, CLASS_MAGE));
    CHECK_FALSE(s.Matches(0, CLASS_PRIEST));

    REQUIRE(ParseSelector("Druid", Roles(), s));
    CHECK(s.Matches(0, CLASS_DRUID));
    CHECK_FALSE(s.Matches(0, CLASS_SHAMAN));

    REQUIRE(ParseSelector("all", Roles(), s));
    CHECK(s.Matches(7, CLASS_PALADIN));
}

TEST_CASE("BotChat selector: plural forms from the design", "[BotChat]")
{
    Selector s;
    REQUIRE(ParseSelector("healers", Roles(), s));
    CHECK(s.Matches(0, CLASS_PRIEST));
    REQUIRE(ParseSelector("warriors", Roles(), s));
    CHECK(s.Matches(0, CLASS_WARRIOR));
    REQUIRE(ParseSelector("tanks", Roles(), s));
    CHECK(s.Matches(0, CLASS_WARRIOR));
    REQUIRE(ParseSelector("dps", Roles(), s));       // already ends in s, must not need stripping
    CHECK(s.Matches(0, CLASS_MAGE));
    CHECK_FALSE(ParseSelector("warriorss", Roles(), s));
}

TEST_CASE("BotChat selector: subgroup and role intersect", "[BotChat]")
{
    Selector s;
    REQUIRE(ParseSelector("g1,tank", Roles(), s));
    CHECK(s.Matches(0, CLASS_WARRIOR));
    CHECK_FALSE(s.Matches(0, CLASS_MAGE));        // right subgroup, wrong role
    CHECK_FALSE(s.Matches(1, CLASS_WARRIOR));     // right role, wrong subgroup

    // a role whose class mask is empty must match nobody, not "everybody in the subgroup"
    RoleMasks noTank = Roles();
    noTank.Tank = 0;
    REQUIRE(ParseSelector("g1,tank", noTank, s));
    CHECK_FALSE(s.Matches(0, CLASS_WARRIOR));
    CHECK_FALSE(s.Matches(0, CLASS_MAGE));
    REQUIRE(ParseSelector("tank", noTank, s));
    CHECK_FALSE(s.Matches(0, CLASS_WARRIOR));
}

TEST_CASE("BotChat selector: rejects non selectors", "[BotChat]")
{
    Selector s;
    CHECK_FALSE(ParseSelector("", Roles(), s));
    CHECK_FALSE(ParseSelector("follow", Roles(), s));
    CHECK_FALSE(ParseSelector("status", Roles(), s));
    CHECK_FALSE(ParseSelector("g0", Roles(), s));
    CHECK_FALSE(ParseSelector("g9", Roles(), s));
    CHECK_FALSE(ParseSelector("g1-", Roles(), s));
    CHECK_FALSE(ParseSelector("g1-g", Roles(), s));
    CHECK_FALSE(ParseSelector("g1x", Roles(), s));
    CHECK_FALSE(ParseSelector("g1,", Roles(), s));
    CHECK_FALSE(ParseSelector(",g1", Roles(), s));
    CHECK_FALSE(ParseSelector("g1,bogus", Roles(), s));
    CHECK_FALSE(ParseSelector("hello", Roles(), s));
}

TEST_CASE("BotChat goto arguments", "[BotChat]")
{
    GotoArgs g;
    CHECK(ParseGotoArgs("here", g) == nullptr);
    CHECK(g.Here);

    CHECK(ParseGotoArgs("  HERE ", g) == nullptr);
    CHECK(g.Here);

    CHECK(ParseGotoArgs("-8913.5 -134.2", g) == nullptr);
    CHECK_FALSE(g.Here);
    CHECK_FALSE(g.HasZ);
    CHECK(g.X == -8913.5f);
    CHECK(g.Y == -134.2f);

    CHECK(ParseGotoArgs("10 20 30.5", g) == nullptr);
    CHECK(g.HasZ);
    CHECK(g.Z == 30.5f);

    CHECK(ParseGotoArgs("", g) != nullptr);
    CHECK(ParseGotoArgs("10", g) != nullptr);
    CHECK(ParseGotoArgs("10 20 30 40", g) != nullptr);
    CHECK(ParseGotoArgs("a b", g) != nullptr);
    CHECK(ParseGotoArgs("10 20 z", g) != nullptr);
    CHECK(ParseGotoArgs("nan 20", g) != nullptr);
    CHECK(ParseGotoArgs("10 inf", g) != nullptr);
    CHECK(ParseGotoArgs("10 20 nan", g) != nullptr);
    CHECK(ParseGotoArgs("1e30 20", g) != nullptr);
    CHECK(ParseGotoArgs("10 20 1e9", g) != nullptr);
}
