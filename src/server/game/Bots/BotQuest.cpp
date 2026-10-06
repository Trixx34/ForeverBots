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
#include "Util.h"
#include "WorldSession.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
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

struct GrindPt
{
    float X, Y, Z;
    uint32 Entry;
};

constexpr float GRID_CELL = 200.0f;
constexpr uint32 MAX_STARTER_POINTS = 8;

struct Index
{
    std::atomic<bool> Ready{false};
    std::unordered_map<uint32, std::vector<Pt>> Spawns;            // creature entry -> spawn points (only quest-relevant entries)
    std::unordered_map<uint32, std::vector<uint32>> CreditSrc;     // kill credit entry -> creature entries that grant it
    std::unordered_map<uint32, std::vector<uint32>> ItemSrc;       // quest item -> creature entries that drop it (QuestRequired rows)
    std::unordered_set<uint32> ItemGoSrc;                          // quest items dropped by game objects only (unsupported in v1)
    std::unordered_map<uint64, std::vector<StarterRef>> Grid;      // (map, cell) -> starters with at least one spawn
    std::unordered_map<uint32, std::vector<uint32>> StartsBy;      // creature entry -> quests it starts
    std::unordered_map<uint32, std::vector<uint32>> EndsBy;        // creature entry -> quests it ends
    std::vector<Hub> Hubs;
    std::unordered_map<uint64, uint32> HubByCell;                  // (map, cell) -> index in Hubs
    std::unordered_map<uint64, std::vector<GrindPt>> GrindGrid;    // (map, cell) -> hostile normal-rank spawns (grind fallback)
    uint32 NumGrind = 0;
    uint32 NumStarterQuests = 0, NumStarterPoints = 0, NumSpawnEntries = 0, NumItemSources = 0;
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
                bool any = false;
                if (it != g.ItemSrc.end())
                    for (uint32 e : it->second)
                        any = any || HasSpawn(e);
                if (!any)
                {
                    b.Entry = uint32(obj.ObjectID);
                    if (g.ItemGoSrc.count(uint32(obj.ObjectID)))
                    {
                        b.Code = "OBJECTIVE_UNSUPPORTED"; b.Info = "item comes from a game object";
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
    std::unordered_map<uint32, std::vector<uint32>> goLootItems;
    if (QueryResult r = WorldDatabase.Query("SELECT Entry, Item FROM gameobject_loot_template WHERE QuestRequired = 1 AND ItemType = 0"))
    {
        do
        {
            Field* f = r->Fetch();
            uint32 item = f[1].GetUInt32();
            if (neededItems.count(item))
                g.ItemGoSrc.insert(item);
        } while (r->NextRow());
    }
    (void)goLootItems;
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

    g.Ready.store(true, std::memory_order_release);
    TC_LOG_INFO("server.worldserver", "Bot quest index: {} starter quests, {} starter points, {} spawn entries, {} quest items with creature sources, {} hubs, {} grind spawns, {} ms",
        g.NumStarterQuests, g.NumStarterPoints, g.NumSpawnEntries, g.NumItemSources, uint32(g.Hubs.size()), g.NumGrind, GetMSTimeDiffToNow(startMs));
}

// ---------------------------------------------------------------------------------------------------------------------
// 3. Per-bot context
// ---------------------------------------------------------------------------------------------------------------------
enum class Kind : uint8 { None, GoGiver, GoEnder, Kill, Talk, Grind };

char const* KindName(Kind k)
{
    switch (k)
    {
        case Kind::GoGiver: return "go_giver";
        case Kind::GoEnder: return "go_ender";
        case Kind::Kill: return "kill";
        case Kind::Talk: return "talk";
        case Kind::Grind: return "grind";
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
};

struct Visited
{
    float X, Y;
};

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
    uint32 Completed = 0, Accepted = 0, Rewarded = 0;
    uint32 LastPickMs = 0;
    std::unordered_map<uint32, uint32> HubBlack;    // hub index -> AI clock until
    uint32 NextHubMs = 0;
    uint8 NoLocalLevel = 0, HubNoneLevel = 0;
    bool LastWasGrind = false;
    uint32 Grinds = 0, HubTrips = 0;
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

// quest_blocked event, once per (quest, code) per bot unless `once` is false
void Blocked(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 questId, char const* code, std::string summary, std::string details = std::string(), uint32 entry = 0, bool once = true, uint8 severity = BOTLOG_WARN)
{
    if (once && !c.Logged.insert(LogKey(questId, code)).second)
        return;
    BotEvent ev = ai->MakeEvent(bot, "quest_blocked", severity, code, std::move(summary));
    if (questId)
        ev.QuestId = questId;
    if (entry)
        ev.TargetEntry = entry;
    ev.Details = std::move(details);
    sBotMgr->LogEvent(std::move(ev));
}

std::string QuestTitle(uint32 questId)
{
    Quest const* q = sObjectMgr->GetQuestTemplate(questId);
    return q ? q->GetLogTitle() : std::string("?");
}

Creature* FindLiveNpc(Player* bot, uint32 entry, float range)
{
    return bot->FindNearestCreature(entry, range, true);
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

// effective level gap used to skip dangerous kill targets (same shape as class-ai's flee check, stricter threshold)
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
        c.T = Task();
        c.GoalFails = 0;
        c.ExpectGoal = false;
        c.NextChooseMs = immediate ? 0 : now + 1000;
    }

    void Drop(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now, uint32 questId, char const* code, std::string summary, std::string details, uint32 entry, uint32 blacklistSec)
    {
        Blocked(ai, bot, c, questId, code, std::move(summary), std::move(details), entry);
        if (questId)
            c.Blacklist[questId] = now + blacklistSec * 1000;
        if (c.T.K == Kind::Grind)
            c.Seen.push_back({ c.T.GoalX, c.T.GoalY });   // do not pick the same unreachable grind spot again
        if (c.T.HubId != 0xFFFFFFFFu && c.T.K == Kind::GoGiver)
            c.HubBlack[c.T.HubId] = now + Cfg().HubBlackSec * 1000;   // the whole hub is out of reach for a while
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
                    c.ExpectGoal = true;
                    c.ExpectX = t.WpX; c.ExpectY = t.WpY;
                }
                return true;
            }
        }
        if (motion.HasGoal() && std::strcmp(motion.GetTag(), "quest") == 0 &&
            Dist2D(motion.GoalX(), motion.GoalY(), x, y) < 4.0f)
            return true; // already walking there
        BotPathInfo info = BotMotion::QueryPath(bot, x, y, z);
        bool const bad = info.NoPath || (info.Partial && info.EndGap3D > 25.0f) || (info.Valid && info.GoalOffMesh && info.EndGap3D > 25.0f);
        if (bad)
        {
            float hx = 0, hy = 0, hz = 0;
            if (t.Hops < 40 && FindHop(bot, x, y, hx, hy, hz))
            {
                ++t.Hops;
                t.HasWp = true;
                t.WpX = hx; t.WpY = hy; t.WpZ = hz;
                t.FinalX = x; t.FinalY = y;
                t.GoalX = x; t.GoalY = y; t.GoalZ = z;
                motion.SetGoal(bot->GetMapId(), hx, hy, hz, 8.0f, "quest");
                c.ExpectGoal = true;
                c.ExpectX = hx; c.ExpectY = hy;
                return true;
            }
            std::string det = StringFormat(R"({{"task":"{}","dest":[{:.0f},{:.0f},{:.0f}],"no_path":{},"partial":{},"gap3d":{:.0f},"goal_off_mesh":{},"length":{:.0f},"hops":{}}})",
                KindName(t.K), x, y, z, info.NoPath, info.Partial, info.EndGap3D, info.GoalOffMesh, info.Length, t.Hops);
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
            struct Cand { LogEntry const* E; QuestObjective const* O; float D; uint32 Entry; };
            Cand best{ nullptr, nullptr, 1e9f, 0 };
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
                        best = { &e, obj, d, en };
                }
            }
            if (best.E)
            {
                Quest const* q = sObjectMgr->GetQuestTemplate(best.E->Quest);
                c.T = Task();
                c.T.K = best.O->Type == QUEST_OBJECTIVE_TALKTO ? Kind::Talk : Kind::Kill;
                c.T.Quest = best.E->Quest;
                c.T.ObjId = best.O->ID;
                c.T.NpcEntry = best.O->Type == QUEST_OBJECTIVE_TALKTO ? uint32(best.O->ObjectID) : 0;
                c.T.SinceMs = c.T.ProgressMs = c.T.TargetBestMs = now;
                c.GoalFails = 0;
                Decision(ai, bot, "QUEST_WORK", StringFormat("working '{}' ({} of {} open quests)", q ? q->GetLogTitle() : "?", 1, considered), best.E->Quest, best.Entry,
                    StringFormat(R"({{"objective_type":{},"object_id":{},"amount":{},"nearest_spawn_dist":{:.0f}}})", uint32(best.O->Type), best.O->ObjectID, best.O->Amount, best.D));
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
        if (c.Blacklisted(ref.Quest, now))
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
                    StringFormat(R"({{"map":{},"x":{:.0f},"y":{:.0f},"level":{},"hubs":{}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetLevel(), uint32(g.Hubs.size())), 0, false, BOTLOG_WARN);
            }
        }
        return StartGrind(ai, bot, c, now);
    }

    // E3: the best quest hub on this map for the bot (sum of the takeable quest scores, distance discounted), then a go_giver task into it.
    bool PickHub(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        int32 const lvl = int32(bot->GetLevel());
        float const bx = bot->GetPositionX(), by = bot->GetPositionY();
        struct HubScore { uint32 Idx; float Score; uint32 N; float Dist; StarterRef const* Best; float BestScore; };
        std::vector<HubScore> top;
        uint32 scanned = 0;
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
            StringFormat(R"({{"hub":{},"x":{:.0f},"y":{:.0f},"z":{:.0f},"zone":{},"dist":{:.0f},"takeable":{},"givers":{},"min_q":{},"max_q":{},"hubs_scanned":{},"alternatives":[{}]}})",
                w.Idx, h.X, h.Y, h.Z, zone, w.Dist, w.N, h.Givers, h.MinQ, h.MaxQ, scanned, alt));
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
        return EffectiveDiff(bot, m) <= 1;
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
                if (d < bd)
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
            if (!c.Seen.empty())
                c.Seen.clear();
            else if (c.IdleLoggedLevel != bot->GetLevel())
            {
                c.IdleLoggedLevel = bot->GetLevel();
                Blocked(ai, bot, c, 0, "NO_GRIND_TARGET", StringFormat("no hostile spawn within 500 yd to grind at level {}", bot->GetLevel()),
                    StringFormat(R"({{"map":{},"x":{:.0f},"y":{:.0f}}})", bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY()), 0, false, BOTLOG_WARN);
            }
            return false;
        }
        if (bd < 15.0f)
        {
            t.WaitStartMs = now;
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

    // ----- running the task -----
    bool RunTask(BotAI* ai, Player* bot, BotQuestCtx& c, uint32 now)
    {
        Task& t = c.T;
        if (t.K == Kind::Grind)
            return RunGrind(ai, bot, c, now);
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
        if (m->isWorldBoss() || EffectiveDiff(bot, m) > Cfg().MaxMobLevelDiff)
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
                    if (d < bd)
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
