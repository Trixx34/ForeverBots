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

// Bot questing, see BotQuest.h. Layout:
//   1. Index        static data built once on the world thread (starter grid, spawns, credit map, quest-item sources)
//   2. Analysis     static "can a bot ever do this quest" check -> stable blocker code
//   3. Ctx          per-bot task state (never recalculated, a plain container like combat_ctx)
//   4. Actions      quest_think: accept / travel / kill / loot / talk / turn-in
//   5. Registration "quest" strategy for the NonCombat engine (add it to Bot.AI.Default.NonCombat or with `bot strategy`)

#include "Bag.h"
#include "BotQuest.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootItemType.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include "Trainer.h"
#include "Util.h"
#include "WorldSession.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Trinity::StringFormat;

namespace BotQuest
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------------------------------------------------
struct QuestCfg
{
    uint32 MaxActive = 12;
    uint32 StallSec = 240;
    int32 MaxMobLevelDiff = 2;     // kill targets more than this many levels above the bot (elite counts +EliteBonus) are skipped
    int32 EliteBonus = 3;
    float SelectRadius = 350.0f;
    float FarRadius = 1500.0f;
    uint32 SelBlockCap = 40;       // quest_blocked rows from quest selection per bot
    float PullMelee = 3.5f;
    float PullRanged = 24.0f;
    float ScanRange = 70.0f;
    uint32 GrindSec = 90;          // one grind stint before the bot re-checks for quests
    uint32 HubBlackSec = 600;      // an unreachable hub is skipped for this long
    uint32 HubScanSec = 20;        // minimum time between hub scans of one bot
    int32 GrindMaxGap = 1;         // Bot.AI.Grind.MaxLevelGap: grind targets at most this many (effective) levels above the bot
    float GrindMaxRadius = 150.0f; // Bot.AI.Grind.MaxRadius: grind targets stay this close to the grind anchor, 0 = off
    int32 FleeMode = 0;            // Bot.AI.Flee.Mode (0 current, 1 aggro avoidance, 2 flee to guard); here: mode >= 1 avoids gap >= 2 mobs
};

QuestCfg const& Cfg()
{
    static QuestCfg cfg;
    static std::once_flag once;
    std::call_once(once, []()
    {
        cfg.MaxActive = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Quest.MaxActive", 12)));
        cfg.StallSec = uint32(std::max<int32>(30, sConfigMgr->GetIntDefault("Bot.Quest.StallSec", 240)));
        cfg.MaxMobLevelDiff = sConfigMgr->GetIntDefault("Bot.Quest.MaxMobLevelDiff", 2);
        cfg.EliteBonus = sConfigMgr->GetIntDefault("Bot.AI.Combat.EliteLevelBonus", 3);
        cfg.SelectRadius = float(sConfigMgr->GetIntDefault("Bot.Quest.SelectRadius", 350));
        cfg.FarRadius = float(sConfigMgr->GetIntDefault("Bot.Quest.FarRadius", 1500));
        cfg.GrindSec = uint32(std::max<int32>(20, sConfigMgr->GetIntDefault("Bot.Quest.GrindSec", 90)));
        cfg.HubBlackSec = uint32(std::max<int32>(30, sConfigMgr->GetIntDefault("Bot.Quest.HubBlacklistSec", 600)));
        cfg.HubScanSec = uint32(std::max<int32>(5, sConfigMgr->GetIntDefault("Bot.Quest.HubScanSec", 20)));
        cfg.SelBlockCap = uint32(std::max<int32>(0, sConfigMgr->GetIntDefault("Bot.Quest.SelectionBlockLogCap", 40)));
        cfg.GrindMaxGap = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Grind.MaxLevelGap", 1), -5, 10);
        cfg.GrindMaxRadius = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Grind.MaxRadius", 150), 0, 2000));
        cfg.FleeMode = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Flee.Mode", 0), 0, 2);
    });
    return cfg;
}

// ---------------------------------------------------------------------------------------------------------------------
// 1. Index
// ---------------------------------------------------------------------------------------------------------------------
struct Pt
{
    uint32 Map;
    float X, Y, Z;
};

struct StarterRef
{
    uint32 Quest;
    uint32 Entry;
    Pt P;
    int32 QLevel;                  // classic quest level, 0 = unknown (content tuning)
};

// A quest hub: all quest givers of one 200 yd grid cell (E3). Built once at startup, read-only afterwards.
struct Hub
{
    uint32 Map = 0;
    float X = 0, Y = 0, Z = 0;     // mean of the giver points
    int32 MinQ = 0, MaxQ = 0;      // min/max known quest level of the starters (0 = none known)
    uint32 Givers = 0;             // distinct giver creatures
    std::vector<StarterRef> Refs;
};

// a spawned game object (chest-type loot source for quest items)
struct GoPt
{
    Pt P;
    uint64 SpawnId;
    uint32 RespawnSec;
    uint32 Entry;
};

struct GrindPt
{
    float X, Y, Z;
    uint32 Entry;
};

// E1/E4: NPC service points (class trainers, vendors), indexed once at startup
struct SvcPt
{
    uint32 Map = 0;
    float X = 0, Y = 0, Z = 0;
    uint32 Entry = 0;
    uint32 Faction = 0;
    uint32 TrainerId = 0;     // trainer.Id for trainers, 0 for vendors
    bool Repair = false;      // vendor can repair
};

struct BagOffer
{
    uint32 Item = 0;
    uint32 VendorSlot = 0;
    uint32 Slots = 0;
    uint32 Price = 0;
    int32 ReqLevel = 0;
};

constexpr float GRID_CELL = 200.0f;
constexpr uint32 MAX_STARTER_POINTS = 8;

struct Index
{
    std::atomic<bool> Ready{false};
    std::unordered_map<uint32, std::vector<Pt>> Spawns;            // creature entry -> spawn points (only quest-relevant entries)
    std::unordered_map<uint32, std::vector<uint32>> CreditSrc;     // kill credit entry -> creature entries that grant it
    std::unordered_map<uint32, std::vector<uint32>> ItemSrc;       // quest item -> creature entries that drop it (QuestRequired rows)
    std::unordered_map<uint32, std::vector<uint32>> ItemGoSrc;     // quest item -> chest game object entries that hold it (QuestRequired rows)
    std::unordered_map<uint32, std::vector<GoPt>> GoSpawns;        // game object entry -> spawns (only the entries in ItemGoSrc; own id space, never mixed with Spawns)
    std::unordered_map<uint64, std::vector<StarterRef>> Grid;      // (map, cell) -> starters with at least one spawn
    std::unordered_map<uint32, std::vector<uint32>> StartsBy;      // creature entry -> quests it starts
    std::unordered_map<uint32, std::vector<uint32>> EndsBy;        // creature entry -> quests it ends
    std::vector<Hub> Hubs;
    std::unordered_map<uint64, uint32> HubByCell;                  // (map, cell) -> index in Hubs
    std::unordered_map<uint64, std::vector<GrindPt>> GrindGrid;    // (map, cell) -> hostile normal-rank spawns (grind fallback)
    uint32 NumGrind = 0;
    // trainers by key: class id (1..11). Profession trainers would use 0x100 | skill line (not indexed in v1).
    std::unordered_map<uint32, std::vector<SvcPt>> Trainers;
    std::vector<SvcPt> Vendors;
    std::unordered_map<uint32, std::vector<BagOffer>> VendorBags;   // vendor entry -> plain bags it sells for gold
    uint32 NumTrainers = 0, NumBagVendors = 0, MinBagPrice = 0xFFFFFFFFu;
    uint32 NumStarterQuests = 0, NumStarterPoints = 0, NumSpawnEntries = 0, NumItemSources = 0, NumGoItems = 0, NumGoSpawns = 0;
} g;

uint64 CellKey(uint32 map, float x, float y)
{
    int32 cx = int32(std::floor(x / GRID_CELL)) + 512;
    int32 cy = int32(std::floor(y / GRID_CELL)) + 512;
    return (uint64(map) << 40) | (uint64(uint32(cx) & 0xFFFFF) << 20) | uint64(uint32(cy) & 0xFFFFF);
}

float Dist2D(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2, dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<uint32> const* SpawnEntriesOf(std::unordered_map<uint32, std::vector<uint32>> const& m, uint32 key)
{
    auto it = m.find(key);
    return it == m.end() ? nullptr : &it->second;
}

// creature entries that count for a MONSTER objective (the entry itself and entries that give its kill credit)
void KillEntries(uint32 credit, std::vector<uint32>& out)
{
    out.push_back(credit);
    if (std::vector<uint32> const* v = SpawnEntriesOf(g.CreditSrc, credit))
        out.insert(out.end(), v->begin(), v->end());
}

bool HasSpawn(uint32 entry)
{
    auto it = g.Spawns.find(entry);
    return it != g.Spawns.end() && !it->second.empty();
}

bool HasGoSpawn(uint32 goEntry)
{
    auto it = g.GoSpawns.find(goEntry);
    return it != g.GoSpawns.end() && !it->second.empty();
}

// game object entries that hold a quest item and have at least one spawn
void GoEntriesOf(uint32 item, std::vector<uint32>& out)
{
    auto it = g.ItemGoSrc.find(item);
    if (it == g.ItemGoSrc.end())
        return;
    for (uint32 e : it->second)
        if (HasGoSpawn(e))
            out.push_back(e);
}

// the quest hands the item over itself (start item, or the ItemDrop list), at least in the amount the objective needs
bool QuestSuppliesItem(Quest const* q, QuestObjective const& obj)
{
    uint32 const item = uint32(obj.ObjectID);
    uint32 const need = uint32(std::max<int32>(1, obj.Amount));
    if (q->GetSrcItemId() == item && std::max<uint32>(1, q->GetSrcItemCount()) >= need)
        return true;
    for (uint32 i = 0; i < QUEST_ITEM_DROP_COUNT; ++i)
        if (q->ItemDrop[i] == item && std::max<uint32>(1, q->ItemDropQuantity[i]) >= need)
            return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// 2. Analysis: static blockers
// ---------------------------------------------------------------------------------------------------------------------
struct Block
{
    char const* Code = nullptr;  // quest_blocked reason code, null = plannable
    uint32 Entry = 0;
    bool Silent = false;         // not worth a log row (daily, repeatable)
    std::string Info;
};

std::vector<uint32> EnderEntries(uint32 questId)
{
    std::vector<uint32> out;
    auto bounds = sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId);
    for (auto itr = bounds.begin(); itr != bounds.end(); ++itr)
        if (HasSpawn(itr->second))
            out.push_back(itr->second);
    return out;
}

bool HasEnderRow(uint32 questId)
{
    auto bounds = sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId);
    return bounds.begin() != bounds.end();
}

Block Analyze(Quest const* q)
{
    Block b;
    if (q->IsDailyOrWeekly() || q->IsMonthly() || q->IsRepeatable())
    {
        b.Code = "REPEATABLE"; b.Silent = true;
        return b;
    }
    if (q->GetSuggestedPlayers() > 1)
    {
        b.Code = "NEEDS_GROUP"; b.Info = StringFormat("suggested players {}", q->GetSuggestedPlayers());
        return b;
    }
    if (q->GetLimitTime() > 0)
    {
        b.Code = "TIMED_UNSUPPORTED";
        return b;
    }
    if (q->GetRequiredSkill())
    {
        b.Code = "SKILL_REQUIRED"; b.Entry = q->GetRequiredSkill();
        return b;
    }
    if (q->GetRequiredMinRepFaction())
    {
        b.Code = "REPUTATION_REQUIRED"; b.Entry = q->GetRequiredMinRepFaction();
        return b;
    }
    if (q->HasFlag(QUEST_FLAGS_COMPLETION_EVENT))
    {
        b.Code = "NEEDS_EVENT";
        return b;
    }
    if (q->HasFlag(QUEST_FLAGS_COMPLETION_AREA_TRIGGER))
    {
        b.Code = "OBJECTIVE_UNSUPPORTED"; b.Info = "area trigger completion";
        return b;
    }

    for (QuestObjective const& obj : q->GetObjectives())
    {
        if (obj.Flags & QUEST_OBJECTIVE_FLAG_OPTIONAL)
            continue;
        switch (obj.Type)
        {
            case QUEST_OBJECTIVE_MONSTER:
            {
                std::vector<uint32> entries;
                KillEntries(uint32(obj.ObjectID), entries);
                bool any = false;
                for (uint32 e : entries)
                    any = any || HasSpawn(e);
                if (!any)
                {
                    b.Code = "NO_TARGET_SPAWN"; b.Entry = uint32(obj.ObjectID);
                    return b;
                }
                break;
            }
            case QUEST_OBJECTIVE_ITEM:
            {
                auto it = g.ItemSrc.find(uint32(obj.ObjectID));
                bool any = QuestSuppliesItem(q, obj);   // delivery / report-to quests: the item comes with the quest
                if (it != g.ItemSrc.end())
                    for (uint32 e : it->second)
                        any = any || HasSpawn(e);
                if (!any)
                {
                    std::vector<uint32> gos;
                    GoEntriesOf(uint32(obj.ObjectID), gos);
                    any = !gos.empty();
                }
                if (!any)
                {
                    b.Entry = uint32(obj.ObjectID);
                    if (g.ItemGoSrc.count(uint32(obj.ObjectID)))
                    {
                        b.Code = "OBJECTIVE_UNSUPPORTED"; b.Info = "item comes from a game object without a spawn";
                    }
                    else if (it != g.ItemSrc.end())
                        b.Code = "NO_TARGET_SPAWN";
                    else
                        b.Code = "MISSING_ITEM_SOURCE";
                    return b;
                }
                break;
            }
            case QUEST_OBJECTIVE_TALKTO:
                if (!HasSpawn(uint32(obj.ObjectID)))
                {
                    b.Code = "NO_TARGET_SPAWN"; b.Entry = uint32(obj.ObjectID);
                    return b;
                }
                break;
            default:
                b.Code = "OBJECTIVE_UNSUPPORTED"; b.Entry = uint32(obj.ObjectID);
                b.Info = StringFormat("objective type {}", uint32(obj.Type));
                return b;
        }
    }

    if (!q->HasFlag(QUEST_FLAGS_AUTO_COMPLETE))
    {
        if (!HasEnderRow(q->GetQuestId()))
            b.Code = "NO_ENDER_ROW";
        else if (EnderEntries(q->GetQuestId()).empty())
            b.Code = "NO_ENDER_SPAWN";
    }
    return b;
}

Block const& CachedAnalysis(Quest const* q)
{
    // quests without a starter row are analysed on demand (rare: quests in the log from test commands); the result is not
    // cached because the index is read-only on map threads
    static thread_local std::unordered_map<uint32, Block> cache;
    auto it = cache.find(q->GetQuestId());
    if (it == cache.end())
        it = cache.emplace(q->GetQuestId(), Analyze(q)).first;
    return it->second;
}

