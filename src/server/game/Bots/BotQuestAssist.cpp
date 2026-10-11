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

#include "BotQuestAssist.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotDungeonRun.h"
#include "BotMgr.h"
#include "BotParty.h"
#include "BotPet.h"
#include "BotQuestAssistPlan.h"
#include "BotSocial.h"
#include "BotTravel.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

using Trinity::StringFormat;

namespace BotQuestAssist
{
namespace
{
struct Opts
{
    Config Plan;
    uint32 IntervalMs = 2000;
    uint32 ScanEveryMs = 10000;
    uint32 MaxSessions = 20;
    bool Notify = true;
    bool EscortGuard = true;
};

struct Session
{
    ObjectGuid Player;
    uint32 Quest = 0;
    Kind K = Kind::None;
    std::vector<uint64> Helpers;
    uint32 AgeSec = 0;
    uint32 LifetimeSec = 0;
    uint32 PlayerGoneSec = 0;
    uint32 QuestClosedSec = 0;
    std::unordered_map<uint64, uint32> FarSec, DeadSec;
};

Opts s_opt;
bool s_optRead = false;
std::vector<Session> s_sessions;
uint32 s_accMs = 0, s_scanAccMs = 0, s_nowMs = 1;
std::set<std::pair<uint64, uint32>> s_dismissed;          // (player, quest): the player sent the bots away, do not ask again
std::map<std::pair<uint64, uint32>, uint32> s_cool;       // (player, quest) -> no new session before this time (ms clock)
std::mutex s_busyLock;
std::unordered_set<uint64> s_busy;

void ReadOpts()
{
    if (s_optRead)
        return;
    s_optRead = true;
    auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(k, def), lo, hi); };
    Config& c = s_opt.Plan;
    c.Enabled = true;
    c.MaxHelpers = uint32(opt("Bot.Quest.Assist.MaxHelpers", 2, 1, 4));
    c.RecruitRadius = float(opt("Bot.Quest.Assist.RecruitRadius", 60, 10, 300));
    c.MaxLevelGap = uint32(opt("Bot.Quest.Assist.MaxLevelGap", 4, 0, 20));
    c.MinHelperLevel = uint32(opt("Bot.Quest.Assist.MinLevel", 5, 1, 80));
    c.LifetimeSec = uint32(opt("Bot.Quest.Assist.LifetimeMin", 30, 2, 240)) * 60;
    c.LeashYards = float(opt("Bot.Quest.Assist.LeashYards", 80, 20, 500));
    c.LeashSec = uint32(opt("Bot.Quest.Assist.LeashSec", 45, 5, 600));
    c.DoneGraceSec = uint32(opt("Bot.Quest.Assist.DoneGraceSec", 30, 0, 600));
    c.GuardRadius = float(opt("Bot.Quest.Assist.GuardRadius", 45, 10, 100));
    s_opt.IntervalMs = uint32(opt("Bot.Quest.Assist.IntervalSec", 2, 1, 30)) * 1000;
    s_opt.ScanEveryMs = uint32(opt("Bot.Quest.Assist.ScanEverySec", 10, 2, 600)) * 1000;
    s_opt.MaxSessions = uint32(opt("Bot.Quest.Assist.MaxSessions", 20, 1, 500));
    s_opt.Notify = sConfigMgr->GetBoolDefault("Bot.Quest.Assist.Notify", true);
    s_opt.EscortGuard = sConfigMgr->GetBoolDefault("Bot.Quest.Assist.EscortGuard", true);
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

Player* Find(uint64 counter)
{
    return ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(counter));
}

void SetBusy(uint64 guid, bool on)
{
    std::lock_guard<std::mutex> lock(s_busyLock);
    if (on)
        s_busy.insert(guid);
    else
        s_busy.erase(guid);
}

