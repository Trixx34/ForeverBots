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

#include "BotConsumablePlan.h"
#include <vector>

using namespace BotConsumable;

namespace
{
Candidate Pot(uint32 entry, Kind k, uint32 restore, uint32 count = 1)
{
    Candidate c;
    c.Entry = entry;
    c.K = k;
    c.Restore = restore;
    c.Count = count;
    return c;
}

Facts Hurt(int32 hpPct = 20)
{
    Facts f;
    f.InCombat = true;
    f.HpPct = hpPct;
    f.HpMax = 1000;
    f.UsesMana = true;
    f.ManaPct = 80;
    f.ManaMax = 1000;
    return f;
}
}

TEST_CASE("consumables: nothing is drunk out of combat or while healthy", "[BotConsumable]")
{
    std::vector<Candidate> c = { Pot(1, Kind::Health, 500) };
    Facts f = Hurt();
    f.InCombat = false;
    CHECK(Choose(c, f, Config()).Index == -1);
    CHECK(Choose(c, Hurt(60), Config()).Index == -1);
}

TEST_CASE("consumables: the smallest potion that covers the hole wins", "[BotConsumable]")
{
    // 1000 max, 20% left: 800 missing
    std::vector<Candidate> c = { Pot(1, Kind::Health, 300), Pot(2, Kind::Health, 900), Pot(3, Kind::Health, 1500) };
    Pick const p = Choose(c, Hurt(20), Config());
    CHECK(p.Index == 1);
    CHECK(p.K == Kind::Health);
}

TEST_CASE("consumables: when none covers the hole the biggest is used", "[BotConsumable]")
{
    std::vector<Candidate> c = { Pot(1, Kind::Health, 300), Pot(2, Kind::Health, 500) };
    CHECK(Choose(c, Hurt(10), Config()).Index == 1);
}

TEST_CASE("consumables: health comes before mana, mana only for mana users", "[BotConsumable]")
{
    std::vector<Candidate> c = { Pot(1, Kind::Mana, 600), Pot(2, Kind::Health, 600) };
    Facts f = Hurt(20);
    f.ManaPct = 5;
    CHECK(Choose(c, f, Config()).K == Kind::Health);
    f.HpPct = 90;
    Pick p = Choose(c, f, Config());
    CHECK(p.Index == 0);
    CHECK(p.K == Kind::Mana);
    f.UsesMana = false;
    CHECK(Choose(c, f, Config()).Index == -1);
}

TEST_CASE("consumables: a kind on cooldown is skipped, empty stacks are ignored", "[BotConsumable]")
{
    std::vector<Candidate> c = { Pot(1, Kind::Health, 600, 0), Pot(2, Kind::Mana, 600) };
    CHECK(Choose(c, Hurt(10), Config()).Index == -1);
    Facts f = Hurt(90);
    f.ManaPct = 5;
    f.ManaReady = false;
    CHECK(Choose(c, f, Config()).Index == -1);
}