void BuildIndex()
{
    uint32 const startMs = getMSTime();

    // 1. quests that creatures start
    std::unordered_set<uint32> wanted;      // creature entries whose spawns we keep
    std::unordered_set<uint32> neededItems; // objective items of the planned quests
    std::unordered_set<uint32> starterQuests;
    {
        QuestRelations const* rel = sObjectMgr->GetCreatureQuestRelationMapHACK();
        for (auto const& [creature, quest] : *rel)
        {
            g.StartsBy[creature].push_back(quest);
            wanted.insert(creature);
            starterQuests.insert(quest);
        }
    }
    for (auto& [creature, quests] : g.StartsBy)
        std::sort(quests.begin(), quests.end());

    // 2. kill credit map
    for (auto const& [entry, tmpl] : sObjectMgr->GetCreatureTemplates())
        for (uint32 credit : tmpl.KillCredit)
            if (credit)
                g.CreditSrc[credit].push_back(entry);

    // 3. objectives and enders of those quests
    for (uint32 questId : starterQuests)
    {
        Quest const* q = sObjectMgr->GetQuestTemplate(questId);
        if (!q)
            continue;
        auto bounds = sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId);
        for (auto itr = bounds.begin(); itr != bounds.end(); ++itr)
        {
            wanted.insert(itr->second);
            g.EndsBy[itr->second].push_back(questId);
        }
        for (QuestObjective const& obj : q->GetObjectives())
        {
            switch (obj.Type)
            {
                case QUEST_OBJECTIVE_MONSTER:
                {
                    std::vector<uint32> entries;
                    KillEntries(uint32(obj.ObjectID), entries);
                    wanted.insert(entries.begin(), entries.end());
                    break;
                }
                case QUEST_OBJECTIVE_ITEM:
                    neededItems.insert(uint32(obj.ObjectID));
                    break;
                case QUEST_OBJECTIVE_TALKTO:
                    wanted.insert(uint32(obj.ObjectID));
                    break;
                default:
                    break;
            }
        }
    }

    // 4. quest-item drop sources (the only synchronous query, world thread at startup)
    std::unordered_map<uint32, std::vector<uint32>> lootItems;  // creature loot id -> quest items
    if (QueryResult r = WorldDatabase.Query("SELECT Entry, Item FROM creature_loot_template WHERE QuestRequired = 1 AND ItemType = 0"))
    {
        do
        {
            Field* f = r->Fetch();
            uint32 item = f[1].GetUInt32();
            if (neededItems.count(item))
                lootItems[f[0].GetUInt32()].push_back(item);
        } while (r->NextRow());
    }
    else
        TC_LOG_ERROR("server.worldserver", "Bot quest index: creature_loot_template query returned nothing (quest items will be MISSING_ITEM_SOURCE)");
    std::unordered_map<uint32, std::vector<uint32>> goLootItems;  // chest loot id -> quest items
    if (QueryResult r = WorldDatabase.Query("SELECT Entry, Item FROM gameobject_loot_template WHERE QuestRequired = 1 AND ItemType = 0"))
    {
        do
        {
            Field* f = r->Fetch();
            uint32 item = f[1].GetUInt32();
            if (neededItems.count(item))
                goLootItems[f[0].GetUInt32()].push_back(item);
        } while (r->NextRow());
    }
    // the loot template Entry is a loot id: resolve it to the chest game objects (type 3) that use it
    std::unordered_set<uint32> wantedGo;
    for (auto const& [goEntry, goTmpl] : sObjectMgr->GetGameObjectTemplates())
    {
        if (goTmpl.type != GAMEOBJECT_TYPE_CHEST || !goTmpl.GetLootId())
            continue;
        auto it = goLootItems.find(goTmpl.GetLootId());
        if (it == goLootItems.end())
            continue;
        for (uint32 item : it->second)
            g.ItemGoSrc[item].push_back(goEntry);
        wantedGo.insert(goEntry);
    }
    for (auto& [item, entries] : g.ItemGoSrc)
    {
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    }
    g.NumGoItems = uint32(g.ItemGoSrc.size());
    for (auto const& [entry, tmpl] : sObjectMgr->GetCreatureTemplates())
    {
        CreatureDifficulty const* diff = tmpl.GetDifficulty(DIFFICULTY_NONE);
        if (!diff || !diff->LootID)
            continue;
        auto it = lootItems.find(diff->LootID);
        if (it == lootItems.end())
            continue;
        for (uint32 item : it->second)
        {
            g.ItemSrc[item].push_back(entry);
            wanted.insert(entry);
        }
    }
    for (auto& [item, entries] : g.ItemSrc)
    {
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    }
    g.NumItemSources = uint32(g.ItemSrc.size());

    // 5. spawn points of every wanted entry
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (!wanted.count(data.id))
            continue;
        g.Spawns[data.id].push_back({ data.mapId, data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() });
    }
    g.NumSpawnEntries = uint32(g.Spawns.size());
    for (auto const& [spawnId, data] : sObjectMgr->GetAllGameObjectData())
    {
        if (!wantedGo.count(data.id))
            continue;
        g.GoSpawns[data.id].push_back({ { data.mapId, data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() },
            data.spawnId, uint32(std::max<int32>(30, data.spawntimesecs)), data.id });
        ++g.NumGoSpawns;
    }

    // 6. starter grid (only starters that exist in the world)
    for (auto const& [creature, quests] : g.StartsBy)
    {
        auto sp = g.Spawns.find(creature);
        if (sp == g.Spawns.end())
            continue;
        uint32 n = 0;
        for (Pt const& p : sp->second)
        {
            if (++n > MAX_STARTER_POINTS)
                break;
            for (uint32 questId : quests)
            {
                Quest const* sq = sObjectMgr->GetQuestTemplate(questId);
                g.Grid[CellKey(p.Map, p.X, p.Y)].push_back({ questId, creature, p, sq ? int32(sq->GetClassicQuestLevel()) : 0 });
                ++g.NumStarterPoints;
            }
        }
    }
    g.NumStarterQuests = uint32(starterQuests.size());

    // 7. quest hubs: one per grid cell that holds starters
    for (auto const& [key, refs] : g.Grid)
    {
        Hub h;
        h.Map = refs.front().P.Map;
        h.Refs = refs;
        std::unordered_set<uint32> givers;
        for (StarterRef const& r : refs)
        {
            h.X += r.P.X; h.Y += r.P.Y; h.Z += r.P.Z;
            givers.insert(r.Entry);
            if (r.QLevel > 0)
            {
                h.MinQ = h.MinQ ? std::min(h.MinQ, r.QLevel) : r.QLevel;
                h.MaxQ = std::max(h.MaxQ, r.QLevel);
            }
        }
        float const n = float(refs.size());
        h.X /= n; h.Y /= n; h.Z /= n;
        h.Givers = uint32(givers.size());
        g.HubByCell[key] = uint32(g.Hubs.size());
        g.Hubs.push_back(std::move(h));
    }

    // 8. grind spawns: hostile normal-rank creatures without NPC functions
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(data.id);
        if (!t || t->npcflag || t->Classification != CreatureClassifications::Normal)
            continue;
        if (t->type == 8 || t->type == 11 || t->type == 12 || t->type == 13 || (t->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN) || (t->unit_flags & 0x2))
            continue;
        FactionTemplateEntry const* ft = sFactionTemplateStore.LookupEntry(t->faction);
        if (!ft || !ft->IsHostileToPlayers())
            continue;
        g.GrindGrid[CellKey(data.mapId, data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY())].push_back(
            { data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ(), data.id });
        ++g.NumGrind;
    }

    // 9. class trainers and vendors (E1/E4)
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(data.id);
        if (!t || !t->npcflag)
            continue;
        uint64 const flags = uint64(t->npcflag);
        SvcPt pt;
        pt.Map = data.mapId;
        pt.X = data.spawnPoint.GetPositionX(); pt.Y = data.spawnPoint.GetPositionY(); pt.Z = data.spawnPoint.GetPositionZ();
        pt.Entry = data.id;
        pt.Faction = t->faction;
        if (t->trainer_class > 0 && t->trainer_class < 32)
        {
            uint32 const tid = sObjectMgr->GetCreatureDefaultTrainer(data.id);
            if (tid && sObjectMgr->GetTrainer(tid))
            {
                pt.TrainerId = tid;
                g.Trainers[t->trainer_class].push_back(pt);
                ++g.NumTrainers;
            }
        }
        if (flags & uint64(UNIT_NPC_FLAG_VENDOR))
        {
            VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(data.id);
            if (!items || items->Empty())
                continue;
            auto vb = g.VendorBags.find(data.id);
            if (vb == g.VendorBags.end())
            {
                std::vector<BagOffer> offers;
                for (uint32 i = 0; i < items->GetItemCount(); ++i)
                {
                    VendorItem const* vi = items->GetItem(i);
                    if (!vi || vi->ExtendedCost || vi->maxcount || vi->PlayerConditionId)
                        continue;
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(vi->item);
                    if (!proto || proto->GetClass() != ITEM_CLASS_CONTAINER || proto->GetSubClass() != 0 || proto->GetContainerSlots() < 6 || !proto->GetBuyPrice())
                        continue;
                    offers.push_back({ vi->item, i, proto->GetContainerSlots(), proto->GetBuyPrice(), proto->GetBaseRequiredLevel() });
                    g.MinBagPrice = std::min(g.MinBagPrice, proto->GetBuyPrice());
                }
                vb = g.VendorBags.emplace(data.id, std::move(offers)).first;
                if (!vb->second.empty())
                    ++g.NumBagVendors;
            }
            pt.Repair = (flags & uint64(UNIT_NPC_FLAG_REPAIR)) != 0;
            g.Vendors.push_back(pt);
        }
    }

    g.Ready.store(true, std::memory_order_release);
    TC_LOG_INFO("server.worldserver", "Bot quest index: {} starter quests, {} starter points, {} spawn entries, {} quest items with creature sources, {} quest items in chest objects ({} object spawns), {} hubs, {} grind spawns, {} class trainer spawns, {} vendor spawns ({} bag vendor entries), {} ms",
        g.NumStarterQuests, g.NumStarterPoints, g.NumSpawnEntries, g.NumItemSources, g.NumGoItems, g.NumGoSpawns, uint32(g.Hubs.size()), g.NumGrind, g.NumTrainers, uint32(g.Vendors.size()), g.NumBagVendors, GetMSTimeDiffToNow(startMs));
}

// ---------------------------------------------------------------------------------------------------------------------
// 3. Per-bot context
// ---------------------------------------------------------------------------------------------------------------------
enum class Kind : uint8 { None, GoGiver, GoEnder, Kill, Talk, Grind, Service, Loot };

char const* KindName(Kind k)
{
    switch (k)
    {
        case Kind::GoGiver: return "go_giver";
        case Kind::GoEnder: return "go_ender";
        case Kind::Kill: return "kill";
        case Kind::Talk: return "talk";
        case Kind::Grind: return "grind";
        case Kind::Service: return "service";
        case Kind::Loot: return "loot";
        default: return "none";
    }
}

struct Task
{
    Kind K = Kind::None;
    uint32 Quest = 0;
    uint32 ObjId = 0;              // QuestObjective::ID being worked (Kill/Talk)
    uint32 NpcEntry = 0;           // giver / ender / talk target entry
    uint32 SinceMs = 0;            // task start (AI clock)
    uint32 ProgressMs = 0;         // last progress (objective count change, new target, arrival)
    int32 LastCount = -1;
    ObjectGuid Target;             // mob being hunted / killed (Kill) or NPC (others)
    uint32 TargetSinceMs = 0;
    float TargetBest = 0.0f;       // best distance to the target so far (stuck check)
    uint32 TargetBestMs = 0;
    uint32 Kills = 0;
    uint32 TooStrongSeen = 0;
    uint32 EliteSeen = 0;
    uint32 SeenLive = 0;           // live candidate mobs seen at least once during the task
    uint32 WaitStartMs = 0;        // waiting at an empty spawn point
    uint32 LegIssues = 0;
    uint32 Scans = 0, ScanRaw = 0, ScanOk = 0; // diagnostics: scans done, creatures seen in the last scan, candidates in the last scan
    float GoalX = 0, GoalY = 0, GoalZ = 0;
    bool Chasing = false;
    bool HasWp = false;            // long-route hop: intermediate waypoint (PathGenerator fails on paths longer than ~300 yd)
    float WpX = 0, WpY = 0, WpZ = 0;
    float FinalX = 0, FinalY = 0;
    uint32 Hops = 0;
    float LastDist = 0.0f;   // diagnostics: distance to the target at the last approach tick
    uint32 Approaches = 0;
    uint32 Ignored = 0;
    uint32 HubId = 0xFFFFFFFFu;    // hub this go_giver task travels to (E3)
    uint8 Svc = 0;                 // Service task: 1 = train, 2 = vendor visit
    float SvcX = 0, SvcY = 0, SvcZ = 0;
    uint32 SvcTrainerId = 0;
    bool SvcRepair = false, SvcBags = false;
    // Loot task (quest item inside a chest game object)
    uint64 GoSpawn = 0;            // claimed spawn (0 = none picked yet)
    uint64 GoBot = 0;              // claimant key (bot guid counter)
    uint32 GoEntry = 0;
    uint32 GoRespawn = 0;          // respawn seconds of the claimed spawn
    float GoX = 0, GoY = 0, GoZ = 0;
    uint32 GoAttempts = 0;         // spawns found empty / unusable during this task
    uint32 GoLooted = 0;
    uint32 GoWaitMs = 0;           // start of a respawn wait
    uint32 GoNextMs = 0;           // next scan at the claimed spawn
    uint32 GoUseTries = 0;
    uint32 GoApproachTries = 0;    // passes spent walking up to the live object (counted every pass)
    float GoBestGo = 0.0f;         // best 3D distance to the live object so far (0 = not measured yet)
    uint32 GoBestGoMs = 0;
    uint32 GoDangerMs = 0;         // next danger scan near the claimed spawn
};

struct Visited
{
    float X, Y;
};

// (b) quarantine: a quest whose drops for path/reach reasons pile up across bots is skipped by everybody for a while
struct DeadQuest { uint32 Fails = 0; uint32 UntilMs = 0; };
std::shared_mutex g_deadMx;
std::unordered_map<uint32, DeadQuest> g_dead;
constexpr uint32 DEAD_FAILS = 10;
constexpr uint32 DEAD_MS = 3600 * 1000;

bool GloballyDead(uint32 questId)
{
    std::shared_lock<std::shared_mutex> lk(g_deadMx);
    auto it = g_dead.find(questId);
    return it != g_dead.end() && it->second.UntilMs && int32(it->second.UntilMs - getMSTime()) > 0;
}

// returns true when this failure made the quest globally dead
bool NoteQuestFail(uint32 questId)
{
    std::unique_lock<std::shared_mutex> lk(g_deadMx);
    DeadQuest& d = g_dead[questId];
    if (d.UntilMs && int32(d.UntilMs - getMSTime()) > 0)
        return false;
    if (++d.Fails < DEAD_FAILS)
        return false;
    d.Fails = 0;
    d.UntilMs = std::max<uint32>(1, getMSTime() + DEAD_MS);
    return true;
}

// (b2) chest objects holding quest items: a spawn is claimed by one bot at a time and cools down after it was looted, so the
// handful of single-spawn objects (one chest, several hundred bots) are not crowded. Keyed by spawn id, clock = getMSTime().
struct GoGate { uint64 Bot = 0; uint32 ClaimUntil = 0; uint32 CoolUntil = 0; };
std::mutex g_goMx;
std::unordered_map<uint64, GoGate> g_goGate;
constexpr uint32 GO_CLAIM_MS = 150 * 1000;

bool Past(uint32 until) { return !until || int32(until - getMSTime()) <= 0; }

enum class GoState : uint8 { Free, Busy, Cooling };

// state of a spawn as seen by `bot` (a claim by the same bot counts as free); coolLeftMs set for Cooling
GoState GoGateState(uint64 spawn, uint64 bot, uint32* coolLeftMs = nullptr)
{
    std::lock_guard<std::mutex> lk(g_goMx);
    auto it = g_goGate.find(spawn);
    if (it == g_goGate.end())
        return GoState::Free;
    if (!Past(it->second.CoolUntil))
    {
        if (coolLeftMs)
            *coolLeftMs = uint32(int32(it->second.CoolUntil - getMSTime()));
        return GoState::Cooling;
    }
    if (it->second.Bot != bot && !Past(it->second.ClaimUntil))
        return GoState::Busy;
    return GoState::Free;
}

bool GoTryClaim(uint64 spawn, uint64 bot)
{
    std::lock_guard<std::mutex> lk(g_goMx);
    GoGate& gt = g_goGate[spawn];
    if (!Past(gt.CoolUntil) || (gt.Bot != bot && !Past(gt.ClaimUntil)))
        return false;
    gt.Bot = bot;
    gt.ClaimUntil = std::max<uint32>(1, getMSTime() + GO_CLAIM_MS);
    return true;
}

// release a claim; coolMs > 0 also marks the object as looted / gone for that long
void GoRelease(uint64 spawn, uint64 bot, uint32 coolMs)
{
    std::lock_guard<std::mutex> lk(g_goMx);
    auto it = g_goGate.find(spawn);
    if (it == g_goGate.end())
        return;
    if (it->second.Bot == bot)
    {
        it->second.Bot = 0;
        it->second.ClaimUntil = 0;
    }
    if (coolMs)
        it->second.CoolUntil = std::max<uint32>(1, getMSTime() + coolMs);
}

// (c) quest hub cache (R1): a hub that a bot could not reach is skipped by every bot for HubFailGlobalSec; a hub a bot
// reached once is "verified" and exempt from the distance cap. The zone faction mask is cached per hub.
struct HubState { uint32 FailUntilMs = 0; uint32 Fails = 0; bool Verified = false; int32 Mask = -1; };
std::shared_mutex g_hubMx;
std::unordered_map<uint32, HubState> g_hubState;

bool HubGloballyFailed(uint32 idx)
{
    std::shared_lock<std::shared_mutex> lk(g_hubMx);
    auto it = g_hubState.find(idx);
    return it != g_hubState.end() && it->second.FailUntilMs && int32(it->second.FailUntilMs - getMSTime()) > 0;
}

void NoteHubFail(uint32 idx)
{
    std::unique_lock<std::shared_mutex> lk(g_hubMx);
    HubState& h = g_hubState[idx];
    ++h.Fails;
    h.FailUntilMs = std::max<uint32>(1, getMSTime() + uint32(std::max<int32>(60, sConfigMgr->GetIntDefault("Bot.Quest.HubFailGlobalSec", 1800))) * 1000);
}

void NoteHubVerified(uint32 idx)
{
    {
        std::shared_lock<std::shared_mutex> lk(g_hubMx);
        auto it = g_hubState.find(idx);
        if (it != g_hubState.end() && it->second.Verified)
            return;
    }
    std::unique_lock<std::shared_mutex> lk(g_hubMx);
    g_hubState[idx].Verified = true;
}

// Returns a skip reason ("global_fail", "faction", "far") or nullptr when the hub may be tried. Per-bot blacklist is checked by the caller.
char const* HubSkipReason(Player* bot, uint32 idx, Hub const& h, float dist)
{
    int32 mask = -1;
    bool verified = false;
    {
        std::shared_lock<std::shared_mutex> lk(g_hubMx);
        auto it = g_hubState.find(idx);
        if (it != g_hubState.end())
        {
            if (it->second.FailUntilMs && int32(it->second.FailUntilMs - getMSTime()) > 0)
                return "global_fail";
            mask = it->second.Mask;
            verified = it->second.Verified;
        }
    }
    if (mask < 0)
    {
        mask = 0;
        if (Map* map = bot->GetMap())
        {
            uint32 zone = map->GetZoneId(bot->GetPhaseShift(), h.X, h.Y, h.Z);
            if (AreaTableEntry const* a = sAreaTableStore.LookupEntry(zone))
                mask = int32(a->FactionGroupMask);
        }
        std::unique_lock<std::shared_mutex> lk(g_hubMx);
        g_hubState[idx].Mask = mask;
    }
    uint32 const own = bot->GetTeamId() == TEAM_ALLIANCE ? 2u : 4u;
    if (mask && !(uint32(mask) & own))
        return "faction";
    static uint32 const maxDist = uint32(std::max<int32>(100, sConfigMgr->GetIntDefault("Bot.Quest.HubMaxDist", 2500)));
    if (!verified && dist > float(maxDist))
        return "far";
    return nullptr;
}

bool IsReachCode(char const* code)
{
    static char const* const codes[] = { "NO_PATH", "PATH_PARTIAL_FAR", "UNREACHABLE", "TARGET_UNREACHABLE", "ITEM_NOT_DROPPING", "GIVER_NOT_INTERACTABLE" };
    for (char const* c : codes)
        if (std::strcmp(c, code) == 0)
            return true;
    return false;
}

class BotQuestCtx : public UntypedValue
{
public:
    explicit BotQuestCtx(BotAI* ai) : UntypedValue(ai, "quest_ctx", 0) { }
    uint32 ExecCalls = 0;
    char const* Why = "none";

    Task T;
    uint32 NextChooseMs = 0;
    uint32 NextScanMs = 0;
    bool ExpectGoal = false;
    float ExpectX = 0, ExpectY = 0;
    uint32 GoalFails = 0;                       // movement failures in the current task
    std::unordered_map<uint32, uint32> Blacklist;   // quest -> AI clock until
    std::unordered_set<uint64> Logged;              // (quest, code) pairs already written
    std::unordered_map<uint64, uint32> Ignore;      // mob guid (counter) -> AI clock until
    std::vector<Visited> Seen;                      // recently searched spawn points
    uint32 SelBlocks = 0;
    uint8 IdleLoggedLevel = 0;
    uint32 IdleLogMs = 0;          // R5: IDLE_WAIT_SPAWN rows at most one per 60 s
    uint32 Completed = 0, Accepted = 0, Rewarded = 0;
    uint32 LastPickMs = 0;
    std::unordered_map<uint32, uint32> HubBlack;    // hub index -> AI clock until
    uint32 NextHubMs = 0;
    uint8 NoLocalLevel = 0, HubNoneLevel = 0;
    bool LastWasGrind = false;
    uint32 GrindAnchorMap = 0xFFFFFFFFu;            // R2: where the current grind run started (last quest area)
    float GrindAnchorX = 0, GrindAnchorY = 0;
    uint32 Grinds = 0, HubTrips = 0;
    uint32 HubSkip[3] = { 0, 0, 0 };                // last hub scan: skipped by global failure cache / opposite faction / too far
    // E1/E4 service state
    uint32 NextSvcMs = 0;                           // next service-due evaluation
    uint8 TrainLevel = 0;                           // level at which spells were last evaluated/trained
    uint32 TrainWant = 0;                           // copper needed for the cheapest available but unaffordable spell (0 = none)
    uint32 TrainRetryMs = 0;
    uint32 TrainedTotal = 0, VendorTrips = 0;
    uint32 NextVendorMs = 0, NextBagMs = 0;
    bool VendorNow = false;                         // a reward could not be stored: go to a vendor now
    std::unordered_map<uint64, uint8> Repeats;      // (quest, npc) -> reach failures (quarantine)
    std::unordered_map<uint32, uint32> SvcBlack;    // npc entry -> AI clock until
    std::unordered_map<uint64, uint32> GoIgnore;    // chest spawn id -> AI clock until (empty / unusable for this bot)
    uint8 NoTrainerLevel = 0, NoMoneyLevel = 0, BagNoMoneyLevel = 0, NoVendorLevel = 0;
    std::string LastNote;

