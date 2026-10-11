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

#include "BotCraftOrder.h"

using namespace BotCraftOrder;

TEST_CASE("ParseRequest reads a link with a count", "[bot][craft]")
{
    Request r = ParseRequest("craft |cff1eff00|Hitem:4240:0:0:0:0:0:0:0:60|h[Woolen Bag]|h|r x3", 20);
    CHECK(r.Type == Kind::Craft);
    CHECK(r.ItemEntry == 4240);
    CHECK(r.Count == 3);
    CHECK(r.Name.empty());
}

TEST_CASE("ParseRequest reads names and counts at either end", "[bot][craft]")
{
    Request a = ParseRequest("Craft linen bandage 5", 20);
    CHECK(a.Type == Kind::Craft);
    CHECK(a.Name == "linen bandage");
    CHECK(a.Count == 5);
    Request b = ParseRequest("  craft 2x Minor Healing Potion", 20);
    CHECK(b.Name == "minor healing potion");
    CHECK(b.Count == 2);
    Request c = ParseRequest("craft bag", 20);
    CHECK(c.Name == "bag");
    CHECK(c.Count == 1);
}

TEST_CASE("ParseRequest clamps the count", "[bot][craft]")
{
    CHECK(ParseRequest("craft bandage x500", 20).Count == 20);
    CHECK(ParseRequest("craft bandage x0", 20).Count == 1);
    CHECK(ParseRequest("craft bandage x99999999999", 20).Count == 1);   // too long to be a count: part of the name
}

TEST_CASE("ParseRequest list and non orders", "[bot][craft]")
{
    CHECK(ParseRequest("craft list", 20).Type == Kind::List);
    CHECK(ParseRequest("CRAFT LIST", 20).Type == Kind::List);
    CHECK(ParseRequest("craft", 20).Type == Kind::None);
    CHECK(ParseRequest("craft 5", 20).Type == Kind::None);
    CHECK(ParseRequest("crafting bags", 20).Type == Kind::None);
    CHECK(ParseRequest("follow me", 20).Type == Kind::None);
    CHECK(ParseRequest("", 20).Type == Kind::None);
    // a link plus the word list is an item, not a list
    CHECK(ParseRequest("craft |Hitem:7|h[list]|h", 20).Type == Kind::Craft);
}

TEST_CASE("Match prefers link, exact, prefix, then substring", "[bot][craft]")
{
    std::vector<Option> const opts = {
        { 10, 100, 1, "linen bandage" }, { 11, 101, 1, "heavy linen bandage" }, { 12, 102, 1, "bolt of linen cloth" }, { 13, 100, 1, "linen bandage" } };
    Request link; link.Type = Kind::Craft; link.ItemEntry = 101;
    CHECK(Match(link, opts) == 1);
    link.ItemEntry = 999;
    CHECK(Match(link, opts) == -1);
    link.ItemEntry = 100;
    CHECK(Match(link, opts) == 0);   // lower spell id on a duplicate

    Request name; name.Type = Kind::Craft; name.Name = "linen bandage";
    CHECK(Match(name, opts) == 0);
    name.Name = "linen";
    CHECK(Match(name, opts) == 0);   // prefix beats substring, shorter name first
    name.Name = "bandage";
    CHECK(Match(name, opts) == 0);   // substring, shorter name first
    name.Name = "nothing";
    CHECK(Match(name, opts) == -1);
}

TEST_CASE("PlanOrder rounds casts up and limits by reagents and caps", "[bot][craft]")
{
    Facts f; f.Wanted = 5; f.Yield = 1; f.MaxItems = 20; f.MaxStack = 20; f.CastsPossible = 10;
    Plan p = PlanOrder(f);
    CHECK(p.Casts == 5); CHECK(p.Items == 5); CHECK_FALSE(p.Trimmed); CHECK(p.Refusal == nullptr);

    f.Yield = 2; f.Wanted = 5;   // 3 casts make 6
    p = PlanOrder(f);
    CHECK(p.Casts == 3); CHECK(p.Items == 6); CHECK_FALSE(p.Trimmed);

    f.CastsPossible = 2;
    p = PlanOrder(f);
    CHECK(p.Casts == 2); CHECK(p.Items == 4); CHECK(p.Trimmed);

    f.CastsPossible = 100; f.Wanted = 100; f.MaxItems = 20;
    p = PlanOrder(f);
    CHECK(p.Items == 20); CHECK(p.Trimmed);

    f.MaxItems = 200; f.MaxStack = 1; f.Yield = 1;   // 12 attachment slots, one item each
    p = PlanOrder(f);
    CHECK(p.Items == 12);
}

TEST_CASE("PlanOrder refusals", "[bot][craft]")
{
    Facts f; f.Wanted = 3; f.MaxItems = 20; f.CastsPossible = 0;
    CHECK(std::string(PlanOrder(f).Refusal) == "NO_REAGENTS");
    f.CastsPossible = 3; f.MaxItems = 0;
    CHECK(std::string(PlanOrder(f).Refusal) == "TOO_MANY");
}

TEST_CASE("MailStacks splits into full stacks and a rest", "[bot][craft]")
{
    CHECK(MailStacks(45, 20) == std::vector<uint32>{ 20, 20, 5 });
    CHECK(MailStacks(40, 20) == std::vector<uint32>{ 20, 20 });
    CHECK(MailStacks(3, 1) == std::vector<uint32>{ 1, 1, 1 });
    CHECK(MailStacks(0, 20).empty());
    CHECK(MailStacks(5, 0) == std::vector<uint32>{ 1, 1, 1, 1, 1 });
}

TEST_CASE("Fee is a percent of the vendor price and saturates", "[bot][craft]")
{
    CHECK(Fee(100, 5, 0) == 0);
    CHECK(Fee(100, 5, 100) == 500);
    CHECK(Fee(100, 5, 150) == 750);
    CHECK(Fee(4000000000u, 200, 1000) == 4294967295u);
}

TEST_CASE("FormatMoney", "[bot][craft]")
{
    CHECK(FormatMoney(0) == "0 copper");
    CHECK(FormatMoney(5) == "5 copper");
    CHECK(FormatMoney(10305) == "1 gold 3 silver 5 copper");
    CHECK(FormatMoney(20000) == "2 gold");
}
