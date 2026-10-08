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

#include "BotTradeLink.h"
#include <string>

using namespace BotTradeLink;

namespace
{
BotStock Stock(uint64 bot, std::vector<Holding> items, bool inRange = true)
{
    BotStock s;
    s.Bot = bot;
    s.InRange = inRange;
    s.Items = std::move(items);
    return s;
}
}

TEST_CASE("ParseItemLinks reads entries in order, once each", "[bot][tradelink]")
{
    std::string const text = "need |cff1eff00|Hitem:2589:0:0:0:0:0:0:0:60|h[Linen Cloth]|h|r and |cffffffff|Hitem:2592:0|h[Wool Cloth]|h|r "
        "again |Hitem:2589:0|h[Linen Cloth]|h";
    CHECK(ParseItemLinks(text, 10) == std::vector<uint32>{ 2589, 2592 });
}

TEST_CASE("ParseItemLinks ignores other links and malformed ones", "[bot][tradelink]")
{
    CHECK(ParseItemLinks("", 5).empty());
    CHECK(ParseItemLinks("hello |Hquest:1234:5|h[Q]|h", 5).empty());
    CHECK(ParseItemLinks("|Hitem:abc:0|h[x]|h", 5).empty());
    CHECK(ParseItemLinks("|Hitem:0:0|h[x]|h", 5).empty());
    CHECK(ParseItemLinks("|Hitem:123", 5).empty());
    CHECK(ParseItemLinks("|Hitem:99999999999:0|h", 5).empty());
    CHECK(ParseItemLinks("|Hitem:7|h[x]|h", 5) == std::vector<uint32>{ 7 });
}

TEST_CASE("ParseItemLinks stops at the limit", "[bot][tradelink]")
{
    std::string text;
    for (int i = 1; i <= 8; ++i)
        text += "|Hitem:" + std::to_string(i) + ":0|h[i]|h";
    CHECK(ParseItemLinks(text, 3) == std::vector<uint32>{ 1, 2, 3 });
}

TEST_CASE("PlanTrades picks the bot with the most of an item", "[bot][tradelink]")
{
    std::vector<BotStock> bots = { Stock(1, { { 100, 5 } }), Stock(2, { { 100, 20 } }), Stock(3, { { 100, 20 } }) };
    std::vector<uint32> wanted = { 100 };
    Plan p = PlanTrades(wanted, bots, 6, 3);
    REQUIRE(p.Trades.size() == 1);
    CHECK(p.Trades[0].Bot == 2);   // tie goes to the first bot
    CHECK(p.Trades[0].Items == std::vector<uint32>{ 100 });
    CHECK(p.Skipped.empty());
}

TEST_CASE("PlanTrades ignores bots out of range", "[bot][tradelink]")
{
    std::vector<BotStock> bots = { Stock(1, { { 100, 50 } }, false), Stock(2, { { 100, 1 } }) };
    std::vector<uint32> wanted = { 100 };
    Plan p = PlanTrades(wanted, bots, 6, 3);
    REQUIRE(p.Trades.size() == 1);
    CHECK(p.Trades[0].Bot == 2);
}

TEST_CASE("PlanTrades groups items per bot and skips items nobody has", "[bot][tradelink]")
{
    std::vector<BotStock> bots = { Stock(1, { { 100, 3 }, { 300, 9 } }), Stock(2, { { 200, 4 } }) };
    std::vector<uint32> wanted = { 100, 200, 999, 300 };
    Plan p = PlanTrades(wanted, bots, 6, 3);
    REQUIRE(p.Trades.size() == 2);
    CHECK(p.Trades[0].Bot == 1);
    CHECK(p.Trades[0].Items == std::vector<uint32>{ 100, 300 });
    CHECK(p.Trades[1].Bot == 2);
    REQUIRE(p.Skipped.size() == 1);
    CHECK(p.Skipped[0].Entry == 999);
    CHECK(p.Skipped[0].Why == Skip::NoneHave);
}

TEST_CASE("PlanTrades splits at the slot count and caps the trades", "[bot][tradelink]")
{
    std::vector<Holding> held;
    std::vector<uint32> wanted;
    for (uint32 i = 1; i <= 5; ++i)
    {
        held.push_back({ i, 1 });
        wanted.push_back(i);
    }
    std::vector<BotStock> bots = { Stock(1, held) };

    Plan p = PlanTrades(wanted, bots, 2, 3);
    REQUIRE(p.Trades.size() == 3);
    CHECK(p.Trades[0].Items == std::vector<uint32>{ 1, 2 });
    CHECK(p.Trades[2].Items == std::vector<uint32>{ 5 });
    CHECK(p.Skipped.empty());

    p = PlanTrades(wanted, bots, 2, 2);
    REQUIRE(p.Trades.size() == 2);
    REQUIRE(p.Skipped.size() == 1);
    CHECK(p.Skipped[0].Entry == 5);
    CHECK(p.Skipped[0].Why == Skip::TooMany);
}

TEST_CASE("PlanTrades with no bots plans nothing", "[bot][tradelink]")
{
    std::vector<uint32> wanted = { 1, 2 };
    Plan p = PlanTrades(wanted, {}, 6, 3);
    CHECK(p.Trades.empty());
    CHECK(p.Skipped.size() == 2);
    CHECK(PlanTrades({}, {}, 6, 3).Trades.empty());
}
