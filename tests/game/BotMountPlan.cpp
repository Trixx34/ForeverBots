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

#include "BotMountPlan.h"

#include <limits>
#include <string>
#include <vector>

using namespace BotMount;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    return c;
}

// A bot on foot that follows a leader who has been mounted for a while.
Facts LeaderRides()
{
    Facts f;
    f.Follows = true;
    f.HasMount = true;
    f.LeaderMounted = true;
    f.LeaderMountedMs = 5000;
    f.Distance = 30.0f;
    return f;
}

// A mounted bot behind a leader who has been on foot for a while.
Facts LeaderWalks()
{
    Facts f;
    f.Follows = true;
    f.BotMounted = true;
    f.LeaderMounted = false;
    f.LeaderOnFootMs = 5000;
    f.Distance = 8.0f;
    return f;
}
}

TEST_CASE("mount: a follower mounts after its leader", "[bot][mount]")
{
    Config const cfg = On();
    Decision d = Decide(LeaderRides(), cfg);
    CHECK(d.What == Act::Mount);
    CHECK(std::string(d.Why) == "leader_mounted");

    Facts brief = LeaderRides();
    brief.LeaderMountedMs = cfg.MountDelayMs - 1;
    CHECK(Decide(brief, cfg).What == Act::None);
    brief.LeaderMountedMs = cfg.MountDelayMs;
    CHECK(Decide(brief, cfg).What == Act::Mount);
}

TEST_CASE("mount: no mount without a reason or a way", "[bot][mount]")
{
    Config const cfg = On();
    Facts f = LeaderRides();
    f.Follows = false;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.HasMount = false;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.CanMountHere = false;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.BotAlive = false;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.BotBusy = true;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.BotInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);

    f = LeaderRides();
    f.LeaderInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);

    // a leader on foot and close: nothing to catch up
    f = LeaderRides();
    f.LeaderMounted = false;
    f.LeaderMountedMs = 0;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("mount: attempts are spaced and a refusal is remembered", "[bot][mount]")
{
    Config const cfg = On();
    Facts f = LeaderRides();
    f.SinceActMs = cfg.ActCooldownMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.SinceActMs = cfg.ActCooldownMs;
    CHECK(Decide(f, cfg).What == Act::Mount);

    f.SinceFailMs = cfg.FailBackoffMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.SinceFailMs = cfg.FailBackoffMs;
    CHECK(Decide(f, cfg).What == Act::Mount);
}

TEST_CASE("mount: a bot left far behind a leader on foot rides to catch up", "[bot][mount]")
{
    Config const cfg = On();
    Facts f;
    f.Follows = true;
    f.HasMount = true;
    f.Distance = cfg.CatchUpYards + 10.0f;
    f.FarForMs = cfg.CatchUpMs;
    Decision d = Decide(f, cfg);
    CHECK(d.What == Act::Mount);
    CHECK(std::string(d.Why) == "catch_up");

    f.FarForMs = cfg.CatchUpMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);

    f.FarForMs = cfg.CatchUpMs;
    f.Distance = cfg.CatchUpYards;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("mount: a follower dismounts when the leader is on foot and close", "[bot][mount]")
{
    Config const cfg = On();
    Facts f = LeaderWalks();
    Decision d = Decide(f, cfg);
    CHECK(d.What == Act::Dismount);
    CHECK(std::string(d.Why) == "leader_on_foot");

    f.LeaderOnFootMs = cfg.DismountDelayMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);

    // far from the leader the bot keeps riding
    f = LeaderWalks();
    f.Distance = cfg.DismountYards + 1.0f;
    CHECK(Decide(f, cfg).What == Act::None);
    f.Distance = cfg.DismountYards;
    CHECK(Decide(f, cfg).What == Act::Dismount);

    // the leader still rides
    f = LeaderWalks();
    f.LeaderMounted = true;
    f.LeaderOnFootMs = 0;
    f.LeaderMountedMs = 9000;
    CHECK(Decide(f, cfg).What == Act::None);

    // busy casting or too soon after the last attempt
    f = LeaderWalks();
    f.BotBusy = true;
    CHECK(Decide(f, cfg).What == Act::None);
    f = LeaderWalks();
    f.SinceActMs = 0;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("mount: a fight ends every ride, even an ordered one", "[bot][mount]")
{
    Config const cfg = On();
    Facts f;
    f.BotMounted = true;
    f.Follows = true;
    f.LeaderMounted = true;
    f.LeaderMountedMs = 9000;

    f.BotInCombat = true;
    CHECK(Decide(f, cfg).What == Act::Dismount);
    f.Ordered = true;
    f.SinceActMs = 0;
    f.BotBusy = true;
    Decision d = Decide(f, cfg);
    CHECK(d.What == Act::Dismount);
    CHECK(std::string(d.Why) == "combat");

    f = Facts();
    f.BotMounted = true;
    f.Follows = true;
    f.LeaderInCombat = true;
    CHECK(Decide(f, cfg).What == Act::Dismount);

    f.BotAlive = false;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("mount: a ride ordered in chat is left to the player", "[bot][mount]")
{
    Config const cfg = On();
    Facts f = LeaderWalks();
    f.Ordered = true;
    CHECK(Decide(f, cfg).What == Act::None);

    f.Follows = false;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("mount: a mounted bot without a leader gets off", "[bot][mount]")
{
    Config const cfg = On();
    Facts f;
    f.BotMounted = true;
    f.Follows = false;
    Decision d = Decide(f, cfg);
    CHECK(d.What == Act::Dismount);
    CHECK(std::string(d.Why) == "no_leader");
}

TEST_CASE("mount: the mount matches the leader's speed", "[bot][mount]")
{
    CHECK(PickMatching({}, 60) == -1);

    std::vector<MountOption> options = { { 100, 60, false }, { 90, 100, false }, { 80, 280, true }, { 70, 100, false }, { 0, 400, false } };
    // unknown leader speed: the fastest ground mount, lower id on a tie (the chat order's choice)
    int32 pick = PickMatching(options, 0);
    REQUIRE(pick >= 0);
    CHECK(options[size_t(pick)].SpellId == 70);

    // the slowest that keeps up
    pick = PickMatching(options, 60);
    REQUIRE(pick >= 0);
    CHECK(options[size_t(pick)].SpellId == 100);
    pick = PickMatching(options, 61);
    REQUIRE(pick >= 0);
    CHECK(options[size_t(pick)].SpellId == 70);

    // nothing keeps up: the fastest there is
    pick = PickMatching(options, 150);
    REQUIRE(pick >= 0);
    CHECK(options[size_t(pick)].SpellId == 70);

    std::vector<MountOption> flyers = { { 1, 280, true } };
    CHECK(PickMatching(flyers, 60) == -1);
}

TEST_CASE("mount: speed percent from a run speed rate", "[bot][mount]")
{
    CHECK(SpeedPctFromRate(1.0f) == 0);
    CHECK(SpeedPctFromRate(0.5f) == 0);
    CHECK(SpeedPctFromRate(1.6f) == 60);
    CHECK(SpeedPctFromRate(2.0f) == 100);
    CHECK(SpeedPctFromRate(std::numeric_limits<float>::infinity()) == 0);
    CHECK(SpeedPctFromRate(std::numeric_limits<float>::quiet_NaN()) == 0);
}
