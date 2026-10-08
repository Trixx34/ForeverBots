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

#include "BotReputationPlan.h"
#include <vector>

using namespace BotReputation;

namespace
{
Reward Gain(int32 amount, int32 rank = 3, int32 standing = 0)
{
    Reward r;
    r.Amount = amount;
    r.Rank = rank;
    r.Standing = standing;
    return r;
}

float Weight(std::vector<Reward> const& v)
{
    return QuestWeight(v, Config{});
}
}

TEST_CASE("reputation: a quest without reputation rewards is neutral", "[BotReputation]")
{
    REQUIRE(Weight({}) == Catch::Approx(1.0f));
    REQUIRE(Weight({ Gain(0) }) == Catch::Approx(1.0f));
}

TEST_CASE("reputation: gains raise the weight in proportion up to the full amount", "[BotReputation]")
{
    REQUIRE(Weight({ Gain(250) }) == Catch::Approx(1.25f));
    REQUIRE(Weight({ Gain(125) }) == Catch::Approx(1.125f));
    REQUIRE(Weight({ Gain(1000) }) == Catch::Approx(1.25f));
    // two factions cannot push the bonus past one full gain
    REQUIRE(Weight({ Gain(250), Gain(250) }) == Catch::Approx(1.25f));
}

TEST_CASE("reputation: gains that cannot count are ignored", "[BotReputation]")
{
    REQUIRE(Weight({ Gain(250, RankExalted, StandingExalted) }) == Catch::Approx(1.0f));
    Reward capped = Gain(250, 5);
    capped.CapRank = 5;
    REQUIRE(Weight({ capped }) == Catch::Approx(1.0f));
    Reward other = Gain(250);
    other.OtherSide = true;
    REQUIRE(Weight({ other }) == Catch::Approx(1.0f));
    REQUIRE(Weight({ Gain(250, RankRevered, 25000) }) == Catch::Approx(1.125f));
}

TEST_CASE("reputation: losses lower the weight", "[BotReputation]")
{
    REQUIRE(Weight({ Gain(-250) }) == Catch::Approx(0.6f));
    REQUIRE(Weight({ Gain(-125) }) == Catch::Approx(0.8f));
    REQUIRE(Weight({ Gain(250), Gain(-250) }) == Catch::Approx(1.25f * 0.6f));
}

TEST_CASE("reputation: a loss into Hostile is never taken, unless already there", "[BotReputation]")
{
    REQUIRE(Weight({ Gain(-250, 2, -2900) }) == Catch::Approx(0.0f));
    REQUIRE(Weight({ Gain(250), Gain(-250, 2, -2900) }) == Catch::Approx(0.0f));
    // already Hostile: nothing left to lose
    REQUIRE(Weight({ Gain(-250, 1, -5000) }) == Catch::Approx(1.0f));
    // staying above the line is only a penalty
    REQUIRE(Weight({ Gain(-250, 2, -1000) }) == Catch::Approx(0.6f));
}

TEST_CASE("reputation: config scales bonus and penalty", "[BotReputation]")
{
    Config cfg;
    cfg.BonusPct = 0;
    cfg.LossPenaltyPct = 100;
    cfg.FullPoints = 100;
    std::vector<Reward> up{ Gain(100) };
    std::vector<Reward> down{ Gain(-100) };
    REQUIRE(QuestWeight(up, cfg) == Catch::Approx(1.0f));
    REQUIRE(QuestWeight(down, cfg) == Catch::Approx(0.0f));
}
