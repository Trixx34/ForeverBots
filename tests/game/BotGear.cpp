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

#include "BotGear.h"

using namespace BotGear;

namespace
{
ItemFacts Piece(uint32 inv, uint32 ilvl, uint32 armor, std::vector<std::pair<int32, int32>> stats, float dps = 0.0f)
{
    ItemFacts f;
    f.InvType = inv;
    f.ItemLevel = ilvl;
    f.Armor = armor;
    f.Dps = dps;
    f.Stats = std::move(stats);
    return f;
}
}

TEST_CASE("BotGear role per class", "[BotGear]")
{
    CHECK(RoleForClass(1) == Role::MeleeStr);
    CHECK(RoleForClass(2) == Role::MeleeStr);
    CHECK(RoleForClass(3) == Role::Ranged);
    CHECK(RoleForClass(4) == Role::MeleeAgi);
    CHECK(RoleForClass(5) == Role::Caster);
    CHECK(RoleForClass(7) == Role::MeleeStr);
    CHECK(RoleForClass(8) == Role::Caster);
    CHECK(RoleForClass(9) == Role::Caster);
    CHECK(RoleForClass(11) == Role::MeleeAgi);
    CHECK(RoleForClass(0) == Role::MeleeStr);
}

TEST_CASE("BotGear scores the stat that matters for the role", "[BotGear]")
{
    ItemFacts strChest = Piece(5, 20, 100, { { STAT_STRENGTH, 1500 }, { STAT_STAMINA, 1000 } });
    ItemFacts intChest = Piece(5, 20, 100, { { STAT_INTELLECT, 1500 }, { STAT_STAMINA, 1000 } });
    CHECK(Score(Role::MeleeStr, strChest) > Score(Role::MeleeStr, intChest));
    CHECK(Score(Role::Caster, intChest) > Score(Role::Caster, strChest));
}

TEST_CASE("BotGear lower item level with the right stat beats higher without", "[BotGear]")
{
    ItemFacts rightStat = Piece(5, 15, 50, { { STAT_INTELLECT, 2000 } });
    ItemFacts wrongStat = Piece(5, 18, 50, { { STAT_STRENGTH, 2000 } });
    CHECK(IsUpgrade(Score(Role::Caster, rightStat), Score(Role::Caster, wrongStat)));
}

TEST_CASE("BotGear weapon dps matters to melee, not to casters", "[BotGear]")
{
    ItemFacts slow = Piece(17, 20, 0, {}, 8.0f);
    ItemFacts fast = Piece(17, 20, 0, {}, 12.0f);
    CHECK(Score(Role::MeleeStr, fast) > Score(Role::MeleeStr, slow));
    CHECK(Score(Role::Caster, fast) < Score(Role::MeleeStr, fast));
}

TEST_CASE("BotGear ranged weapon is worthless to melee classes", "[BotGear]")
{
    ItemFacts bow = Piece(15, 20, 0, {}, 10.0f);
    bow.RangedWeapon = true;
    CHECK(Score(Role::MeleeStr, bow) == 0.0);
    CHECK(Score(Role::Ranged, bow) > 0.0);
    CHECK(Score(Role::Caster, bow) > 0.0);   // wand
}

TEST_CASE("BotGear upgrade rule", "[BotGear]")
{
    CHECK_FALSE(IsUpgrade(0.0, 0.0));
    CHECK(IsUpgrade(5.0, 0.0));            // empty slot
    CHECK_FALSE(IsUpgrade(10.5, 10.0));    // near equal
    CHECK_FALSE(IsUpgrade(10.0, 10.0));
    CHECK(IsUpgrade(13.0, 10.0));
    CHECK_FALSE(IsUpgrade(0.0, 10.0));
}

TEST_CASE("BotGear slot lookup", "[BotGear]")
{
    CHECK(SlotsForInvType(11) == std::vector<uint8>{ 10, 11 });
    CHECK(SlotsForInvType(12) == std::vector<uint8>{ 12, 13 });
    CHECK(SlotsForInvType(5) == SlotsForInvType(20));
    CHECK(SlotsForInvType(17) == std::vector<uint8>{ 15 });
    CHECK(SlotsForInvType(0).empty());
    CHECK(SlotsForInvType(18).empty());    // bag
    CHECK(SlotsForInvType(19).empty());    // tabard
}
