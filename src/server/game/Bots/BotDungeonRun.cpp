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

#include "BotDungeonRun.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotDungeon.h"
#include "BotDungeonData.h"
#include "BotMgr.h"
#include "BotPlayerLed.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Item.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>

using Trinity::StringFormat;

namespace BotDungeonRun
{
namespace
{
    using namespace BotDungeon;

    struct Opts
    {
        uint32 MapId = 36;            // Bot.AI.Dungeon.Map: the dungeon to run (default Deadmines)
        uint32 MaxLevel = 25;         // Bot.AI.Dungeon.MaxLevel: highest level the dungeon takes
        uint32 IntervalMs = 5000;     // Bot.AI.Dungeon.IntervalSec
        uint32 CooldownMs = 120000;   // Bot.AI.Dungeon.StartCooldownSec: between two group attempts
        uint32 PackTimeoutMs = 240000; // a pack not cleared this long after the pull is written off
        uint32 RezDelayMs = 8000;     // Bot.AI.Dungeon.ResurrectDelaySec: dead members of a group that is out of combat come back after this
        float LinkRadius = 14.0f;     // Bot.AI.Dungeon.PackLinkYards: spawns this close belong to one pack
        uint32 MaxPack = 6;           // Bot.AI.Dungeon.PackMaxMobs: larger clusters (without a boss) are cut up
        bool AutoMap = true;          // Bot.AI.Dungeon.Map = 0: pick the dungeon from the group level
        bool FreeRepair = true;       // Bot.AI.Dungeon.FreeRepair: the group's gear is repaired when the run ends
    };

    struct PackInfo
    {
        Pack P;
        float X = 0, Y = 0, Z = 0;
        std::vector<uint32> Entries;
        uint32 PulledMs = 0;
        bool Final = false;       // holds the dungeon's last boss
    };

    struct RunState
    {
        bool Active = false;
        ObjectGuid Leader;
        std::vector<Member> Members;
        Phase Cur = Phase::Gather;
        uint32 StartedMs = 0, PhaseSinceMs = 0, Wipes = 0, EnterMs = 0;
        uint32 OutMap = 0; float OutX = 0, OutY = 0, OutZ = 0;      // the entrance, outside
        uint32 InMap = 0; float InX = 0, InY = 0, InZ = 0, InO = 0; // where the entrance leads
        std::vector<PackInfo> Packs;
        int32 CurPack = -1;
        uint32 MapId = 0;                 // the dungeon being run
        DungeonInfo const* Info = nullptr;
        uint32 DeadSinceMs = 0;           // somebody has been dead (group out of combat) since then
    };

    RunState s_run;
    Opts s_opt;
    bool s_optRead = false;
    uint32 s_runCount = 0;
    uint32 s_accMs = 0, s_nowMs = 0, s_nextStartMs = 0;
    std::mutex s_busyLock;
    std::unordered_set<uint64> s_busy;

    void ReadOpts()
    {
        if (s_optRead)
            return;
        s_optRead = true;
        auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::max(lo, std::min(hi, sConfigMgr->GetIntDefault(k, def))); };
        s_opt.MapId = uint32(opt("Bot.AI.Dungeon.Map", 36, 0, 100000));
        s_opt.AutoMap = s_opt.MapId == 0;
        s_opt.RezDelayMs = uint32(opt("Bot.AI.Dungeon.ResurrectDelaySec", 8, 0, 600)) * 1000;
        s_opt.LinkRadius = float(opt("Bot.AI.Dungeon.PackLinkYards", 14, 4, 40));
        s_opt.MaxPack = uint32(opt("Bot.AI.Dungeon.PackMaxMobs", 6, 2, 12));
        s_opt.FreeRepair = sConfigMgr->GetBoolDefault("Bot.AI.Dungeon.FreeRepair", true);
        s_opt.MaxLevel = uint32(opt("Bot.AI.Dungeon.MaxLevel", 25, 1, 80));
        s_opt.IntervalMs = uint32(opt("Bot.AI.Dungeon.IntervalSec", 5, 1, 60)) * 1000;
        s_opt.CooldownMs = uint32(opt("Bot.AI.Dungeon.StartCooldownSec", 120, 10, 36000)) * 1000;
        s_opt.PackTimeoutMs = uint32(opt("Bot.AI.Dungeon.PackTimeoutSec", 240, 30, 3600)) * 1000;
    }

