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

#include "BotLootPlan.h"
#include <map>
#include <set>

using namespace BotLoot;

namespace
{
SpawnCandidate Spawn(uint64 id, float straight, bool pooled = false)
{
    SpawnCandidate s;
    s.SpawnId = id;
    s.Straight = straight;
    s.X = straight;
    s.Pooled = pooled;
    return s;
}

// path costs by spawn id; a missing id means no path; counts the queries
struct FakePaths
{
    std::map<uint64, PathCost> Costs;
    mutable uint32 Queries = 0;
    CostFn Fn() const
    {
        return [this](SpawnCandidate const& s)
        {
            ++Queries;
            auto it = Costs.find(s.SpawnId);
            return it == Costs.end() ? PathCost{} : it->second;
        };
    }
};

VetoFn None() { return {}; }
VetoFn Ids(std::set<uint64> ids)
{
    return [ids](SpawnCandidate const& s) { return ids.count(s.SpawnId) != 0; };
}
}

TEST_CASE("BotLoot: spawn choice by path cost", "[BotLoot]")
{
    PickConfig cfg;
    FakePaths paths;

    SECTION("the nearer spawn in a straight line loses when its path is long")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 20.0f), Spawn(2, 40.0f) };
        paths.Costs[1] = { true, false, 140.0f };   // behind a ridge
        paths.Costs[2] = { true, false, 45.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), None());
        CHECK(r.Index == 1);
        CHECK(r.Cost == Catch::Approx(45.0f));
    }

    SECTION("nothing is chosen above the distance limit, straight or by path")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 151.0f), Spawn(2, 100.0f) };
        paths.Costs[1] = { true, false, 160.0f };
        paths.Costs[2] = { true, false, 151.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), None(), true);
        CHECK(r.Index == -1);
        CHECK(r.RejectedFar == 2);
        CHECK(r.Why[0] == Reject::TooFar);
        CHECK(r.Why[1] == Reject::TooLong);
        paths.Costs[2].Length = 150.0f;
        CHECK(PickSpawn(c, cfg, paths.Fn(), None(), None(), None()).Index == 1);   // exactly 150 is fine
    }

    SECTION("deadly areas and the blacklist are skipped before any path is computed")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 10.0f), Spawn(2, 20.0f), Spawn(3, 30.0f) };
        for (uint64 i = 1; i <= 3; ++i)
            paths.Costs[i] = { true, false, float(i) * 10.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), Ids({ 1 }), Ids({ 2 }), None(), true);
        CHECK(r.Index == 2);
        CHECK(r.RejectedDeadly == 1);
        CHECK(r.RejectedBlacklisted == 1);
        CHECK(paths.Queries == 1);
        CHECK(r.Why[0] == Reject::Deadly);
        CHECK(r.Why[1] == Reject::Blacklisted);
    }

    SECTION("the extra filter hook can veto spawns")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 10.0f), Spawn(2, 20.0f) };
        paths.Costs[1] = paths.Costs[2] = { true, false, 15.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), Ids({ 1 }));
        CHECK(r.Index == 1);
        CHECK(r.RejectedFiltered == 1);
    }

    SECTION("failed and partial paths are skipped; the next candidate is tried")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 10.0f), Spawn(2, 20.0f), Spawn(3, 30.0f) };
        paths.Costs[2] = { true, true, 22.0f };
        paths.Costs[3] = { true, false, 35.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), None(), true);
        CHECK(r.Index == 2);
        CHECK(r.Why[0] == Reject::NoPath);
        CHECK(r.Why[1] == Reject::PartialPath);
        CHECK(r.RejectedPath == 2);

        cfg.AllowPartial = true;   // allowed, but it counts 1.6 times longer: 22 * 1.6 = 35.2 loses to 35
        CHECK(PickSpawn(c, cfg, paths.Fn(), None(), None(), None()).Index == 2);
    }

    SECTION("pooled spawns lose a close call")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 20.0f, true), Spawn(2, 21.0f) };
        paths.Costs[1] = { true, false, 22.0f };
        paths.Costs[2] = { true, false, 23.0f };
        CHECK(PickSpawn(c, cfg, paths.Fn(), None(), None(), None()).Index == 1);
    }

    SECTION("a pick costs a bounded number of path queries")
    {
        std::vector<SpawnCandidate> c;
        for (uint64 i = 1; i <= 50; ++i)
            c.push_back(Spawn(i, float(i)));
        // no path to any of them
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), None());
        CHECK(r.Index == -1);
        CHECK(paths.Queries == cfg.MaxProbes);
    }

    SECTION("probing stops once the best cost beats the straight distance of the rest")
    {
        std::vector<SpawnCandidate> c = { Spawn(1, 10.0f), Spawn(2, 30.0f), Spawn(3, 60.0f) };
        paths.Costs[1] = { true, false, 12.0f };
        paths.Costs[2] = { true, false, 40.0f };
        paths.Costs[3] = { true, false, 61.0f };
        PickResult r = PickSpawn(c, cfg, paths.Fn(), None(), None(), None());
        CHECK(r.Index == 0);
        CHECK(paths.Queries == 1);   // the rest are straight 30 and 60 away: they cannot beat a path of 12
    }

    SECTION("no candidates, no crash")
    {
        CHECK(PickSpawn({}, cfg, paths.Fn(), None(), None(), None()).Index == -1);
    }
}

