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

#include "BotLoopGuard.h"

using namespace BotLoop;

TEST_CASE("BotLoop::Reached is wrap safe", "[bots]")
{
    CHECK(Reached(100, 100));
    CHECK(Reached(101, 100));
    CHECK_FALSE(Reached(99, 100));
    CHECK(Reached(5, 0xFFFFFFF0u));          // the clock wrapped past t
    CHECK_FALSE(Reached(0xFFFFFFF0u, 5));    // t is still ahead across the wrap
}

TEST_CASE("BotLoop::IsToolEntry and MainHandNeedsWeapon", "[bots]")
{
    std::vector<uint32_t> const tools{ 7005, 2901 };
    CHECK(IsToolEntry(tools, 7005));
    CHECK_FALSE(IsToolEntry(tools, 1234));
    CHECK_FALSE(IsToolEntry(tools, 0));
    CHECK_FALSE(IsToolEntry({}, 7005));

    CHECK(MainHandNeedsWeapon(7005, true));
    CHECK(MainHandNeedsWeapon(0, false));
    CHECK_FALSE(MainHandNeedsWeapon(25, false));
}

TEST_CASE("BotLoop::PickCombatWeapon", "[bots]")
{
    std::vector<WeaponCand> cands{
        { 7005, true, true, 99.0 },    // tool: never picked, however high it scores
        { 25, true, false, 4.0 },
        { 2092, false, false, 50.0 },  // not usable
        { 36, true, false, 7.5 },
        { 40, true, false, 7.5 },      // tie: the first wins
    };
    CHECK(PickCombatWeapon(cands) == 3);
    CHECK(PickCombatWeapon({}) == -1);
    CHECK(PickCombatWeapon({ { 7005, true, true, 1.0 }, { 2092, false, false, 5.0 } }) == -1);
}

TEST_CASE("BotLoop::ClassifyApproach", "[bots]")
{
    CHECK(ClassifyApproach(true, true) == ApproachCause::Weapon);
    CHECK(ClassifyApproach(false, true) == ApproachCause::Ammo);
    CHECK(ClassifyApproach(false, false) == ApproachCause::Unknown);
    CHECK(std::string(CauseName(ApproachCause::Weapon)) == "weapon");
    CHECK(std::string(CauseName(ApproachCause::Unknown)) == "unknown");
}

TEST_CASE("BotLoop::ApproachTracker counts timeouts inside the window", "[bots]")
{
    ApproachTracker t;
    uint32_t const window = 600000;
    CHECK(t.Note(1000, window) == 1);
    CHECK(t.Note(2000, window) == 2);
    CHECK(t.Note(3000, window) == 3);
    // later: only the first one fell out of the window (the other two are still inside), plus this one
    CHECK(t.Note(1000 + window + 500, window) == 3);
    // much later: everything old fell out
    CHECK(t.Note(1000 + 3 * window, window) == 1);
    t.Clear();
    CHECK(t.Note(5000, window) == 1);
}

TEST_CASE("BotLoop::ApproachTracker never overflows", "[bots]")
{
    ApproachTracker t;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 100; ++i)
        n = t.Note(i * 10, 1000000);
    CHECK(n == t.Ms.size());
    CHECK(t.Count == t.Ms.size());
}

TEST_CASE("BotLoop trip guard: cooldown between trips", "[bots]")
{
    TripConfig const cfg{ 1800000, 5, 21600000 };
    TripState s;
    CHECK(TripAllowed(s, 1000, cfg));
    CHECK_FALSE(NoteTripPlanned(s, 1000, cfg));
    CHECK_FALSE(TripAllowed(s, 1000 + 60000, cfg));
    CHECK_FALSE(TripAllowed(s, 1000 + 1799999, cfg));
    CHECK(TripAllowed(s, 1000 + 1800000, cfg));
}

TEST_CASE("BotLoop trip guard: gives up after the attempt cap and recovers", "[bots]")
{
    TripConfig const cfg{ 1000, 3, 50000 };
    TripState s;
    uint32_t now = 0;
    CHECK_FALSE(NoteTripPlanned(s, now, cfg));
    now += 1000;
    CHECK_FALSE(NoteTripPlanned(s, now, cfg));
    now += 1000;
    CHECK(NoteTripPlanned(s, now, cfg));           // third attempt without a success: given up
    CHECK_FALSE(TripAllowed(s, now + 1000, cfg));
    CHECK_FALSE(TripAllowed(s, now + 49999, cfg));
    CHECK(TripAllowed(s, now + 50000, cfg));       // the give-up period is over
    CHECK_FALSE(NoteTripPlanned(s, now + 50000, cfg));
    CHECK(s.Attempts == 1);                        // counting starts over
}

TEST_CASE("BotLoop trip guard: a success resets the key", "[bots]")
{
    TripConfig const cfg{ 100000, 3, 50000 };
    TripState s;
    NoteTripPlanned(s, 0, cfg);
    NoteTripPlanned(s, 100000, cfg);
    NoteTripSuccess(s);
    CHECK(TripAllowed(s, 100001, cfg));
    CHECK(s.Attempts == 0);
    // no cap configured: never given up
    TripConfig const open{ 0, 0, 0 };
    TripState o;
    for (uint32_t i = 0; i < 50; ++i)
        CHECK_FALSE(NoteTripPlanned(o, i, open));
    CHECK(TripAllowed(o, 51, open));
}