    void Log(uint64 botGuid, char const* code, std::string summary, uint8 sev = BOTLOG_INFO)
    {
        BotEvent ev;
        ev.BotGuid = botGuid;
        ev.Type = "decision";
        ev.Severity = sev;
        ev.Reason = code;
        ev.Summary = std::move(summary);
        sBotMgr->LogEvent(std::move(ev));
    }

    BotAI* AiOf(Player* p)
    {
        return p && p->GetSession() ? p->GetSession()->GetBotAI() : nullptr;
    }

    bool Eligible(Player* p, uint32 minLevel, uint32 maxLevel)
    {
        if (!p || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || p->GetGroup() || p->IsBeingTeleported() || !AiOf(p))
            return false;
        if (p->GetLevel() < minLevel || p->GetLevel() > maxLevel || sBotMgr->IsActiveAlt(p->GetGUID().GetCounter()))
            return false;
        Map const* m = p->GetMap();
        return m && !m->IsDungeon();
    }

    bool FindEntrance(RunState& r, uint32 dungeonMap)
    {
        for (AreaTriggerEntry const* e : sAreaTriggerStore)
        {
            AreaTriggerTeleport const* t = sObjectMgr->GetAreaTrigger(e->ID);
            if (!t || t->Loc.GetMapId() != dungeonMap)
                continue;
            r.OutMap = e->ContinentID;
            r.OutX = e->Pos.X; r.OutY = e->Pos.Y; r.OutZ = e->Pos.Z;
            r.InMap = t->Loc.GetMapId();
            r.InX = t->Loc.GetPositionX(); r.InY = t->Loc.GetPositionY(); r.InZ = t->Loc.GetPositionZ(); r.InO = t->Loc.GetOrientation();
            return true;
        }
        return false;
    }

