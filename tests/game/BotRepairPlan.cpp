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

#include "BotRepairPlan.h"

#include <vector>

using namespace BotRepair;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    return c;
}

std::vector<Product> Potions()
{
    return { { 118, PotionKind::Health, 1, 25 }, { 858, PotionKind::Health, 3, 100 }, { 929, PotionKind::Health, 12, 400 },
             { 2455, PotionKind::Mana, 5, 100 }, { 3385, PotionKind::Mana, 14, 300 } };
}
}

TEST_CASE("repair: disabled or healthy gear is left alone", "[bot][repair]")
{
    RepairFacts f;
    f.LowestPct = 10;
    f.Cost = 500;
    f.Gold = 10000;
    CHECK(DecideRepair(f, Config()).Kind == RepairKind::None);
    f.LowestPct = 90;
    CHECK(DecideRepair(f, On()).Kind == RepairKind::None);
    f.LowestPct = 10;
    f.Cost = 0;
    CHECK(DecideRepair(f, On()).Kind == RepairKind::None);
}

TEST_CASE("repair: paid when affordable, with the cost", "[bot][repair]")
{
    RepairFacts f;
    f.LowestPct = 40;
    f.Cost = 500;
    f.Gold = 500;
    RepairPick p = DecideRepair(f, On());
    CHECK(p.Kind == RepairKind::Paid);
    CHECK(p.Spend == 500);
}

TEST_CASE("repair: a bot that cannot pay stays broken or is repaired free by the setting", "[bot][repair]")
{
    RepairFacts f;
    f.LowestPct = 40;
    f.Cost = 500;
    f.Gold = 499;
    CHECK(DecideRepair(f, On()).Kind == RepairKind::TooPoor);
    Config c = On();
    c.FreeWhenBroke = true;
    RepairPick p = DecideRepair(f, c);
    CHECK(p.Kind == RepairKind::Free);
    CHECK(p.Spend == 0);
}

TEST_CASE("repair: free mode never charges", "[bot][repair]")
{
    Config c = On();
    c.Paid = false;
    RepairFacts f;
    f.LowestPct = 40;
    f.Cost = 500;
    f.Gold = 0;
    RepairPick p = DecideRepair(f, c);
    CHECK(p.Kind == RepairKind::Free);
    CHECK(p.Spend == 0);
}

TEST_CASE("repair: the spend cap is a share of the gold", "[bot][repair]")
{
    Config c = On();
    c.MaxSpendPct = 60;
    CHECK(SpendCap(10000, c) == 6000);
    CHECK(SpendCap(0, c) == 0);
    c.MaxSpendPct = 0;
    CHECK(SpendCap(10000, c) == 0);
    c.MaxSpendPct = 250;   // clamped
    CHECK(SpendCap(10000, c) == 10000);
}

TEST_CASE("supplies: the best usable potion of each kind up to the target", "[bot][repair]")
{
    Config c = On();
    SupplyFacts f;
    f.Level = 13;
    f.UsesMana = true;
    f.Budget = 100000;
    std::vector<Product> products = Potions();
    std::vector<Purchase> buy = PlanSupplies(products, f, c);
    REQUIRE(buy.size() == 2);
    CHECK(buy[0].Entry == 929);
    CHECK(buy[0].Count == c.PotionsPerKind);
    CHECK(buy[0].Cost == 5u * 400);
    CHECK(buy[1].Entry == 2455);   // 3385 needs level 14
    CHECK(buy[1].Count == c.PotionsPerKind);
}

TEST_CASE("supplies: owned potions count against the target", "[bot][repair]")
{
    Config c = On();
    SupplyFacts f;
    f.Level = 20;
    f.UsesMana = true;
    f.Budget = 100000;
    f.OwnedHealth = 3;
    f.OwnedMana = c.PotionsPerKind;
    std::vector<Product> products = Potions();
    std::vector<Purchase> buy = PlanSupplies(products, f, c);
    REQUIRE(buy.size() == 1);
    CHECK(buy[0].Entry == 929);
    CHECK(buy[0].Count == c.PotionsPerKind - 3);
}

TEST_CASE("supplies: no mana potions for a bot without mana", "[bot][repair]")
{
    SupplyFacts f;
    f.Level = 20;
    f.UsesMana = false;
    f.Budget = 100000;
    std::vector<Product> products = Potions();
    std::vector<Purchase> buy = PlanSupplies(products, f, On());
    REQUIRE(buy.size() == 1);
    CHECK(buy[0].Entry == 929);
}

TEST_CASE("supplies: the budget limits the count, health first", "[bot][repair]")
{
    SupplyFacts f;
    f.Level = 13;
    f.UsesMana = true;
    f.Budget = 1000;   // two health potions at 400, then 200 left: two mana potions at 100
    std::vector<Product> products = Potions();
    std::vector<Purchase> buy = PlanSupplies(products, f, On());
    REQUIRE(buy.size() == 2);
    CHECK(buy[0].Count == 2);
    CHECK(buy[0].Cost == 800);
    CHECK(buy[1].Count == 2);
    f.Budget = 399;
    buy = PlanSupplies(products, f, On());
    REQUIRE(buy.size() == 1);
    CHECK(buy[0].Entry == 2455);
    CHECK(buy[0].Count == 3);
}

TEST_CASE("supplies: off by setting or nothing usable", "[bot][repair]")
{
    SupplyFacts f;
    f.Level = 1;
    f.Budget = 100000;
    std::vector<Product> products = { { 858, PotionKind::Health, 3, 100 } };
    CHECK(PlanSupplies(products, f, On()).empty());
    f.Level = 5;
    Config c = On();
    c.Supplies = false;
    CHECK(PlanSupplies(products, f, c).empty());
    c = On();
    c.PotionsPerKind = 0;
    CHECK(PlanSupplies(products, f, c).empty());
}
