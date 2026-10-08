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

#include "BotDungeonData.h"
#include <algorithm>
#include <vector>

using namespace BotDungeon;

namespace
{
SpawnPoint S(uint64 id, float x, float y, float z = 0.0f, int32 level = 20)
{
    SpawnPoint s;
    s.SpawnId = id; s.Entry = uint32(1000 + id); s.X = x; s.Y = y; s.Z = z; s.Level = level;
    return s;
}
}

TEST_CASE("BotDungeonData: lookup", "[BotDungeonData]")
{
    CHECK(FindDungeon("dm") == FindDungeon("Deadmines"));
    CHECK(FindDungeon("The Deadmines") == FindDungeon("dm"));
    CHECK(FindDungeon("36") == FindDungeon("dm"));
    REQUIRE(FindDungeon("dm"));
    CHECK(FindDungeon("dm")->MapId == 36);
    CHECK(FindDungeon("Zul'Farrak") == FindDungeon("zf"));
    CHECK(FindDungeon("shadowfang-keep") == FindDungeon("sfk"));
    CHECK(FindDungeon("nowhere") == nullptr);
    CHECK(FindDungeon("") == nullptr);
    CHECK(FindDungeon("   ") == nullptr);

    for (DungeonInfo const& d : Dungeons())
    {
        CHECK(d.MinLevel < d.MaxLevel);
        CHECK(FindDungeon(d.Key) == &d);
        CHECK(FindDungeon(d.Name) == &d);
    }
}

TEST_CASE("BotDungeonData: dungeons for a level", "[BotDungeonData]")
{
    CHECK(DungeonsForLevel(5).empty());
    std::vector<DungeonInfo const*> v = DungeonsForLevel(19);
    REQUIRE_FALSE(v.empty());
    for (DungeonInfo const* d : v)
        CHECK((19 >= d->MinLevel && 19 <= d->MaxLevel));
    CHECK(v.front() == FindDungeon("sfk"));          // 14-24 is centred on 19, the closest fit
    CHECK(DungeonsForLevel(14).size() == 2);         // Ragefire Chasm and Shadowfang Keep
    CHECK(DungeonsForLevel(14, 1).size() == 4);      // a slack of one level lets Deadmines and Wailing Caverns in
    CHECK(DungeonsForLevel(90).empty());
}

TEST_CASE("BotDungeonData: pack clustering", "[BotDungeonData]")
{
    SECTION("chains link, distant groups stay apart")
    {
        std::vector<SpawnPoint> v = { S(1, 0, 0), S(2, 8, 0), S(3, 16, 0), S(4, 100, 0), S(5, 105, 0) };
        std::vector<PackSpec> p = ClusterPacks(v, 10.0f, 0);
        REQUIRE(p.size() == 2);
        CHECK(p[0].Mobs == 3);
        CHECK(p[1].Mobs == 2);
        CHECK(p[0].Id == 1);
        CHECK(p[1].Id == 2);
        CHECK(p[0].X == Catch::Approx(8.0f));
    }
    SECTION("empty input")
    {
        CHECK(ClusterPacks({}, 10.0f, 4).empty());
    }
    SECTION("a large cluster is cut into packs of at most maxPack")
    {
        std::vector<SpawnPoint> v;
        for (uint64 i = 0; i < 10; ++i)
            v.push_back(S(i + 1, float(i) * 6.0f, 0));   // one chain of ten, 6 yards apart
        std::vector<PackSpec> p = ClusterPacks(v, 8.0f, 4);
        uint32 total = 0;
        for (PackSpec const& pk : p)
        {
            CHECK(pk.Mobs <= 4);
            CHECK(pk.Mobs >= 1);
            total += pk.Mobs;
        }
        CHECK(total == 10);
        CHECK(p.size() >= 3);
    }
    SECTION("a boss keeps its adds together whatever the size")
    {
        std::vector<SpawnPoint> v;
        for (uint64 i = 0; i < 7; ++i)
            v.push_back(S(i + 1, float(i), 0));
        v[3].Boss = true;
        std::vector<PackSpec> p = ClusterPacks(v, 8.0f, 4);
        REQUIRE(p.size() == 1);
        CHECK(p[0].Boss);
        CHECK(p[0].Mobs == 7);
    }
    SECTION("final boss entry marks the pack")
    {
        std::vector<SpawnPoint> v = { S(1, 0, 0), S(2, 3, 0), S(3, 200, 0) };
        v[2].Entry = 639;
        std::vector<PackSpec> p = ClusterPacks(v, 10.0f, 4, 639);
        REQUIRE(p.size() == 2);
        CHECK_FALSE(p[0].HasFinalBoss);
        CHECK(p[1].HasFinalBoss);
        CHECK(p[1].Boss);
    }
    SECTION("flags and levels are collected")
    {
        std::vector<SpawnPoint> v = { S(1, 0, 0, 0, 20), S(2, 3, 0, 0, 23), S(3, 5, 0, 0, 21) };
        v[1].Elite = true;
        v[2].Patrol = true;
        std::vector<PackSpec> p = ClusterPacks(v, 10.0f, 4);
        REQUIRE(p.size() == 1);
        CHECK(p[0].Elites == 1);
        CHECK(p[0].MaxLevel == 23);
        CHECK(p[0].Patrols);
        CHECK_FALSE(p[0].Boss);
    }
    SECTION("the numbering does not depend on the input order")
    {
        std::vector<SpawnPoint> v = { S(5, 100, 0), S(1, 0, 0), S(3, 50, 0), S(2, 4, 0), S(4, 52, 0) };
        std::vector<PackSpec> a = ClusterPacks(v, 10.0f, 4);
        std::reverse(v.begin(), v.end());
        std::vector<PackSpec> b = ClusterPacks(v, 10.0f, 4);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].Id == b[i].Id);
            CHECK(a[i].Points.front().SpawnId == b[i].Points.front().SpawnId);
            CHECK(a[i].Mobs == b[i].Mobs);
        }
    }
    SECTION("height counts: a floor above is another pack")
    {
        std::vector<SpawnPoint> v = { S(1, 0, 0, 0), S(2, 0, 0, 30) };
        CHECK(ClusterPacks(v, 10.0f, 4).size() == 2);
    }
}