TEST_CASE("BotLoot: deadly areas", "[BotLoot]")
{
    DeadlyAreas areas;
    CHECK_FALSE(areas.IsDeadly(1, 0, 0, 40, 1000, 600000));

    areas.Note(1, 100.0f, 100.0f, 1000);
    CHECK(areas.IsDeadly(1, 120.0f, 100.0f, 40.0f, 2000, 600000));
    CHECK_FALSE(areas.IsDeadly(1, 150.0f, 100.0f, 40.0f, 2000, 600000));   // outside the radius
    CHECK_FALSE(areas.IsDeadly(2, 100.0f, 100.0f, 40.0f, 2000, 600000));   // another map
    CHECK_FALSE(areas.IsDeadly(1, 100.0f, 100.0f, 40.0f, 700000, 600000)); // too long ago

    SECTION("the ring is bounded and replaces the oldest")
    {
        DeadlyAreas small(4);
        for (uint32 i = 0; i < 10; ++i)
            small.Note(1, float(i) * 1000.0f, 0.0f, 1000 + i);
        CHECK(small.Size() == 4);
        CHECK(small.IsDeadly(1, 9000.0f, 0.0f, 10.0f, 2000, 600000));
        CHECK_FALSE(small.IsDeadly(1, 0.0f, 0.0f, 10.0f, 2000, 600000));
    }
}

TEST_CASE("BotLoot: per-bot spawn blacklist", "[BotLoot]")
{
    SpawnBlacklist bl;
    CHECK_FALSE(bl.Blocked(5, 1000));

    SECTION("one no-progress result blocks the spawn")
    {
        uint32 len = bl.Add(5, 1000, 900000);
        CHECK(len == 900000);
        CHECK(bl.Blocked(5, 1000));
        CHECK(bl.Blocked(5, 1000 + len - 1));
        CHECK_FALSE(bl.Blocked(5, 1000 + len + 1));
        CHECK_FALSE(bl.Blocked(6, 1000));
    }

    SECTION("repeats double the time up to the cap")
    {
        CHECK(bl.Add(5, 1000, 900000) == 900000);
        CHECK(bl.Add(5, 2000000, 900000) == 1800000);
        CHECK(bl.Add(5, 5000000, 900000) == 3600000);
        for (int i = 0; i < 10; ++i)
            bl.Add(5, 20000000 + uint32(i), 900000);
        CHECK(bl.Add(5, 30000000, 900000) == 2u * 3600 * 1000);
        CHECK(bl.Strikes(5) >= 4);
        bl.Forget(5);
        CHECK(bl.Strikes(5) == 0);
    }

    SECTION("the list is bounded")
    {
        for (uint64 i = 1; i <= 200; ++i)
            bl.Add(i, uint32(i * 10), 60000);
        CHECK(bl.Size() <= 48);
        CHECK(bl.Blocked(200, 2100));
    }
}