void Notify(Player* player, std::string const& text)
{
    if (s_opt.Notify && player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(text);
}

QuestFacts FactsOf(Quest const* q)
{
    QuestFacts f;
    f.SuggestedPlayers = q->GetSuggestedPlayers();
    uint32 const info = q->GetQuestInfoID();
    f.Escort = info == QUEST_INFO_ESCORT;
    f.GroupInfo = info == QUEST_INFO_GROUP;
    f.Excluded = info == QUEST_INFO_DUNGEON || info == QUEST_INFO_RAID || info == QUEST_INFO_RAID_10 || info == QUEST_INFO_RAID_25
        || info == QUEST_INFO_PVP || info == QUEST_INFO_WORLD_EVENT || info == QUEST_INFO_LEGENDARY;
    return f;
}

bool QuestOpen(Player* p, uint32 questId)
{
    return p->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE;
}

// A bot that may be asked: awake, alone, in the open world, not owned by a player (alt) and not held by another bot feature.
bool Free(Player* p, uint32 minLevel)
{
    if (!p || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || p->IsInFlight() || p->GetGroup() || p->IsBeingTeleported() || !AiOf(p))
        return false;
    if (p->GetLevel() < minLevel || sBotMgr->IsActiveAlt(p->GetGUID().GetCounter()))
        return false;
    Map const* m = p->GetMap();
    if (!m || m->Instanceable())
        return false;
    BotAI* ai = AiOf(p);
    return !BotDungeonRun::Busy(ai) && !BotTravel::Busy(ai) && !BotPet::Busy(ai) && !BotParty::Busy(ai) && !Busy(ai);
}

void ReleaseBot(uint64 guid)
{
    SetBusy(guid, false);
    if (Player* p = Find(guid))
        if (BotAI* ai = AiOf(p))
        {
            ai->Motion().SetFollow(ObjectGuid::Empty);
            ai->RemoveStrategy(p, "follow", "quest_assist");
        }
}

void Drop(Session& s, uint64 guid, std::string const& why)
{
    Log(guid, "QUEST_ASSIST_RELEASED", StringFormat("lets go of the help for quest {}: {}", s.Quest, why));
    ReleaseBot(guid);
    if (Player* p = Find(guid))
        if (Group* g = p->GetGroup())
            if (g->GetLeaderGUID() == s.Player)
                g->RemoveMember(p->GetGUID());
    s.Helpers.erase(std::remove(s.Helpers.begin(), s.Helpers.end(), guid), s.Helpers.end());
    s.FarSec.erase(guid);
    s.DeadSec.erase(guid);
    if (why == "gone")
        s_dismissed.emplace(s.Player.GetCounter(), s.Quest);
}

void End(Session& s, std::string const& why)
{
    Player* player = ObjectAccessor::FindConnectedPlayer(s.Player);
    Log(s.Player.GetCounter(), "QUEST_ASSIST_ENDED", StringFormat("help for quest {} ends: {} (helpers {}, age {}s)", s.Quest, why, s.Helpers.size(), s.AgeSec));
    std::vector<uint64> helpers = s.Helpers;
    for (uint64 h : helpers)
        Drop(s, h, "session_" + why);
    if (why != "quest_done")
        s_cool[{ s.Player.GetCounter(), s.Quest }] = s_nowMs + 300000;
    if (why == "lifetime" || why == "no_helpers")
        Notify(player, StringFormat("Your bot helpers for '{}' have left.", [&]{ Quest const* q = sObjectMgr->GetQuestTemplate(s.Quest); return q ? q->GetLogTitle() : std::string("?"); }()));
}

// The escorted NPC: a friendly creature near the player whose entry is an objective or ender of the quest.
Creature* FindEscort(Player* player, Quest const* q)
{
    std::vector<uint32> entries;
    for (QuestObjective const& o : q->GetObjectives())
        if (o.Type == QUEST_OBJECTIVE_MONSTER && o.ObjectID > 0)
            entries.push_back(uint32(o.ObjectID));
    auto bounds = sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(q->GetQuestId());
    for (auto itr = bounds.begin(); itr != bounds.end(); ++itr)
        entries.push_back(itr->second);
    Creature* best = nullptr;
    float bd = 1.0e9f;
    std::vector<Creature*> list;
    for (uint32 en : entries)
    {
        list.clear();
        player->GetCreatureListWithEntryInGrid(list, en, 80.0f);
        for (Creature* c : list)
        {
            if (!c->IsAlive() || c->IsPet() || c->IsTotem() || player->IsHostileTo(c))
                continue;
            float const d = player->GetExactDist(c);
            if (d < bd)
            {
                bd = d;
                best = c;
            }
        }
    }
    return best;
}

void GuardEscort(Session& s, Player* player, Quest const* q)
{
    Creature* escort = FindEscort(player, q);
    if (!escort || !escort->IsInCombat())
        return;
    std::vector<Attacker> attackers;
    std::unordered_map<uint64, Creature*> byGuid;
    for (Unit* u : escort->getAttackers())
    {
        Creature* a = u ? u->ToCreature() : nullptr;
        if (!a || !a->IsAlive() || a->IsPet() || a->IsTotem() || !player->IsHostileTo(a))
            continue;
        Attacker at;
        at.Guid = a->GetGUID().GetCounter();
        at.OnEscort = a->GetVictim() == escort;
        at.EscortDist = a->GetExactDist2d(escort);
        at.X = a->GetPositionX();
        at.Y = a->GetPositionY();
        attackers.push_back(at);
        byGuid[at.Guid] = a;
    }
    if (attackers.empty())
        return;
    std::vector<Guard> guards;
    std::unordered_map<uint64, Player*> bots;
    for (uint64 h : s.Helpers)
    {
        Player* bot = Find(h);
        if (!bot || !bot->IsInWorld() || bot->GetMapId() != escort->GetMapId())
            continue;
        Guard g;
        g.Guid = h;
        g.Free = bot->IsAlive() && !bot->IsInCombat();
        g.X = bot->GetPositionX();
        g.Y = bot->GetPositionY();
        guards.push_back(g);
        bots[h] = bot;
    }
    for (Assignment const& a : AssignGuards(std::move(attackers), std::move(guards), s_opt.Plan))
    {
        Player* bot = bots[a.Guard];
        Creature* m = byGuid[a.Target];
        if (!bot || !m || !bot->IsValidAttackTarget(m))
            continue;
        bot->SetFacingToObject(m);
        bot->Attack(m, true);
        m->EngageWithTarget(bot);
        bot->SetInCombatWith(m);
        Log(a.Guard, "QUEST_ESCORT_GUARD", StringFormat("guards {} against {}", escort->GetName(), m->GetName()), BOTLOG_TRACE);
    }
}

void Step(uint32 dtSec)
{
    for (auto it = s_sessions.begin(); it != s_sessions.end();)
    {
        Session& s = *it;
        Player* player = ObjectAccessor::FindConnectedPlayer(s.Player);
        Group* group = player ? player->GetGroup() : nullptr;
        s.AgeSec += dtSec;

        SessionState st;
        st.AgeSec = s.AgeSec;
        st.LifetimeSec = s.LifetimeSec;
        st.PlayerPresent = player && player->IsInWorld();
        if (!st.PlayerPresent || !player->IsAlive() || player->IsBeingTeleported() || player->IsInFlight())
            s.PlayerGoneSec += dtSec;
        else
            s.PlayerGoneSec = 0;
        st.PlayerGoneSec = s.PlayerGoneSec;
        st.QuestOpen = player && QuestOpen(player, s.Quest);
        s.QuestClosedSec = st.QuestOpen ? 0 : s.QuestClosedSec + dtSec;
        st.QuestClosedSec = s.QuestClosedSec;

        for (uint64 guid : s.Helpers)
        {
            Player* p = Find(guid);
            HelperState h;
            h.Guid = guid;
            h.Present = p && p->IsInWorld() && group && p->GetGroup() == group;
            if (h.Present)
            {
                h.Alive = p->IsAlive();
                uint32& dead = s.DeadSec[guid];
                dead = h.Alive ? 0 : dead + dtSec;
                h.DeadSec = dead;
                h.SameMap = player && p->GetMapId() == player->GetMapId();
                uint32& far = s.FarSec[guid];
                if (!h.SameMap || (player && p->GetDistance2d(player) > s_opt.Plan.LeashYards))
                    far += dtSec;
                else
                    far = 0;
                h.FarSec = far;
            }
            st.Helpers.push_back(h);
        }

        Verdict v = Evaluate(st, s_opt.Plan);
        if (v.End)
        {
            End(s, v.Reason);
            it = s_sessions.erase(it);
            continue;
        }
        for (size_t i = 0; i < v.Drop.size(); ++i)
            Drop(s, v.Drop[i], v.DropReasons[i]);

        if (s_opt.EscortGuard && s.K == Kind::Escort && player && st.QuestOpen && !s.Helpers.empty())
            if (Quest const* q = sObjectMgr->GetQuestTemplate(s.Quest))
                GuardEscort(s, player, q);
        ++it;
    }
}

bool HasSession(ObjectGuid player)
{
    return std::any_of(s_sessions.begin(), s_sessions.end(), [&](Session const& s) { return s.Player == player; });
}

void TryRecruit(Player* player)
{
    if (!player->IsInWorld() || !player->IsAlive() || player->IsInCombat() || player->IsBeingTeleported() || player->IsInFlight())
        return;
    Map const* map = player->GetMap();
    if (!map || map->Instanceable() || HasSession(player->GetGUID()))
        return;
    Group* group = player->GetGroup();
    if (group && (group->isRaidGroup() || group->GetLeaderGUID() != player->GetGUID()))
        return;   // someone else leads, or a raid: not ours to fill
    uint32 const groupSize = group ? group->GetMembersCount() : 1;
    uint64 const pid = player->GetGUID().GetCounter();

    // the first open quest of the player that wants company
    Quest const* quest = nullptr;
    Kind kind = Kind::None;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE && !quest; ++slot)
    {
        uint32 const id = player->GetQuestSlotQuestId(slot);
        if (!id || !QuestOpen(player, id) || s_dismissed.count({ pid, id }))
            continue;
        if (auto c = s_cool.find({ pid, id }); c != s_cool.end() && int32(c->second - s_nowMs) > 0)
            continue;
        Quest const* q = sObjectMgr->GetQuestTemplate(id);
        if (!q)
            continue;
        Kind const k = Classify(FactsOf(q));
        if (k != Kind::None && HelpersWanted(k, q->GetSuggestedPlayers(), groupSize, s_opt.Plan) > 0)
        {
            quest = q;
            kind = k;
        }
    }
    if (!quest)
        return;
    uint32 const wanted = HelpersWanted(kind, quest->GetSuggestedPlayers(), groupSize, s_opt.Plan);

    PlayerFacts pf;
    pf.MapId = player->GetMapId();
    pf.Level = player->GetLevel();
    pf.X = player->GetPositionX();
    pf.Y = player->GetPositionY();
    std::vector<Candidate> cands;
    for (Player* p : sBotMgr->GetOnlineBotPlayers())
    {
        if (!Free(p, s_opt.Plan.MinHelperLevel))
            continue;
        Candidate c;
        c.Guid = p->GetGUID().GetCounter();
        c.MapId = p->GetMapId();
        c.Level = p->GetLevel();
        c.X = p->GetPositionX();
        c.Y = p->GetPositionY();
        cands.push_back(c);
    }
    std::vector<uint64> picked = PickHelpers(pf, cands, wanted, s_opt.Plan);
    if (picked.empty())
        return;

    if (!group)
    {
        group = new Group;
        if (!group->Create(player))
        {
            delete group;
            return;
        }
        sGroupMgr->AddGroup(group);
    }

    Session s;
    s.Player = player->GetGUID();
    s.Quest = quest->GetQuestId();
    s.K = kind;
    s.LifetimeSec = s_opt.Plan.LifetimeSec;
    std::string names;
    for (uint64 guid : picked)
    {
        Player* bot = Find(guid);
        if (!bot || bot->GetGroup() || !group->AddMember(bot))
            continue;
        s.Helpers.push_back(guid);
        SetBusy(guid, true);
        if (BotAI* ai = AiOf(bot))
        {
            ai->Motion().ClearGoal();
            ai->AddStrategy(bot, "follow", "quest_assist");
            ai->Motion().SetFollow(player->GetGUID());
        }
        if (player->CanShareQuest(s.Quest))
            BotSocial::OnQuestPushed(player, bot, s.Quest);
        names += (names.empty() ? "" : ", ") + bot->GetName();
        Log(guid, "QUEST_ASSIST_JOINED", StringFormat("joins {} for the {} quest '{}'", player->GetName(), KindName(kind), quest->GetLogTitle()));
    }
    if (s.Helpers.empty())
    {
        if (group->GetMembersCount() <= 1)
            group->Disband();
        return;
    }
    group->BroadcastGroupUpdate();
    Notify(player, StringFormat("{} joined your group to help with '{}'. Remove them from the group to send them away.", names, quest->GetLogTitle()));
    Log(player->GetGUID().GetCounter(), "QUEST_ASSIST_STARTED",
        StringFormat("{} helpers for the {} quest {} ({})", s.Helpers.size(), KindName(kind), s.Quest, names));
    s_sessions.push_back(std::move(s));
}

