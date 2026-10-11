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

#include <string>

#include "BotProgressRules.h"

using namespace BotRule;

namespace
{
constexpr uint32_t MIN_MS = 60u * 1000u;
}

TEST_CASE("N-01 XpStall escalates clear, relog, hub and restarts", "[bots]")
{
    StallConfig cfg;   // 20 / 40 / 90 min, 6 per hour
    cfg.MaxPerHour = 10;
    XpStall s;
    CHECK(s.Update(0, 1, false, cfg).Act == StallStep::None);
    CHECK(s.Update(19 * MIN_MS, 1, false, cfg).Act == StallStep::None);
    StallVerdict v = s.Update(20 * MIN_MS, 1, false, cfg);
    CHECK(v.Act == StallStep::Clear);
    CHECK(v.StalledSec == 1200);
    CHECK(s.Update(21 * MIN_MS, 1, false, cfg).Act == StallStep::None);     // once per stage
    CHECK(s.Update(40 * MIN_MS, 1, false, cfg).Act == StallStep::Relog);
    CHECK(s.Update(89 * MIN_MS, 1, false, cfg).Act == StallStep::None);
    CHECK(s.Update(90 * MIN_MS, 1, false, cfg).Act == StallStep::Hub);
    // after the last step the clock starts over
    CHECK(s.Update(100 * MIN_MS, 1, false, cfg).Act == StallStep::None);
    CHECK(s.Update(110 * MIN_MS, 1, false, cfg).Act == StallStep::Clear);
}

TEST_CASE("N-01 XpStall progress and busy reset the clock", "[bots]")
{
    StallConfig cfg;
    XpStall s;
    s.Update(0, 1, false, cfg);
    CHECK(s.Update(15 * MIN_MS, 2, false, cfg).Act == StallStep::None);     // key changed: progress
    CHECK(s.Update(30 * MIN_MS, 2, false, cfg).Act == StallStep::None);     // 15 min since the change
    CHECK(s.Update(35 * MIN_MS, 2, false, cfg).Act == StallStep::Clear);
    // busy (dead, flying) holds the clock
    XpStall b;
    b.Update(0, 1, false, cfg);
    CHECK(b.Update(19 * MIN_MS, 1, true, cfg).Act == StallStep::None);
    CHECK(b.Update(25 * MIN_MS, 1, false, cfg).Act == StallStep::None);     // clock restarted at 19 min
    CHECK(b.Update(39 * MIN_MS, 1, false, cfg).Act == StallStep::Clear);
}

TEST_CASE("N-01 XpStall hourly cap reports instead of acting", "[bots]")
{
    StallConfig cfg;
    cfg.ClearSec = 60;
    cfg.RelogSec = 120;
    cfg.HubSec = 180;
    cfg.MaxPerHour = 2;
    XpStall s;
    s.Update(0, 1, false, cfg);
    CHECK(s.Update(60 * 1000, 1, false, cfg).Act == StallStep::Clear);
    CHECK(s.Update(120 * 1000, 1, false, cfg).Act == StallStep::Relog);
    StallVerdict v = s.Update(180 * 1000, 1, false, cfg);
    CHECK(v.Act == StallStep::None);
    CHECK(v.Exhausted);
    // an hour later the cap is free again
    XpStall t;
    t.Update(0, 1, false, cfg);
    t.Update(60 * 1000, 1, false, cfg);
    t.Update(120 * 1000, 1, false, cfg);
    CHECK(t.RecoveriesInWindow() == 2);
    t.Update(70 * MIN_MS, 9, false, cfg);                                    // progress at 70 min
    CHECK(t.Update(71 * MIN_MS, 9, false, cfg).Act == StallStep::Clear);
    CHECK(t.RecoveriesInWindow() == 1);
}

