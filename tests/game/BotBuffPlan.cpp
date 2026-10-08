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

#include "BotBuffPlan.h"

using namespace BotBuff;

TEST_CASE("buffs: who a buff applies to", "[BotBuff]")
{
    for (uint8 cls : { 1, 2, 3, 4, 5, 7, 8, 9, 11 })
        CHECK(Applies(Targets::All, cls));
    CHECK(Applies(Targets::ManaUsers, 5));
    CHECK(Applies(Targets::ManaUsers, 8));
    CHECK_FALSE(Applies(Targets::ManaUsers, 1));
    CHECK_FALSE(Applies(Targets::ManaUsers, 4));
    CHECK(Applies(Targets::Melee, 1));
    CHECK(Applies(Targets::Melee, 4));
    CHECK_FALSE(Applies(Targets::Melee, 8));
    CHECK_FALSE(Applies(Targets::Melee, 5));
}

TEST_CASE("buffs: the throttle is per target and buff", "[BotBuff]")
{
    Throttle t;
    CHECK(t.Ready(1, 100, 0, 5000));
    t.Note(1, 100, 1000);
    CHECK_FALSE(t.Ready(1, 100, 5999, 5000));
    CHECK(t.Ready(1, 100, 6000, 5000));
    CHECK(t.Ready(2, 100, 1000, 5000));
    CHECK(t.Ready(1, 101, 1000, 5000));
    t.Note(1, 100, 7000);
    CHECK_FALSE(t.Ready(1, 100, 8000, 5000));
    CHECK(t.Size() == 1);
}

TEST_CASE("buffs: the throttle stays bounded and drops the oldest", "[BotBuff]")
{
    Throttle t;
    for (uint32 i = 0; i < 200; ++i)
        t.Note(i, 1, i);
    CHECK(t.Size() <= 64);
    CHECK(t.Ready(0, 1, 201, 100000));          // the first record is gone
    CHECK_FALSE(t.Ready(199, 1, 201, 100000));  // the newest is kept
}

TEST_CASE("buffs: the clock may wrap", "[BotBuff]")
{
    Throttle t;
    t.Note(1, 1, 0xFFFFFF00u);
    CHECK_FALSE(t.Ready(1, 1, 100, 5000));
    CHECK(t.Ready(1, 1, 0x2000u, 5000));
}