    Player* Find(uint64 counter)
    {
        return ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(counter));
    }

    // Mob level of a spawn: the creature templates here scale with content tuning, so the level comes from the dungeon's own range
    // (middle of it, one more for elites and two for bosses). Close enough for the "do not pull far above the group" filter.
    int32 SpawnLevel(DungeonInfo const* info, bool elite, bool boss)
    {
        int32 const mid = info ? (int32(info->MinLevel) + int32(info->MaxLevel)) / 2 : 1;
        return mid + (boss ? 2 : elite ? 1 : 0);
    }

    // Hostile, non-civilian spawns of the dungeon map, clustered into packs by BotDungeonData (3D link radius, big clusters cut up).
    void BuildPacks(RunState& r)
    {
        r.Packs.clear();
        std::vector<SpawnPoint> pts;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapId != r.InMap)
                continue;
            CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(data.id);
            if (!t || t->npcflag || (t->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN))
                continue;
            FactionTemplateEntry const* ft = sFactionTemplateStore.LookupEntry(t->faction);
            if (!ft || !ft->IsHostileToPlayers())
                continue;
            SpawnPoint sp;
            sp.SpawnId = spawnId;
            sp.Entry = data.id;
            sp.X = data.spawnPoint.GetPositionX(); sp.Y = data.spawnPoint.GetPositionY(); sp.Z = data.spawnPoint.GetPositionZ();
            sp.Elite = t->Classification == CreatureClassifications::Elite || t->Classification == CreatureClassifications::RareElite;
            sp.Boss = (t->flags_extra & (CREATURE_FLAG_EXTRA_INSTANCE_BIND | CREATURE_FLAG_EXTRA_DUNGEON_BOSS)) != 0;
            sp.Patrol = data.movementType == 2;
            sp.Level = SpawnLevel(r.Info, sp.Elite, sp.Boss);
            pts.push_back(sp);
        }
        for (PackSpec const& spec : ClusterPacks(pts, s_opt.LinkRadius, s_opt.MaxPack, r.Info ? r.Info->FinalBoss : 0))
        {
            PackInfo pk;
            pk.P.Id = spec.Id;
            pk.P.Mobs = spec.Mobs;
            pk.P.Elites = spec.Elites;
            pk.P.MaxMobLevel = spec.MaxLevel;
            pk.P.Patrols = spec.Patrols;
            pk.P.Boss = spec.Boss;
            pk.Final = spec.HasFinalBoss;
            pk.X = spec.X; pk.Y = spec.Y; pk.Z = spec.Z;
            for (SpawnPoint const& sp : spec.Points)
                if (std::find(pk.Entries.begin(), pk.Entries.end(), sp.Entry) == pk.Entries.end())
                    pk.Entries.push_back(sp.Entry);
            r.Packs.push_back(std::move(pk));
        }
    }

    // Lowest durability percentage of the equipped gear (100 when nothing wears out).
    int32 DurabilityPct(Player* p)
    {
        int32 low = 100;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* it = p->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (uint32 maxDur = it->m_itemData->MaxDurability)
                    low = std::min(low, int32(100u * it->m_itemData->Durability / maxDur));
        return low;
    }

    // The member that walks into packs: the group's tank when it is alive and here, otherwise the leader.
    Player* Puller(RunState const& r, Player* leader)
    {
        for (Member const& m : r.Members)
            if (m.AsRole == Role::Tank)
                if (Player* p = Find(m.Guid); p && p->IsAlive() && p->IsInWorld() && p->GetMapId() == leader->GetMapId())
                    return p;
        return leader;
    }

    void SetBusy(RunState const& r, bool on)
    {
        std::lock_guard<std::mutex> lock(s_busyLock);
        for (Member const& m : r.Members)
            if (on)
                s_busy.insert(m.Guid);
            else
                s_busy.erase(m.Guid);
    }

    void End(RunState& r, Phase final, char const* why)
    {
        Player* leader = ObjectAccessor::FindPlayer(r.Leader);
        Log(r.Leader.GetCounter(), final == Phase::Done ? "DUNGEON_DONE" : "DUNGEON_ABORTED", StringFormat("dungeon run ends: {} (wipes {}, packs left {})", why, r.Wipes,
            std::count_if(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return !p.P.Done; })), final == Phase::Done ? BOTLOG_INFO : BOTLOG_WARN);
        for (Member const& m : r.Members)
            if (Player* p = Find(m.Guid))
            {
                if (BotAI* ai = AiOf(p))
                {
                    ai->Motion().SetFollow(ObjectGuid::Empty);
                    ai->Motion().ClearGoal();
                }
                if (s_opt.FreeRepair)
                    p->DurabilityRepairAll(false, 0.0f, false);
                if (p->IsAlive() && p->GetMapId() == r.InMap)
                    p->TeleportTo(r.OutMap, r.OutX, r.OutY, r.OutZ, 0.0f);
            }
        if (leader)
            if (Group* g = leader->GetGroup())
                g->Disband();
        SetBusy(r, false);
        r = RunState();
    }

    void TryStart(Settings const& cfg)
    {
        if (s_nowMs < s_nextStartMs)
            return;
        s_nextStartMs = s_nowMs + s_opt.CooldownMs;

        RunState r;
        std::vector<Player*> bots;
        for (Player* p : sBotMgr->GetOnlineBotPlayers())
            if (Eligible(p, cfg.Compose.MinLevel, s_opt.MaxLevel))
                bots.push_back(p);
        if (bots.empty())
            return;
        r.MapId = s_opt.MapId;
        if (s_opt.AutoMap)
        {
            // the dungeon that fits the middle level of the online bots best, one after the other
            std::vector<int32> lv;
            for (Player* p : bots)
                lv.push_back(int32(p->GetLevel()));
            std::sort(lv.begin(), lv.end());
            auto fit = DungeonsForLevel(lv[lv.size() / 2], 0);
            if (fit.empty())
                return;
            r.MapId = fit[(s_runCount++) % fit.size()]->MapId;
        }
        r.Info = nullptr;
        for (DungeonInfo const& d : Dungeons())
            if (d.MapId == r.MapId)
                r.Info = &d;
        if (!FindEntrance(r, r.MapId))
        {
            Log(0, "DUNGEON_NO_ENTRANCE", StringFormat("no entrance trigger leads to map {}", r.MapId), BOTLOG_WARN);
            s_nextStartMs = s_nowMs + 3600000;
            return;
        }
        std::vector<Player*> onContinent;
        for (Player* p : bots)
            if (p->GetMapId() == r.OutMap)
                onContinent.push_back(p);
        if (onContinent.empty())
            return;
        Player* leader = onContinent[urand(0, uint32(onContinent.size() - 1))];

        std::vector<Candidate> cands;
        for (Player* p : bots)
        {
            Candidate c;
            c.Guid = p->GetGUID().GetCounter();
            c.ClassId = p->GetClass();
            c.Level = p->GetLevel();
            c.Leader = p == leader;
            c.DistanceToLeader = p->GetMapId() == leader->GetMapId() ? p->GetDistance2d(leader) : 1e9f;
            cands.push_back(c);
        }
        ComposeResult res = Compose(cands, cfg.Comp, cfg.Compose.MinLevel, s_opt.MaxLevel, cfg.Compose);
        if (!res.Complete())
            return;

        Group* group = new Group;
        group->Create(leader);
        sGroupMgr->AddGroup(group);
        for (Member const& m : res.Members)
        {
            Player* p = Find(m.Guid);
            if (!p || p == leader)
                continue;
            if (!group->AddMember(p))
                continue;
            if (BotAI* ai = AiOf(p))
                ai->Motion().SetFollow(leader->GetGUID());
        }
        group->BroadcastGroupUpdate();

        r.Active = true;
        r.Leader = leader->GetGUID();
        r.Members = res.Members;
        r.StartedMs = r.PhaseSinceMs = s_nowMs;
        std::string roles;
        for (Member const& m : r.Members)
            roles += StringFormat("{}{}:{}", roles.empty() ? "" : ",", m.Guid, RoleName(m.AsRole));
        s_run = std::move(r);
        SetBusy(s_run, true);
        if (BotAI* ai = AiOf(leader))
            ai->Motion().ClearGoal();
        Log(leader->GetGUID().GetCounter(), "DUNGEON_GROUP", StringFormat("dungeon group formed for map {}: {}", s_run.MapId, roles));
    }

    void Step(Settings const& cfg)
    {
        RunState& r = s_run;
        Player* leader = ObjectAccessor::FindPlayer(r.Leader);
        if (!leader || !leader->IsInWorld())
        {
            End(r, Phase::Aborted, "leader gone");
            return;
        }

        std::vector<MemberState> states;
        uint32 alive = 0, present = 0;
        for (Member const& m : r.Members)
        {
            Player* p = Find(m.Guid);
            MemberState st;
            st.Guid = m.Guid;
            st.AsRole = m.AsRole;
            if (!p || !p->IsInWorld())
            {
                st.Alive = false;
                st.Present = false;
            }
            else
            {
                st.Alive = p->IsAlive();
                st.InCombat = p->IsInCombat();
                st.Present = p->GetMapId() == leader->GetMapId() && p->GetDistance2d(leader) <= 40.0f;
                st.HealthPct = int32(p->GetHealthPct());
                st.ManaPct = p->GetMaxPower(POWER_MANA) ? int32(100.0f * p->GetPower(POWER_MANA) / p->GetMaxPower(POWER_MANA)) : 100;
                st.Eating = p->HasAuraType(SPELL_AURA_MOD_POWER_REGEN) || p->HasAuraType(SPELL_AURA_MOD_REGEN);
                st.DurabilityPct = DurabilityPct(p);
            }
            alive += st.Alive;
            present += st.Alive && st.Present;
            states.push_back(st);
        }

        RunFacts f;
        f.Current = r.Cur;
        f.NowMs = s_nowMs;
        f.StartedMs = r.StartedMs;
        f.PhaseSinceMs = r.PhaseSinceMs;
        f.Wipes = r.Wipes;
        f.Alive = alive;
        f.Size = uint32(r.Members.size());
        f.Present = present;
        f.Inside = leader->GetMapId() == r.InMap;
        f.AtEntrance = leader->GetMapId() == r.OutMap && leader->GetDistance2d(r.OutX, r.OutY) <= 10.0f;
        f.PacksLeft = r.Packs.empty() || std::any_of(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return !p.P.Done; });
        // the dungeon's last boss when the table knows it, otherwise every boss pack
        bool const hasFinal = std::any_of(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return p.Final; });
        f.BossKilled = !r.Packs.empty() && (hasFinal
            ? std::any_of(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return p.Final && p.P.Done; })
            : (std::none_of(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return p.P.Boss && !p.P.Done; })
                && std::any_of(r.Packs.begin(), r.Packs.end(), [](PackInfo const& p) { return p.P.Boss; })));

        RunResult next = AdvanceRun(f, cfg.Run);
        if (next.Next != r.Cur)
        {
            if (next.Next == Phase::Recover)
                ++r.Wipes;
            Log(r.Leader.GetCounter(), "DUNGEON_PHASE", StringFormat("{} -> {}: {}", PhaseName(r.Cur), PhaseName(next.Next), next.Why));
            r.Cur = next.Next;
            r.PhaseSinceMs = s_nowMs;
            if (r.Cur == Phase::Clear && r.Packs.empty())
                BuildPacks(r);
            if (r.Cur == Phase::Done || r.Cur == Phase::Aborted)
            {
                End(r, r.Cur, next.Why);
                return;
            }
        }

        BotAI* lai = AiOf(leader);
        if (!lai)
            return;

        // Dead members: nobody is fighting any more (or the whole group is down), so after a short delay they are raised next to the
        // puller (a stand-in for a healer's resurrection and the walk back, the bots have no corpse-run inside instances yet).
        bool anyCombat = false;
        for (MemberState const& st : states)
            anyCombat |= st.Alive && st.InCombat;
        bool const anyDead = alive < f.Size;
        if (anyDead && (!anyCombat || alive == 0) && f.Inside)
        {
            if (!r.DeadSinceMs)
                r.DeadSinceMs = s_nowMs;
            if (s_nowMs - r.DeadSinceMs >= s_opt.RezDelayMs)
            {
                Player* anchor = alive ? Puller(r, leader) : nullptr;
                for (Member const& m : r.Members)
                {
                    Player* p = Find(m.Guid);
                    if (!p || !p->IsInWorld() || p->IsAlive())
                        continue;
                    p->ResurrectPlayer(0.5f);
                    p->SpawnCorpseBones();
                    if (anchor && anchor->IsAlive())
                        p->TeleportTo(anchor->GetMapId(), anchor->GetPositionX(), anchor->GetPositionY(), anchor->GetPositionZ(), anchor->GetOrientation());
                    else
                        p->TeleportTo(r.InMap, r.InX, r.InY, r.InZ, r.InO);
                    Log(m.Guid, "DUNGEON_RAISED", StringFormat("raised after {} s", (s_nowMs - r.DeadSinceMs) / 1000));
                }
                r.DeadSinceMs = 0;
                if (r.CurPack >= 0 && alive == 0)
                {
                    // a wiped pull is tried again later: back to the start of the queue
                    r.Packs[r.CurPack].PulledMs = 0;
                    r.CurPack = -1;
                    lai->Motion().ClearGoal();
                }
            }
        }
        else
            r.DeadSinceMs = 0;

        // Everybody follows the puller, who walks the pull; the leader keeps the group's identity (loot, disband).
        Player* const puller = Puller(r, leader);
        if (r.Cur == Phase::Clear || r.Cur == Phase::Recover)
            for (Member const& m : r.Members)
                if (Player* p = Find(m.Guid); p && p != puller && p->IsAlive())
                    if (BotAI* ai = AiOf(p))
                        ai->Motion().SetFollow(puller->GetGUID());

        switch (r.Cur)
        {
            case Phase::Travel:
                if (f.Inside)
                    break;
                if (f.AtEntrance && present >= f.Size)
                {
                    if (s_nowMs - r.EnterMs < 20000 && r.EnterMs)
                        break;
                    r.EnterMs = s_nowMs;
                    lai->Motion().ClearGoal();
                    leader->TeleportTo(r.InMap, r.InX, r.InY, r.InZ, r.InO);
                    for (Member const& m : r.Members)
                        if (Player* p = Find(m.Guid); p && p != leader && p->IsAlive())
                            p->TeleportTo(r.InMap, r.InX, r.InY, r.InZ, r.InO);
                    Log(r.Leader.GetCounter(), "DUNGEON_ENTER", "group enters the dungeon");
                }
                else if (leader->GetMapId() == r.OutMap && !lai->Motion().HasGoal())
                    lai->Motion().SetGoal(r.OutMap, r.OutX, r.OutY, r.OutZ, 4.0f, "dungeon");
                break;
            case Phase::Clear:
            {
                if (!f.Inside)
                    break;
                BotAI* pai = AiOf(puller);
                if (!pai)
                    break;
                // write off or finish the pack in progress
                if (r.CurPack >= 0)
                {
                    PackInfo& pk = r.Packs[r.CurPack];
                    bool cleared = false;
                    if (puller->GetDistance2d(pk.X, pk.Y) <= 30.0f && !anyCombat)
                    {
                        cleared = true;
                        for (uint32 entry : pk.Entries)
                            if (puller->FindNearestCreature(entry, 35.0f, true))
                            {
                                cleared = false;
                                break;
                            }
                    }
                    bool const timeout = s_nowMs - pk.PulledMs > s_opt.PackTimeoutMs;
                    if (cleared || timeout)
                    {
                        pk.P.Done = true;
                        Log(r.Leader.GetCounter(), cleared ? "DUNGEON_PACK_DONE" : "DUNGEON_PACK_SKIPPED", StringFormat("pack {} ({} mobs{}) {}", pk.P.Id, pk.P.Mobs, pk.P.Boss ? ", boss" : "", cleared ? "cleared" : "written off after the timeout"));
                        r.CurPack = -1;
                        pai->Motion().ClearGoal();
                    }
                }
                if (r.CurPack >= 0)
                    break;
                int32 avg = 0;
                for (Member const& m : r.Members)
                    if (Player* p = Find(m.Guid))
                        avg += p->GetLevel();
                avg = r.Members.empty() ? 1 : avg / int32(r.Members.size());
                std::vector<Pack> view;
                for (PackInfo& pk : r.Packs)
                {
                    pk.P.Distance = puller->GetDistance2d(pk.X, pk.Y);
                    view.push_back(pk.P);
                }
                Ready ready = CheckReady(states, cfg.Ready);
                PullChoice pc = ChoosePull(view, avg, ready, alive, f.Size, cfg.Pull);
                if (pc.Kind == PullKind::Pull)
                {
                    for (size_t i = 0; i < r.Packs.size(); ++i)
                        if (r.Packs[i].P.Id == pc.PackId)
                            r.CurPack = int32(i);
                    PackInfo& pk = r.Packs[r.CurPack];
                    pk.PulledMs = s_nowMs;
                    pai->Motion().SetGoal(r.InMap, pk.X, pk.Y, pk.Z, 6.0f, "dungeon");
                    Log(r.Leader.GetCounter(), "DUNGEON_PULL", StringFormat("pulling pack {} ({} mobs, {} elites{}): {}", pk.P.Id, pk.P.Mobs, pk.P.Elites, pk.P.Boss ? ", boss" : "", pc.Why));
                }
                else if (pc.Kind == PullKind::Rest)
                    pai->Motion().ClearGoal();
                else
                    End(r, pc.Kind == PullKind::Finished ? Phase::Done : Phase::Aborted, pc.Why);
                break;
            }
            default:
                break;
        }
    }
}

bool Busy(BotAI* ai)
{
    Player* bot = ai ? ai->GetTickBot() : nullptr;
    if (!bot)
        return false;
    if (BotPlayerLed::Busy(ai)) // following a player through a dungeon
        return true;
    std::lock_guard<std::mutex> lock(s_busyLock);
    return s_busy.count(bot->GetGUID().GetCounter()) != 0;
}

void Update(uint32 diff)
{
    Settings const& cfg = BotDungeon::Cfg();
    s_nowMs += diff;
    if (!cfg.Enabled)
    {
        if (s_run.Active)
            End(s_run, Phase::Aborted, "disabled");
        return;
    }
    ReadOpts();
    if ((s_accMs += diff) < s_opt.IntervalMs)
        return;
    s_accMs = 0;
    if (s_run.Active)
        Step(cfg);
    else
        TryStart(cfg);
}
}