TEST_CASE("N-01 StallBook keeps one tracker per bot and survives forgetting only on request", "[bots]")
{
    StallConfig cfg;
    StallBook book;
    book.Update(1, 0, 5, false, cfg);
    book.Update(2, 0, 5, false, cfg);
    CHECK(book.Size() == 2);
    CHECK(book.Update(1, 20 * MIN_MS, 5, false, cfg).Act == StallStep::Clear);
    CHECK(book.Update(2, 10 * MIN_MS, 5, false, cfg).Act == StallStep::None);
    book.Forget(1);
    CHECK(book.Size() == 1);
}

TEST_CASE("N-01 FoldKey and DecisionRing", "[bots]")
{
    CHECK(FoldKey(1, 2) != FoldKey(1, 3));
    CHECK(FoldKey(FoldKey(0, 1), 2) != FoldKey(FoldKey(0, 2), 1));
    DecisionRing r;
    CHECK(r.Json() == "[]");
    for (int i = 0; i < 12; ++i)
        r.Add("CODE" + std::to_string(i), "say \"hi\"");
    CHECK(r.Size() == DecisionRing::kSize);
    std::string const j = r.Json();
    CHECK(j.find("CODE0\"") == std::string::npos);
    CHECK(j.find("CODE2\"") != std::string::npos);
    CHECK(j.find("CODE11\"") != std::string::npos);
    CHECK(j.find("\\\"") == std::string::npos);           // no escaped quotes: they are dropped
}

TEST_CASE("N-02 ZoneFenced and LegSamples", "[bots]")
{
    CHECK(ZoneFenced(6, 19, 4));          // Duskwood band 19 for a level 6 bot
    CHECK_FALSE(ZoneFenced(15, 19, 4));   // 19 <= 15 + 4
    CHECK_FALSE(ZoneFenced(6, 0, 4));     // unknown zone
    auto pts = LegSamples(0, 0, 300, 0, 100, 10);
    REQUIRE(pts.size() == 3);
    CHECK(pts.back().X == Catch::Approx(300.0f));
    pts = LegSamples(0, 0, 10, 0, 100, 10);
    REQUIRE(pts.size() == 1);
    CHECK(pts[0].X == Catch::Approx(10.0f));
    pts = LegSamples(0, 0, 10000, 0, 100, 5);
    REQUIRE(pts.size() == 5);
    CHECK(pts.back().X == Catch::Approx(10000.0f));
    CHECK(LegSamples(0, 0, 10, 0, 100, 0).empty());
    CHECK(LegSamples(5, 5, 5, 5, 100, 4).size() == 1);   // zero length still checks the end point
}

TEST_CASE("N-03 PullGate order and wait", "[bots]")
{
    PullConfig cfg;
    PullState s;
    CHECK(PullGate(s, cfg) == PullBlock::None);
    s.HealthPct = 79.0f;
    CHECK(PullGate(s, cfg) == PullBlock::Health);
    s.HealthPct = 100.0f;
    s.UsesMana = true;
    s.PowerPct = 49.0f;
    CHECK(PullGate(s, cfg) == PullBlock::Power);
    s.UsesMana = false;                    // rage and energy users are not held back by power
    CHECK(PullGate(s, cfg) == PullBlock::None);
    s.RangedNeedsAmmo = true;
    CHECK(PullGate(s, cfg) == PullBlock::Ammo);
    s.MainNeedsWeapon = true;
    CHECK(PullGate(s, cfg) == PullBlock::Weapon);
    CHECK(std::string(PullBlockName(PullBlock::Weapon)) == "weapon");

    PullWait w;
    CHECK(w.Blocked(0, PullBlock::Health, cfg));
    CHECK(w.Blocked(119 * 1000, PullBlock::Health, cfg));
    CHECK_FALSE(w.Blocked(120 * 1000, PullBlock::Health, cfg));              // let go after MaxWaitSec
    CHECK(w.Blocked(1000 * 1000, PullBlock::Weapon, cfg));                   // a missing weapon stays a block
    CHECK_FALSE(w.Blocked(1001 * 1000, PullBlock::None, cfg));
    CHECK(w.Blocked(1002 * 1000, PullBlock::Power, cfg));                    // a new wait starts from scratch
}