TEST_CASE("BotLoot: bag space", "[BotLoot]")
{
    BagConfig cfg;
    BagFacts f;
    f.FreeSlots = 10;
    CHECK(PlanBags(f, cfg) == BagPlan::Ok);
    f.FreeSlots = 3;
    CHECK(PlanBags(f, cfg) == BagPlan::Ok);
    f.FreeSlots = 2;
    CHECK(PlanBags(f, cfg) == BagPlan::Tight);
    f.FreeSlots = 1;
    CHECK(PlanBags(f, cfg) == BagPlan::Tight);
    f.FreeSlots = 0;
    CHECK(PlanBags(f, cfg) == BagPlan::VendorFirst);
    f.VendorKnown = false;
    CHECK(PlanBags(f, cfg) == BagPlan::Blocked);
}

TEST_CASE("BotLoot: quests that hand over their items (M3)", "[BotLoot]")
{
    SupplyQuest q;
    q.Objectives.push_back({ OBJECTIVE_TYPE_ITEM, 500, 3, 0, false, false });
    q.ItemDrops = { { 500, 3 } };

    SECTION("an item-drop-only objective is supplied in full")
    {
        auto plan = PlanQuestSupply(q);
        REQUIRE(plan.size() == 1);
        CHECK(plan[0].Item == 500);
        CHECK(plan[0].Count == 3);
    }

    SECTION("only the missing amount is given")
    {
        q.Objectives[0].Have = 2;
        auto plan = PlanQuestSupply(q);
        REQUIRE(plan.size() == 1);
        CHECK(plan[0].Count == 1);
        q.Objectives[0].Have = 3;
        CHECK(PlanQuestSupply(q).empty());
    }

    SECTION("a world source, a start item, an optional objective or another type are left alone")
    {
        q.Objectives[0].HasWorldSource = true;
        CHECK(PlanQuestSupply(q).empty());
        q.Objectives[0].HasWorldSource = false;
        q.SrcItemId = 500;
        CHECK(PlanQuestSupply(q).empty());
        q.SrcItemId = 0;
        q.Objectives[0].Optional = true;
        CHECK(PlanQuestSupply(q).empty());
        q.Objectives[0].Optional = false;
        q.Objectives[0].Type = 0;
        CHECK(PlanQuestSupply(q).empty());
    }

    SECTION("an item that is not in the drop list is never invented")
    {
        q.ItemDrops = { { 501, 3 } };
        CHECK(PlanQuestSupply(q).empty());
    }

    SECTION("the grant never exceeds the drop quantity")
    {
        q.Objectives[0].Amount = 5;
        q.ItemDrops = { { 500, 2 } };
        auto plan = PlanQuestSupply(q);
        REQUIRE(plan.size() == 1);
        CHECK(plan[0].Count == 2);
    }

    SECTION("two objectives for different items")
    {
        q.Objectives.push_back({ OBJECTIVE_TYPE_ITEM, 600, 1, 0, false, false });
        q.ItemDrops.emplace_back(600, 1);
        CHECK(PlanQuestSupply(q).size() == 2);
    }
}

TEST_CASE("BotLoot: spawn index filter (M1)", "[BotLoot]")
{
    SpawnFacts f;
    CHECK(ClassifySpawn(f) == SpawnUse::Plain);

    f.Difficulties = { 0 };
    CHECK(ClassifySpawn(f) == SpawnUse::Plain);
    f.Difficulties = { 2, 3 };
    CHECK(ClassifySpawn(f) == SpawnUse::SkipDifficulty);
    f.Difficulties = { 0, 2 };
    CHECK(ClassifySpawn(f) == SpawnUse::Plain);
    f.Difficulties.clear();

    f.PhaseId = 169;
    CHECK(ClassifySpawn(f) == SpawnUse::SkipPhase);
    f.PhaseId = 0;
    f.PhaseGroup = 12;
    CHECK(ClassifySpawn(f) == SpawnUse::SkipPhase);
    f.PhaseGroup = 0;
    f.TerrainSwapMap = 5;
    CHECK(ClassifySpawn(f) == SpawnUse::SkipPhase);
    f.TerrainSwapMap = -1;

    f.PoolId = 77;
    CHECK(ClassifySpawn(f) == SpawnUse::Pooled);
    CHECK(CountsAsSpawn(SpawnUse::Pooled));
    f.PoolId = 0;

    f.ManualSpawnGroup = true;
    CHECK(ClassifySpawn(f) == SpawnUse::SkipSpawnGroup);
    f.ManualSpawnGroup = false;
    f.SystemSpawnGroup = true;
    CHECK(ClassifySpawn(f) == SpawnUse::SkipSpawnGroup);

    CHECK_FALSE(CountsAsSpawn(SpawnUse::SkipPhase));
    CHECK_FALSE(CountsAsSpawn(SpawnUse::SkipDifficulty));
    CHECK_FALSE(CountsAsSpawn(SpawnUse::SkipSpawnGroup));
}

