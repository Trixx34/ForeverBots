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

#ifndef TRINITY_BOT_LOOT_PLAN_H
#define TRINITY_BOT_LOOT_PLAN_H

// Decision logic of the improved loot task (Bot.AI.Loot.Improved.*, used by BotQuest.cpp): spawn choice by path cost, deadly-area
// and distance filters, per-bot spawn blacklist, bag-space planning, supply of item-drop quests, and the spawn filter of the quest
// index. Pure functions over plain data; the game queries (path cost, deaths) come in as callbacks or arguments.
// See docs/playerbots/feature-bot-pets-movement-loot-20261008.md.

#include "Define.h"
#include <functional>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace BotLoot
{
    // ---------------------------------------------------------------------------------------------------------------------
    // spawn choice
    // ---------------------------------------------------------------------------------------------------------------------
    struct SpawnCandidate
    {
        uint64 SpawnId = 0;
        float X = 0.0f, Y = 0.0f, Z = 0.0f;
        float Straight = 0.0f;       // 2D distance from the bot
        bool Pooled = false;         // member of a spawn pool: it may simply not be spawned right now
    };

    struct PathCost
    {
        bool Ok = false;             // a path exists
        bool Partial = false;        // it only gets near the goal
        float Length = 0.0f;         // yards
    };

    enum class Reject : uint8 { None, TooFar, Deadly, Blacklisted, Filtered, NoPath, PartialPath, TooLong, NotProbed };

    struct PickConfig
    {
        float MaxYards = 150.0f;         // spawns farther than this (straight line or by path) are skipped
        uint32 MaxProbes = 4;            // path queries per pick; the straight-line nearest ones are probed first
        float PartialPenalty = 1.6f;     // a partial path counts this much longer
        bool AllowPartial = false;       // false: partial paths are rejected
        float PooledPenalty = 1.15f;     // pooled spawns are less likely to be there
    };

    struct PickResult
    {
        int32 Index = -1;                // into the candidate span, -1 = none
        float Cost = 0.0f;               // path length times penalties of the chosen spawn
        uint32 Probes = 0;
        uint32 RejectedFar = 0, RejectedDeadly = 0, RejectedBlacklisted = 0, RejectedFiltered = 0, RejectedPath = 0;
        std::vector<Reject> Why;         // per candidate; empty unless asked for
    };

    using CostFn = std::function<PathCost(SpawnCandidate const&)>;
    using VetoFn = std::function<bool(SpawnCandidate const&)>;   // true = skip this spawn

    // Chooses the spawn with the lowest path cost. Order of checks: straight-line distance, deadly area, blacklist, extra filter (the
    // hook other systems plug into, e.g. the danger gating of under-level bots), then the nearest `MaxProbes` survivors are probed
    // for a path; the best cost under MaxYards wins. A failed probe does not use up the budget of the next candidate beyond MaxProbes
    // in total, so a pick costs a bounded number of path queries.
    TC_GAME_API PickResult PickSpawn(std::span<SpawnCandidate const> candidates, PickConfig const& cfg, CostFn const& cost,
        VetoFn const& deadly, VetoFn const& blacklisted, VetoFn const& filter, bool wantReasons = false);

    // ---------------------------------------------------------------------------------------------------------------------
    // recently deadly areas (shared by all bots, filled from bot deaths)
    // ---------------------------------------------------------------------------------------------------------------------
    class TC_GAME_API DeadlyAreas
    {
    public:
        explicit DeadlyAreas(uint32 capacity = 128) : _cap(capacity) { }
        void Note(uint32 mapId, float x, float y, uint32 nowMs);
        // a death within `radius` yards of (x,y) on that map in the last `windowMs`
        bool IsDeadly(uint32 mapId, float x, float y, float radius, uint32 nowMs, uint32 windowMs) const;
        size_t Size() const;
        void Clear();
    private:
        struct Rec { uint32 Map; float X, Y; uint32 Ms; };
        uint32 _cap;
        mutable std::mutex _mx;
        std::vector<Rec> _recs;
        uint32 _next = 0;
    };

    // ---------------------------------------------------------------------------------------------------------------------
    // per-bot spawn blacklist
    // ---------------------------------------------------------------------------------------------------------------------
    class TC_GAME_API SpawnBlacklist
    {
    public:
        // Blocks a spawn for baseMs; every repeat doubles it (cap maxMs). Returns the length actually used.
        uint32 Add(uint64 spawnId, uint32 nowMs, uint32 baseMs, uint32 maxMs = 2 * 3600 * 1000);
        bool Blocked(uint64 spawnId, uint32 nowMs) const;
        uint32 Strikes(uint64 spawnId) const;
        void Forget(uint64 spawnId);
        void Prune(uint32 nowMs);
        void Clear() { _e.clear(); }
        size_t Size() const { return _e.size(); }
    private:
        struct Entry { uint64 Id; uint32 UntilMs; uint32 Strikes; uint32 LastMs; };
        static constexpr size_t CAP = 48;
        std::vector<Entry> _e;
    };

    // ---------------------------------------------------------------------------------------------------------------------
    // bag space
    // ---------------------------------------------------------------------------------------------------------------------
    enum class BagPlan : uint8
    {
        Ok,           // enough room
        Tight,        // loot, then plan a vendor trip (free slots are low)
        VendorFirst,  // no room: go to a vendor before looting
        Blocked       // no room and no vendor known: skip loot tasks
    };
    struct BagFacts
    {
        uint32 FreeSlots = 0;
        bool VendorKnown = true;
        bool InCombat = false;
    };
    struct BagConfig
    {
        uint32 NeedFree = 1;        // slots needed for the item about to be taken
        uint32 TripBelow = 2;       // plan a trip when the free slots after the loot would be fewer than this
    };
    TC_GAME_API BagPlan PlanBags(BagFacts const& f, BagConfig const& cfg);

    // ---------------------------------------------------------------------------------------------------------------------
    // quests that hand over their items (the ItemDrop list) but were never supplied (review item M3)
    // ---------------------------------------------------------------------------------------------------------------------
    struct SupplyObjective
    {
        int32 Type = 0;               // QuestObjectiveType; 1 = item
        int32 ObjectId = 0;
        int32 Amount = 0;
        int32 Have = 0;               // items in the bags / counted by the quest log
        bool Optional = false;
        bool HasWorldSource = false;  // a creature or chest drops it (the bot loots it normally)
    };
    struct SupplyQuest
    {
        uint32 SrcItemId = 0;         // handed over by the core at accept
        std::vector<std::pair<uint32, uint32>> ItemDrops;   // (item, quantity) of the quest
        std::vector<SupplyObjective> Objectives;
    };
    struct SupplyItem
    {
        uint32 Item = 0;
        uint32 Count = 0;
    };
    constexpr int32 OBJECTIVE_TYPE_ITEM = 1;
    // Items the bot must be given: item objectives that only the quest itself supplies (ItemDrop, no start item, no world source) and
    // that are not complete yet. Count = what is missing, never more than the drop list gives.
    TC_GAME_API std::vector<SupplyItem> PlanQuestSupply(SupplyQuest const& q);

    // ---------------------------------------------------------------------------------------------------------------------
    // quest index spawn filter (review item M1)
    // ---------------------------------------------------------------------------------------------------------------------
    struct SpawnFacts
    {
        uint32 PhaseId = 0;
        uint32 PhaseGroup = 0;
        int32 TerrainSwapMap = -1;
        uint32 PoolId = 0;
        bool ManualSpawnGroup = false;   // the spawn group is spawned by a script only
        bool SystemSpawnGroup = false;
        std::vector<int32> Difficulties; // spawn difficulties; empty = default
    };
    enum class SpawnUse : uint8
    {
        Plain,             // counts as a spawn
        Pooled,            // counts, but only some members of its pool exist at a time
        SkipPhase,         // visible only in a phase the bots are not in
        SkipDifficulty,    // spawns only on another difficulty
        SkipSpawnGroup     // manual / system spawn group
    };
    TC_GAME_API SpawnUse ClassifySpawn(SpawnFacts const& f);
    inline bool CountsAsSpawn(SpawnUse u) { return u == SpawnUse::Plain || u == SpawnUse::Pooled; }
}

#endif