TEST_CASE("N-04 NoDamage and cause", "[bots]")
{
    NoDamageConfig cfg;
    CHECK_FALSE(NoDamage(9999, 100, 100, 100, cfg));        // too early
    CHECK(NoDamage(10000, 100, 100, 100, cfg));             // nothing dealt
    CHECK(NoDamage(12000, 100, 96, 100, cfg));              // 4 % is below 5 %
    CHECK_FALSE(NoDamage(12000, 100, 95, 100, cfg));        // 5 % counts
    CHECK_FALSE(NoDamage(12000, 100, 100, 0, cfg));         // no max health: no verdict
    CHECK(NoDamage(12000, 50, 70, 100, cfg));               // the mob healed: still nothing dealt
    CHECK(ClassifyNoDamage(true, true, true, true, true) == NoDmgCause::Weapon);
    CHECK(ClassifyNoDamage(false, true, true, true, true) == NoDmgCause::Ammo);
    CHECK(ClassifyNoDamage(false, false, true, true, true) == NoDmgCause::Power);
    CHECK(ClassifyNoDamage(false, false, false, true, true) == NoDmgCause::Los);
    CHECK(ClassifyNoDamage(false, false, false, false, true) == NoDmgCause::Range);
    CHECK(ClassifyNoDamage(false, false, false, false, false) == NoDmgCause::Unknown);
    CHECK(std::string(NoDmgCauseName(NoDmgCause::Los)) == "los");
}

TEST_CASE("N-05 bag pressure and grey loot", "[bots]")
{
    BagConfig cfg;
    CHECK(BagPressure(4, cfg));
    CHECK_FALSE(BagPressure(5, cfg));
    CHECK(BagPressure(0, cfg));
    CHECK(SkipLoot(3, true, false, cfg));                   // grey below 4 free slots
    CHECK_FALSE(SkipLoot(4, true, false, cfg));
    CHECK_FALSE(SkipLoot(0, true, true, cfg));              // quest items always
    CHECK_FALSE(SkipLoot(0, false, false, cfg));            // greens and better always
    cfg.GreyFreeSlots = 0;                                  // 0 switches the grey rule off
    CHECK_FALSE(SkipLoot(0, true, false, cfg));
}

TEST_CASE("N-06 CrowdBook counts live claims in range", "[bots]")
{
    CrowdBook b;
    uint64_t const key = ObjectiveKey(1, 3361);
    for (uint64_t g = 1; g <= 9; ++g)
        b.Claim(key, g, 100.0f + float(g), 100.0f, 1000, 60000);
    b.Claim(ObjectiveKey(1, 5), 20, 100.0f, 100.0f, 1000, 60000);           // other objective
    b.Claim(key, 21, 900.0f, 900.0f, 1000, 60000);                          // far away
    CHECK(b.Count(key, 1, 100.0f, 100.0f, 150.0f, 2000) == 8);              // not counting itself
    CHECK(b.Count(key, 99, 100.0f, 100.0f, 150.0f, 2000) == 9);
    CHECK(Crowded(8, 8));
    CHECK_FALSE(Crowded(7, 8));
    CHECK_FALSE(Crowded(100, 0));                                           // cap 0 = off
    CHECK(CrowdFactor(8, 8, 0.35f) == Catch::Approx(0.35f));
    CHECK(CrowdFactor(7, 8, 0.35f) == Catch::Approx(1.0f));
    // expiry and release
    CHECK(b.Count(key, 99, 100.0f, 100.0f, 150.0f, 1000 + 60000) == 0);
    b.Release(3);
    CHECK(b.Count(key, 99, 100.0f, 100.0f, 150.0f, 2000) == 8);
    // a bot has one claim: a new one replaces the old
    b.Claim(ObjectiveKey(1, 7), 4, 100.0f, 100.0f, 2000, 60000);
    CHECK(b.Count(key, 99, 100.0f, 100.0f, 150.0f, 3000) == 7);
    CHECK(b.Count(ObjectiveKey(1, 7), 99, 100.0f, 100.0f, 150.0f, 3000) == 1);
    // the clock may wrap
    CrowdBook w;
    w.Claim(key, 1, 0, 0, 0xFFFFFF00u, 1000);
    CHECK(w.Count(key, 2, 0, 0, 10.0f, 5) == 1);                            // 0x100 ms before the claim runs out
    CHECK(w.Count(key, 2, 0, 0, 10.0f, 0x400) == 0);
}

