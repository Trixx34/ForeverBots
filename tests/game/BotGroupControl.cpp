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

#include "BotGroupControl.h"

using namespace BotGroupControl;

TEST_CASE("BotGroupControl: claims", "[BotGroupControl]")
{
    ClaimBoard board;

    SECTION("the first claimer wins, others wait")
    {
        CHECK(board.Claim(10, 1, 1000, 1500));
        CHECK_FALSE(board.Claim(10, 2, 1100, 1500));
        CHECK(board.HeldByOther(10, 2, 1100));
        CHECK_FALSE(board.HeldByOther(10, 1, 1100));
        CHECK(board.Claim(11, 2, 1100, 1500));   // another key is free
    }
    SECTION("the holder renews its own claim")
    {
        CHECK(board.Claim(10, 1, 1000, 1500));
        CHECK(board.Claim(10, 1, 2000, 1500));
        CHECK_FALSE(board.Claim(10, 2, 3000, 1500));   // renewed until 3500
        CHECK(board.Claim(10, 2, 3500, 1500));         // expired exactly at 3500
    }
    SECTION("a backup takes over when the hold runs out")
    {
        CHECK(board.Claim(10, 1, 1000, 1500));
        CHECK_FALSE(board.Claim(10, 2, 2499, 1500));
        CHECK(board.Claim(10, 2, 2500, 1500));
        CHECK(board.HeldByOther(10, 1, 2600));
    }
    SECTION("release gives the claim up, only for its owner")
    {
        CHECK(board.Claim(10, 1, 1000, 1500));
        board.Release(10, 2);
        CHECK_FALSE(board.Claim(10, 2, 1100, 1500));
        board.Release(10, 1);
        CHECK(board.Claim(10, 2, 1100, 1500));
    }
    SECTION("expired claims are dropped and the board stays bounded")
    {
        for (uint64 k = 0; k < 200; ++k)
            CHECK(board.Claim(k, 1, 1000, 100000));
        CHECK(board.Size() <= 64);
        CHECK(board.Claim(500, 1, 1000, 100));
        CHECK(board.Claim(501, 1, 5000, 100));   // 500 expired meanwhile
    }
    SECTION("the clock may wrap around")
    {
        uint32 const near = 0xFFFFFF00u;
        CHECK(board.Claim(10, 1, near, 1000));   // until 744 after the wrap
        CHECK_FALSE(board.Claim(10, 2, 100, 1000));
        CHECK(board.Claim(10, 2, 800, 1000));
    }
    SECTION("the shared boards are separate")
    {
        CHECK(InterruptClaims().Claim(900001, 1, 1000, 1500));
        CHECK(DispelClaims().Claim(900001, 2, 1000, 1500));
        CHECK(ControlClaims().Claim(900001, 3, 1000, 1500));
        InterruptClaims().Release(900001, 1);
        DispelClaims().Release(900001, 2);
        ControlClaims().Release(900001, 3);
    }
}

TEST_CASE("BotGroupControl: which casts are worth an interrupt", "[BotGroupControl]")
{
    InterruptConfig cfg;   // 1500 ms, 400 ms
    CastFacts c;
    c.TotalMs = 2500;
    c.RemainingMs = 2000;

    CHECK(WorthInterrupting(c, cfg));
    SECTION("not interruptible")
    {
        c.Interruptible = false;
        CHECK_FALSE(WorthInterrupting(c, cfg));
        c.Heal = true;
        CHECK_FALSE(WorthInterrupting(c, cfg));
    }
    SECTION("a short cast is let through, a heal or control cast is not")
    {
        c.TotalMs = 1000;
        c.RemainingMs = 900;
        CHECK_FALSE(WorthInterrupting(c, cfg));
        c.Heal = true;
        CHECK(WorthInterrupting(c, cfg));
        c.Heal = false;
        c.Control = true;
        CHECK(WorthInterrupting(c, cfg));
    }
    SECTION("a cast about to land is not worth the cooldown")
    {
        c.RemainingMs = 399;
        CHECK_FALSE(WorthInterrupting(c, cfg));
        c.RemainingMs = 400;
        CHECK(WorthInterrupting(c, cfg));
        c.Heal = true;
        c.RemainingMs = 100;
        CHECK_FALSE(WorthInterrupting(c, cfg));
    }
    SECTION("the thresholds are configurable")
    {
        cfg.MinCastMs = 3000;
        CHECK_FALSE(WorthInterrupting(c, cfg));
        cfg.MinCastMs = 0;
        c.TotalMs = 0;
        CHECK(WorthInterrupting(c, cfg));
    }
}

