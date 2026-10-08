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

#include "BotGroupRoles.h"
#include <vector>

using namespace BotGroupRoles;

namespace
{
Ally A(uint64 guid, int32 hp, bool tank = false, uint32 missing = 500)
{
    Ally a;
    a.Guid = guid;
    a.HealthPct = hp;
    a.IsTank = tank;
    a.MissingHealth = hp >= 100 ? 0 : missing;
    return a;
}
Config const cfg;
}

TEST_CASE("BotGroupRoles: who to heal", "[BotGroupRoles]")
{
    SECTION("nobody hurt")
    {
        std::vector<Ally> v = { A(1, 100), A(2, 100, true), A(3, 100) };
        CHECK(PickHealTarget(v, cfg) == -1);
        CHECK_FALSE(AnyoneNeedsHeal(v, cfg));
    }
    SECTION("an emergency comes before the tank")
    {
        std::vector<Ally> v = { A(1, 30), A(2, 50, true), A(3, 60) };
        CHECK(PickHealTarget(v, cfg) == 0);
    }
    SECTION("the tank comes before the others when nobody is in danger")
    {
        std::vector<Ally> v = { A(1, 60), A(2, 85, true), A(3, 70) };
        CHECK(PickHealTarget(v, cfg) == 1);
    }
    SECTION("the tank is healed from 90 percent, the others from 80")
    {
        std::vector<Ally> v = { A(1, 85), A(2, 88, true) };
        CHECK(PickHealTarget(v, cfg) == 1);
        v = { A(1, 85) };
        CHECK(PickHealTarget(v, cfg) == -1);
        v = { A(1, 79) };
        CHECK(PickHealTarget(v, cfg) == 0);
    }
    SECTION("lowest percent wins among equals of rank, then the lower guid")
    {
        std::vector<Ally> v = { A(5, 60), A(4, 50), A(3, 50) };
        CHECK(PickHealTarget(v, cfg) == 2);
    }
    SECTION("dead, out-of-range and unscathed members are skipped")
    {
        std::vector<Ally> v = { A(1, 20), A(2, 30), A(3, 40) };
        v[0].Alive = false;
        v[1].InRange = false;
        CHECK(PickHealTarget(v, cfg) == 2);
        CHECK(AnyoneNeedsHeal(v, cfg));
        v[2].InRange = false;
        CHECK(PickHealTarget(v, cfg) == -1);
        CHECK(AnyoneNeedsHeal(v, cfg));              // still needs one, just cannot be reached
        CHECK_FALSE(AnyoneNeedsHeal({}, cfg));
    }
}

TEST_CASE("BotGroupRoles: which heal", "[BotGroupRoles]")
{
    // quick heal 400 in 1.5 s for 100 mana, big heal 1200 in 3 s for 220 mana, efficient heal 700 in 2.5 s for 90 mana
    std::vector<HealOption> o = { { 1, 400, 100, 1500, true }, { 2, 1200, 220, 3000, true }, { 3, 700, 90, 2500, true } };

    SECTION("emergency takes the most healing per second of cast")
    {
        CHECK(PickHeal(o, 1500, 1000, true) == 1);    // 400/1.5 = 267, 1200/3 = 400 -> big heal
    }
    SECTION("emergency counts only what is missing")
    {
        CHECK(PickHeal(o, 300, 1000, true) == 0);     // 300/1.5 = 200 beats 300/3 and 300/2.5
    }
    SECTION("emergency respects mana and readiness")
    {
        o[1].Ready = false;
        CHECK(PickHeal(o, 1500, 1000, true) == 2);
        CHECK(PickHeal(o, 1500, 50, true) == -1);
    }
    SECTION("normal heal is the cheapest per point that covers the gap")
    {
        CHECK(PickHeal(o, 700, 1000, false) == 2);    // 90/700 = 0.13 vs 220/1200 = 0.18 vs 100/400
        CHECK(PickHeal(o, 1100, 1000, false) == 1);   // only the big heal covers 80 percent
    }
    SECTION("no overhealing by a wide margin")
    {
        CHECK(PickHeal(o, 100, 1000, false) == 0);    // 400 is 4x too much but the smallest that covers it
    }
    SECTION("nothing covers the gap: the largest that fits")
    {
        o[1].Ready = false;
        CHECK(PickHeal(o, 5000, 1000, false) == 2);
        CHECK(PickHeal({}, 100, 1000, false) == -1);
    }
}