TEST_CASE("N-07 ZoneLeave triggers", "[bots]")
{
    ZoneLeaveConfig cfg;
    ZoneLeave z;
    CHECK_FALSE(z.NoteHubNone(1, 10, cfg));
    CHECK(z.NoteHubNone(1, 10, cfg));                                       // second row for the same map and level
    CHECK_FALSE(z.NoteHubNone(1, 11, cfg));                                 // a level up starts over
    CHECK_FALSE(z.NoteHubNone(0, 11, cfg));                                 // so does another map
    z.Reset();
    CHECK_FALSE(z.NoteHubNone(1, 11, cfg));

    ZoneLeave g;
    CHECK_FALSE(g.NoteGrindXp(0, 30, cfg));
    CHECK_FALSE(g.NoteGrindXp(5 * MIN_MS, 30, cfg));
    CHECK(g.NoteGrindXp(10 * MIN_MS, 30, cfg));                             // 10 minutes of kills below 40 xp
    CHECK_FALSE(g.NoteGrindXp(11 * MIN_MS, 80, cfg));                       // a good kill ends the streak
    CHECK_FALSE(g.NoteGrindXp(12 * MIN_MS, 30, cfg));
    CHECK(std::string(LeaveReasonName(LeaveReason::NoHub)) == "no_hub");
}

TEST_CASE("N-08 class pull margin", "[bots]")
{
    CHECK(ClassPullMargin(3, 8, 10, 0, 1) == 0);        // hunter
    CHECK(ClassPullMargin(1, 8, 10, 0, 1) == 0);        // warrior
    CHECK(ClassPullMargin(4, 8, 10, 0, 1) == 0);        // rogue
    CHECK(ClassPullMargin(9, 8, 10, 0, 1) == 0);        // warlock
    CHECK(ClassPullMargin(2, 8, 10, 0, 1) == 1);        // paladin
    CHECK(ClassPullMargin(5, 8, 10, 0, 1) == 1);        // priest
    CHECK(ClassPullMargin(11, 8, 10, 0, 1) == 1);       // druid
    CHECK(ClassPullMargin(8, 8, 10, 0, 1) == -1);       // mage: no class rule
    CHECK(ClassPullMargin(3, 11, 10, 0, 1) == -1);      // above the level limit
    CHECK(EffectivePullCap(2, -1) == 2);
    CHECK(EffectivePullCap(2, 0) == 0);
    CHECK(EffectivePullCap(0, 1) == 0);                 // never raises the global cap
}

TEST_CASE("N-09 hourly caps parse and count", "[bots]")
{
    auto caps = ParseHourlyCaps("TARGET_PICKED:60, AGGRO_AVOID:30,bad,:5,X:,Y:abc,Z:0");
    REQUIRE(caps.size() == 3);
    CHECK(caps[0].first == "TARGET_PICKED");
    CHECK(caps[0].second == 60);
    CHECK(caps[1].first == "AGGRO_AVOID");
    CHECK(caps[1].second == 30);
    CHECK(caps[2].first == "Z");
    CHECK(caps[2].second == 0);
    CHECK(ParseHourlyCaps("").empty());

    HourlyCounter c;
    for (int i = 0; i < 3; ++i)
        CHECK_FALSE(c.Hit(100.0, 3));
    CHECK(c.Hit(200.0, 3));                          // the 4th row inside the hour is dropped
    CHECK(c.Hit(3599.0, 3));
    CHECK_FALSE(c.Hit(3600.0, 3));                   // a new hour starts counting again
    CHECK(c.Hit(10.0 * 3600.0, 0));                  // cap 0 drops everything
}
