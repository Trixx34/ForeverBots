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

TEST_CASE("SortForListing orders by quality, count, entry", "[bot][tradelink]")
{
    std::vector<Holding> h = { { 5, 10, 1 }, { 3, 2, 3 }, { 4, 10, 1 }, { 9, 50, 1 }, { 1, 2, 3 } };
    SortForListing(h);
    std::vector<uint32> order;
    for (Holding const& x : h)
        order.push_back(x.Entry);
    CHECK(order == std::vector<uint32>{ 1, 3, 9, 4, 5 });
}

TEST_CASE("PackLines joins pieces up to the line length", "[bot][tradelink]")
{
    std::vector<std::string> pieces = { "aaaa", "bbbb", "cccc", "dddd" };
    CHECK(PackLines(pieces, 100) == std::vector<std::string>{ "aaaa, bbbb, cccc, dddd" });
    CHECK(PackLines(pieces, 10) == std::vector<std::string>{ "aaaa, bbbb", "cccc, dddd" });
    CHECK(PackLines(pieces, 3) == std::vector<std::string>{ "aaaa", "bbbb", "cccc", "dddd" });   // oversized pieces stay whole
    CHECK(PackLines({}, 10).empty());
}

TEST_CASE("PlanOffer adds held items in order", "[bot][tradelink]")
{
    std::vector<Holding> held = { { 100, 5, 1 }, { 200, 1, 2 } };
    std::vector<uint32> wanted = { 200, 100 };
    OfferPlan p = PlanOffer(wanted, held, {}, 6);
    CHECK(p.Add == std::vector<uint32>{ 200, 100 });
    CHECK(p.Missing.empty());
    CHECK(p.NoRoom.empty());
}

TEST_CASE("PlanOffer reports missing items and skips ones already in the window", "[bot][tradelink]")
{
    std::vector<Holding> held = { { 100, 5, 1 }, { 300, 0, 1 } };
    std::vector<uint32> wanted = { 100, 999, 300, 100 };
    std::vector<uint32> in = { 100 };
    OfferPlan p = PlanOffer(wanted, held, in, 6);
    CHECK(p.Add.empty());
    CHECK(p.Missing == std::vector<uint32>{ 999, 300 });
}

TEST_CASE("PlanOffer stops at the free slot count", "[bot][tradelink]")
{
    std::vector<Holding> held = { { 1, 1, 1 }, { 2, 1, 1 }, { 3, 1, 1 } };
    std::vector<uint32> wanted = { 1, 2, 3 };
    OfferPlan p = PlanOffer(wanted, held, {}, 2);
    CHECK(p.Add == std::vector<uint32>{ 1, 2 });
    CHECK(p.NoRoom == std::vector<uint32>{ 3 });
    p = PlanOffer(wanted, held, {}, 0);
    CHECK(p.Add.empty());
    CHECK(p.NoRoom.size() == 3);
}