    bool Blacklisted(uint32 quest, uint32 now) const
    {
        auto it = Blacklist.find(quest);
        return it != Blacklist.end() && now < it->second;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------------
std::string Esc(std::string const& s)
{
    std::string o;
    o.reserve(s.size() + 4);
    for (char ch : s)
    {
        if (ch == '"' || ch == '\\')
            o.push_back('\\');
        if (uint8(ch) < 0x20)
            continue;
        o.push_back(ch);
    }
    return o;
}

uint32 CodeId(char const* code)
{
    uint32 h = 2166136261u;
    for (char const* p = code; *p; ++p)
        h = (h ^ uint8(*p)) * 16777619u;
    return h & 0xFFFFFF;
}

uint64 LogKey(uint32 quest, char const* code) { return (uint64(quest) << 24) | CodeId(code); }

bool IsMeleeClass(Player const* bot)
{
    switch (bot->GetClass())
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_PALADIN:
        case CLASS_DRUID:
            return true;
        default:
            return false;
    }
}

float PullRange(Player const* bot) { return IsMeleeClass(bot) ? Cfg().PullMelee : Cfg().PullRanged; }

std::string QuestTitle(uint32 questId);

// quest_blocked event, once per (quest, code) per bot unless `once` is false; the quest title is added to the details
void Blocked(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 questId, char const* code, std::string summary, std::string details = std::string(), uint32 entry = 0, bool once = true, uint8 severity = BOTLOG_WARN)
{
    if (once && !c.Logged.insert(LogKey(questId, code)).second)
        return;
    BotEvent ev = ai->MakeEvent(bot, "quest_blocked", severity, code, std::move(summary));
    if (questId)
        ev.QuestId = questId;
    if (entry)
        ev.TargetEntry = entry;
    if (questId && details.find("\"quest_title\"") == std::string::npos)
    {
        std::string const title = StringFormat(R"("quest_title":"{}")", Esc(QuestTitle(questId)));
        if (details.size() > 1 && details.front() == '{' && details.back() == '}')
            details.insert(details.size() - 1, "," + title);
        else if (details.empty())
            details = "{" + title + "}";
    }
    ev.Details = std::move(details);
    sBotMgr->LogEvent(std::move(ev));
}

std::string QuestTitle(uint32 questId)
{
    Quest const* q = sObjectMgr->GetQuestTemplate(questId);
    return q ? q->GetLogTitle() : std::string("?");
}

bool IsRewardDataGap(Quest const* q)
{
    for (uint32 i = 0; i < q->GetRewItemsCount(); ++i)
        if (q->RewardItemId[i] && !sObjectMgr->GetItemTemplate(q->RewardItemId[i]))
            return true;
    return false;
}

Creature* FindLiveNpc(Player* bot, uint32 entry, float range)
{
    return bot->FindNearestCreature(entry, range, true);
}

// nearest chest spawn of a game object entry on the bot's map (2D distance), or null
GoPt const* NearestGoSpawn(Player const* bot, uint32 goEntry, float* distOut = nullptr)
{
    auto it = g.GoSpawns.find(goEntry);
    if (it == g.GoSpawns.end())
        return nullptr;
    GoPt const* best = nullptr;
    float bd = 1e9f;
    for (GoPt const& p : it->second)
    {
        if (p.P.Map != bot->GetMapId())
            continue;
        float d = Dist2D(p.P.X, p.P.Y, bot->GetPositionX(), bot->GetPositionY());
        if (d < bd)
        {
            bd = d;
            best = &p;
        }
    }
    if (distOut)
        *distOut = bd;
    return best;
}

// nearest spawn point of an entry on the bot's map, or null
Pt const* NearestSpawn(Player const* bot, uint32 entry, float* distOut = nullptr)
{
    auto it = g.Spawns.find(entry);
    if (it == g.Spawns.end())
        return nullptr;
    Pt const* best = nullptr;
    float bd = 1e9f;
    for (Pt const& p : it->second)
    {
        if (p.Map != bot->GetMapId())
            continue;
        float d = Dist2D(p.X, p.Y, bot->GetPositionX(), bot->GetPositionY());
        if (d < bd)
        {
            bd = d;
            best = &p;
        }
    }
    if (distOut)
        *distOut = bd;
    return best;
}

// quest log snapshot
struct LogEntry
{
    uint32 Quest;
    uint16 Slot;
    QuestStatus Status;
};

void ReadLog(Player* bot, std::vector<LogEntry>& out)
{
    out.clear();
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 id = bot->GetQuestSlotQuestId(slot);
        if (!id)
            continue;
        out.push_back({ id, slot, bot->GetQuestStatus(id) });
    }
}

// first non-optional objective that is not complete (null when none)
QuestObjective const* FirstOpenObjective(Player* bot, Quest const* q, uint16 slot)
{
    for (QuestObjective const& obj : q->GetObjectives())
    {
        if (obj.Flags & QUEST_OBJECTIVE_FLAG_OPTIONAL)
            continue;
        if (!bot->IsQuestObjectiveComplete(slot, q, obj))
            return &obj;
    }
    return nullptr;
}

