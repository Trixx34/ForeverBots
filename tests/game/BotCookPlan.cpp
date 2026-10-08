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

#include "BotCookPlan.h"
#include <vector>

using namespace BotCook;

namespace
{
Recipe R(uint32 spell, Kind k, int32 grey, uint32 batches = 3)
{
    Recipe r;
    r.SpellId = spell;
    r.Produces = k;
    r.Grey = grey;
    r.Yellow = grey - 20;
    r.Batches = batches;
    return r;
}

Food F(uint32 entry, uint32 count, Kind k, uint32 req)
{
    Food f;
    f.Entry = entry;
    f.Count = count;
    f.K = k;
    f.ReqLevel = req;
    return f;
}
}

TEST_CASE("Needed: buff food first, then plain, then nothing", "[bots][cook]")
{
    Config cfg;
    cfg.StockBuff = 10;
    cfg.StockPlain = 20;
    CHECK(Needed({ 0, 0 }, cfg) == Kind::Buff);
    CHECK(Needed({ 0, 9 }, cfg) == Kind::Buff);
    CHECK(Needed({ 0, 10 }, cfg) == Kind::Plain);
    CHECK(Needed({ 19, 10 }, cfg) == Kind::Plain);
    CHECK(Needed({ 20, 10 }, cfg) == Kind::None);
    cfg.StockBuff = 0;
    CHECK(Needed({ 5, 0 }, cfg) == Kind::Plain);
}

TEST_CASE("PickStock: nothing to do when stocked or no recipe", "[bots][cook]")
{
    Config cfg;
    std::vector<Recipe> rec = { R(1, Kind::Plain, 100) };
    CHECK(PickStock(50, { 20, 10 }, cfg, rec) == -1);
    CHECK(PickStock(50, { 0, 0 }, cfg, std::vector<Recipe>{}) == -1);
    std::vector<Recipe> none = { R(1, Kind::Plain, 100, 0) };
    CHECK(PickStock(50, { 0, 10 }, cfg, none) == -1);
}

TEST_CASE("PickStock: the needed kind wins, with a fallback to the other unmet kind", "[bots][cook]")
{
    Config cfg;
    std::vector<Recipe> rec = { R(1, Kind::Plain, 100), R(2, Kind::Buff, 150) };
    CHECK(PickStock(50, { 0, 0 }, cfg, rec) == 1);
    CHECK(PickStock(50, { 0, 10 }, cfg, rec) == 0);
    // buff is needed but only plain can be made
    std::vector<Recipe> plainOnly = { R(1, Kind::Plain, 100), R(2, Kind::Buff, 150, 0) };
    CHECK(PickStock(50, { 0, 0 }, cfg, plainOnly) == 0);
    // buff is needed, only plain can be made, but plain is stocked: nothing
    CHECK(PickStock(50, { 20, 0 }, cfg, plainOnly) == -1);
}

TEST_CASE("PickStock: recipes that still raise the skill come first, then better food", "[bots][cook]")
{
    Config cfg;
    cfg.StockBuff = 0;
    // skill 120: spell 1 is grey (100), 2 and 3 still raise the skill
    std::vector<Recipe> rec = { R(1, Kind::Plain, 100), R(2, Kind::Plain, 130), R(3, Kind::Plain, 160) };
    CHECK(PickStock(120, { 0, 0 }, cfg, rec) == 2);
    // all grey: the best food (highest threshold) is made
    CHECK(PickStock(200, { 0, 0 }, cfg, rec) == 2);
    // a tie on the threshold goes to the lower spell id
    std::vector<Recipe> tie = { R(9, Kind::Plain, 130), R(4, Kind::Plain, 130) };
    CHECK(PickStock(100, { 0, 0 }, cfg, tie) == 1);
}

TEST_CASE("BagsTooFull compares with the configured minimum", "[bots][cook]")
{
    Config cfg;
    cfg.MinFreeSlots = 4;
    CHECK(BagsTooFull(3, cfg));
    CHECK_FALSE(BagsTooFull(4, cfg));
}

TEST_CASE("OfferOrder: usable food only, buff first, better and larger first", "[bots][cook]")
{
    std::vector<Food> held = {
        F(10, 20, Kind::Plain, 5), F(11, 5, Kind::Buff, 15), F(12, 40, Kind::Drink, 1), F(13, 8, Kind::Plain, 25),
        F(14, 6, Kind::Buff, 25), F(15, 0, Kind::Plain, 1), F(16, 30, Kind::Plain, 5) };
    std::vector<size_t> const order = OfferOrder(held, 20, 10);
    // level 20 player: entries 13 and 14 need level 25, entry 15 is empty
    REQUIRE(order.size() == 4);
    CHECK(held[order[0]].Entry == 11);   // buff food
    CHECK(held[order[1]].Entry == 16);   // plain, same level as 10 but the larger stack
    CHECK(held[order[2]].Entry == 10);
    CHECK(held[order[3]].Entry == 12);   // drink last
}

TEST_CASE("OfferOrder: respects the list limit and handles an empty bag", "[bots][cook]")
{
    std::vector<Food> held = { F(1, 1, Kind::Plain, 1), F(2, 1, Kind::Plain, 2), F(3, 1, Kind::Plain, 3) };
    CHECK(OfferOrder(held, 80, 2).size() == 2);
    CHECK(OfferOrder(held, 80, 2)[0] == 2);   // highest required level first
    CHECK(OfferOrder(std::vector<Food>{}, 80, 5).empty());
}

TEST_CASE("CountOf sums one kind", "[bots][cook]")
{
    std::vector<Food> held = { F(1, 4, Kind::Plain, 1), F(2, 6, Kind::Plain, 1), F(3, 2, Kind::Buff, 1) };
    CHECK(CountOf(held, Kind::Plain) == 10);
    CHECK(CountOf(held, Kind::Buff) == 2);
    CHECK(CountOf(held, Kind::Drink) == 0);
}
