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

#include "BotPlayerLedPlan.h"
#include <cmath>
#include <set>

using namespace BotPlayerLed;

namespace
{
Config const cfg;

Facts Inside()   // bot and leader in the same dungeon instance, close, quiet
{
    Facts f;
    f.Follows = true;
    f.LeaderInDungeon = true;
    f.BotInDungeon = true;
    f.BotInInstanceOfLeader = true;
    f.Distance = 10.0f;
    f.LeaderMapSinceMs = 60000;
    return f;
}

Facts Outside() // bot outside, leader inside
{
    Facts f;
    f.Follows = true;
    f.LeaderInDungeon = true;
    f.LeaderMapSinceMs = 60000;
    return f;
}
}

TEST_CASE("BotPlayerLed: bots come in after the leader", "[BotPlayerLed]")
{
    Facts f = Outside();
    CHECK(Decide(f, cfg).What == Act::Enter);

    f.LeaderMapSinceMs = cfg.EnterDelayMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.LeaderMapSinceMs = cfg.EnterDelayMs;
    CHECK(Decide(f, cfg).What == Act::Enter);

    f = Outside();
    f.Follows = false; // ordered to stay: left alone
    CHECK(Decide(f, cfg).What == Act::None);

    f = Outside();
    f.BotInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);

    f = Outside();
    f.LeaderInCombat = true; // the bot joins a fight in progress
    CHECK(Decide(f, cfg).What == Act::Enter);

    f = Outside();
    f.SinceActMs = cfg.ActCooldownMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.SinceActMs = cfg.ActCooldownMs;
    CHECK(Decide(f, cfg).What == Act::Enter);
}

TEST_CASE("BotPlayerLed: a bot in another instance is left alone", "[BotPlayerLed]")
{
    Facts f = Inside();
    f.BotInInstanceOfLeader = false;
    CHECK(Decide(f, cfg).What == Act::None);
    f.LeaderInDungeon = false;
    f.LeaderOnWorldMap = true;
    CHECK(Decide(f, cfg).What == Act::Leave); // the leader walked out; the bot is still in a dungeon
}

TEST_CASE("BotPlayerLed: catching up needs a long, quiet absence", "[BotPlayerLed]")
{
    Facts f = Inside();
    f.Distance = 200.0f;
    f.FarForMs = cfg.CatchUpMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.FarForMs = cfg.CatchUpMs;
    CHECK(Decide(f, cfg).What == Act::CatchUp);

    f.LeaderInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);
    f.LeaderInCombat = false;
    f.BotInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);
    f.BotInCombat = false;

    f.Distance = cfg.CatchUpYards; // back within range
    CHECK(Decide(f, cfg).What == Act::None);

    f = Inside();
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("BotPlayerLed: leaving with the leader", "[BotPlayerLed]")
{
    Facts f = Inside();
    f.LeaderInDungeon = false;
    f.LeaderOnWorldMap = true;
    f.BotInInstanceOfLeader = false;
    CHECK(Decide(f, cfg).What == Act::Leave);

    f.LeaderOnWorldMap = false; // a battleground or arena: the bot stays where it is
    CHECK(Decide(f, cfg).What == Act::None);

    f = Inside();
    f.LeaderInDungeon = false;
    f.LeaderOnWorldMap = true;
    f.BotInDungeon = false; // both outside: nothing to do here
    f.BotInInstanceOfLeader = false;
    CHECK(Decide(f, cfg).What == Act::None);

    f = Inside();
    f.LeaderInDungeon = false;
    f.LeaderOnWorldMap = true;
    f.BotInInstanceOfLeader = false;
    f.LeaderMapSinceMs = 100;
    CHECK(Decide(f, cfg).What == Act::None); // the leader just left, wait for the map change to settle
}

TEST_CASE("BotPlayerLed: dead bots are raised after the fight", "[BotPlayerLed]")
{
    Facts f = Inside();
    f.BotAlive = false;
    f.DeadForMs = cfg.RaiseDelayMs - 1;
    CHECK(Decide(f, cfg).What == Act::None);
    f.DeadForMs = cfg.RaiseDelayMs;
    CHECK(Decide(f, cfg).What == Act::Raise);

    f.LeaderInCombat = true;
    CHECK(Decide(f, cfg).What == Act::None);
    f.LeaderInCombat = false;

    f.LeaderAlive = false; // wipe: the player runs back first
    CHECK(Decide(f, cfg).What == Act::None);
    f.LeaderAlive = true;

    f.Follows = false;
    CHECK(Decide(f, cfg).What == Act::None);
    f.Follows = true;

    Config off = cfg;
    off.RaiseDelayMs = 0;
    CHECK(Decide(f, off).What == Act::None);

    // dead somewhere outside while the leader is inside: raised next to the leader
    Facts g = Outside();
    g.BotAlive = false;
    g.DeadForMs = cfg.RaiseDelayMs;
    CHECK(Decide(g, cfg).What == Act::Raise);

    // dead while the leader is not in a dungeon: the chat order `revive` does that
    g.LeaderInDungeon = false;
    g.LeaderOnWorldMap = true;
    CHECK(Decide(g, cfg).What == Act::None);
}