void ObjectiveEntries(QuestObjective const& obj, std::vector<uint32>& out)
{
    switch (obj.Type)
    {
        case QUEST_OBJECTIVE_MONSTER:
            KillEntries(uint32(obj.ObjectID), out);
            break;
        case QUEST_OBJECTIVE_ITEM:
        {
            auto it = g.ItemSrc.find(uint32(obj.ObjectID));
            if (it != g.ItemSrc.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
            break;
        }
        case QUEST_OBJECTIVE_TALKTO:
            out.push_back(uint32(obj.ObjectID));
            break;
        default:
            break;
    }
}

// effective level gap used to skip dangerous kill targets (same shape as the combat flee check, stricter threshold)
int32 EffectiveDiff(Player const* bot, Creature const* c)
{
    return int32(c->GetLevel()) - int32(bot->GetLevel()) + (c->IsElite() ? Cfg().EliteBonus : 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// 4. quest_think
// ---------------------------------------------------------------------------------------------------------------------
class QuestThinkAction : public Action
{
public:
    explicit QuestThinkAction(BotAI* ai) : Action(ai, "quest_think", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG) { }

    bool IsPossible() override { return g.Ready.load(std::memory_order_acquire); }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        if (!g.Ready.load(std::memory_order_acquire))
        {
            ai->WarnOnce("quest_index", "quest strategy active but the quest index is not built (BotQuest::EnsureIndex)");
            return false;
        }
        BotQuestCtx* cp = static_cast<BotQuestCtx*>(ai->GetValueRaw("quest_ctx"));
        if (!cp)
            return false;
        BotQuestCtx& c = *cp;
        uint32 const now = ai->GetNowMs();
        ++c.ExecCalls;

        if (!bot->IsAlive())
        {
            if (c.T.K == Kind::Loot && c.T.GoSpawn)
            {
                GoRelease(c.T.GoSpawn, c.T.GoBot, 0);   // L2: a dead bot does not keep the chest claimed
                c.T.GoSpawn = 0;
            }
            c.ExpectGoal = false;
            c.Why = "dead";
            return false;
        }
        if (ai->Rest().Resting() || bot->IsNonMeleeSpellCast(false, false, true) || bot->IsInFlight())
        {
            c.Why = ai->Rest().Resting() ? "resting" : (bot->IsInFlight() ? "flight" : "casting");
            return false;
        }
        BotMotion& motion = ai->Motion();
        if (motion.HasGoal() && std::strcmp(motion.GetTag(), "quest") != 0)
        { c.Why = "othergoal"; return false; } // flee / goto / follow owns the movement slot
        if (!motion.GetFollow().IsEmpty())
        {
            c.Why = "follow";
            return false;
        }
        c.Why = "run";

        CheckGoalOutcome(ai, bot, c, now);

        if (c.T.K == Kind::None)
        {
            if (now < c.NextChooseMs)
                return false;
            if (!Choose(ai, bot, c, now))
            {
                c.NextChooseMs = now + 6000;
                return false;
            }
            return true; // the choice consumed this tick (accept / new task)
        }
        return RunTask(ai, bot, c, now);
    }

private:
    // ----- task bookkeeping -----
    void Finish(BotQuestCtx& c, uint32 now, bool immediate = true)
    {
        if (c.T.K == Kind::Loot && c.T.GoSpawn)
            GoRelease(c.T.GoSpawn, c.T.GoBot, 0);
        c.T = Task();
        c.GoalFails = 0;
        c.ExpectGoal = false;
        c.NextChooseMs = immediate ? 0 : now + 1000;
    }

    void Drop(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, uint32 questId, char const* code, std::string summary, std::string details, uint32 entry, uint32 blacklistSec)
    {
        if (c.T.K == Kind::Service)
        {
            // E1/E4: the failure is reported under the service code, the movement code goes into the summary
            c.SvcBlack[c.T.NpcEntry] = now + 600 * 1000;
            if (c.T.Svc == 1)
                c.TrainRetryMs = now + 300 * 1000;
            else
                c.NextVendorMs = now + 300 * 1000;
            summary = StringFormat("{} ({})", summary, code);
            code = c.T.Svc == 1 ? "TRAIN_UNREACHABLE" : "VENDOR_UNREACHABLE";
        }
        Blocked(ai, bot, c, questId, code, std::move(summary), std::move(details), entry);
        if (questId && IsReachCode(code))
        {
            uint8& n = c.Repeats[(uint64(questId) << 32) | entry];
            if (n < 250)
                ++n;
            if (n >= 3)
            {
                blacklistSec = 6 * 3600;
                if (n == 3)
                    Blocked(ai, bot, c, questId, "QUEST_QUARANTINED", StringFormat("quest '{}' failed {} times ({}) at npc/mob {}, skipped for 6 h", QuestTitle(questId), n, code, entry),
                        StringFormat(R"({{"last_code":"{}","repeats":{}}})", code, n), entry);
            }
            else
                blacklistSec *= n;
            if (NoteQuestFail(questId))
                Blocked(ai, bot, c, questId, "QUEST_QUARANTINED_GLOBAL", StringFormat("quest '{}' failed for {} drops across bots ({}), skipped by all bots for 1 h", QuestTitle(questId), DEAD_FAILS, code),
                    StringFormat(R"({{"last_code":"{}","npc":{}}})", code, entry), entry);
        }
        if (questId)
            c.Blacklist[questId] = now + blacklistSec * 1000;
        if (c.T.K == Kind::Grind)
            c.Seen.push_back({ c.T.GoalX, c.T.GoalY });   // do not pick the same unreachable grind spot again
        if (c.T.HubId != 0xFFFFFFFFu && c.T.K == Kind::GoGiver)
        {
            c.HubBlack[c.T.HubId] = now + Cfg().HubBlackSec * 1000;   // the whole hub is out of reach for a while
            NoteHubFail(c.T.HubId);                                   // ... for every bot
        }
        BotMotion::Halt(bot);
        ai->Motion().ClearGoal();
        Finish(c, now);
    }

    // The goal vanished without us clearing it: Step() arrived or failed. Count a failure when we are not at the destination.
    void CheckGoalOutcome(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        if (!c.ExpectGoal || ai->Motion().HasGoal())
            return;
        c.ExpectGoal = false;
        float d = Dist2D(bot->GetPositionX(), bot->GetPositionY(), c.ExpectX, c.ExpectY);
        if (d <= 12.0f)
        {
            c.T.ProgressMs = now;
            return;
        }
        if (c.T.K == Kind::None)
            return;
        ++c.GoalFails;
        if (c.GoalFails >= 3)
            Drop(ai, bot, c, now, c.T.Quest, "UNREACHABLE",
                StringFormat("movement toward the {} target failed {} times", KindName(c.T.K), c.GoalFails),
                StringFormat(R"({{"task":"{}","dist":{:.0f},"dest":[{:.0f},{:.0f}]}})", KindName(c.T.K), d, c.ExpectX, c.ExpectY), c.T.NpcEntry, 1200);
    }

    // Starts a walk toward (x,y,z); a path query first so impossible legs are reported instead of walked forever.
    // Returns false when the leg is impossible (the task was dropped).
    // Finds a valid intermediate waypoint towards (x,y): the single PathGenerator query fails (NOPATH|SHORTCUT) on long winding routes.
    bool FindHop(Player* bot, float x, float y, float& hx, float& hy, float& hz)
    {
        float const bx = bot->GetPositionX(), by = bot->GetPositionY();
        float const dx = x - bx, dy = y - by;
        float const dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 60.0f)
            return false;
        float const ux = dx / dist, uy = dy / dist;
        float const frac[] = { 0.7f, 0.55f, 0.4f, 0.28f, 0.18f };
        float const lat[] = { 0.0f, 50.0f, -50.0f, 100.0f, -100.0f };
        float bestRemain = dist;
        bool found = false;
        for (float f : frac)
            for (float l : lat)
            {
                float const cx = bx + ux * dist * f - uy * l;
                float const cy = by + uy * dist * f + ux * l;
                float cz = bot->GetMap()->GetHeight(bot->GetPhaseShift(), cx, cy, bot->GetPositionZ() + 60.0f);
                if (cz < -1000.0f)
                    continue;
                BotPathInfo i = BotMotion::QueryPath(bot, cx, cy, cz);
                if (!i.Valid || i.Partial || i.GoalOffMesh || i.Length > dist * 2.5f + 80.0f)
                    continue;
                float const remain = Dist2D(cx, cy, x, y);
                if (remain < bestRemain - 20.0f)
                {
                    bestRemain = remain;
                    hx = cx; hy = cy; hz = cz;
                    found = true;
                }
            }
        return found;
    }

    bool Travel(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, float x, float y, float z, float arrive, uint32 entry)
    {
        BotMotion& motion = ai->Motion();
        Task& t = c.T;
        bool const sameFinal = Dist2D(t.FinalX, t.FinalY, x, y) < 4.0f;
        if (t.HasWp && sameFinal)
        {
            if (Dist2D(t.WpX, t.WpY, bot->GetPositionX(), bot->GetPositionY()) < 12.0f)
                t.HasWp = false; // hop reached, plan the next one
            else
            {
                if (!(motion.HasGoal() && std::strcmp(motion.GetTag(), "quest") == 0))
                {
                    motion.SetGoal(bot->GetMapId(), t.WpX, t.WpY, t.WpZ, 8.0f, "quest");
                    motion.SetQuestCtx(t.Quest, entry, KindName(t.K));
                    c.ExpectGoal = true;
                    c.ExpectX = t.WpX; c.ExpectY = t.WpY;
                }
                return true;
            }
        }
        if (motion.HasGoal() && std::strcmp(motion.GetTag(), "quest") == 0 &&
            Dist2D(motion.GoalX(), motion.GoalY(), x, y) < 4.0f)
            return true; // already walking there
        auto const pathT0 = std::chrono::steady_clock::now();
        BotPathInfo info = BotMotion::QueryPath(bot, x, y, z);
        uint32 const pathUs = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pathT0).count());
        bool const bad = info.NoPath || (info.Partial && info.EndGap3D > 25.0f) || (info.Valid && info.GoalOffMesh && info.EndGap3D > 25.0f);
        if (bad)
        {
            float hx = 0, hy = 0, hz = 0;
            auto const hopT0 = std::chrono::steady_clock::now();
            bool const hopFound = t.Hops < 40 && FindHop(bot, x, y, hx, hy, hz);
            uint32 const hopUs = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - hopT0).count());
            if (hopFound)
            {
                ++t.Hops;
                t.HasWp = true;
                t.WpX = hx; t.WpY = hy; t.WpZ = hz;
                t.FinalX = x; t.FinalY = y;
                t.GoalX = x; t.GoalY = y; t.GoalZ = z;
                motion.SetGoal(bot->GetMapId(), hx, hy, hz, 8.0f, "quest");
                motion.SetQuestCtx(t.Quest, entry, KindName(t.K));
                c.ExpectGoal = true;
                c.ExpectX = hx; c.ExpectY = hy;
                return true;
            }
            if (t.K == Kind::Loot && t.GoSpawn)
            {
                // a chest spawn this bot cannot path to is a per-bot problem: try the next spawn, no quarantine
                GoSpawnFailed(ai, bot, c, now, t, info.NoPath ? "no path" : "path partial far");
                return false;
            }
            std::string det = StringFormat(R"({{"task":"{}","dest":[{:.0f},{:.0f},{:.0f}],"no_path":{},"partial":{},"gap3d":{:.0f},"goal_off_mesh":{},"length":{:.0f},"hops":{},"path_us":{},"hop_us":{}}})",
                KindName(t.K), x, y, z, info.NoPath, info.Partial, info.EndGap3D, info.GoalOffMesh, info.Length, t.Hops, pathUs, hopUs);
            Drop(ai, bot, c, now, t.Quest, info.NoPath ? "NO_PATH" : "PATH_PARTIAL_FAR",
                StringFormat("no usable path to the {} target (npc/mob {})", KindName(t.K), entry), std::move(det), entry, 1800);
            return false;
        }
        if (t.GoalX != x || t.GoalY != y)
            ++t.LegIssues;
        t.GoalX = x; t.GoalY = y; t.GoalZ = z;
        t.FinalX = x; t.FinalY = y;
        t.HasWp = false;
        motion.SetGoal(bot->GetMapId(), x, y, z, arrive, "quest");
        motion.SetQuestCtx(t.Quest, entry, KindName(t.K));
        c.ExpectGoal = true;
        c.ExpectX = x; c.ExpectY = y;
        return true;
    }

    void StopMoving(BotAI* ai, Player* bot, BotQuestCtx& c)
    {
        if (ai->Motion().HasGoal() && std::strcmp(ai->Motion().GetTag(), "quest") == 0)
            ai->Motion().ClearGoal();
        c.ExpectGoal = false;
        if (c.T.Chasing)
        {
            bot->GetMotionMaster()->Clear();
            c.T.Chasing = false;
        }
        BotMotion::Halt(bot);
    }

    // ----- choosing -----
    bool Choose(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        if (ServiceDue(ai, bot, c, now))
            return true;

        std::vector<LogEntry> log;
        ReadLog(bot, log);

        // (a) turn in the nearest completed quest
        {
            LogEntry const* best = nullptr;
            uint32 bestEntry = 0;
            float bd = 1e9f;
            for (LogEntry const& e : log)
            {
                if (e.Status != QUEST_STATUS_COMPLETE || c.Blacklisted(e.Quest, now))
                    continue;
                Quest const* q = sObjectMgr->GetQuestTemplate(e.Quest);
                if (!q)
                    continue;
                if (q->HasFlag(QUEST_FLAGS_AUTO_COMPLETE))
                    continue;
                std::vector<uint32> enders = EnderEntries(e.Quest);
                if (enders.empty())
                {
                    Blocked(ai, bot, c, e.Quest, HasEnderRow(e.Quest) ? "NO_ENDER_SPAWN" : "NO_ENDER_ROW", StringFormat("quest '{}' is complete but nobody can take it", q->GetLogTitle()));
                    c.Blacklist[e.Quest] = now + 3600 * 1000;
                    continue;
                }
                for (uint32 en : enders)
                {
                    float d;
                    if (NearestSpawn(bot, en, &d) && d < bd)
                    {
                        bd = d;
                        best = &e;
                        bestEntry = en;
                    }
                }
            }
            if (best)
            {
                c.T = Task();
                c.T.K = Kind::GoEnder;
                c.T.Quest = best->Quest;
                c.T.NpcEntry = bestEntry;
                c.T.SinceMs = c.T.ProgressMs = now;
                c.GoalFails = 0;
                Decision(ai, bot, "QUEST_TURNIN_PLAN", StringFormat("heading to turn in '{}'", QuestTitle(best->Quest)), best->Quest, bestEntry,
                    StringFormat(R"({{"dist":{:.0f}}})", bd));
                return true;
            }
        }

        // (b) opportunistic accept when standing at a giver, then work the nearest open objective
        if (log.size() < Cfg().MaxActive && TryAcceptNearby(ai, bot, c, now, log))
            return true;

        {
            struct Cand { LogEntry const* E; QuestObjective const* O; float D; uint32 Entry; bool Go; };
            Cand best{ nullptr, nullptr, 1e9f, 0, false };
            uint32 considered = 0;
            for (LogEntry const& e : log)
            {
                if (e.Status != QUEST_STATUS_INCOMPLETE || c.Blacklisted(e.Quest, now))
                    continue;
                Quest const* q = sObjectMgr->GetQuestTemplate(e.Quest);
                if (!q)
                    continue;
                Block blk = Analyze(q);
                if (blk.Code && !blk.Silent && std::strncmp(blk.Code, "NO_ENDER", 8) != 0)
                {
                    Blocked(ai, bot, c, e.Quest, blk.Code, StringFormat("quest '{}' in the log cannot be worked: {}", q->GetLogTitle(), blk.Info), std::string(), blk.Entry);
                    c.Blacklist[e.Quest] = now + 3600 * 1000;
                    continue;
                }
                QuestObjective const* obj = FirstOpenObjective(bot, q, e.Slot);
                if (!obj)
                    continue;
                std::vector<uint32> entries;
                ObjectiveEntries(*obj, entries);
                ++considered;
                for (uint32 en : entries)
                {
                    float d;
                    if (NearestSpawn(bot, en, &d) && d < best.D)
                        best = { &e, obj, d, en, false };
                }
                if (obj->Type == QUEST_OBJECTIVE_ITEM)
                {
                    std::vector<uint32> gos;
                    GoEntriesOf(uint32(obj->ObjectID), gos);
                    for (uint32 en : gos)
                    {
                        float d;
                        if (NearestGoSpawn(bot, en, &d) && d < best.D)
                            best = { &e, obj, d, en, true };
                    }
                    if (entries.empty() && gos.empty())
                    {
                        // nothing in the world gives this item (the quest start item does not cover the open amount)
                        Blocked(ai, bot, c, e.Quest, "MISSING_ITEM_SOURCE", StringFormat("quest '{}' in the log needs item {} and no source is known", q->GetLogTitle(), obj->ObjectID),
                            StringFormat(R"({{"item":{},"amount":{}}})", obj->ObjectID, obj->Amount), uint32(obj->ObjectID));
                        c.Blacklist[e.Quest] = now + 3600 * 1000;
                    }
                }
            }
            if (best.E)
            {
                Quest const* q = sObjectMgr->GetQuestTemplate(best.E->Quest);
                c.T = Task();
                c.T.K = best.Go ? Kind::Loot : best.O->Type == QUEST_OBJECTIVE_TALKTO ? Kind::Talk : Kind::Kill;
                c.T.Quest = best.E->Quest;
                c.T.ObjId = best.O->ID;
                c.T.NpcEntry = best.O->Type == QUEST_OBJECTIVE_TALKTO ? uint32(best.O->ObjectID) : 0;
                c.T.SinceMs = c.T.ProgressMs = c.T.TargetBestMs = now;
                c.GoalFails = 0;
                Decision(ai, bot, "QUEST_WORK", StringFormat("working '{}' ({} of {} open quests)", q ? q->GetLogTitle() : "?", 1, considered), best.E->Quest, best.Entry,
                    StringFormat(R"({{"objective_type":{},"object_id":{},"amount":{},"nearest_spawn_dist":{:.0f},"game_object":{}}})", uint32(best.O->Type), best.O->ObjectID, best.O->Amount, best.D, best.Go));
                return true;
            }
        }

        // (c) a new quest
        if (log.size() >= Cfg().MaxActive)
            return false;
        return PickNew(ai, bot, c, now, log);
    }

    void Decision(BotAI* ai, Player* bot, char const* reason, std::string summary, uint32 questId, uint32 entry, std::string details = std::string())
    {
        BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, reason, std::move(summary));
        if (questId)
            ev.QuestId = questId;
        if (entry)
            ev.TargetEntry = entry;
        ev.Details = std::move(details);
        sBotMgr->LogEvent(std::move(ev));
    }

    // Candidate evaluation of a starter reference. Returns score > 0 when takeable now. `log` blocks are throttled.
    float Score(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, StarterRef const& ref, float dist, bool logBlocks)
    {
        Quest const* q = sObjectMgr->GetQuestTemplate(ref.Quest);
        if (!q)
            return 0.0f;
        if (bot->GetQuestStatus(ref.Quest) != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(ref.Quest))
            return 0.0f;
        if (c.Blacklisted(ref.Quest, now) || GloballyDead(ref.Quest))
            return 0.0f;
        if (!c.HubBlack.empty() && dist > 100.0f)
        {
            auto hb = g.HubByCell.find(CellKey(ref.P.Map, ref.P.X, ref.P.Y));
            if (hb != g.HubByCell.end())
            {
                auto bl = c.HubBlack.find(hb->second);
                if (bl != c.HubBlack.end() && now < bl->second)
                    return 0.0f;
            }
        }
        if (dist > 100.0f)
        {
            auto hb = g.HubByCell.find(CellKey(ref.P.Map, ref.P.X, ref.P.Y));
            if (hb != g.HubByCell.end() && HubGloballyFailed(hb->second))
                return 0.0f;
        }
        // not for this race/class: silent (the quest exists, just not for this bot)
        if (!bot->SatisfyQuestRace(q, false) || !bot->SatisfyQuestClass(q, false))
            return 0.0f;

        bool canLog = logBlocks && dist <= 120.0f && c.SelBlocks < Cfg().SelBlockCap;
        auto block = [&](char const* code, std::string summary, uint32 entry = 0)
        {
            if (canLog && c.Logged.find(LogKey(ref.Quest, code)) == c.Logged.end())
            {
                ++c.SelBlocks;
                Blocked(ai, bot, c, ref.Quest, code, std::move(summary), StringFormat(R"({{"giver":{},"dist":{:.0f}}})", ref.Entry, dist), entry, true, BOTLOG_INFO);
            }
            return 0.0f;
        };

        Block const& blk = CachedAnalysis(q);
        if (blk.Code)
        {
            if (blk.Silent)
                return 0.0f;
            return block(blk.Code, StringFormat("quest '{}' cannot be done by bots: {}", q->GetLogTitle(), blk.Info), blk.Entry);
        }
        if (!bot->SatisfyQuestLevel(q, false))
            return block("QUEST_LEVEL", StringFormat("quest '{}' needs a different level (bot {}, quest min {})", q->GetLogTitle(), bot->GetLevel(), bot->GetQuestMinLevel(q)));
        if (!bot->SatisfyQuestDependentQuests(q, false) || !bot->SatisfyQuestPreviousQuest(q, false))
            return block("QUEST_PREREQ", StringFormat("quest '{}' needs an earlier quest", q->GetLogTitle()));
        if (!bot->CanTakeQuest(q, false))
            return 0.0f; // exclusive group, day/week, conditions: not worth a row

        int32 ql = bot->GetQuestLevel(q);
        int32 gap = ql - int32(bot->GetLevel());
        if (gap > 2)
            return 0.0f; // too hard for now, becomes takeable as the bot levels
        float fit = gap >= 2 ? 0.5f : gap >= 0 ? 1.0f : gap >= -2 ? 0.8f : 0.4f;
        float xp = float(q->XPValue(bot)) + 20.0f;
        float work = 40.0f * float(q->GetObjectives().size());
        float const chain = q->GetPrevQuestId() > 0 ? 1.4f : 1.0f;   // chain successors first
        return chain * fit * xp / (dist + 150.0f + work);
    }

    bool PickNew(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, std::vector<LogEntry> const& log)
    {
        (void)log;
        for (int pass = 0; pass < 2; ++pass)
        {
            float const radius = pass == 0 ? Cfg().SelectRadius : Cfg().FarRadius;
            int32 const cells = int32(std::ceil(radius / GRID_CELL));
            int32 const cx0 = int32(std::floor(bot->GetPositionX() / GRID_CELL));
            int32 const cy0 = int32(std::floor(bot->GetPositionY() / GRID_CELL));
            StarterRef const* best = nullptr;
            float bestScore = 0.0f, bestDist = 0.0f;
            std::unordered_set<uint32> seen;
            struct Alt { uint32 Quest; float Score; };
            std::vector<Alt> alts;
            for (int32 dx = -cells; dx <= cells; ++dx)
                for (int32 dy = -cells; dy <= cells; ++dy)
                {
                    auto it = g.Grid.find(CellKey(bot->GetMapId(), (cx0 + dx) * GRID_CELL + 1.0f, (cy0 + dy) * GRID_CELL + 1.0f));
                    if (it == g.Grid.end())
                        continue;
                    for (StarterRef const& ref : it->second)
                    {
                        float d = Dist2D(ref.P.X, ref.P.Y, bot->GetPositionX(), bot->GetPositionY());
                        if (d > radius || !seen.insert(ref.Quest).second)
                            continue;
                        float s = Score(ai, bot, c, now, ref, d, pass == 0);
                        if (s <= 0.0f)
                            continue;
                        if (alts.size() < 3)
                            alts.push_back({ ref.Quest, s });
                        if (s > bestScore)
                        {
                            bestScore = s;
                            best = &ref;
                            bestDist = d;
                        }
                    }
                }
            if (!best)
                continue;

            c.T = Task();
            c.T.K = Kind::GoGiver;
            c.T.Quest = best->Quest;
            c.T.NpcEntry = best->Entry;
            c.T.SinceMs = c.T.ProgressMs = now;
            c.GoalFails = 0;
            std::string altJson;
            for (Alt const& a : alts)
                altJson += StringFormat(R"({}{{"quest":{},"score":{:.3f}}})", altJson.empty() ? "" : ",", a.Quest, a.Score);
            Decision(ai, bot, pass == 0 ? "QUEST_PICK" : "QUEST_PICK_FAR", StringFormat("picked '{}' from giver {} {:.0f} yd away", QuestTitle(best->Quest), best->Entry, bestDist), best->Quest, best->Entry,
                StringFormat(R"({{"score":{:.3f},"dist":{:.0f},"radius":{:.0f},"alternatives":[{}]}})", bestScore, bestDist, radius, altJson));
            c.IdleLoggedLevel = 0;
            return true;
        }

        // nothing takeable within FarRadius: E3 hub travel, then grinding
        if (c.NoLocalLevel != bot->GetLevel())
        {
            c.NoLocalLevel = bot->GetLevel();
            Decision(ai, bot, "QUEST_NO_LOCAL", StringFormat("no takeable quest within {:.0f} yd at level {}", Cfg().FarRadius, bot->GetLevel()), 0, 0,
                StringFormat(R"({{"map":{},"x":{:.0f},"y":{:.0f},"level":{},"rewarded":{}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetLevel(), c.Rewarded));
        }
        if (now >= c.NextHubMs)
        {
            c.NextHubMs = now + Cfg().HubScanSec * 1000;
            if (PickHub(ai, bot, c, now))
                return true;
            if (c.HubNoneLevel != bot->GetLevel())
            {
                c.HubNoneLevel = bot->GetLevel();
                Blocked(ai, bot, c, 0, "QUEST_HUB_NONE", StringFormat("no quest hub on map {} fits level {}, grinding", bot->GetMapId(), bot->GetLevel()),
                    StringFormat(R"({{"map":{},"x":{:.0f},"y":{:.0f},"level":{},"hubs":{},"skipped_failed":{},"skipped_faction":{},"skipped_far":{}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetLevel(), uint32(g.Hubs.size()), c.HubSkip[0], c.HubSkip[1], c.HubSkip[2]), 0, false, BOTLOG_WARN);
            }
        }
        return StartGrind(ai, bot, c, now);
    }

    // E3: the best quest hub on this map for the bot (sum of the takeable quest scores, distance discounted), then a go_giver task into it.
    bool PickHub(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        auto const pickT0 = std::chrono::steady_clock::now();
        int32 const lvl = int32(bot->GetLevel());
        float const bx = bot->GetPositionX(), by = bot->GetPositionY();
        struct HubScore { uint32 Idx; float Score; uint32 N; float Dist; StarterRef const* Best; float BestScore; };
        std::vector<HubScore> top;
        uint32 scanned = 0;
        uint32 skipped[3] = { 0, 0, 0 };   // global_fail, faction, far
        for (uint32 idx = 0; idx < g.Hubs.size(); ++idx)
        {
            Hub const& h = g.Hubs[idx];
            if (h.Map != bot->GetMapId())
                continue;
            if (h.MinQ > 0 && h.MinQ > lvl + 2)
                continue;
            if (h.MaxQ > 0 && h.MaxQ < lvl - 4)
                continue;
            auto bl = c.HubBlack.find(idx);
            if (bl != c.HubBlack.end() && now < bl->second)
                continue;
            float const dist = Dist2D(h.X, h.Y, bx, by);
            if (char const* why = HubSkipReason(bot, idx, h, dist))
            {
                ++skipped[why[0] == 'g' ? 0 : why[0] == 'f' && why[1] == 'a' ? 1 : 2];
                continue;
            }
            ++scanned;
            HubScore hs{ idx, 0.0f, 0, dist, nullptr, 0.0f };
            std::vector<uint32> seenQ;
            for (StarterRef const& r : h.Refs)
            {
                if (r.QLevel > 0 && (r.QLevel > lvl + 2 || r.QLevel < lvl - 4))
                    continue;
                if (std::find(seenQ.begin(), seenQ.end(), r.Quest) != seenQ.end())
                    continue;
                seenQ.push_back(r.Quest);
                float sc = Score(ai, bot, c, now, r, dist, false);
                if (sc <= 0.0f)
                    continue;
                hs.Score += sc;
                ++hs.N;
                if (sc > hs.BestScore)
                {
                    hs.BestScore = sc;
                    hs.Best = &r;
                }
            }
            if (!hs.N)
                continue;
            top.push_back(hs);
        }
        std::copy(skipped, skipped + 3, c.HubSkip);
        if (top.empty())
            return false;
        std::sort(top.begin(), top.end(), [](HubScore const& a, HubScore const& b) { return a.Score > b.Score; });
        HubScore const w = top.front();
        Hub const& h = g.Hubs[w.Idx];

        c.T = Task();
        c.T.K = Kind::GoGiver;
        c.T.Quest = w.Best->Quest;
        c.T.NpcEntry = w.Best->Entry;
        c.T.HubId = w.Idx;
        c.T.SinceMs = c.T.ProgressMs = now;
        c.GoalFails = 0;
        ++c.HubTrips;
        std::string alt;
        for (size_t i = 1; i < top.size() && i < 4; ++i)
            alt += StringFormat(R"({}{{"hub":{},"score":{:.3f},"quests":{},"dist":{:.0f}}})", alt.empty() ? "" : ",", top[i].Idx, top[i].Score, top[i].N, top[i].Dist);
        uint32 zone = bot->GetMap()->GetZoneId(bot->GetPhaseShift(), h.X, h.Y, h.Z);
        Decision(ai, bot, "QUEST_HUB_TRAVEL", StringFormat("travelling to quest hub {} ({} takeable quests, {:.0f} yd, zone {})", w.Idx, w.N, w.Dist, zone), w.Best->Quest, w.Best->Entry,
            StringFormat(R"({{"hub":{},"x":{:.0f},"y":{:.0f},"z":{:.0f},"zone":{},"dist":{:.0f},"takeable":{},"givers":{},"min_q":{},"max_q":{},"hubs_scanned":{},"select_us":{},"alternatives":[{}]}})",
                w.Idx, h.X, h.Y, h.Z, zone, w.Dist, w.N, h.Givers, h.MinQ, h.MaxQ, scanned,
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pickT0).count(), alt));
        c.LastWasGrind = false;
        return true;
    }

    // ----- grind fallback -----
    bool StartGrind(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        c.T = Task();
        c.T.K = Kind::Grind;
        c.T.ObjId = bot->GetLevel();     // level at the start: a level-up ends the stint
        c.T.SinceMs = c.T.ProgressMs = c.T.TargetBestMs = now;
        c.GoalFails = 0;
        ++c.Grinds;
        if (!c.LastWasGrind)
        {
            c.LastWasGrind = true;
            c.GrindAnchorMap = bot->GetMapId();
            c.GrindAnchorX = bot->GetPositionX();
            c.GrindAnchorY = bot->GetPositionY();
            Decision(ai, bot, "QUEST_GRIND", StringFormat("no quest to do, grinding mobs of level {}", bot->GetLevel()), 0, 0,
                StringFormat(R"({{"map":{},"x":{:.0f},"y":{:.0f},"level":{}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetLevel()));
        }
        return true;
    }

    bool GrindMob(Player* bot, BotQuestCtx& c, Task& t, Creature* m, uint32 now)
    {
        if (m->IsCritter() || m->GetCreatureTemplate()->npcflag || m->IsCivilian())
            return false;
        if (!CandidateMob(bot, c, t, m, now))
            return false;
        if (EffectiveDiff(bot, m) > Cfg().GrindMaxGap)
            return false;
        if (Cfg().GrindMaxRadius > 0.0f && c.GrindAnchorMap == m->GetMapId() && Dist2D(c.GrindAnchorX, c.GrindAnchorY, m->GetPositionX(), m->GetPositionY()) > Cfg().GrindMaxRadius)
            return false;
        return true;
    }

    // R2: a live hostile mob with effective gap >= 2 close to `m` would aggro when the bot walks up to it.
    bool HighGapMobNear(Player* bot, std::vector<Creature*> const& list, Creature* m)
    {
        for (Creature* o : list)
            if (o != m && o->IsAlive() && EffectiveDiff(bot, o) >= 2 && m->GetExactDist2d(o) < 22.0f && bot->IsValidAttackTarget(o))
                return true;
        return false;
    }

    bool RunGrind(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        Task& t = c.T;
        bool const idleNow = t.Target.IsEmpty() && !bot->IsInCombat();
        if (bot->GetLevel() != t.ObjId || (idleNow && now - t.SinceMs > Cfg().GrindSec * 1000))
        {
            if (bot->GetLevel() != t.ObjId)
                c.NextHubMs = 0;   // new level: re-check the hubs at once
            StopMoving(ai, bot, c);
            Finish(c, now);
            return false;
        }
        if (!t.Target.IsEmpty())
        {
            Creature* m = ObjectAccessor::GetCreature(*bot, t.Target);
            if (!m)
            {
                t.Target.Clear();
                t.Chasing = false;
            }
            else if (m->isDead())
            {
                if (LootCorpse(ai, bot, c, t, m))
                    return true;
                if (m->IsWithinDistInMap(bot, 3.0f) || now - t.TargetSinceMs > 20000 || !m->isTappedBy(bot))
                {
                    t.Target.Clear();
                    return false;
                }
                Travel(ai, bot, c, now, m->GetPositionX(), m->GetPositionY(), m->GetPositionZ(), 2.0f, m->GetEntry());
                return false;
            }
            else
            {
                if (!ApproachAndPull(ai, bot, c, now, t, m))
                    return true;
                return false;
            }
        }
        if (bot->IsInCombat())
            return false;
        if (now >= c.NextScanMs)
        {
            c.NextScanMs = now + 1200;
            FindCreatureOptions opt;
            opt.IsAlive = FindCreatureAliveState::Alive;
            std::vector<Creature*> list;
            bot->GetCreatureListWithOptionsInGrid(list, Cfg().ScanRange, opt);
            ++t.Scans;
            t.ScanRaw = uint32(list.size());
            t.ScanOk = 0;
            Creature* best = nullptr;
            float bd = 1e9f;
            for (Creature* m : list)
            {
                if (!GrindMob(bot, c, t, m, now))
                    continue;
                ++t.ScanOk;
                float d = bot->GetExactDist2d(m);
                if (d < bd && !HighGapMobNear(bot, list, m) && !BotAggroGuarded(ai, bot, m))
                {
                    bd = d;
                    best = m;
                }
            }
            if (best)
            {
                if (bd > 45.0f)
                {
                    BotPathInfo info = BotMotion::QueryPath(bot, best->GetPositionX(), best->GetPositionY(), best->GetPositionZ());
                    if (info.NoPath || (info.Partial && info.EndGap3D > 25.0f))
                    {
                        c.Ignore[best->GetGUID().GetCounter()] = now + 120000;
                        return false;
                    }
                }
                t.Target = best->GetGUID();
                t.TargetSinceMs = now;
                t.TargetBest = bd;
                t.TargetBestMs = now;
                t.WaitStartMs = 0;
                return false;
            }
        }
        return SearchGrind(ai, bot, c, now, t);
    }

    // No live candidate in scan range: walk to the nearest grind spawn point not searched recently.
    bool SearchGrind(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Task& t)
    {
        if (t.WaitStartMs)
        {
            if (now - t.WaitStartMs < 5000)
                return false;
            c.Seen.push_back({ bot->GetPositionX(), bot->GetPositionY() });
            if (c.Seen.size() > 12)
                c.Seen.erase(c.Seen.begin());
            t.WaitStartMs = 0;
        }
        int32 const cx0 = int32(std::floor(bot->GetPositionX() / GRID_CELL));
        int32 const cy0 = int32(std::floor(bot->GetPositionY() / GRID_CELL));
        GrindPt const* best = nullptr;
        float bd = 1e9f;
        for (int32 dx = -2; dx <= 2; ++dx)
            for (int32 dy = -2; dy <= 2; ++dy)
            {
                auto it = g.GrindGrid.find(CellKey(bot->GetMapId(), (cx0 + dx) * GRID_CELL + 1.0f, (cy0 + dy) * GRID_CELL + 1.0f));
                if (it == g.GrindGrid.end())
                    continue;
                for (GrindPt const& p : it->second)
                {
                    bool recent = false;
                    for (Visited const& v : c.Seen)
                        recent = recent || Dist2D(v.X, v.Y, p.X, p.Y) < 40.0f;
                    if (recent)
                        continue;
                    if (Cfg().GrindMaxRadius > 0.0f && c.GrindAnchorMap == bot->GetMapId() && Dist2D(c.GrindAnchorX, c.GrindAnchorY, p.X, p.Y) > Cfg().GrindMaxRadius)
                        continue;
                    float d = Dist2D(p.X, p.Y, bot->GetPositionX(), bot->GetPositionY());
                    if (d < bd)
                    {
                        bd = d;
                        best = &p;
                    }
                }
            }
        if (!best)
        {
            if (Cfg().GrindMaxRadius > 0.0f && c.GrindAnchorMap == bot->GetMapId() && Dist2D(c.GrindAnchorX, c.GrindAnchorY, bot->GetPositionX(), bot->GetPositionY()) > 20.0f)
            {
                // nothing left inside the radius: the area is grazed out, move the anchor here instead of idling
                c.GrindAnchorX = bot->GetPositionX();
                c.GrindAnchorY = bot->GetPositionY();
                c.Seen.clear();
                return false;
            }
            if (!c.Seen.empty())
                c.Seen.clear();
            else if (c.IdleLoggedLevel != bot->GetLevel())
            {
                c.IdleLoggedLevel = bot->GetLevel();
                Blocked(ai, bot, c, 0, "NO_GRIND_TARGET", StringFormat("no hostile spawn within 500 yd to grind at level {}", bot->GetLevel()),
                    StringFormat(R"({{"idle_reason":"NO_GRIND_TARGET","map":{},"x":{:.0f},"y":{:.0f}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY()), 0, false, BOTLOG_WARN);
            }
            return false;
        }
        if (bd < 15.0f)
        {
            t.WaitStartMs = now;
            if (!c.IdleLogMs || now - c.IdleLogMs >= 60000)
            {
                c.IdleLogMs = now ? now : 1;
                Decision(ai, bot, "IDLE_WAIT_SPAWN", "idle: waiting at an empty grind spawn point", 0, 0,
                    StringFormat(R"({{"idle_reason":"WAIT_SPAWN","wait_ms":5000,"seen":{}}})", c.Seen.size()));
            }
            StopMoving(ai, bot, c);
            return false;
        }
        Travel(ai, bot, c, now, best->X, best->Y, best->Z, 8.0f, 0);
        return false;
    }

    // Accepts one takeable quest from a giver within interaction range. Returns true when one was accepted.
    bool TryAcceptNearby(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, std::vector<LogEntry> const&)
    {
        auto it = g.Grid.find(CellKey(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY()));
        if (it == g.Grid.end())
            return false;
        std::unordered_set<uint32> entries;
        for (StarterRef const& ref : it->second)
            if (Dist2D(ref.P.X, ref.P.Y, bot->GetPositionX(), bot->GetPositionY()) < 40.0f)
                entries.insert(ref.Entry);
        for (uint32 entry : entries)
        {
            Creature* giver = FindLiveNpc(bot, entry, 8.0f);
            if (!giver || !bot->CanInteractWithQuestGiver(giver))
                continue;
            if (AcceptFrom(ai, bot, c, now, giver))
                return true;
        }
        return false;
    }

    bool AcceptFrom(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Creature* giver)
    {
        auto it = g.StartsBy.find(giver->GetEntry());
        if (it == g.StartsBy.end())
            return false;
        for (uint32 questId : it->second)
        {
            Quest const* q = sObjectMgr->GetQuestTemplate(questId);
            if (!q || c.Blacklisted(questId, now) || !giver->hasQuest(questId))
                continue;
            if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId))
                continue;
            if (CachedAnalysis(q).Code)
                continue;
            if (!bot->CanTakeQuest(q, false))
                continue;
            if (bot->GetQuestLevel(q) > int32(bot->GetLevel()) + 2)
                continue;
            if (!bot->CanAddQuest(q, false))
            {
                Blocked(ai, bot, c, questId, bot->SatisfyQuestLog(false) ? "BAG_FULL" : "QUEST_LOG_FULL", StringFormat("cannot add '{}' to the log", q->GetLogTitle()));
                continue;
            }
            bot->AddQuestAndCheckCompletion(q, giver);
            ++c.Accepted;
            c.T.ProgressMs = now;
            return true;
        }
        return false;
    }

    // ----- E1/E4: trainer and vendor visits -----
    static bool FriendlyNpc(Player* bot, uint32 faction)
    {
        FactionTemplateEntry const* bf = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* nf = sFactionTemplateStore.LookupEntry(faction);
        return !(bf && nf && nf->IsHostileTo(bf));
    }

    SvcPt const* NearestSvc(Player* bot, BotQuestCtx& c, uint32 now, std::vector<SvcPt> const& list, bool needRepair, bool needBags, float maxDist, float& dOut)
    {
        SvcPt const* best = nullptr;
        float bs = 1e9f;
        dOut = 0.0f;
        for (SvcPt const& p : list)
        {
            if (p.Map != bot->GetMapId())
                continue;
            float const d = Dist2D(p.X, p.Y, bot->GetPositionX(), bot->GetPositionY());
            if (d > maxDist)
                continue;
            auto bl = c.SvcBlack.find(p.Entry);
            if (bl != c.SvcBlack.end() && now < bl->second)
                continue;
            if (needRepair && !p.Repair)
                continue;
            if (needBags)
            {
                auto vb = g.VendorBags.find(p.Entry);
                if (vb == g.VendorBags.end() || vb->second.empty())
                    continue;
            }
            if (!FriendlyNpc(bot, p.Faction))
                continue;
            if (d < bs)
            {
                bs = d;
                best = &p;
                dOut = d;
            }
        }
        return best;
    }

    struct TrainEval { uint32 Avail = 0, Affordable = 0, Want = 0; uint64 CostAll = 0; };

    static TrainEval EvalTrain(Player* bot, Trainer::Trainer const* tr)
    {
        TrainEval ev;
        for (Trainer::Spell const& sp : tr->GetSpells())
        {
            if (tr->GetSpellState(bot, &sp) != Trainer::SpellState::Available || !tr->CanTeachSpell(bot, &sp))
                continue;
            ++ev.Avail;
            ev.CostAll += sp.MoneyCost;
            if (bot->GetMoney() >= sp.MoneyCost)
                ++ev.Affordable;
            else
                ev.Want = ev.Want ? std::min<uint32>(ev.Want, sp.MoneyCost) : sp.MoneyCost;
        }
        return ev;
    }

    // copper the bot still wants to spend on spells at the nearest class trainer (bags come second)
    uint64 TrainReserve(Player* bot, BotQuestCtx& c, uint32 now)
    {
        auto it = g.Trainers.find(bot->GetClass());
        if (it == g.Trainers.end())
            return 0;
        float d;
        SvcPt const* tp = NearestSvc(bot, c, now, it->second, false, false, 4000.0f, d);
        Trainer::Trainer const* tr = tp ? sObjectMgr->GetTrainer(tp->TrainerId) : nullptr;
        return tr ? EvalTrain(bot, tr).CostAll : 0;
    }

    static uint64 RepairCost(Player* bot)
    {
        uint64 total = 0;
        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                total += item->CalculateDurabilityRepairCost(1.0f, false);
        return total;
    }

    // bag slot to fill: an empty bag slot, else the smallest empty bag worn. 0xFF when nothing can be upgraded.
    static uint8 BagTargetSlot(Player* bot, uint32& replaceSize)
    {
        replaceSize = 0;
        uint8 best = 0xFF;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Item* it = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!it)
            {
                replaceSize = 0;
                return slot;
            }
            Bag* b = bot->GetBagByPos(slot);
            if (b && b->IsEmpty() && (best == 0xFF || b->GetBagSize() < replaceSize))
            {
                best = slot;
                replaceSize = b->GetBagSize();
            }
        }
        return best;
    }

    // 0 = keep, 1 = grey, 2 = unusable armor/weapon
    static int JunkClass(Player* bot, Item* item)
    {
        if (item->IsBag())
            return 0;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->GetClass() == ITEM_CLASS_QUEST || proto->GetStartQuest() || bot->HasQuestForItem(item->GetEntry()))
            return 0;
        if (!proto->GetSellPrice())
            return 0;
        if (proto->GetQuality() == ITEM_QUALITY_POOR)
            return 1;
        if ((proto->GetClass() == ITEM_CLASS_ARMOR || proto->GetClass() == ITEM_CLASS_WEAPON) && proto->GetQuality() < ITEM_QUALITY_EPIC && bot->CanUseItem(proto, true) != EQUIP_ERR_OK)
            return 2;
        return 0;
    }

    void StartService(Player*, BotQuestCtx& c, uint32 now, uint8 svc, SvcPt const& pt, bool repair, bool bags)
    {
        c.T = Task();
        c.T.K = Kind::Service;
        c.T.Svc = svc;
        c.T.NpcEntry = pt.Entry;
        c.T.SvcX = pt.X; c.T.SvcY = pt.Y; c.T.SvcZ = pt.Z;
        c.T.SvcTrainerId = pt.TrainerId;
        c.T.SvcRepair = repair;
        c.T.SvcBags = bags;
        c.T.SinceMs = c.T.ProgressMs = now;
        c.GoalFails = 0;
        c.LastWasGrind = false;
    }

    // Decides whether a trainer or vendor trip is due and starts it. Throttled, cheap in the common case.
    bool ServiceDue(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        if (now < c.NextSvcMs || bot->IsInCombat())
            return false;
        c.NextSvcMs = now + 4000;
        uint8 const level = bot->GetLevel();

        // 1. trainer: new level, or enough money for the cheapest spell we could not afford before. Spells have money priority.
        if (level >= 2 && now >= c.TrainRetryMs && (c.TrainLevel != level || (c.TrainWant && bot->GetMoney() >= c.TrainWant)))
        {
            auto tl = g.Trainers.find(bot->GetClass());
            float d = 0.0f;
            SvcPt const* tp = tl == g.Trainers.end() ? nullptr : NearestSvc(bot, c, now, tl->second, false, false, 4000.0f, d);
            if (!tp)
            {
                c.TrainLevel = level;
                c.TrainWant = 0;
                if (c.NoTrainerLevel != level)
                {
                    c.NoTrainerLevel = level;
                    // classify the gap: no trainer of the class at all, none friendly to this race anywhere (data gap, e.g. Undead paladin has no Horde
                    // paladin trainer), none on this map, or only blacklisted/too far ones
                    uint32 onMap = 0, friendlyOnMap = 0, friendlyAny = 0;
                    if (tl != g.Trainers.end())
                        for (SvcPt const& p : tl->second)
                        {
                            bool const fr = FriendlyNpc(bot, p.Faction);
                            friendlyAny += fr;
                            if (p.Map == bot->GetMapId())
                            {
                                ++onMap;
                                friendlyOnMap += fr;
                            }
                        }
                    char const* why = tl == g.Trainers.end() ? "no_trainer_of_class_in_world" : !friendlyAny ? "no_friendly_trainer_for_race_in_world" :
                        !friendlyOnMap ? "no_friendly_trainer_on_map" : "friendly_trainers_blacklisted_or_far";
                    Blocked(ai, bot, c, 0, "TRAIN_NO_TRAINER", StringFormat("no reachable class trainer on map {} within 4000 yd (class {}, race {}, level {}): {}", bot->GetMapId(), bot->GetClass(), bot->GetRace(), level, why),
                        StringFormat(R"({{"map":{},"class":{},"race":{},"level":{},"x":{:.0f},"y":{:.0f},"why":"{}","on_map":{},"friendly_on_map":{},"friendly_any":{}}})", bot->GetMapId(), bot->GetClass(), bot->GetRace(), level, bot->GetPositionX(), bot->GetPositionY(), why, onMap, friendlyOnMap, friendlyAny), 0, false);
                    // no point re-checking every level when the world has no friendly trainer for this class and race
                    if (!friendlyAny)
                        c.TrainRetryMs = now + 10 * MINUTE * IN_MILLISECONDS;
                }
            }
            else
            {
                Trainer::Trainer const* tr = sObjectMgr->GetTrainer(tp->TrainerId);
                TrainEval ev = tr ? EvalTrain(bot, tr) : TrainEval();
                c.TrainLevel = level;
                c.TrainWant = ev.Want;
                if (ev.Affordable)
                {
                    StartService(bot, c, now, 1, *tp, false, false);
                    Decision(ai, bot, "TRAIN_TRIP", StringFormat("walking to class trainer {} ({:.0f} yd) for {} spells", tp->Entry, d, ev.Affordable), 0, tp->Entry,
                        StringFormat(R"({{"trainer_id":{},"dist":{:.0f},"affordable":{},"available":{},"money":{}}})", tp->TrainerId, d, ev.Affordable, ev.Avail, bot->GetMoney()));
                    return true;
                }
                if (ev.Avail && c.NoMoneyLevel != level)
                {
                    c.NoMoneyLevel = level;
                    Blocked(ai, bot, c, 0, "TRAIN_NO_MONEY", StringFormat("{} spells available at level {} but the cheapest costs {} copper (bot has {})", ev.Avail, level, ev.Want, bot->GetMoney()),
                        StringFormat(R"({{"available":{},"cheapest":{},"money":{},"level":{}}})", ev.Avail, ev.Want, bot->GetMoney(), level), tp->Entry, false);
                }
            }
        }

        // 2. vendor
        uint32 const freeSlots = bot->GetFreeInventorySlotCount();
        bool const forced = c.VendorNow;
        bool const full = freeSlots <= 1;
        uint64 const repairCost = RepairCost(bot);
        bool const needRepair = repairCost >= 150 && bot->GetMoney() >= repairCost * 2;
        uint32 replaceSize = 0;
        bool const needBags = level >= 3 && now >= c.NextBagMs && g.MinBagPrice != 0xFFFFFFFFu && bot->GetMoney() >= g.MinBagPrice && BagTargetSlot(bot, replaceSize) != 0xFF;
        if (!forced && !(now >= c.NextVendorMs && (full || needRepair || needBags)))
            return false;

        float d = 0.0f;
        float const maxD = (full || forced) ? 3000.0f : 800.0f;
        SvcPt const* vp = nullptr;
        if (needBags && !full && !needRepair)
            vp = NearestSvc(bot, c, now, g.Vendors, false, true, maxD, d);
        if (!vp && needRepair)
            vp = NearestSvc(bot, c, now, g.Vendors, true, false, maxD, d);
        if (!vp)
            vp = NearestSvc(bot, c, now, g.Vendors, false, false, maxD, d);
        c.VendorNow = false;
        if (needBags)
            c.NextBagMs = now + 300 * 1000;
        if (!vp)
        {
            c.NextVendorMs = now + 120 * 1000;
            if (c.NoVendorLevel != level)
            {
                c.NoVendorLevel = level;
                Blocked(ai, bot, c, 0, "VENDOR_NONE", StringFormat("no reachable vendor on map {} within {:.0f} yd (free slots {})", bot->GetMapId(), maxD, freeSlots),
                    StringFormat(R"({{"map":{},"free_slots":{},"repair_cost":{},"x":{:.0f},"y":{:.0f}}})", bot->GetMapId(), freeSlots, repairCost, bot->GetPositionX(), bot->GetPositionY()), 0, false);
            }
            return false;
        }
        StartService(bot, c, now, 2, *vp, needRepair && vp->Repair, needBags);
        ++c.VendorTrips;
        Decision(ai, bot, "VENDOR_TRIP", StringFormat("walking to vendor {} ({:.0f} yd): free slots {}, repair {} copper, bag upgrade {}", vp->Entry, d, freeSlots, repairCost, needBags ? "yes" : "no"), 0, vp->Entry,
            StringFormat(R"({{"dist":{:.0f},"free_slots":{},"repair_cost":{},"repair_vendor":{},"bag_upgrade":{},"forced":{},"money":{}}})", d, freeSlots, repairCost, vp->Repair, needBags, forced, bot->GetMoney()));
        return true;
    }

    bool RunService(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        Task& t = c.T;
        if (now - t.SinceMs > 10 * 60 * 1000)
        {
            Drop(ai, bot, c, now, 0, "UNREACHABLE", StringFormat("gave up walking to {} npc {} after 10 min", t.Svc == 1 ? "trainer" : "vendor", t.NpcEntry), std::string(), t.NpcEntry, 0);
            return true;
        }
        Creature* npc = FindLiveNpc(bot, t.NpcEntry, 45.0f);
        if (npc && npc->IsAlive() && bot->IsWithinDistInMap(npc, 4.5f))
        {
            StopMoving(ai, bot, c);
            if (t.Svc == 1)
                DoTrain(ai, bot, c, now, npc);
            else
                DoVendor(ai, bot, c, now, npc);
            Finish(c, now);
            return true;
        }
        float tx = t.SvcX, ty = t.SvcY, tz = t.SvcZ;
        if (npc)
        {
            tx = npc->GetPositionX(); ty = npc->GetPositionY(); tz = npc->GetPositionZ();
            if (Dist2D(tx, ty, bot->GetPositionX(), bot->GetPositionY()) < 10.0f && ++t.LegIssues > 40)
            {
                Drop(ai, bot, c, now, 0, "GIVER_NOT_INTERACTABLE", StringFormat("npc {} seen but never interactable", t.NpcEntry), std::string(), t.NpcEntry, 0);
                return true;
            }
        }
        else if (Dist2D(tx, ty, bot->GetPositionX(), bot->GetPositionY()) < 10.0f)
        {
            if (!t.WaitStartMs)
                t.WaitStartMs = now;
            else if (now - t.WaitStartMs > 25000)
            {
                Drop(ai, bot, c, now, 0, "GIVER_NOT_INTERACTABLE", StringFormat("npc {} is not at its spawn point", t.NpcEntry), std::string(), t.NpcEntry, 0);
                return true;
            }
            return false;
        }
        Travel(ai, bot, c, now, tx, ty, tz, 3.0f, t.NpcEntry);
        return false;
    }

    void DoTrain(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Creature* npc)
    {
        (void)now;
        uint32 const tid = sObjectMgr->GetCreatureDefaultTrainer(npc->GetEntry());
        Trainer::Trainer const* tr = tid ? sObjectMgr->GetTrainer(tid) : nullptr;
        uint8 const level = bot->GetLevel();
        c.TrainLevel = level;
        if (!tr)
        {
            Blocked(ai, bot, c, 0, "TRAIN_NO_TRAINER", StringFormat("npc {} has no trainer data", npc->GetEntry()), std::string(), npc->GetEntry(), false);
            return;
        }
        uint64 const moneyBefore = bot->GetMoney();
        uint32 learned = 0, attempted = 0;
        std::string ids;
        for (int pass = 0; pass < 4; ++pass)
        {
            std::vector<Trainer::Spell const*> todo;
            for (Trainer::Spell const& sp : tr->GetSpells())
                if (tr->GetSpellState(bot, &sp) == Trainer::SpellState::Available && tr->CanTeachSpell(bot, &sp) && bot->GetMoney() >= sp.MoneyCost)
                    todo.push_back(&sp);
            if (todo.empty())
                break;
            std::sort(todo.begin(), todo.end(), [](Trainer::Spell const* a, Trainer::Spell const* b)
            {
                return a->ReqLevel != b->ReqLevel ? a->ReqLevel < b->ReqLevel : a->MoneyCost < b->MoneyCost;
            });
            uint32 passLearned = 0;
            for (Trainer::Spell const* sp : todo)
            {
                if (bot->GetMoney() < sp->MoneyCost || tr->GetSpellState(bot, sp) != Trainer::SpellState::Available)
                    continue;
                ++attempted;
                tr->TeachSpell(npc, bot, sp->SpellId);
                if (tr->GetSpellState(bot, sp) == Trainer::SpellState::Known)
                {
                    ++passLearned;
                    if (ids.size() < 200)
                        ids += StringFormat("{}{}", ids.empty() ? "" : ",", sp->SpellId);
                }
            }
            learned += passLearned;
            if (!passLearned)
                break;
        }
        TrainEval left = EvalTrain(bot, tr);
        c.TrainWant = left.Want;
        c.TrainedTotal += learned;
        if (learned)
            Decision(ai, bot, "TRAINED", StringFormat("learned {} spells from trainer {} for {} copper", learned, npc->GetEntry(), moneyBefore - bot->GetMoney()), 0, npc->GetEntry(),
                StringFormat(R"({{"learned":{},"attempted":{},"spent":{},"money_left":{},"level":{},"unaffordable_left":{},"spells":[{}]}})", learned, attempted, moneyBefore - bot->GetMoney(), bot->GetMoney(), level, left.Avail, ids));
        else
            Blocked(ai, bot, c, 0, "TRAIN_NO_MONEY", StringFormat("nothing learned at trainer {}: {} available, attempted {}", npc->GetEntry(), left.Avail, attempted),
                StringFormat(R"({{"available":{},"attempted":{},"money":{},"cheapest":{}}})", left.Avail, attempted, bot->GetMoney(), left.Want), npc->GetEntry(), false);
    }

    void DoVendor(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Creature* npc)
    {
        uint8 const level = bot->GetLevel();
        uint32 const freeBefore = bot->GetFreeInventorySlotCount();
        uint64 const money0 = bot->GetMoney();

        // sell junk: greys first, then unusable armor/weapons
        std::vector<std::pair<int, Item*>> sells;
        bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
        {
            if (int k = JunkClass(bot, item))
                sells.push_back({ k, item });
            return ItemSearchCallbackResult::Continue;
        });
        std::stable_sort(sells.begin(), sells.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        uint32 nGrey = 0, nUnusable = 0, nStacks = 0;
        std::string sold;
        for (auto const& [kind, item] : sells)
        {
            uint32 const cnt = item->GetCount();
            uint32 const entry = item->GetEntry();
            if (bot->CanSellItemToVendor(item, cnt) || bot->SellItemToVendor(item, cnt))
                continue;   // Optional<SellResult>: a value means refused
            (kind == 1 ? nGrey : nUnusable) += 1;
            ++nStacks;
            if (sold.size() < 120)
                sold += StringFormat("{}{}", sold.empty() ? "" : ",", entry);
        }
        uint32 const freeAfter = bot->GetFreeInventorySlotCount();
        if (nStacks)
            Decision(ai, bot, "SOLD_ITEMS", StringFormat("sold {} stacks ({} grey, {} unusable) for {} copper", nStacks, nGrey, nUnusable, bot->GetMoney() - money0), 0, npc->GetEntry(),
                StringFormat(R"({{"grey":{},"unusable":{},"copper":{},"free_before":{},"free_after":{},"items":[{}]}})", nGrey, nUnusable, bot->GetMoney() - money0, freeBefore, freeAfter, sold));

        // repair
        uint64 const repairCost = RepairCost(bot);
        if (repairCost && (npc->GetCreatureTemplate()->npcflag & uint64(UNIT_NPC_FLAG_REPAIR)) && bot->GetMoney() >= repairCost)
        {
            uint64 const before = bot->GetMoney();
            bot->DurabilityRepairAll(true, bot->GetReputationPriceDiscount(npc), false);
            if (bot->GetMoney() < before)
                Decision(ai, bot, "REPAIRED", StringFormat("repaired equipment for {} copper", before - bot->GetMoney()), 0, npc->GetEntry(),
                    StringFormat(R"({{"cost":{},"money_left":{}}})", before - bot->GetMoney(), bot->GetMoney()));
        }

        // bag upgrade: training money has priority
        uint32 replaceSize = 0;
        uint8 const slot = BagTargetSlot(bot, replaceSize);
        auto vb = g.VendorBags.find(npc->GetEntry());
        if (slot != 0xFF && vb != g.VendorBags.end() && !vb->second.empty() && level >= 3)
        {
            uint64 const reserve = TrainReserve(bot, c, now);
            uint64 const money = bot->GetMoney();
            BagOffer const* pick = nullptr;
            uint32 cheapest = 0xFFFFFFFFu;
            for (BagOffer const& o : vb->second)
            {
                if (o.Slots <= replaceSize || o.ReqLevel > int32(level))
                    continue;
                cheapest = std::min<uint32>(cheapest, o.Price);
                if (money < reserve + o.Price)
                    continue;
                if (!pick || o.Slots > pick->Slots || (o.Slots == pick->Slots && o.Price < pick->Price))
                    pick = &o;
            }
            if (pick)
            {
                uint64 const before = bot->GetMoney();
                bot->BuyItemFromVendorSlot(npc->GetGUID(), pick->VendorSlot, pick->Item, 1, NULL_BAG, NULL_SLOT);
                Item* item = bot->GetMoney() < before ? bot->GetItemByEntry(pick->Item) : nullptr;
                bool equipped = false;
                if (item)
                {
                    uint16 dest = 0;
                    if (bot->CanEquipItem(slot, dest, item, replaceSize != 0) == EQUIP_ERR_OK)
                    {
                        bot->SwapItem(item->GetPos(), dest);
                        Bag* nb = bot->GetBagByPos(slot);
                        equipped = nb && nb->GetBagSize() == pick->Slots;
                    }
                }
                if (item)
                    Decision(ai, bot, "BAG_BOUGHT", StringFormat("bought a {} slot bag (item {}) for {} copper, {}", pick->Slots, pick->Item, before - bot->GetMoney(), equipped ? "equipped" : "left in bags"), 0, npc->GetEntry(),
                        StringFormat(R"({{"item":{},"slots":{},"price":{},"bag_slot":{},"replaced_size":{},"equipped":{},"money_left":{}}})", pick->Item, pick->Slots, before - bot->GetMoney(), slot, replaceSize, equipped, bot->GetMoney()));
                else
                    Blocked(ai, bot, c, 0, "BAG_BUY_FAILED", StringFormat("could not buy bag {} (no inventory space or vendor refused)", pick->Item),
                        StringFormat(R"({{"item":{},"free_slots":{},"money":{}}})", pick->Item, bot->GetFreeInventorySlotCount(), bot->GetMoney()), npc->GetEntry(), false);
            }
            else if (cheapest != 0xFFFFFFFFu && c.BagNoMoneyLevel != level)
            {
                c.BagNoMoneyLevel = level;
                Blocked(ai, bot, c, 0, "BAG_NO_MONEY", StringFormat("cannot afford a bag: cheapest {} copper, bot has {} (spell reserve {})", cheapest, money, reserve),
                    StringFormat(R"({{"cheapest":{},"money":{},"reserve":{},"level":{}}})", cheapest, money, reserve, level), npc->GetEntry(), false);
            }
        }
        c.NextVendorMs = now + (bot->GetFreeInventorySlotCount() <= 1 ? 600 : 120) * 1000;
    }

    // ----- running the task -----
    bool RunTask(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        Task& t = c.T;
        if (t.K == Kind::Grind)
            return RunGrind(ai, bot, c, now);
        if (t.K == Kind::Service)
            return RunService(ai, bot, c, now);
        Quest const* q = sObjectMgr->GetQuestTemplate(t.Quest);
        if (!q)
        {
            Finish(c, now);
            return false;
        }
        switch (t.K)
        {
            case Kind::GoGiver:
                return RunNpcVisit(ai, bot, c, now, q, true);
            case Kind::GoEnder:
                return RunNpcVisit(ai, bot, c, now, q, false);
            case Kind::Talk:
                return RunTalk(ai, bot, c, now, q);
            case Kind::Kill:
                return RunKill(ai, bot, c, now, q);
            case Kind::Loot:
                return RunLoot(ai, bot, c, now, q);
            default:
                Finish(c, now);
                return false;
        }
    }

    // Go to a giver (accept) or ender (turn in). `giver` true = accept flow.
    bool RunNpcVisit(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q, bool giver)
    {
        Task& t = c.T;
        uint32 const questId = t.Quest;
        QuestStatus st = bot->GetQuestStatus(questId);
        if (giver && (st != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId)))
        {
            Finish(c, now);   // taken meanwhile (opportunistic accept) or done
            return false;
        }
        if (!giver && st != QUEST_STATUS_COMPLETE)
        {
            Finish(c, now);
            return false;
        }
        if (now - t.SinceMs > 15 * 60 * 1000)
        {
            Drop(ai, bot, c, now, questId, "UNREACHABLE", StringFormat("gave up walking to the {} of '{}' after 15 min", giver ? "giver" : "ender", q->GetLogTitle()), std::string(), t.NpcEntry, 1800);
            return true;
        }

        Creature* npc = FindLiveNpc(bot, t.NpcEntry, 45.0f);
        float const arrive = 3.0f;
        if (npc && giver && t.HubId != 0xFFFFFFFFu)
            NoteHubVerified(t.HubId);   // a bot got here: the hub is reachable
        if (npc && bot->CanInteractWithQuestGiver(npc))
        {
            StopMoving(ai, bot, c);
            if (giver)
            {
                if (!npc->hasQuest(questId) || !bot->CanTakeQuest(q, false))
                {
                    Drop(ai, bot, c, now, questId, bot->CanTakeQuest(q, false) ? "GIVER_NOT_INTERACTABLE" : "QUEST_PREREQ",
                        StringFormat("giver {} cannot give '{}' now", t.NpcEntry, q->GetLogTitle()), std::string(), t.NpcEntry, 900);
                    return true;
                }
                if (!bot->CanAddQuest(q, false))
                {
                    Drop(ai, bot, c, now, questId, "QUEST_LOG_FULL", StringFormat("cannot add '{}' to the log", q->GetLogTitle()), std::string(), t.NpcEntry, 300);
                    return true;
                }
                bot->AddQuestAndCheckCompletion(q, npc);
                ++c.Accepted;
                Finish(c, now);  // next Choose also accepts the other quests of this giver
                return true;
            }
            return TurnIn(ai, bot, c, now, q, npc);
        }

        // walk
        float tx, ty, tz;
        if (npc)
        {
            tx = npc->GetPositionX(); ty = npc->GetPositionY(); tz = npc->GetPositionZ();
        }
        else
        {
            Pt const* p = NearestSpawn(bot, t.NpcEntry);
            if (!p)
            {
                Drop(ai, bot, c, now, questId, giver ? "NO_STARTER_SPAWN" : "NO_ENDER_SPAWN", StringFormat("npc {} has no spawn on this map", t.NpcEntry), std::string(), t.NpcEntry, 3600);
                return true;
            }
            tx = p->X; ty = p->Y; tz = p->Z;
            // at the spawn point but nobody there
            if (Dist2D(tx, ty, bot->GetPositionX(), bot->GetPositionY()) < 10.0f)
            {
                if (!t.WaitStartMs)
                    t.WaitStartMs = now;
                else if (now - t.WaitStartMs > 25000)
                {
                    Drop(ai, bot, c, now, questId, "GIVER_NOT_INTERACTABLE", StringFormat("npc {} is not at its spawn point", t.NpcEntry), std::string(), t.NpcEntry, 900);
                    return true;
                }
                return false;
            }
        }
        if (npc && Dist2D(tx, ty, bot->GetPositionX(), bot->GetPositionY()) < 10.0f && ++t.LegIssues > 40)
        {
            Drop(ai, bot, c, now, questId, "GIVER_NOT_INTERACTABLE", StringFormat("npc {} seen but never interactable", t.NpcEntry), std::string(), t.NpcEntry, 900);
            return true;
        }
        Travel(ai, bot, c, now, tx, ty, tz, arrive, t.NpcEntry);
        return false; // let move_to_goal run in the same tick
    }

    uint32 ChooseReward(Player* bot, Quest const* q)
    {
        uint32 bestId = 0;
        double bestScore = -1.0;
        for (uint32 i = 0; i < q->GetRewChoiceItemsCount(); ++i)
        {
            uint32 id = q->RewardChoiceItemId[i];
            if (!id || q->RewardChoiceItemType[i] != LootItemType::Item)
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id);
            if (!proto)
                continue;
            double s = double(proto->GetSellPrice());
            if (bot->CanUseItem(proto) == EQUIP_ERR_OK)
                s += (proto->IsArmor() || proto->IsWeapon()) ? 1e9 : 1e6;
            if (s > bestScore)
            {
                bestScore = s;
                bestId = id;
            }
        }
        return bestId;
    }

    // Equips a quest reward only when it is a clear upgrade: empty slot, or higher item level than what is worn.
    void TryEquipReward(BotAI* ai, Player* bot, uint32 itemId, uint32 questId)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto || !(proto->IsArmor() || proto->IsWeapon()) || bot->IsInCombat())
            return;
        Item* item = bot->GetItemByEntry(itemId);
        if (!item || item->IsEquipped())
            return;
        uint16 dest = 0;
        if (bot->CanEquipItem(NULL_SLOT, dest, item, true) != EQUIP_ERR_OK)
            return;
        Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(dest & 255));
        uint32 wornLevel = 0;
        if (worn)
        {
            ItemTemplate const* wp = worn->GetTemplate();
            wornLevel = wp->GetBaseItemLevel();
            if (wp->GetQuality() > proto->GetQuality() || proto->GetBaseItemLevel() <= wornLevel)
                return;
        }
        uint32 wornEntry = worn ? worn->GetEntry() : 0;
        bot->SwapItem(item->GetPos(), dest);
        Item* now = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(dest & 255));
        BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "QUEST_REWARD_EQUIPPED",
            StringFormat("equipped quest reward {} ({})", proto->GetName(DEFAULT_LOCALE), now && now->GetEntry() == itemId ? "ok" : "swap failed"));
        ev.QuestId = questId;
        ev.TargetEntry = itemId;
        ev.Details = StringFormat(R"({{"slot":{},"replaced_entry":{},"old_ilvl":{},"new_ilvl":{}}})", dest & 255, wornEntry, wornLevel, proto->GetBaseItemLevel());
        sBotMgr->LogEvent(std::move(ev));
    }

    bool TurnIn(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q, Creature* npc)
    {
        uint32 const questId = q->GetQuestId();
        if (!npc->hasInvolvedQuest(questId))
        {
            Drop(ai, bot, c, now, questId, "NO_ENDER_ROW", StringFormat("npc {} does not take '{}'", npc->GetEntry(), q->GetLogTitle()), std::string(), npc->GetEntry(), 1800);
            return true;
        }
        if (!bot->CanRewardQuest(q, false))
        {
            Drop(ai, bot, c, now, questId, "DATA_ERROR", StringFormat("'{}' is marked complete but cannot be rewarded", q->GetLogTitle()), std::string(), npc->GetEntry(), 900);
            return true;
        }
        uint32 reward = ChooseReward(bot, q);
        LootItemType type = LootItemType::Item;
        if (!bot->CanRewardQuest(q, type, reward, false))
        {
            // a reward item that does not exist in the item data can never be stored (data gap): reward the rest of the quest anyway
            uint32 missing = 0;
            for (uint32 i = 0; i < q->GetRewItemsCount(); ++i)
                if (q->RewardItemId[i] && !sObjectMgr->GetItemTemplate(q->RewardItemId[i]))
                    missing = q->RewardItemId[i];
            if (reward && !sObjectMgr->GetItemTemplate(reward))
                missing = reward;
            if (missing)
            {
                Blocked(ai, bot, c, questId, "REWARD_ITEM_MISSING", StringFormat("reward item {} of '{}' is not in the item data, turning in without it", missing, q->GetLogTitle()),
                    StringFormat(R"({{"reward_item":{}}})", missing), npc->GetEntry(), true, BOTLOG_INFO);
                reward = 0;
                for (uint32 i = 0; i < q->GetRewChoiceItemsCount(); ++i)
                    if (q->RewardChoiceItemId[i] && sObjectMgr->GetItemTemplate(q->RewardChoiceItemId[i]) && bot->CanRewardQuest(q, type, q->RewardChoiceItemId[i], false))
                    {
                        reward = q->RewardChoiceItemId[i];
                        break;
                    }
            }
        }
        if (!bot->CanRewardQuest(q, type, reward, false) && !(reward == 0 && IsRewardDataGap(q)))
        {
            c.VendorNow = true;   // E4: bags are full, sell first
            Drop(ai, bot, c, now, questId, "BAG_FULL", StringFormat("no room for the reward of '{}'", q->GetLogTitle()), StringFormat(R"({{"reward_item":{}}})", reward), npc->GetEntry(), 300);
            return true;
        }
        uint8 levelBefore = bot->GetLevel();
        bot->RewardQuest(q, type, reward, npc);
        ++c.Rewarded;
        if (reward)
            TryEquipReward(ai, bot, reward, questId);
        Decision(ai, bot, "QUEST_TURNED_IN", StringFormat("turned in '{}'", q->GetLogTitle()), questId, npc->GetEntry(),
            StringFormat(R"({{"reward_item":{},"level_before":{},"level_after":{}}})", reward, levelBefore, bot->GetLevel()));
        Finish(c, now);
        return true;
    }

    bool RunTalk(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q)
    {
        Task& t = c.T;
        if (bot->GetQuestStatus(t.Quest) != QUEST_STATUS_INCOMPLETE)
        {
            Finish(c, now);
            return false;
        }
        Creature* npc = FindLiveNpc(bot, t.NpcEntry, 45.0f);
        if (npc && npc->IsWithinDistInMap(bot, npc->GetCombatReach() + 4.0f))
        {
            StopMoving(ai, bot, c);
            bot->TalkedToCreature(npc->GetEntry(), npc->GetGUID());
            Finish(c, now);
            return true;
        }
        if (now - t.SinceMs > 10 * 60 * 1000)
        {
            Drop(ai, bot, c, now, t.Quest, "TARGET_UNREACHABLE", StringFormat("could not reach npc {} for '{}'", t.NpcEntry, q->GetLogTitle()), std::string(), t.NpcEntry, 1800);
            return true;
        }
        float tx, ty, tz;
        if (npc)
        {
            tx = npc->GetPositionX(); ty = npc->GetPositionY(); tz = npc->GetPositionZ();
        }
        else
        {
            Pt const* p = NearestSpawn(bot, t.NpcEntry);
            if (!p)
            {
                Drop(ai, bot, c, now, t.Quest, "NO_TARGET_SPAWN", StringFormat("npc {} has no spawn on this map", t.NpcEntry), std::string(), t.NpcEntry, 3600);
                return true;
            }
            tx = p->X; ty = p->Y; tz = p->Z;
        }
        Travel(ai, bot, c, now, tx, ty, tz, 3.0f, t.NpcEntry);
        return false;
    }

    // ----- chest object loot -----
    // Backs a Loot task off: the quest is parked for this bot for `sec` seconds (no quarantine, this is contention, not failure).
    void GoBackOff(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q, char const* code, std::string summary, std::string details, uint32 entry, uint32 sec)
    {
        Blocked(ai, bot, c, q->GetQuestId(), code, std::move(summary), std::move(details), entry, true, BOTLOG_INFO);
        c.Blacklist[q->GetQuestId()] = now + sec * 1000;
        StopMoving(ai, bot, c);
        Finish(c, now);
    }

    // The claimed spawn turned out empty / unusable for this bot: forget it and try the next one.
    // `global` is only true for a fact about the object (confirmed not spawned with the grid loaded): then every bot cools the spawn
    // for a short while. Everything else (no path, danger, cannot interact, store failed) is this bot's problem: per-bot ignore only.
    void GoSpawnFailed(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Task& t, char const* why, bool global = false, std::string extra = std::string())
    {
        BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "QUEST_LOOT_SPAWN_EMPTY", StringFormat("object {} at spawn {} not usable ({})", t.GoEntry, t.GoSpawn, why));
        ev.QuestId = t.Quest;
        ev.TargetEntry = t.GoEntry;
        ev.Details = StringFormat(R"({{"cause":"{}","global":{},"spawn_dist":{:.0f},"bot":[{:.0f},{:.0f},{:.0f}]{}}})", why, global,
            Dist2D(t.GoX, t.GoY, bot->GetPositionX(), bot->GetPositionY()), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), extra);
        sBotMgr->LogEvent(std::move(ev));
        uint32 const respawnMs = std::min<uint32>(t.GoRespawn, 600) * 1000;
        c.GoIgnore[t.GoSpawn] = now + (global ? respawnMs : 300 * 1000);
        GoRelease(t.GoSpawn, t.GoBot, global ? std::min<uint32>(respawnMs, 120 * 1000) : 0);
        ++t.GoAttempts;
        t.GoSpawn = 0;
        t.GoUseTries = 0;
        t.GoApproachTries = 0;
        t.GoBestGo = 0.0f;
        t.GoWaitMs = 0;
        StopMoving(ai, bot, c);
    }

    // H3: hostile units around a point that would kill a low level bot: an elite or a mob 2+ levels above (EffectiveDiff >= 2), or a
    // camp of 3 or more hostile mobs within 25 yards. `list` is the bot's own grid scan (the point must be inside its range).
    // Returns true on danger; entry/count describe the worst mob and the number of hostiles.
    bool GoDangerAt(Player* bot, std::vector<Creature*> const& list, float x, float y, uint32& entry, uint32& count)
    {
        entry = 0;
        count = 0;
        bool danger = false;
        for (Creature* m : list)
        {
            if (!m->IsAlive() || m->IsCritter() || m->IsPet() || m->IsTotem() || m->IsCivilian() || m->GetCreatureTemplate()->npcflag)
                continue;
            if (Dist2D(m->GetPositionX(), m->GetPositionY(), x, y) > 25.0f || !bot->IsValidAttackTarget(m))
                continue;
            ++count;
            if (m->isWorldBoss() || EffectiveDiff(bot, m) >= 2)
            {
                if (!danger)
                    entry = m->GetEntry();
                danger = true;
            }
            else if (!entry)
                entry = m->GetEntry();
        }
        return danger || count >= 3;
    }

    void GoScanList(Player* bot, std::vector<Creature*>& list)
    {
        FindCreatureOptions opt;
        opt.IsAlive = FindCreatureAliveState::Alive;
        bot->GetCreatureListWithOptionsInGrid(list, 90.0f, opt);
    }

    // Picks and claims the nearest free spawn of the item's chest objects. 0 = claimed, 1 = all busy (other bots), 2 = all cooling or
    // ignored (respawn wait), 3 = none on this map.
    int GoPickSpawn(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Task& t, std::vector<uint32> const& gos, uint32& minWaitMs)
    {
        uint64 const me = bot->GetGUID().GetCounter();
        GoPt const* best = nullptr;
        float bd = 1e9f;
        uint32 busy = 0, cooling = 0;
        minWaitMs = 0xFFFFFFFFu;
        std::vector<Creature*> list;
        bool listed = false;
        for (uint32 en : gos)
        {
            auto it = g.GoSpawns.find(en);
            if (it == g.GoSpawns.end())
                continue;
            for (GoPt const& p : it->second)
            {
                if (p.P.Map != bot->GetMapId())
                    continue;
                auto ign = c.GoIgnore.find(p.SpawnId);
                if (ign != c.GoIgnore.end() && now < ign->second)
                {
                    ++cooling;
                    minWaitMs = std::min<uint32>(minWaitMs, ign->second - now);
                    continue;
                }
                uint32 left = 0;
                GoState st = GoGateState(p.SpawnId, me, &left);
                if (st == GoState::Busy) { ++busy; continue; }
                if (st == GoState::Cooling) { ++cooling; minWaitMs = std::min<uint32>(minWaitMs, left); continue; }
                float d = Dist2D(p.P.X, p.P.Y, bot->GetPositionX(), bot->GetPositionY());
                if (d < bd)
                {
                    // sanity before this becomes the candidate: another floor / ledge needs a real path, and a camp of mobs near the
                    // spawn point (when it is in scan range) rules the spawn out for this bot
                    bool bad = false;
                    char const* badWhy = "";
                    if (std::fabs(p.P.Z - bot->GetPositionZ()) > 15.0f && d < 150.0f)
                    {
                        BotPathInfo info = BotMotion::QueryPath(bot, p.P.X, p.P.Y, p.P.Z);
                        if (info.NoPath || (info.Partial && info.EndGap3D > 25.0f) || (info.Valid && info.GoalOffMesh && info.EndGap3D > 25.0f))
                        { bad = true; badWhy = "pick: no path to another level"; }
                    }
                    if (!bad && d < 80.0f)
                    {
                        if (!listed)
                        {
                            GoScanList(bot, list);
                            listed = true;
                        }
                        uint32 dEntry = 0, dCount = 0;
                        if (GoDangerAt(bot, list, p.P.X, p.P.Y, dEntry, dCount))
                        { bad = true; badWhy = "pick: danger"; }
                    }
                    if (bad)
                    {
                        c.GoIgnore[p.SpawnId] = now + 300 * 1000;
                        ++cooling;
                        minWaitMs = std::min<uint32>(minWaitMs, 300 * 1000);
                        BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "QUEST_LOOT_SPAWN_EMPTY", StringFormat("object {} at spawn {} skipped ({})", p.Entry, p.SpawnId, badWhy));
                        ev.QuestId = t.Quest;
                        ev.TargetEntry = p.Entry;
                        ev.Details = StringFormat(R"({{"cause":"{}","global":false,"spawn_dist":{:.0f},"bot":[{:.0f},{:.0f},{:.0f}]}})", badWhy, d, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
                        sBotMgr->LogEvent(std::move(ev));
                        continue;
                    }
                    bd = d;
                    best = &p;
                }
            }
        }
        if (best && GoTryClaim(best->SpawnId, me))
        {
            t.GoSpawn = best->SpawnId;
            t.GoBot = me;
            t.GoEntry = best->Entry;
            t.GoRespawn = best->RespawnSec;
            t.GoX = best->P.X; t.GoY = best->P.Y; t.GoZ = best->P.Z;
            t.GoWaitMs = 0;
            t.GoUseTries = 0;
            t.GoApproachTries = 0;
            t.GoBestGo = 0.0f;
            t.GoDangerMs = 0;
            return 0;
        }
        if (best || busy)
            return 1;
        return cooling ? 2 : 3;
    }

    bool RunLoot(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q)
    {
        Task& t = c.T;
        if (bot->GetQuestStatus(t.Quest) != QUEST_STATUS_INCOMPLETE)
        {
            StopMoving(ai, bot, c);
            Finish(c, now);
            return false;
        }
        uint16 slot = bot->FindQuestSlot(t.Quest);
        QuestObjective const* obj = nullptr;
        for (QuestObjective const& o : q->GetObjectives())
            if (o.ID == t.ObjId)
                obj = &o;
        if (!obj || slot >= MAX_QUEST_LOG_SIZE || bot->IsQuestObjectiveComplete(slot, q, *obj))
        {
            StopMoving(ai, bot, c);
            Finish(c, now);
            return false;
        }
        int32 const count = bot->GetQuestObjectiveData(*obj);
        if (count != t.LastCount)
        {
            t.LastCount = count;
            t.ProgressMs = now;
        }
        // overall caps: respawn waits are longer than the kill stall timer, but a task is never open forever
        if (now - t.SinceMs > 12 * 60 * 1000)
        {
            Drop(ai, bot, c, now, t.Quest, "LOOT_EXHAUSTED", StringFormat("no progress on '{}' from chest objects in 12 minutes", q->GetLogTitle()),
                StringFormat(R"({{"item":{},"attempts":{},"looted":{},"count":{},"amount":{}}})", obj->ObjectID, t.GoAttempts, t.GoLooted, count, obj->Amount), uint32(obj->ObjectID), 1800);
            return true;
        }
        if (t.GoAttempts >= 8)
        {
            Drop(ai, bot, c, now, t.Quest, "LOOT_EXHAUSTED", StringFormat("{} chest object spawns for '{}' were empty or unusable", t.GoAttempts, q->GetLogTitle()),
                StringFormat(R"({{"item":{},"attempts":{},"looted":{},"count":{},"amount":{}}})", obj->ObjectID, t.GoAttempts, t.GoLooted, count, obj->Amount), uint32(obj->ObjectID), 1800);
            return true;
        }
        if (bot->IsInCombat())
            return false;

        if (!t.GoSpawn)
        {
            std::vector<uint32> gos;
            GoEntriesOf(uint32(obj->ObjectID), gos);
            uint32 waitMs = 0;
            int r = GoPickSpawn(ai, bot, c, now, t, gos, waitMs);
            if (r == 1)
            {
                GoBackOff(ai, bot, c, now, q, "GO_BUSY", StringFormat("object spawns for '{}' are in use by other bots, doing other work", q->GetLogTitle()),
                    StringFormat(R"({{"item":{}}})", obj->ObjectID), uint32(obj->ObjectID), 180);
                return true;
            }
            if (r == 2)
            {
                // everything is looted / respawning / ruled out: do other work meanwhile, never stand around in the open waiting
                GoBackOff(ai, bot, c, now, q, "GO_RESPAWN_WAIT", StringFormat("all object spawns for '{}' are looted, respawn in {} s", q->GetLogTitle(), waitMs / 1000),
                    StringFormat(R"({{"item":{},"wait_s":{}}})", obj->ObjectID, waitMs / 1000), uint32(obj->ObjectID), std::clamp<uint32>(waitMs / 1000, 60, 600));
                return true;
            }
            if (r == 3)
            {
                Drop(ai, bot, c, now, t.Quest, "NO_TARGET_SPAWN", StringFormat("no chest object spawn for item {} on this map", obj->ObjectID), std::string(), uint32(obj->ObjectID), 3600);
                return true;
            }
            t.TargetBest = Dist2D(t.GoX, t.GoY, bot->GetPositionX(), bot->GetPositionY());
            t.TargetBestMs = now;
            BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "QUEST_LOOT_GO", StringFormat("going for object {} (spawn {}) for '{}'", t.GoEntry, t.GoSpawn, q->GetLogTitle()));
            ev.QuestId = t.Quest;
            ev.TargetEntry = t.GoEntry;
            ev.Details = StringFormat(R"({{"item":{},"dist":{:.0f},"attempts":{}}})", obj->ObjectID, t.TargetBest, t.GoAttempts);
            sBotMgr->LogEvent(std::move(ev));
        }

        float const dSpawn = Dist2D(t.GoX, t.GoY, bot->GetPositionX(), bot->GetPositionY());
        if (dSpawn < 60.0f && now >= t.GoDangerMs)
        {
            // H3: re-check the camp around the spawn point while approaching (the first pick often happens out of scan range)
            t.GoDangerMs = now + 1500;
            std::vector<Creature*> list;
            GoScanList(bot, list);
            uint32 dEntry = 0, dCount = 0;
            if (GoDangerAt(bot, list, t.GoX, t.GoY, dEntry, dCount))
            {
                GoSpawnFailed(ai, bot, c, now, t, "danger", false, StringFormat(R"(,"hostile_entry":{},"hostiles":{})", dEntry, dCount));
                return false;
            }
        }
        if (dSpawn > 20.0f)
        {
            // far: walk to the spawn point, then switch to the live object; no progress for a while = give this spawn up
            if (dSpawn < t.TargetBest - 1.0f)
            {
                t.TargetBest = dSpawn;
                t.TargetBestMs = now;
            }
            else if (now - t.TargetBestMs > 60000)
            {
                GoSpawnFailed(ai, bot, c, now, t, "no approach progress");
                return false;
            }
            Travel(ai, bot, c, now, t.GoX, t.GoY, t.GoZ, 8.0f, t.GoEntry);
            return false;
        }

        if (now < t.GoNextMs)
            return false;
        t.GoNextMs = now + 400;
        GameObject* go = bot->FindNearestGameObject(t.GoEntry, 25.0f);
        if (!go || Dist2D(go->GetPositionX(), go->GetPositionY(), t.GoX, t.GoY) > 8.0f)
        {
            // not spawned (looted by someone, respawning) or not loaded: give it a moment on arrival, then next spawn
            if (!t.GoWaitMs)
                t.GoWaitMs = now;
            if (dSpawn > 8.0f)
            {
                if (now - t.GoWaitMs > 20000)
                {
                    GoSpawnFailed(ai, bot, c, now, t, "cannot get near the spawn point");
                    return false;
                }
                Travel(ai, bot, c, now, t.GoX, t.GoY, t.GoZ, 5.0f, t.GoEntry);
                return false;
            }
            if (now - t.GoWaitMs < 3000)
                return false;
            GoSpawnFailed(ai, bot, c, now, t, "not spawned", true);
            return false;
        }
        t.GoWaitMs = 0;
        float const dGo = bot->GetExactDist(go);
        if (dGo > 3.0f)
        {
            // H1: every pass counts, and the approach has its own stall timer (best distance must improve within 20 s)
            ++t.GoApproachTries;
            if (t.GoBestGo <= 0.0f || dGo < t.GoBestGo - 0.5f)
            {
                t.GoBestGo = dGo;
                t.GoBestGoMs = now;
            }
            if (now - t.GoBestGoMs > 20000 || t.GoApproachTries >= 40)
            {
                GoSpawnFailed(ai, bot, c, now, t, "cannot reach object", false, StringFormat(R"(,"object_dist":{:.1f},"tries":{})", dGo, t.GoApproachTries));
                return false;
            }
            Travel(ai, bot, c, now, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), 2.0f, t.GoEntry);
            if (!t.GoSpawn)
                return false;   // the walk could not start and the spawn was given up
            if (dGo > 5.0f || t.GoApproachTries < 12)
                return false;
        }
        StopMoving(ai, bot, c);
        GameObject* usable = bot->GetGameObjectIfCanInteractWith(go->GetGUID());
        if (!usable || !usable->ActivateToQuest(bot))
        {
            if (++t.GoUseTries < 4)
                return false;
            GoSpawnFailed(ai, bot, c, now, t, usable ? "not active for this quest" : "cannot interact");
            return false;
        }
        bot->SetFacingToObject(usable);
        uint32 const itemBefore = obj->Type == QUEST_OBJECTIVE_ITEM ? bot->GetItemCount(uint32(obj->ObjectID), true) : 0;
        usable->Use(bot);
        bool const lootTaken = LootGameObject(bot, usable);
        if (lootTaken && obj->Type == QUEST_OBJECTIVE_ITEM && bot->GetItemCount(uint32(obj->ObjectID), true) <= itemBefore)
        {
            // M4: the loot window was emptied for us but nothing arrived (bags full, unique item): not a success
            ItemPosCountVec dest;
            bool const bagsFull = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, uint32(obj->ObjectID), 1) != EQUIP_ERR_OK;
            if (bagsFull)
                c.VendorNow = true;
            GoSpawnFailed(ai, bot, c, now, t, bagsFull ? "store failed: bags full" : "store failed");
            return false;
        }
        if (lootTaken)
        {
            ++t.GoLooted;
            uint32 const respawnMs = std::min<uint32>(t.GoRespawn, 600) * 1000;
            c.GoIgnore[t.GoSpawn] = now + respawnMs;
            GoRelease(t.GoSpawn, t.GoBot, respawnMs);
            BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "QUEST_LOOT_OBJECT", StringFormat("looted object {} for '{}'", t.GoEntry, q->GetLogTitle()));
            ev.QuestId = t.Quest;
            ev.TargetEntry = t.GoEntry;
            ev.Details = StringFormat(R"({{"item":{},"count_before":{},"amount":{},"looted_total":{}}})", obj->ObjectID, count, obj->Amount, t.GoLooted);
            sBotMgr->LogEvent(std::move(ev));
            t.GoSpawn = 0;
            t.GoUseTries = 0;
            t.GoApproachTries = 0;
            t.GoBestGo = 0.0f;
            t.GoAttempts = 0;   // a successful loot resets the failure budget
            return true;
        }
        // Use() produced no loot for us (already emptied, or the quest item is not in it)
        GoSpawnFailed(ai, bot, c, now, t, "no loot");
        return false;
    }

    // Takes every item and the money of a game object's loot window that Use() opened. Returns true when something was taken.
    bool LootGameObject(Player* bot, GameObject* go)
    {
        Loot* loot = go->GetLootForPlayer(bot);
        if (!loot)
            return false;
        bool took = false;
        for (uint32 i = 0; i < loot->items.size(); ++i)
            if (LootItem* li = loot->LootItemInSlot(i, bot))
                if (!li->is_looted)
                {
                    bot->StoreLootItem(go->GetGUID(), uint8(i), loot);
                    took = true;
                }
        if (loot->gold)
        {
            bot->ModifyMoney(int64(loot->gold));
            loot->LootMoney();
            loot->NotifyMoneyRemoved(go->GetMap());
            took = true;
        }
        bot->GetSession()->DoLootRelease(loot);
        return took;
    }

    // ----- kill / loot -----
    bool CandidateMob(Player* bot, BotQuestCtx& c, Task& t, Creature* m, uint32 now)
    {
        if (!m->IsAlive() || m->IsInCombat() || m->IsPet() || m->IsTotem())
            return false;
        if (m->hasLootRecipient() && !m->isTappedBy(bot))
            return false;
        auto ign = c.Ignore.find(m->GetGUID().GetCounter());
        if (ign != c.Ignore.end() && now < ign->second)
            return false;
        if (!bot->IsValidAttackTarget(m))
            return false;
        ++t.SeenLive;
        if (m->isWorldBoss() || EffectiveDiff(bot, m) > (Cfg().FleeMode >= 1 ? std::min<int32>(Cfg().MaxMobLevelDiff, 1) : Cfg().MaxMobLevelDiff))
        {
            if (m->IsElite() || m->isWorldBoss())
                ++t.EliteSeen;
            ++t.TooStrongSeen;
            return false;
        }
        return true;
    }

    bool RunKill(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Quest const* q)
    {
        Task& t = c.T;
        if (bot->GetQuestStatus(t.Quest) != QUEST_STATUS_INCOMPLETE)
        {
            StopMoving(ai, bot, c);
            Finish(c, now);
            return false;
        }
        uint16 slot = bot->FindQuestSlot(t.Quest);
        QuestObjective const* obj = nullptr;
        for (QuestObjective const& o : q->GetObjectives())
            if (o.ID == t.ObjId)
                obj = &o;
        if (!obj || slot >= MAX_QUEST_LOG_SIZE)
        {
            Finish(c, now);
            return false;
        }
        if (bot->IsQuestObjectiveComplete(slot, q, *obj))
        {
            StopMoving(ai, bot, c);
            Finish(c, now);   // next Choose picks the next objective or the turn-in
            return false;
        }
        int32 count = bot->GetQuestObjectiveData(*obj);
        if (count != t.LastCount)
        {
            t.LastCount = count;
            t.ProgressMs = now;
        }

        // stalled for too long: report why and move on
        if (now - t.ProgressMs > Cfg().StallSec * 1000)
        {
            char const* code = "TARGET_UNREACHABLE";
            if (t.SeenLive && t.TooStrongSeen >= t.SeenLive)
                code = t.EliteSeen ? "ELITE_TOO_STRONG" : "TARGET_LEVEL_TOO_HIGH";
            else if (obj->Type == QUEST_OBJECTIVE_ITEM && t.Kills >= 6)
                code = "ITEM_NOT_DROPPING";
            Drop(ai, bot, c, now, t.Quest, code,
                StringFormat("no progress on '{}' for {} s (objective type {} id {})", q->GetLogTitle(), Cfg().StallSec, uint32(obj->Type), obj->ObjectID),
                StringFormat(R"({{"kills":{},"live_seen":{},"too_strong_seen":{},"count":{},"amount":{}}})", t.Kills, t.SeenLive, t.TooStrongSeen, count, obj->Amount), uint32(obj->ObjectID), 1800);
            return true;
        }

        std::vector<uint32> entries;
        ObjectiveEntries(*obj, entries);

        // a corpse of our target waiting to be looted
        if (!t.Target.IsEmpty())
        {
            Creature* m = ObjectAccessor::GetCreature(*bot, t.Target);
            if (!m)
            {
                t.Target.Clear();
                t.Chasing = false;
            }
            else if (m->isDead())
            {
                if (t.Chasing)
                {
                    bot->GetMotionMaster()->Clear();
                    t.Chasing = false;
                }
                if (LootCorpse(ai, bot, c, t, m))
                    return true;
                if (m->IsWithinDistInMap(bot, 3.0f) || now - t.TargetSinceMs > 20000 || !m->isTappedBy(bot))
                {
                    t.Target.Clear();
                    return false;
                }
                Travel(ai, bot, c, now, m->GetPositionX(), m->GetPositionY(), m->GetPositionZ(), 2.0f, m->GetEntry());
                return false;
            }
            else
            {
                // still alive: keep approaching / pull
                if (!ApproachAndPull(ai, bot, c, now, t, m))
                    return true;
                return false;
            }
        }
        if (bot->IsInCombat())
            return false;

        // find a target (rate limited)
        if (now >= c.NextScanMs)
        {
            c.NextScanMs = now + 1200;
            Creature* best = nullptr;
            float bd = 1e9f;
            std::vector<Creature*> list;
            ++t.Scans;
            t.ScanRaw = t.ScanOk = 0;
            for (uint32 en : entries)
            {
                list.clear();
                bot->GetCreatureListWithEntryInGrid(list, en, Cfg().ScanRange);
                t.ScanRaw += uint32(list.size());
                for (Creature* m : list)
                {
                    if (!CandidateMob(bot, c, t, m, now))
                        continue;
                    ++t.ScanOk;
                    float d = bot->GetExactDist2d(m);
                    if (d < bd && !BotAggroGuarded(ai, bot, m))
                    {
                        bd = d;
                        best = m;
                    }
                }
            }
            if (best)
            {
                // path check on far targets only (the chase generator handles close ones)
                if (bd > 45.0f)
                {
                    BotPathInfo info = BotMotion::QueryPath(bot, best->GetPositionX(), best->GetPositionY(), best->GetPositionZ());
                    if (info.NoPath || (info.Partial && info.EndGap3D > 25.0f))
                    {
                        c.Ignore[best->GetGUID().GetCounter()] = now + 120000;
                        return false;
                    }
                }
                t.Target = best->GetGUID();
                t.TargetSinceMs = now;
                t.TargetBest = bd;
                t.TargetBestMs = now;
                t.WaitStartMs = 0;
                t.ProgressMs = std::max(t.ProgressMs, now - 0); // finding a target counts as progress for the stall timer only once
                return false;
            }
        }

        // nothing visible: search at the spawn points
        return SearchSpawns(ai, bot, c, now, t, entries);
    }

    bool SearchSpawns(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Task& t, std::vector<uint32> const& entries)
    {
        // waiting at a point
        if (t.WaitStartMs)
        {
            if (now - t.WaitStartMs < 7000)
                return false;
            c.Seen.push_back({ bot->GetPositionX(), bot->GetPositionY() });
            if (c.Seen.size() > 8)
                c.Seen.erase(c.Seen.begin());
            t.WaitStartMs = 0;
        }
        Pt const* best = nullptr;
        float bd = 1e9f;
        for (uint32 en : entries)
        {
            auto it = g.Spawns.find(en);
            if (it == g.Spawns.end())
                continue;
            for (Pt const& p : it->second)
            {
                if (p.Map != bot->GetMapId())
                    continue;
                bool recent = false;
                for (Visited const& v : c.Seen)
                    recent = recent || Dist2D(v.X, v.Y, p.X, p.Y) < 40.0f;
                if (recent)
                    continue;
                float d = Dist2D(p.X, p.Y, bot->GetPositionX(), bot->GetPositionY());
                if (d < bd)
                {
                    bd = d;
                    best = &p;
                }
            }
        }
        if (!best)
        {
            c.Seen.clear(); // visited everything, start over
            return false;
        }
        if (bd < 15.0f)
        {
            t.WaitStartMs = now; // arrived, wait and scan
            StopMoving(ai, bot, c);
            return false;
        }
        Travel(ai, bot, c, now, best->X, best->Y, best->Z, 8.0f, 0);
        return false;
    }

    // Returns false when the task was dropped.
    bool ApproachAndPull(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, Task& t, Creature* m)
    {
        float const range = PullRange(bot);
        float const d = bot->GetExactDist(m);
        t.LastDist = d;
        ++t.Approaches;
        if (!bot->IsValidAttackTarget(m) || (m->hasLootRecipient() && !m->isTappedBy(bot)))
        {
            StopMoving(ai, bot, c);
            t.Target.Clear();
            return true;
        }
        if (d < t.TargetBest - 1.0f)
        {
            t.TargetBest = d;
            t.TargetBestMs = now;
        }
        else if (now - t.TargetBestMs > 12000)
        {
            // not getting closer: ignore this mob for a while
            c.Ignore[m->GetGUID().GetCounter()] = now + 120000;
            ++t.Ignored;
            StopMoving(ai, bot, c);
            t.Target.Clear();
            return true;
        }

        bool const inReach = IsMeleeClass(bot) ? (d <= range || bot->IsWithinMeleeRange(m)) : (d <= range && bot->IsWithinLOSInMap(m));
        if (inReach)
        {
            StopMoving(ai, bot, c);
            bot->SetFacingToObject(m);
            bot->Attack(m, true);
            m->EngageWithTarget(bot);
            bot->SetInCombatWith(m);
            ++t.Kills;
            t.TargetSinceMs = now;
            BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_TRACE, "QUEST_PULL", StringFormat("pulling {} (level {})", m->GetName(), m->GetLevel()));
            ev.QuestId = t.Quest;
            ev.TargetEntry = m->GetEntry();
            sBotMgr->LogEvent(std::move(ev));
            return true;
        }
        // approach with the BotMotion goal (proven navmesh walking); MoveChase did not move players
        if (t.Chasing)
        {
            bot->GetMotionMaster()->Clear();
            t.Chasing = false;
        }
        float const arrive = std::max(2.0f, (IsMeleeClass(bot) ? range : range * 0.8f) - 1.0f);
        if (!ai->Motion().HasGoal() || Dist2D(ai->Motion().GoalX(), ai->Motion().GoalY(), m->GetPositionX(), m->GetPositionY()) > 6.0f)
            Travel(ai, bot, c, now, m->GetPositionX(), m->GetPositionY(), m->GetPositionZ(), arrive, m->GetEntry());
        return true;
    }

    // Loots every item and the money of one of our corpses when in range. Returns true when something was done.
    bool LootCorpse(BotAI* ai, Player* bot, BotQuestCtx& c, Task& t, Creature* m)
    {
        (void)ai; (void)c; (void)t;
        if (!m->IsWithinDistInMap(bot, 3.5f) || !m->isTappedBy(bot))
            return false;
        Loot* loot = m->GetLootForPlayer(bot);
        if (!loot || loot->isLooted())
            return false;
        bot->SendLoot(*loot);
        for (uint32 i = 0; i < loot->items.size(); ++i)
            if (LootItem* li = loot->LootItemInSlot(i, bot))
                if (!li->is_looted)
                    bot->StoreLootItem(m->GetGUID(), uint8(i), loot);
        if (loot->gold)
        {
            bot->ModifyMoney(int64(loot->gold));
            loot->LootMoney();
            loot->NotifyMoneyRemoved(m->GetMap());
        }
        bot->GetSession()->DoLootRelease(loot);
        return true;
    }
};