TEST_CASE("BotGroupControl: dispel priorities", "[BotGroupControl]")
{
    uint32 const all = DISPEL_BIT_MAGIC | DISPEL_BIT_CURSE | DISPEL_BIT_DISEASE | DISPEL_BIT_POISON;

    CHECK(DispelBitOf(1) == DISPEL_BIT_MAGIC);
    CHECK(DispelBitOf(2) == DISPEL_BIT_CURSE);
    CHECK(DispelBitOf(3) == DISPEL_BIT_DISEASE);
    CHECK(DispelBitOf(4) == DISPEL_BIT_POISON);
    CHECK(DispelBitOf(0) == 0);
    CHECK(DispelBitOf(5) == 0);   // stealth is not a thing a friend dispels

    AuraFact a;
    a.DispelType = 1;
    CHECK(AuraPriority(a, all) == PRIORITY_OTHER);
    a.Slow = true;
    CHECK(AuraPriority(a, all) == PRIORITY_SLOW);
    a.Damage = true;
    CHECK(AuraPriority(a, all) == PRIORITY_DAMAGE);
    a.Hard = true;
    CHECK(AuraPriority(a, all) == PRIORITY_HARD);

    SECTION("only what the bot can remove counts")
    {
        CHECK(AuraPriority(a, DISPEL_BIT_POISON | DISPEL_BIT_DISEASE) == 0);
        CHECK(AuraPriority(a, DISPEL_BIT_MAGIC) == PRIORITY_HARD);
        a.DispelType = 0;
        CHECK(AuraPriority(a, all) == 0);   // cannot be dispelled at all
    }
    SECTION("buffs and effects about to end are left")
    {
        a.Harmful = false;
        CHECK(AuraPriority(a, all) == 0);
        a.Harmful = true;
        a.RemainingMs = 1999;
        CHECK(AuraPriority(a, all) == 0);
        a.RemainingMs = 2000;
        CHECK(AuraPriority(a, all) == PRIORITY_HARD);
    }
}

TEST_CASE("BotGroupControl: who is dispelled first", "[BotGroupControl]")
{
    std::vector<DispelCandidate> m;
    CHECK(PickDispelTarget(m, 1) == -1);

    m.push_back({ 5, 0, 30 });
    CHECK(PickDispelTarget(m, 1) == -1);   // nothing to dispel

    m.push_back({ 6, PRIORITY_DAMAGE, 80 });
    m.push_back({ 7, PRIORITY_HARD, 90 });
    CHECK(PickDispelTarget(m, 1) == 2);    // polymorph before damage over time, whatever the health

    SECTION("minimum priority")
    {
        m[2].Priority = PRIORITY_SLOW;
        CHECK(PickDispelTarget(m, PRIORITY_DAMAGE) == 1);
        CHECK(PickDispelTarget(m, PRIORITY_HARD) == -1);
    }
    SECTION("ties: lower health, then lower key")
    {
        m[1].Priority = PRIORITY_HARD;
        CHECK(PickDispelTarget(m, 1) == 1);   // 80% < 90%
        m[1].HealthPct = 90;
        CHECK(PickDispelTarget(m, 1) == 1);   // key 6 < key 7
    }
}

TEST_CASE("BotGroupControl: mark lists", "[BotGroupControl]")
{
    CHECK(ParseMarkList("") == 0);
    CHECK(ParseMarkList("none") == 0);
    CHECK(ParseMarkList("moon") == (1u << 4));
    CHECK(ParseMarkList("moon,square,triangle,diamond") == ((1u << 4) | (1u << 5) | (1u << 3) | (1u << 2)));
    CHECK(ParseMarkList(" MOON , Skull ") == ((1u << 4) | (1u << 7)));
    CHECK(ParseMarkList("1,8") == ((1u << 0) | (1u << 7)));

    std::vector<std::string> unknown;
    CHECK(ParseMarkList("moon,banana,9,0", &unknown) == (1u << 4));
    REQUIRE(unknown.size() == 3);
    CHECK(unknown[0] == "banana");
    CHECK(unknown[1] == "9");
    CHECK(unknown[2] == "0");

    uint32 const mask = ParseMarkList("moon,square");
    CHECK(MarkInMask(4, mask));
    CHECK(MarkInMask(5, mask));
    CHECK_FALSE(MarkInMask(7, mask));
    CHECK_FALSE(MarkInMask(ICON_NONE, mask));
    CHECK_FALSE(MarkInMask(8, mask));
}