TEST_CASE("BotPlayerLed: a dead leader makes everybody wait", "[BotPlayerLed]")
{
    Facts f = Outside();
    f.LeaderAlive = false;
    CHECK(Decide(f, cfg).What == Act::None);
    f = Inside();
    f.LeaderAlive = false;
    f.Distance = 500.0f;
    f.FarForMs = 999999;
    CHECK(Decide(f, cfg).What == Act::None);
}

TEST_CASE("BotPlayerLed: every decision has a reason", "[BotPlayerLed]")
{
    Facts f = Outside();
    CHECK(std::string(Decide(f, cfg).Why) == "LEADER_INSIDE");
    f.Follows = false;
    CHECK(std::string(Decide(f, cfg).Why) == "NOT_FOLLOWING");
    CHECK(std::string(ActName(Act::CatchUp)) == "catch_up");
}

TEST_CASE("BotPlayerLed: spread puts bots on distinct points", "[BotPlayerLed]")
{
    std::set<std::pair<int, int>> seen;
    for (uint32 i = 0; i < 12; ++i)
    {
        float dx = 0, dy = 0;
        Spread(i, dx, dy);
        CHECK(std::hypot(dx, dy) >= 2.4f);
        seen.insert({ int(std::lround(dx * 10)), int(std::lround(dy * 10)) });
    }
    CHECK(seen.size() == 12);
    float dx0 = 0, dy0 = 0, dx6 = 0, dy6 = 0;
    Spread(0, dx0, dy0);
    Spread(6, dx6, dy6);
    CHECK(std::hypot(dx6, dy6) > std::hypot(dx0, dy0)); // the second ring is wider
}

TEST_CASE("BotPlayerLed: who needs a rest", "[BotPlayerLed]")
{
    using namespace BotDungeon;
    BotDungeon::ReadyConfig rc;
    auto M = [](uint64 g, Role r) { MemberState m; m.Guid = g; m.AsRole = r; return m; };

    std::vector<MemberState> ms = { M(1, Role::Tank), M(2, Role::Healer), M(3, Role::Dps), M(4, Role::Dps), M(5, Role::Dps) };
    CHECK(ListNeeds(ms, rc).empty());

    ms[0].HealthPct = 40;                 // tank hurt
    ms[1].ManaPct = 80;                   // healer below its own limit (85), would pass the damage limit (70)
    ms[2].ManaPct = 80;                   // damage dealer fine
    ms[3].Alive = false;
    ms[4].Present = false;
    auto needs = ListNeeds(ms, rc);
    REQUIRE(needs.size() == 4);
    CHECK(needs[0].Guid == 4); CHECK(needs[0].Why == NeedWhy::Dead);
    CHECK(needs[1].Guid == 5); CHECK(needs[1].Why == NeedWhy::Away);
    CHECK(needs[2].Guid == 1); CHECK(needs[2].Why == NeedWhy::Health); CHECK(needs[2].Value == 40);
    CHECK(needs[3].Guid == 2); CHECK(needs[3].Why == NeedWhy::Mana); CHECK(needs[3].Value == 80);

    // exactly at the limit is ready
    ms = { M(1, Role::Tank) };
    ms[0].HealthPct = rc.MinHealthPct;
    ms[0].ManaPct = rc.MinManaPct;
    CHECK(ListNeeds(ms, rc).empty());
}

TEST_CASE("BotPlayerLed: the leader hears about rest once, then again after a while", "[BotPlayerLed]")
{
    constexpr uint32 repeat = 60000;
    CHECK(NoticeDecision(false, true, false, 0xFFFFFFFFu, repeat) == Notice::Resting);
    CHECK(NoticeDecision(true, true, false, 1000, repeat) == Notice::None);
    CHECK(NoticeDecision(true, true, false, repeat, repeat) == Notice::Resting);
    CHECK(NoticeDecision(true, false, false, 1000, repeat) == Notice::Ready);
    CHECK(NoticeDecision(false, false, false, 1000, repeat) == Notice::None);
    // nothing during a fight
    CHECK(NoticeDecision(false, true, true, 0xFFFFFFFFu, repeat) == Notice::None);
    CHECK(NoticeDecision(true, false, true, 0xFFFFFFFFu, repeat) == Notice::None);
}
