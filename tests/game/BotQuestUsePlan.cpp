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

#include "BotQuestUsePlan.h"

using namespace BotQuestUse;

TEST_CASE("BotQuestUse: item spells", "[BotQuestUse]")
{
    Config cfg;
    SpellFacts s;
    CHECK_FALSE(IsTargetItem(s));
    s.OnUse = true;
    CHECK_FALSE(IsTargetItem(s));      // self buff, potion, ...
    s.NeedsUnitTarget = true;
    CHECK(IsTargetItem(s));
    s.OnUse = false;
    CHECK_FALSE(IsTargetItem(s));      // on equip / on hit

    s = SpellFacts();
    CHECK(CastRange(s, cfg) == 15.0f);                 // unknown range
    s.MaxRange = 20.0f;
    CHECK(CastRange(s, cfg) == 18.0f);
    s.MaxRange = 2.0f;
    CHECK(CastRange(s, cfg) == cfg.MinRange);
    s.MaxRange = 100.0f;
    CHECK(CastRange(s, cfg) == cfg.MaxRange);
}

TEST_CASE("BotQuestUse: steps", "[BotQuestUse]")
{
    Config cfg;
    Facts f;
    f.Range = 18.0f;
    f.Distance = 10.0f;

    SECTION("in range, first cast")
    {
        Decision d = Decide(f, cfg);
        CHECK(d.What == Step::Cast);
    }
    SECTION("too far")
    {
        f.Distance = 25.0f;
        CHECK(Decide(f, cfg).What == Step::Approach);
    }
    SECTION("no line of sight")
    {
        f.LineOfSight = false;
        Decision d = Decide(f, cfg);
        CHECK(d.What == Step::Approach);
    }
    SECTION("credit ends it, even when the target is gone")
    {
        f.CreditGained = true;
        f.TargetAlive = false;
        CHECK(Decide(f, cfg).What == Step::Done);
    }
    SECTION("target gone")
    {
        f.TargetAlive = false;
        CHECK(Decide(f, cfg).What == Step::Abort);
    }
    SECTION("timeout")
    {
        f.ElapsedMs = cfg.TargetTimeoutMs;
        CHECK(Decide(f, cfg).What == Step::Abort);
    }
    SECTION("waits while casting")
    {
        f.Casting = true;
        CHECK(Decide(f, cfg).What == Step::Wait);
    }
    SECTION("waits between casts, then casts again")
    {
        f.Casts = 1;
        f.SinceCastMs = cfg.RetryMs - 1;
        CHECK(Decide(f, cfg).What == Step::Wait);
        f.SinceCastMs = cfg.RetryMs;
        CHECK(Decide(f, cfg).What == Step::Cast);
    }
    SECTION("waits while fighting something else")
    {
        f.BotInCombat = true;
        CHECK(Decide(f, cfg).What == Step::Wait);
    }
    SECTION("gives up after the last cast got no credit")
    {
        f.Casts = cfg.MaxCasts;
        f.SinceCastMs = cfg.RetryMs - 1;
        CHECK(Decide(f, cfg).What == Step::Wait);   // the credit may still arrive
        f.SinceCastMs = cfg.RetryMs;
        Decision d = Decide(f, cfg);
        CHECK(d.What == Step::Abort);
    }
    SECTION("step names")
    {
        CHECK(std::string(StepName(Step::Cast)) == "cast");
        CHECK(std::string(StepName(Step::Wait)) == "wait");
    }
}
