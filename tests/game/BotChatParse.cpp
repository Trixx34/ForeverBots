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
#include <string>
#include <vector>

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

TEST_CASE("BotChat verbs", "[BotChat]")
{
    CHECK(ParseVerb("follow", false) == Verb::Follow);
    CHECK(ParseVerb("FOLLOW", true) == Verb::Follow);
    CHECK(ParseVerb("release", false) == Verb::Release);
    CHECK(ParseVerb("hello", true) == Verb::None);
    CHECK(ParseVerb("", true) == Verb::None);

    // the order verbs exist only with Bot.Chat.Orders.Enabled
    for (char const* word : { "stop", "aggressive", "passive", "pull", "heal", "mount", "dismount", "summon", "revive" })
    {
        INFO(word);
        CHECK(ParseVerb(word, false) == Verb::None);
        CHECK(ParseVerb(word, true) != Verb::None);
    }
    CHECK(ParseVerb("Pull", true) == Verb::Pull);
    CHECK(ParseVerb("dismount", true) == Verb::Dismount);
    CHECK(ParseVerb("mount", true) == Verb::Mount);
    CHECK(ParseVerb("pulling", true) == Verb::None);
    CHECK(ParseVerb("aggressiveness", true) == Verb::None);
}

TEST_CASE("BotChat order arguments", "[BotChat]")
{
    for (Verb v : { Verb::Stop, Verb::Aggressive, Verb::Passive, Verb::Pull, Verb::Heal, Verb::Mount, Verb::Dismount, Verb::Summon, Verb::Revive })
    {
        CHECK(ValidateOrderArgs(v, "") == nullptr);
        CHECK(ValidateOrderArgs(v, "   ") == nullptr);
        CHECK(ValidateOrderArgs(v, "now") != nullptr);
    }
    CHECK(ValidateOrderArgs(Verb::Follow, "off") == nullptr);   // the old verbs validate their own arguments
}

TEST_CASE("BotChat order role gate", "[BotChat]")
{
    RoleMasks const roles = Roles();   // tank = warrior, healer = priest
    CHECK(RoleGate(Verb::Pull, CLASS_WARRIOR, roles) == nullptr);
    CHECK(std::string(RoleGate(Verb::Pull, CLASS_MAGE, roles)) == "NOT_TANK");
    CHECK(std::string(RoleGate(Verb::Pull, CLASS_PRIEST, roles)) == "NOT_TANK");
    CHECK(RoleGate(Verb::Heal, CLASS_PRIEST, roles) == nullptr);
    CHECK(std::string(RoleGate(Verb::Heal, CLASS_WARRIOR, roles)) == "NOT_HEALER");
    CHECK(std::string(RoleGate(Verb::Heal, 40, roles)) == "NOT_HEALER");   // class id out of mask range
    for (Verb v : { Verb::Stop, Verb::Aggressive, Verb::Passive, Verb::Mount, Verb::Dismount, Verb::Summon, Verb::Revive, Verb::Follow })
        CHECK(RoleGate(v, CLASS_MAGE, roles) == nullptr);

    RoleMasks custom = roles;
    custom.Tank |= Bit(CLASS_PALADIN);
    custom.Healer |= Bit(CLASS_PALADIN) | Bit(CLASS_DRUID);
    CHECK(RoleGate(Verb::Pull, CLASS_PALADIN, custom) == nullptr);
    CHECK(RoleGate(Verb::Heal, CLASS_DRUID, custom) == nullptr);
    CHECK(std::string(RoleGate(Verb::Pull, CLASS_DRUID, custom)) == "NOT_TANK");
}

TEST_CASE("BotChat spread offsets", "[BotChat]")
{
    float dx = 1, dy = 1;
    SpreadOffset(0, dx, dy);
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);

    // every bot stands at least 1.5 yards from the issuer and no two bots share a spot
    float px[12], py[12];
    for (uint32 i = 0; i < 12; ++i)
    {
        SpreadOffset(i, px[i], py[i]);
        if (i)
            CHECK(std::hypot(px[i], py[i]) >= 1.5f);
        for (uint32 j = 0; j < i; ++j)
            CHECK(std::hypot(px[i] - px[j], py[i] - py[j]) > 0.5f);
    }
}

TEST_CASE("BotChat summon and revive rules", "[BotChat]")
{
    auto code = [](SummonFacts const& f, bool revive) { char const* c = SummonCheck(f, revive); return std::string(c ? c : "OK"); };
    SummonFacts f;
    f.Distance = 30.0f;
    CHECK(code(f, false) == "OK");

    SummonFacts near = f;
    near.Distance = 2.0f;
    CHECK(code(near, false) == "ALREADY_HERE");
    CHECK(code(near, true) == "NOT_DEAD");

    SummonFacts dead = f;
    dead.BotAlive = false;
    CHECK(code(dead, false) == "DEAD");
    CHECK(code(dead, true) == "OK");
    CHECK(code(f, true) == "NOT_DEAD");

    SummonFacts issuerFighting = f;
    issuerFighting.IssuerInCombat = true;
    CHECK(code(issuerFighting, false) == "ISSUER_IN_COMBAT");

    SummonFacts issuerDead = f;
    issuerDead.IssuerAlive = false;
    CHECK(code(issuerDead, false) == "ISSUER_DEAD");
    CHECK(code(issuerDead, true) == "NOT_DEAD");

    SummonFacts botFighting = f;
    botFighting.BotInCombat = true;
    CHECK(code(botFighting, false) == "IN_COMBAT");

    // another map is fine, unless an instance is involved
    SummonFacts otherMap = f;
    otherMap.SameMap = false;
    CHECK(code(otherMap, false) == "OK");
    otherMap.InstancedMapInvolved = true;
    CHECK(code(otherMap, false) == "INSTANCE");
    dead.SameMap = false;
    dead.InstancedMapInvolved = true;
    CHECK(code(dead, true) == "INSTANCE");

    // inside one instance a bot may be summoned
    SummonFacts sameInstance = f;
    sameInstance.InstancedMapInvolved = true;
    CHECK(code(sameInstance, false) == "OK");
}

TEST_CASE("BotChat mount choice", "[BotChat]")
{
    CHECK(PickMount({}) == -1);

    std::vector<MountOption> options = { { 100, 60, false }, { 90, 100, false }, { 80, 280, true }, { 70, 100, false }, { 0, 400, false } };
    int32 const pick = PickMount(options);
    REQUIRE(pick >= 0);
    CHECK(options[size_t(pick)].SpellId == 70);   // fastest ground mount; the flier and the id 0 entry are skipped, lower id wins the tie

    std::vector<MountOption> flyers = { { 1, 280, true } };
    CHECK(PickMount(flyers) == -1);

    std::vector<MountOption> unknown = { { 5, 0, false }, { 4, 0, false } };
    CHECK(unknown[size_t(PickMount(unknown))].SpellId == 4);
}
