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

#include "BotRotation.h"
#include <string>

using namespace BotRotation;

namespace
{
bool Ok(Cond c, int32 param, Facts const& f) { return Allowed({ c, param }, f); }
}

TEST_CASE("BotRotation: conditions", "[BotRotation]")
{
    Facts f;

    SECTION("Always passes, also with the rotation off")
    {
        f.Enabled = false;
        CHECK(Ok(Cond::Always, 0, f));
        CHECK_FALSE(Ok(Cond::TargetFleeing, 0, f));
        f.TargetFleeing = true;
        CHECK_FALSE(Ok(Cond::TargetFleeing, 0, f));   // off means off, whatever the facts say
        f.Enabled = true;
        CHECK(Ok(Cond::TargetFleeing, 0, f));
    }
    SECTION("target health")
    {
        f.TargetHpPct = 25;
        CHECK(Ok(Cond::TargetHpBelow, 30, f));
        CHECK_FALSE(Ok(Cond::TargetHpBelow, 25, f));
        CHECK(Ok(Cond::TargetHpAbove, 20, f));
        CHECK_FALSE(Ok(Cond::TargetHpAbove, 25, f));   // strict: a mob at exactly the limit is not worth a damage over time
    }
    SECTION("self health and power")
    {
        f.SelfHpPct = 40;
        f.SelfPowerPct = 15;
        CHECK(Ok(Cond::SelfHpBelow, 50, f));
        CHECK_FALSE(Ok(Cond::SelfHpBelow, 40, f));
        CHECK(Ok(Cond::SelfPowerBelow, 20, f));
        CHECK_FALSE(Ok(Cond::SelfPowerAbove, 25, f));
        f.SelfPowerPct = 25;
        CHECK(Ok(Cond::SelfPowerAbove, 25, f));       // a reserve of exactly the limit is enough
    }
    SECTION("enemies on the bot")
    {
        f.EnemiesOnBot = 1;
        CHECK_FALSE(Ok(Cond::EnemiesAtLeast, 2, f));
        f.EnemiesOnBot = 3;
        CHECK(Ok(Cond::EnemiesAtLeast, 2, f));
        CHECK(Ok(Cond::EnemiesAtLeast, 3, f));
        CHECK(Ok(Cond::EnemiesAtLeast, 0, f));        // a parameter below 1 behaves as 1
    }
    SECTION("casting and fleeing targets")
    {
        CHECK_FALSE(Ok(Cond::TargetCasting, 0, f));
        f.TargetCasting = true;
        CHECK(Ok(Cond::TargetCasting, 0, f));
    }
    SECTION("opener")
    {
        f.FightMs = 7999;
        CHECK(Ok(Cond::Opener, 8, f));
        f.FightMs = 8000;
        CHECK_FALSE(Ok(Cond::Opener, 8, f));
        CHECK_FALSE(Ok(Cond::Opener, 0, f));
    }
    SECTION("strong target: elite or enough levels above")
    {
        CHECK_FALSE(Ok(Cond::TargetStrong, 2, f));
        f.TargetLevelDiff = 2;
        CHECK(Ok(Cond::TargetStrong, 2, f));
        f.TargetLevelDiff = 0;
        f.TargetElite = true;
        CHECK(Ok(Cond::TargetStrong, 2, f));
    }
    SECTION("life tap needs health and a mana gap")
    {
        f.SelfHpPct = 90;
        f.SelfPowerPct = 20;
        CHECK(Ok(Cond::LifeTapSafe, 30, f));
        f.SelfHpPct = 60;
        CHECK_FALSE(Ok(Cond::LifeTapSafe, 30, f));
        f.SelfHpPct = 90;
        f.SelfPowerPct = 30;
        CHECK_FALSE(Ok(Cond::LifeTapSafe, 30, f));
    }
    SECTION("every condition has a name")
    {
        for (uint8 i = 0; i <= uint8(Cond::LifeTapSafe); ++i)
            CHECK(std::string(CondName(Cond(i))) != "?");
    }
}
