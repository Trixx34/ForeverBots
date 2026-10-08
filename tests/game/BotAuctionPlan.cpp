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

#include "BotAuctionPlan.h"
#include <string>

using namespace BotAuctionPlan;

TEST_CASE("BotAuctionPlan listing price", "[BotAuctionPlan]")
{
    CHECK(ListingPrice(1000, 0, 5) == 1425);        // 1500 less 5 percent
    CHECK(ListingPrice(1000, 4000, 5) == 3800);     // the recent median wins when higher
    CHECK(ListingPrice(1000, 0, 90) >= 1000);       // never below the vendor price
    CHECK(ListingPrice(1000, 0, 200) >= 1000);      // silly undercut is clamped
    CHECK(ListingPrice(0, 0, 5) >= 1);              // worthless items still get a price
}

TEST_CASE("BotAuctionPlan seed price", "[BotAuctionPlan]")
{
    CHECK(SeedPrice(100, 500) == 200);              // twice the sell price
    CHECK(SeedPrice(100, 150) == 165);              // capped at 110 percent of the buy price
    CHECK(SeedPrice(100, 90) == 100);               // buy price below sell price: floor at sell
    CHECK(SeedPrice(100, 0) == 200);                // unknown buy price: 2x
    CHECK(SeedPrice(0, 0) >= 1);
}

TEST_CASE("BotAuctionPlan caps and deposit", "[BotAuctionPlan]")
{
    Limits lim;
    CHECK(CanList(lim, 0, 0, 10000, 100) == Verdict::Ok);
    CHECK(CanList(lim, 5, 0, 10000, 100) == Verdict::PerBotLimit);
    CHECK(CanList(lim, 0, 2000, 10000, 100) == Verdict::TotalLimit);
    CHECK(CanList(lim, 0, 0, 50, 100) == Verdict::NoMoney);
    CHECK(CanList(lim, 0, 0, 500, 100) == Verdict::DepositTooHigh);   // 20 percent of the bot's money
    CHECK(CanList(lim, 0, 0, 1000, 100) == Verdict::Ok);              // exactly 10 percent
    CHECK(std::string(VerdictCode(Verdict::PerBotLimit)) == "AH_LIMIT");
    CHECK(std::string(VerdictCode(Verdict::DepositTooHigh)) == "AH_NO_MONEY");
}
