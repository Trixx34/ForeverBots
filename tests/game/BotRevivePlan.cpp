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

#include "BotRevivePlan.h"

#include <vector>

using namespace BotRevive;

namespace
{
Candidate Cand(uint64 key, float dist)
{
    Candidate c;
    c.Key = key;
    c.Dist = dist;
    return c;
}
}

TEST_CASE("revive: the nearest able rezzer is picked", "[bot][revive]")
{
    Config cfg;
    std::vector<Candidate> v = { Cand(1, 40.0f), Cand(2, 12.0f), Cand(3, 25.0f) };
    CHECK(PickResurrector(v, cfg) == 1);
}

TEST_CASE("revive: ties go to the lowest key so exactly one bot acts", "[bot][revive]")
{
    Config cfg;
    std::vector<Candidate> v = { Cand(9, 10.0f), Cand(4, 10.0f), Cand(7, 10.0f) };
    CHECK(PickResurrector(v, cfg) == 1);
}

TEST_CASE("revive: candidates that cannot cast are skipped", "[bot][revive]")
{
    Config cfg;
    std::vector<Candidate> v = { Cand(1, 5.0f), Cand(2, 6.0f), Cand(3, 7.0f), Cand(4, 8.0f), Cand(5, 9.0f), Cand(6, 80.0f) };
    v[0].InCombat = true;
    v[1].ManaPct = cfg.MinManaPct - 1;
    v[2].Ready = false;
    v[3].Busy = true;
    CHECK(PickResurrector(v, cfg) == 4);
    v[4].Dist = cfg.MaxYards + 1.0f;
    CHECK(PickResurrector(v, cfg) == -1);
}

TEST_CASE("revive: nobody to pick from", "[bot][revive]")
{
    CHECK(PickResurrector({}, Config()) == -1);
}

TEST_CASE("revive: approach, cast, then wait out the retry time", "[bot][revive]")
{
    Config cfg;
    Facts f;
    f.CastRange = 30.0f;
    f.Dist = 50.0f;
    CHECK(PlanStep(f, cfg) == Act::Approach);
    f.Dist = 29.0f;   // inside the range but within the 2 yard margin
    CHECK(PlanStep(f, cfg) == Act::Approach);
    f.Dist = 20.0f;
    CHECK(PlanStep(f, cfg) == Act::Cast);
    f.SinceCastMs = cfg.RetryMs - 1;
    CHECK(PlanStep(f, cfg) == Act::Wait);
    f.SinceCastMs = cfg.RetryMs;
    CHECK(PlanStep(f, cfg) == Act::Cast);
    f.Casting = true;
    CHECK(PlanStep(f, cfg) == Act::Wait);
}

TEST_CASE("revive: nothing to do once the member is up, offered or the attempts are used", "[bot][revive]")
{
    Config cfg;
    Facts f;
    f.Dist = 5.0f;
    f.TargetDead = false;
    CHECK(PlanStep(f, cfg) == Act::None);
    f.TargetDead = true;
    f.OfferPending = true;
    CHECK(PlanStep(f, cfg) == Act::None);
    f.OfferPending = false;
    f.Attempts = cfg.MaxAttempts;
    CHECK(PlanStep(f, cfg) == Act::None);
}

TEST_CASE("revive: a dead bot holds its spirit only while a rezzer is on the way", "[bot][revive]")
{
    Config cfg;
    cfg.Enabled = true;
    CHECK(HoldRelease(true, 1000, cfg));
    CHECK_FALSE(HoldRelease(false, 1000, cfg));
    CHECK_FALSE(HoldRelease(true, cfg.WaitBeforeReleaseMs, cfg));
    cfg.WaitBeforeReleaseMs = 0;
    CHECK_FALSE(HoldRelease(true, 1000, cfg));
    cfg.WaitBeforeReleaseMs = 20000;
    cfg.Enabled = false;
    CHECK_FALSE(HoldRelease(true, 1000, cfg));
}

TEST_CASE("revive: an offer is taken after the accept delay", "[bot][revive]")
{
    Config cfg;
    CHECK_FALSE(AcceptOffer(cfg.AcceptDelayMs - 1, cfg));
    CHECK(AcceptOffer(cfg.AcceptDelayMs, cfg));
}