void Scan()
{
    // forget memories of players that left or quests that are gone
    for (auto it = s_dismissed.begin(); it != s_dismissed.end();)
    {
        Player* p = Find(it->first);
        it = (!p || p->GetQuestStatus(it->second) == QUEST_STATUS_NONE) ? s_dismissed.erase(it) : std::next(it);
    }
    for (auto it = s_cool.begin(); it != s_cool.end();)
        it = int32(it->second - s_nowMs) <= 0 ? s_cool.erase(it) : std::next(it);

    for (auto const& [id, session] : sWorld->GetAllSessions())
    {
        if (s_sessions.size() >= s_opt.MaxSessions)
            return;
        if (!session || session->IsBot())
            continue;
        if (Player* p = session->GetPlayer())
            TryRecruit(p);
    }
}
}

bool Busy(BotAI* ai)
{
    Player* bot = ai ? ai->GetTickBot() : nullptr;
    if (!bot)
        return false;
    std::lock_guard<std::mutex> lock(s_busyLock);
    return s_busy.count(bot->GetGUID().GetCounter()) != 0;
}

void Update(uint32 diff)
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.Quest.Assist.Enabled", false);
    if (!enabled)
        return;
    ReadOpts();
    s_nowMs += diff;
    s_scanAccMs += diff;
    if ((s_accMs += diff) < s_opt.IntervalMs)
        return;
    uint32 const dtSec = std::max<uint32>(1, s_accMs / 1000);
    s_accMs = 0;
    Step(dtSec);
    if (s_scanAccMs >= s_opt.ScanEveryMs)
    {
        s_scanAccMs = 0;
        Scan();
    }
}
}