TEST_CASE("BotLoot: chest danger", "[BotLoot]")
{
    DangerConfig cfg;
    cfg.MaxGap = 4;
    cfg.EliteBonus = 3;
    cfg.SpawnRadius = 30.0f;
    cfg.BotRadius = 18.0f;

    CHECK_FALSE(ChestDangerous(std::span<MobFacts const>{}, cfg));

    MobFacts strongAtChest;
    strongAtChest.DistToSpawn = 10.0f;
    strongAtChest.DistToBot = 80.0f;
    strongAtChest.LevelDiff = 4;
    MobFacts const one[] = { strongAtChest };
    CHECK(ChestDangerous(one, cfg));

    MobFacts weakAtChest = strongAtChest;
    weakAtChest.LevelDiff = 3;
    MobFacts const two[] = { weakAtChest };
    CHECK_FALSE(ChestDangerous(two, cfg));

    MobFacts eliteAtChest = weakAtChest;
    eliteAtChest.LevelDiff = 1;
    eliteAtChest.Elite = true;
    MobFacts const three[] = { eliteAtChest };
    CHECK(ChestDangerous(three, cfg));

    MobFacts farAway = strongAtChest;   // strong but neither near the chest nor near the bot
    farAway.DistToSpawn = 60.0f;
    farAway.DistToBot = 60.0f;
    MobFacts const four[] = { farAway };
    CHECK_FALSE(ChestDangerous(four, cfg));

    MobFacts nearBot = farAway;         // strong and close to the bot: it would aggro on the way
    nearBot.DistToBot = 10.0f;
    MobFacts const five[] = { weakAtChest, nearBot };
    CHECK(ChestDangerous(five, cfg));
}

TEST_CASE("BotLoot: shared spawn quarantine", "[BotLoot]")
{
    SpawnQuarantine q;
    constexpr uint32 window = 30 * 60 * 1000, base = 20 * 60 * 1000;

    CHECK_FALSE(q.Note(7, 1000, 3, window, base));
    CHECK_FALSE(q.Note(7, 2000, 3, window, base));
    CHECK_FALSE(q.Quarantined(7, 2500));
    CHECK(q.Note(7, 3000, 3, window, base));     // third strike
    CHECK(q.Quarantined(7, 4000));
    CHECK_FALSE(q.Note(7, 5000, 3, window, base));   // already quarantined: no second report
    CHECK_FALSE(q.Quarantined(7, 3000 + base + 1));
    CHECK_FALSE(q.Quarantined(8, 4000));             // other spawns are untouched

    // a second round doubles the length
    uint32 const t2 = 3000 + base + 10;
    CHECK_FALSE(q.Note(7, t2, 3, window, base));
    CHECK_FALSE(q.Note(7, t2 + 1, 3, window, base));
    CHECK(q.Note(7, t2 + 2, 3, window, base));
    CHECK(q.Quarantined(7, t2 + 2 + base + 5));
    CHECK_FALSE(q.Quarantined(7, t2 + 2 + 2 * base + 5));

    // strikes older than the window do not add up
    CHECK_FALSE(q.Note(9, 0, 2, 1000, 100));
    CHECK_FALSE(q.Note(9, 5000, 2, 1000, 100));
    CHECK_FALSE(q.Quarantined(9, 5001));
    CHECK(q.Note(9, 5100, 2, 1000, 100));

    // a successful loot clears the history
    q.Clear(7);
    CHECK_FALSE(q.Quarantined(7, t2 + 100));
    CHECK_FALSE(q.Note(7, t2 + 100, 3, window, base));
}
