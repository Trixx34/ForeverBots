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

#include "BotLootRollPlan.h"

using namespace BotLootRoll;

TEST_CASE("loot roll: a usable upgrade is needed", "[BotLootRoll]")
{
    Facts f;
    f.Usable = true;
    f.Upgrade = true;
    CHECK(Decide(f, Config()) == Vote::Need);
    f.NeedAllowed = false;
    CHECK(Decide(f, Config()) == Vote::Greed);
}

TEST_CASE("loot roll: an upgrade the class cannot use is not needed", "[BotLootRoll]")
{
    Facts f;
    f.Usable = false;
    f.Upgrade = true;
    CHECK(Decide(f, Config()) == Vote::Greed);
}

TEST_CASE("loot roll: quest items are needed", "[BotLootRoll]")
{
    Facts f;
    f.QuestItem = true;
    CHECK(Decide(f, Config()) == Vote::Need);
}

TEST_CASE("loot roll: other items are greeded or passed by setting", "[BotLootRoll]")
{
    Facts f;
    f.Usable = true;
    CHECK(Decide(f, Config()) == Vote::Greed);
    Config c;
    c.GreedOnOther = false;
    CHECK(Decide(f, c) == Vote::Pass);
}

TEST_CASE("loot roll: a full bag always passes", "[BotLootRoll]")
{
    Facts f;
    f.Usable = true;
    f.Upgrade = true;
    f.QuestItem = true;
    f.BagFull = true;
    CHECK(Decide(f, Config()) == Vote::Pass);
}