namespace
{
ControlCandidate Mob(uint64 key, int32 level, int8 mark = ICON_NONE)
{
    ControlCandidate c;
    c.Key = key;
    c.Level = level;
    c.Mark = mark;
    return c;
}
}

TEST_CASE("BotGroupControl: crowd control target", "[BotGroupControl]")
{
    ControlConfig cfg;
    cfg.MarkMask = ParseMarkList("moon,square");
    std::vector<ControlCandidate> mobs;
    CHECK(PickControlTarget(mobs, cfg) == -1);

    mobs.push_back(Mob(1, 20));   // the fight target
    mobs.back().IsFightTarget = true;
    mobs.push_back(Mob(2, 21));
    mobs.push_back(Mob(3, 23));

    SECTION("automatic control takes the strongest extra attacker")
    {
        CHECK(PickControlTarget(mobs, cfg) == 2);
        cfg.Auto = false;
        CHECK(PickControlTarget(mobs, cfg) == -1);
    }
    SECTION("a mark beats strength")
    {
        mobs[1].Mark = 4;
        CHECK(PickControlTarget(mobs, cfg) == 1);
        cfg.Auto = false;
        CHECK(PickControlTarget(mobs, cfg) == 1);   // marks work without the automatic mode
    }
    SECTION("the strongest marked mob goes first, ties by key")
    {
        mobs[1].Mark = 4;
        mobs[2].Mark = 5;
        CHECK(PickControlTarget(mobs, cfg) == 2);
        mobs[2].Level = 21;
        CHECK(PickControlTarget(mobs, cfg) == 1);
    }
    SECTION("any number of marked mobs, but automatic control stops at MaxAuto")
    {
        mobs[1].Mark = 4;
        mobs[1].Controlled = true;
        mobs[2].Mark = 5;
        CHECK(PickControlTarget(mobs, cfg) == 2);   // a second mark is controlled as well
        mobs[2].Mark = ICON_NONE;
        CHECK(PickControlTarget(mobs, cfg) == -1);  // one automatic control is running already
        cfg.MaxAuto = 2;
        CHECK(PickControlTarget(mobs, cfg) == 2);
    }
    SECTION("the fight target and skull are never controlled automatically")
    {
        mobs[0].Level = 40;
        mobs[0].Mark = 4;
        CHECK(PickControlTarget(mobs, cfg) == 2);   // fight target skipped even when marked
        mobs[2].Mark = ICON_SKULL;
        CHECK(PickControlTarget(mobs, cfg) == 1);
        cfg.MarkMask |= 1u << ICON_SKULL;
        CHECK(PickControlTarget(mobs, cfg) == 2);   // unless the player asked for skull
    }
    SECTION("unusable mobs are skipped")
    {
        mobs[2].TypeOk = false;
        CHECK(PickControlTarget(mobs, cfg) == 1);
        mobs[1].Reachable = false;
        CHECK(PickControlTarget(mobs, cfg) == -1);
        mobs[1].Reachable = true;
        mobs[1].Tried = true;
        CHECK(PickControlTarget(mobs, cfg) == -1);
        mobs[1].Tried = false;
        mobs[1].ClaimedByOther = true;
        CHECK(PickControlTarget(mobs, cfg) == -1);
        mobs[1].ClaimedByOther = false;
        mobs[1].Controlled = true;
        CHECK(PickControlTarget(mobs, cfg) == -1);
    }
}

TEST_CASE("BotGroupControl: mobs the fight target choice leaves alone", "[BotGroupControl]")
{
    uint32 const mask = ParseMarkList("moon");
    CHECK(ReservedForControl(true, ICON_NONE, mask));
    CHECK(ReservedForControl(false, 4, mask));
    CHECK_FALSE(ReservedForControl(false, 7, mask));
    CHECK_FALSE(ReservedForControl(false, ICON_NONE, mask));
    CHECK_FALSE(ReservedForControl(false, 4, 0));
}