class QuestActiveTrigger : public Trigger
{
public:
    explicit QuestActiveTrigger(BotAI* ai) : Trigger(ai, "quest_tick", 0) { }
    bool IsActive() override { return g.Ready.load(std::memory_order_acquire); }
};

class QuestStrategy : public Strategy
{
public:
    QuestStrategy() : Strategy("quest") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // above move_to_goal (30) so the quest layer sets the goal and returns false, below rest/eat (35)
        t.push_back({ "quest_tick", { { "quest_think", BotRelevance::Move + 1.0f } } });
    }
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------------------------------------------------
void EnsureIndex()
{
    static std::once_flag once;
    std::call_once(once, []()
    {
        try
        {
            BuildIndex();
        }
        catch (std::exception const& ex)
        {
            TC_LOG_ERROR("server.worldserver", "Bot quest index failed: {}", ex.what());
        }
    });
}

bool IsReady() { return g.Ready.load(std::memory_order_acquire); }

std::string DescribeTask(BotAI* ai)
{
    BotQuestCtx* cp = static_cast<BotQuestCtx*>(ai->GetValueRaw("quest_ctx"));
    if (!cp)
        return "quest strategy not active";
    Task const& t = cp->T;
    return StringFormat("task {} quest {} npc {} target {} kills {} scans {} raw {} ok {} seenlive {} toostrong {} wait {} chasing {} dist {:.1f} appr {} ign {} expect {} fails {} legs {} accepted {} rewarded {} blacklisted {} nextchoose {} calls {} hubtrips {} grinds {} hub {} why {}",
        KindName(t.K), t.Quest, t.NpcEntry, t.Target.IsEmpty() ? 0 : 1, t.Kills, t.Scans, t.ScanRaw, t.ScanOk, t.SeenLive, t.TooStrongSeen, t.WaitStartMs ? 1 : 0, t.Chasing ? 1 : 0, t.LastDist, t.Approaches, t.Ignored,
        cp->ExpectGoal ? 1 : 0, cp->GoalFails, t.LegIssues, cp->Accepted, cp->Rewarded, cp->Blacklist.size(), cp->NextChooseMs, cp->ExecCalls, cp->HubTrips, cp->Grinds, int64(t.HubId == 0xFFFFFFFFu ? -1 : int64(t.HubId)), cp->Why);
}
} // namespace BotQuest

void RegisterQuestBotObjects(BotRegistry& r)
{
    r.AddValue("quest_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotQuest::BotQuestCtx>(ai); });
    r.AddTrigger("quest_tick", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotQuest::QuestActiveTrigger>(ai); });
    r.AddAction("quest_think", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotQuest::QuestThinkAction>(ai); });
    r.AddStrategy("quest", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotQuest::QuestStrategy>(); });
}