TEST_CASE("BotGroupRoles: assist target", "[BotGroupRoles]")
{
    auto F = [](uint64 guid, int32 hp, bool onTank, bool onHealer = false)
    {
        Foe f;
        f.Guid = guid; f.HealthPct = hp; f.OnTank = onTank; f.OnHealer = onHealer;
        return f;
    };

    SECTION("the tank's target first")
    {
        std::vector<Foe> v = { F(1, 40, true), F(2, 90, true), F(3, 20, true) };
        CHECK(PickAssistTarget(v, 2) == 1);
    }
    SECTION("no tank target known: a mob on the tank, lowest health")
    {
        std::vector<Foe> v = { F(1, 40, true), F(2, 90, true), F(3, 20, false) };
        CHECK(PickAssistTarget(v, 0) == 0);
    }
    SECTION("a mob on the healer is peeled when nearly dead")
    {
        std::vector<Foe> v = { F(1, 90, true), F(2, 20, false, true) };
        CHECK(PickAssistTarget(v, 1) == 1);
        v[1].HealthPct = 60;
        CHECK(PickAssistTarget(v, 1) == 0);
    }
    SECTION("a mob on the healer is taken when the tank has no target")
    {
        std::vector<Foe> v = { F(2, 60, false, true) };
        CHECK(PickAssistTarget(v, 0) == 0);
    }
    SECTION("nothing on the tank: lowest health")
    {
        std::vector<Foe> v = { F(1, 80, false), F(2, 30, false), F(3, 30, false) };
        CHECK(PickAssistTarget(v, 0) == 1);
    }
    SECTION("out of range and empty")
    {
        std::vector<Foe> v = { F(1, 80, true) };
        v[0].InRange = false;
        CHECK(PickAssistTarget(v, 1) == -1);
        CHECK(PickAssistTarget({}, 0) == -1);
    }
}

TEST_CASE("BotGroupRoles: taunt target", "[BotGroupRoles]")
{
    auto F = [](uint64 guid, int32 hp, bool onHealer, int32 victimHp = 100, bool victimHealer = false)
    {
        Foe f;
        f.Guid = guid; f.HealthPct = hp; f.OnHealer = onHealer; f.VictimHealthPct = victimHp; f.VictimIsHealer = victimHealer;
        return f;
    };
    SECTION("nothing loose")
    {
        std::vector<Foe> v = { F(1, 80, false), F(2, 50, false) };
        v[0].OnTank = true;
        CHECK(PickTauntTarget(v) == -1);
        CHECK(PickTauntTarget({}) == -1);
    }
    SECTION("a mob on the healer first")
    {
        std::vector<Foe> v = { F(1, 80, true, 20), F(2, 60, true, 90, true) };
        CHECK(PickTauntTarget(v) == 1);
    }
    SECTION("then the weakest victim")
    {
        std::vector<Foe> v = { F(1, 80, true, 70), F(2, 60, true, 40) };
        CHECK(PickTauntTarget(v) == 1);
    }
    SECTION("then the mob with the most health, then the lower guid")
    {
        std::vector<Foe> v = { F(3, 50, true, 50), F(2, 90, true, 50), F(1, 90, true, 50) };
        CHECK(PickTauntTarget(v) == 2);
    }
    SECTION("taunted, out of range and tank-bound mobs are skipped")
    {
        std::vector<Foe> v = { F(1, 80, true), F(2, 80, true), F(3, 80, true) };
        v[0].Taunted = true;
        v[1].InRange = false;
        CHECK(PickTauntTarget(v) == 2);
        v[2].OnTank = true;
        CHECK(PickTauntTarget(v) == -1);
    }
}

TEST_CASE("BotGroupRoles: hold fire", "[BotGroupRoles]")
{
    HoldFacts f;
    f.TankKnown = true;
    f.TankEngaged = true;
    f.SinceMs = 500;

    CHECK(HoldFire(f, 3000));                      // the tank has just started
    f.SinceMs = 3000;
    CHECK_FALSE(HoldFire(f, 3000));                // waited long enough
    f.SinceMs = 500;
    f.MobOnTank = true;
    CHECK(HoldFire(f, 3000));                      // the mob turned to the tank but the lead is short
    f.SinceMs = 1500;
    CHECK_FALSE(HoldFire(f, 3000));
    f.SinceMs = 100;
    f.MobOnTank = false;
    f.MobOnMe = true;
    CHECK_FALSE(HoldFire(f, 3000));                // the mob is on the bot: fight back
    f.MobOnMe = false;
    f.IsTank = true;
    CHECK_FALSE(HoldFire(f, 3000));
    f.IsTank = false;
    f.TankEngaged = false;
    CHECK_FALSE(HoldFire(f, 3000));                // the tank is not fighting this mob
    f.TankEngaged = true;
    f.TankKnown = false;
    CHECK_FALSE(HoldFire(f, 3000));
}
