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

#include "BotParty.h"
#include "BotRules.h"
#include "BotAI.h"
#include "BotBattleground.h"
#include "BotBehavior.h"
#include "BotDungeonRun.h"
#include "BotMgr.h"
#include "BotPartyPlan.h"
#include "BotPet.h"
#include "BotTravel.h"
#include "Config.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace BotParty
{
namespace
{
using Trinity::StringFormat;

struct Opts
{
    Config Plan;
    uint32 IntervalMs = 5000;
    uint32 FormEveryMs = 20000;
    uint32 MaxParties = 10;
    uint32 MinLevel = 5;
    uint32 GroupAfterDeaths = 0;                  // Bot.Quest.GroupAfterDeaths, 0 = off
    bool General = false;                         // Bot.AI.Party.Enabled
};

struct Party
{
    ObjectGuid Leader;
    std::vector<uint64> Members;                  // without the leader
    uint32 SharedQuest = 0;
    uint32 AgeSec = 0;
    uint32 LifetimeSec = 0;
    uint32 LeaderGoneSec = 0;
    std::unordered_map<uint64, uint32> FarSec;    // per member
};

Opts s_opt;
bool s_optRead = false;
std::vector<Party> s_parties;
uint32 s_accMs = 0, s_formAccMs = 0, s_salt = 0;
std::mutex s_busyLock;
std::unordered_set<uint64> s_busy;

// Deaths per bot and quest (counted while a quest task is active), and the bots that reached the threshold and still look for a group.
// Written by map threads (death, quest hooks), read by the world thread.
std::mutex s_deathLock;
std::map<std::pair<uint64, uint32>, uint32> s_deaths;
std::map<uint64, uint32> s_seekers;               // bot -> quest
std::map<std::pair<uint64, uint32>, uint32> s_fails;   // failed quest attempts per (bot, quest), see NoteQuestFail
std::map<uint64, std::string> s_trigger;          // bot -> what made it seek (deaths / fails / deaths+fails)

void ReadOpts()
{
    if (s_optRead)
        return;
    s_optRead = true;
    auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::max(lo, std::min(hi, sConfigMgr->GetIntDefault(k, def))); };
    Config& c = s_opt.Plan;
    c.Enabled = true;
    c.MaxSize = uint32(opt("Bot.AI.Party.MaxSize", 3, 2, 5));
    c.MinSize = uint32(opt("Bot.AI.Party.MinSize", 2, 2, int32(c.MaxSize)));
    c.JoinRadius = float(opt("Bot.AI.Party.JoinRadius", 40, 5, 200));
    c.MaxLevelSpread = uint32(opt("Bot.AI.Party.MaxLevelSpread", 3, 0, 20));
    c.MinSharedQuests = uint32(opt("Bot.AI.Party.MinSharedQuests", 1, 1, 10));
    c.FormChancePct = uint32(opt("Bot.AI.Party.FormChancePct", 25, 0, 100));
    c.LifetimeSec = uint32(opt("Bot.AI.Party.LifetimeMin", 20, 2, 240)) * 60;
    c.LeashYards = float(opt("Bot.AI.Party.LeashYards", 70, 20, 500));
    c.LeashSec = uint32(opt("Bot.AI.Party.LeashSec", 45, 5, 600));
    c.LeaderGoneSec = uint32(opt("Bot.AI.Party.LeaderGoneSec", 30, 5, 600));
    s_opt.IntervalMs = uint32(opt("Bot.AI.Party.IntervalSec", 5, 1, 60)) * 1000;
    s_opt.FormEveryMs = uint32(opt("Bot.AI.Party.FormEverySec", 20, 5, 3600)) * 1000;
    s_opt.MaxParties = uint32(opt("Bot.AI.Party.MaxParties", 10, 1, 500));
    s_opt.MinLevel = uint32(opt("Bot.AI.Party.MinLevel", 5, 1, 80));
    s_opt.GroupAfterDeaths = uint32(opt("Bot.Quest.GroupAfterDeaths", 0, 0, 100));
    s_opt.General = sConfigMgr->GetBoolDefault("Bot.AI.Party.Enabled", false);
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

// Unfinished quests in the log.
std::vector<uint32> OpenQuests(Player* p)
{
    std::vector<uint32> out;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 id = p->GetQuestSlotQuestId(slot))
            if (p->GetQuestStatus(id) == QUEST_STATUS_INCOMPLETE)
                out.push_back(id);
    return out;
}

// Why a bot cannot join a party right now, or nullptr when it is free.
char const* FreeWhy(Player* p, uint32 minLevel)
{
    if (!p || !p->IsInWorld() || !AiOf(p))
        return "offline";
    if (!p->IsAlive())
        return "dead";
    if (p->IsInCombat())
        return "in_combat";
    if (p->IsInFlight() || p->IsBeingTeleported())
        return "travelling";
    if (p->GetGroup())
        return "already_grouped";
    if (p->GetLevel() < minLevel)
        return "level_below_party_min";
    if (sBotMgr->IsActiveAlt(p->GetGUID().GetCounter()))
        return "active_alt";
    Map const* m = p->GetMap();
    if (!m || m->Instanceable())
        return "instanced";
    BotAI* ai = AiOf(p);
    if (BotDungeonRun::Busy(ai) || BotTravel::Busy(ai) || BotPet::Busy(ai) || BotBattleground::Busy(ai) || Busy(ai))
        return "busy_other_task";
    return nullptr;
}

bool Free(Player* p, uint32 minLevel)
{
    return !FreeWhy(p, minLevel);
}

// An eligible (bot, quest) pair that seeks a group but does not get one: one row per pair whenever the reason changes
// (reason quest_group_skip, details.why). World thread only.
std::map<uint64, std::pair<uint32, std::string>> s_noGroupWhy;

void NoteNoGroup(uint64 guid, uint32 quest, std::string const& why, std::string const& extra = std::string())
{
    if (!BotRules::Cfg().V3Log)
        return;
    auto& last = s_noGroupWhy[guid];
    if (last.first == quest && last.second == why)
        return;
    last = { quest, why };
    BotEvent ev;
    ev.BotGuid = guid;
    ev.Type = "decision";
    ev.Reason = "quest_group_skip";
    ev.QuestId = quest;
    ev.Summary = StringFormat("quest {}: seeks a group but none formed ({})", quest, why);
    ev.Details = StringFormat(R"({{"quest":{},"why":"{}"{}}})", quest, why, extra);
    sBotMgr->LogEvent(std::move(ev));
}

void ReleaseMember(uint64 guid)
{
    SetBusy(guid, false);
    if (Player* p = Find(guid))
        if (BotAI* ai = AiOf(p))
            ai->Motion().SetFollow(ObjectGuid::Empty);
}

void End(Party& party, std::string const& why)
{
    Player* leader = ObjectAccessor::FindPlayer(party.Leader);
    Log(party.Leader.GetCounter(), "PARTY_DISBANDED", StringFormat("party ends: {} (members {}, age {}s)", why, party.Members.size() + 1, party.AgeSec));
    for (uint64 m : party.Members)
        ReleaseMember(m);
    if (leader)
        if (Group* g = leader->GetGroup())
            g->Disband();
}

void Drop(Party& party, uint64 guid, std::string const& why)
{
    Log(guid, "PARTY_MEMBER_DROPPED", StringFormat("leader {} drops member {}: {}", party.Leader.GetCounter(), guid, why));
    ReleaseMember(guid);
    if (Player* p = Find(guid))
        if (Group* g = p->GetGroup())
            g->RemoveMember(p->GetGUID());
    party.Members.erase(std::remove(party.Members.begin(), party.Members.end(), guid), party.Members.end());
    party.FarSec.erase(guid);
}

void Step(uint32 dtSec)
{
    for (auto it = s_parties.begin(); it != s_parties.end();)
    {
        Party& party = *it;
        Player* leader = ObjectAccessor::FindPlayer(party.Leader);
        Group* group = leader ? leader->GetGroup() : nullptr;
        party.AgeSec += dtSec;

        PartyState st;
        st.AgeSec = party.AgeSec;
        st.LifetimeSec = party.LifetimeSec;
        st.LeaderPresent = leader && leader->IsInWorld() && group;
        if (!st.LeaderPresent || !leader->IsAlive() || leader->IsBeingTeleported() || leader->IsInFlight())
            party.LeaderGoneSec += dtSec;
        else
            party.LeaderGoneSec = 0;
        st.LeaderGoneSec = party.LeaderGoneSec;
        if (leader)
            st.LeaderLevel = leader->GetLevel();

        std::vector<uint32> leaderQuests = leader ? OpenQuests(leader) : std::vector<uint32>();
        for (uint64 guid : party.Members)
        {
            Player* p = Find(guid);
            MemberState m;
            m.Guid = guid;
            m.Present = p && p->IsInWorld() && group && p->GetGroup() == group;
            if (m.Present)
            {
                m.Alive = p->IsAlive();
                m.Level = p->GetLevel();
                m.SameMap = leader && p->GetMapId() == leader->GetMapId();
                uint32& far = party.FarSec[guid];
                if (!m.SameMap || (leader && p->GetDistance2d(leader) > s_opt.Plan.LeashYards))
                    far += dtSec;
                else
                    far = 0;
                m.FarSec = far;
                m.SharesQuest = leader && SharedCount(leaderQuests, OpenQuests(p)) >= 1;
            }
            st.Members.push_back(m);
        }

        Verdict v = Evaluate(st, s_opt.Plan);
        if (v.Disband)
        {
            End(party, v.Reason);
            it = s_parties.erase(it);
            continue;
        }
        for (size_t i = 0; i < v.Drop.size(); ++i)
            Drop(party, v.Drop[i], v.DropReasons[i]);
        ++it;
    }
}

// A seeker that landed in a party: logged once as a decision (reason quest_group_after_deaths), then its counter starts over.
void NoteSeekerGrouped(uint64 guid, uint64 leader)
{
    s_noGroupWhy.erase(guid);
    uint32 quest = 0, deaths = 0, fails = 0;
    std::string trigger = "deaths";
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        auto it = s_seekers.find(guid);
        if (it == s_seekers.end())
            return;
        quest = it->second;
        s_seekers.erase(it);
        auto tg = s_trigger.find(guid);
        if (tg != s_trigger.end())
        {
            trigger = tg->second;
            s_trigger.erase(tg);
        }
        auto f = s_fails.find({ guid, quest });
        if (f != s_fails.end())
        {
            fails = f->second;
            s_fails.erase(f);
        }
        auto d = s_deaths.find({ guid, quest });
        if (d != s_deaths.end())
        {
            deaths = d->second;
            s_deaths.erase(d);
        }
    }
    BotEvent ev;
    ev.BotGuid = guid;
    ev.Type = "decision";
    ev.Reason = "quest_group_after_deaths";
    ev.QuestId = quest;
    ev.Summary = StringFormat("{} deaths and {} failed attempts on quest {} (trigger: {}): now grouped with leader {} to continue it", deaths, fails, quest, trigger, leader);
    ev.Details = StringFormat(R"({{"quest":{},"deaths":{},"fails":{},"trigger":"{}","threshold":{},"leader":{},"is_leader":{},"step":"grouped"}})", quest, deaths, fails, trigger, s_opt.GroupAfterDeaths, leader, guid == leader ? "true" : "false");
    sBotMgr->LogEvent(std::move(ev));
}

void TryForm()
{
    std::map<uint64, uint32> seekers;
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        seekers = s_seekers;
    }
    for (auto it = s_noGroupWhy.begin(); it != s_noGroupWhy.end();)
        it = seekers.count(it->first) ? std::next(it) : s_noGroupWhy.erase(it);
    if (s_parties.size() >= s_opt.MaxParties)
    {
        for (auto const& sk : seekers)
            NoteNoGroup(sk.first, sk.second, "max_parties", StringFormat(R"(,"parties":{})", uint32(s_parties.size())));
        return;
    }
    if (!s_opt.General && seekers.empty())
        return;
    std::vector<Candidate> cands;
    std::set<uint64> seenSeekers;
    for (Player* p : sBotMgr->GetOnlineBotPlayers())
    {
        if (char const* busy = FreeWhy(p, s_opt.MinLevel))
        {
            auto sk = seekers.find(p->GetGUID().GetCounter());
            if (sk != seekers.end())
            {
                seenSeekers.insert(sk->first);
                NoteNoGroup(sk->first, sk->second, busy);
            }
            continue;
        }
        Candidate c;
        c.Guid = p->GetGUID().GetCounter();
        c.MapId = p->GetMapId();
        c.Level = p->GetLevel();
        c.X = p->GetPositionX();
        c.Y = p->GetPositionY();
        c.Quests = OpenQuests(p);
        auto sk = seekers.find(c.Guid);
        if (sk != seekers.end())
        {
            if (std::find(c.Quests.begin(), c.Quests.end(), sk->second) != c.Quests.end())
            {
                c.SeekQuest = sk->second;
                c.Quests = { sk->second };   // partners must hold this quest
            }
            else
            {
                NoteNoGroup(c.Guid, sk->second, "quest_not_in_log");
                ClearQuest(c.Guid, sk->second);   // the quest left the log without a hook
            }
        }
        if (!c.Quests.empty())
            cands.push_back(std::move(c));
    }
    if (cands.size() < 2)
    {
        for (Candidate const& c : cands)
            if (c.SeekQuest)
                NoteNoGroup(c.Guid, c.SeekQuest, "no_candidates", StringFormat(R"(,"free_bots":{})", uint32(cands.size())));
        return;
    }

    Config plan = s_opt.Plan;
    if (!s_opt.General)
        plan.FormChancePct = 0;   // only the bots that want a group on their quest seed a party
    std::vector<std::pair<uint64, uint32>> waiting;
    for (Candidate const& c : cands)
        if (c.SeekQuest)
            waiting.emplace_back(c.Guid, c.SeekQuest);
    uint32 const freeBots = uint32(cands.size());
    std::vector<Formed> formed = FormParties(std::move(cands), plan, ++s_salt);
    for (auto const& w : waiting)
    {
        bool placed = false;
        for (Formed const& f : formed)
            placed = placed || f.Leader == w.first || std::find(f.Members.begin(), f.Members.end(), w.first) != f.Members.end();
        if (!placed)
            NoteNoGroup(w.first, w.second, "no_partner_with_quest", StringFormat(R"(,"free_bots":{},"join_radius":{:.0f},"level_spread":{})", freeBots, s_opt.Plan.JoinRadius, s_opt.Plan.MaxLevelSpread));
    }
    for (Formed const& f : formed)
    {
        if (s_parties.size() >= s_opt.MaxParties)
            break;
        Player* leader = Find(f.Leader);
        if (!leader || leader->GetGroup())
            continue;
        Group* group = new Group;
        if (!group->Create(leader))
        {
            delete group;
            continue;
        }
        sGroupMgr->AddGroup(group);
        Party party;
        party.Leader = leader->GetGUID();
        party.SharedQuest = f.SharedQuest;
        party.LifetimeSec = LifetimeFor(s_opt.Plan, f.Leader, s_salt);
        for (uint64 guid : f.Members)
        {
            Player* p = Find(guid);
            if (!p || p->GetGroup() || !group->AddMember(p))
                continue;
            party.Members.push_back(guid);
            SetBusy(guid, true);
            if (BotAI* ai = AiOf(p))
            {
                ai->Motion().ClearGoal();
                ai->Motion().SetFollow(leader->GetGUID());
            }
        }
        if (party.Members.empty())
        {
            group->Disband();
            continue;
        }
        group->BroadcastGroupUpdate();
        std::string ids;
        for (uint64 guid : party.Members)
            ids += StringFormat("{}{}", ids.empty() ? "" : ",", guid);
        for (uint64 guid : party.Members)
            NoteSeekerGrouped(guid, f.Leader);
        NoteSeekerGrouped(f.Leader, f.Leader);
        Log(f.Leader, "PARTY_FORMED", StringFormat("party formed around quest {}: members {} (leader level {})", f.SharedQuest, ids, leader->GetLevel()));
        s_parties.push_back(std::move(party));
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

// Shared by deaths and fails. Seeks when the deaths reach GroupAfterDeaths, or deaths + fails reach GroupAfterFails.
static void NoteQuestEvent(Player* bot, uint32 questId, bool death, char const* code)
{
    static uint32 const threshold = uint32(std::max(0, std::min(100, sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterDeaths", 0))));
    static uint32 const failThreshold = uint32(std::max(0, std::min(100, sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterFails", 3))));
    if ((!threshold && !failThreshold) || !bot || !questId)
        return;
    if (!death && !failThreshold)
        return;
    uint64 const guid = bot->GetGUID().GetCounter();
    uint32 n, fails;
    char const* trigger;
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        uint32& dn = s_deaths[{ guid, questId }];
        uint32& fn = s_fails[{ guid, questId }];
        if (death)
            ++dn;
        else
            ++fn;
        n = dn;
        fails = fn;
        bool const byDeaths = threshold && dn >= threshold;
        bool const byTotal = failThreshold && dn + fn >= failThreshold;
        if (!byDeaths && !byTotal)
            return;
        auto cur = s_seekers.find(guid);
        if (cur != s_seekers.end())
        {
            // eligible but the bot already seeks a group for another quest: logged once per pair, when it first reaches the threshold
            if (cur->second != questId && (dn + fn == (byDeaths ? threshold : failThreshold)) && BotRules::Cfg().V3Log)
            {
                uint32 const other = cur->second;
                BotEvent skip;
                skip.BotGuid = guid;
                skip.Type = "decision";
                skip.Reason = "quest_group_skip";
                skip.QuestId = questId;
                skip.Summary = StringFormat("quest {}: eligible for a group but already seeks one for quest {}", questId, other);
                skip.Details = StringFormat(R"({{"quest":{},"why":"already_seeking_other_quest","other_quest":{},"deaths":{},"fails":{}}})", questId, other, dn, fn);
                sBotMgr->LogEvent(std::move(skip));
            }
            return;
        }
        trigger = byDeaths ? "deaths" : (fn && dn ? "deaths+fails" : fn ? "fails" : "deaths");
        s_seekers[guid] = questId;
        s_trigger[guid] = trigger;
    }
    BotEvent ev;
    ev.BotGuid = guid;
    ev.Type = "decision";
    ev.Reason = "quest_group_after_deaths";
    ev.QuestId = questId;
    ev.Level = uint8(bot->GetLevel());
    ev.MapId = uint16(bot->GetMapId());
    ev.X = bot->GetPositionX();
    ev.Y = bot->GetPositionY();
    ev.Z = bot->GetPositionZ();
    ev.Summary = StringFormat("{} deaths and {} failed attempts on quest {} (trigger: {}): seeks a group to continue it", n, fails, questId, trigger);
    ev.Details = StringFormat(R"({{"quest":{},"deaths":{},"fails":{},"trigger":"{}","last_event":"{}","last_code":"{}","threshold":{},"fail_threshold":{},"step":"seek"}})", questId, n, fails, trigger,
        death ? "death" : "fail", code ? code : "", threshold, failThreshold);
    sBotMgr->LogEvent(std::move(ev));
}

void NoteQuestDeath(Player* bot, uint32 questId)
{
    NoteQuestEvent(bot, questId, true, "");
}

void NoteQuestFail(Player* bot, uint32 questId, char const* code)
{
    NoteQuestEvent(bot, questId, false, code);
}

void ClearQuest(uint64 botGuid, uint32 questId)
{
    std::lock_guard<std::mutex> lock(s_deathLock);
    if (s_deaths.empty() && s_seekers.empty() && s_fails.empty())
        return;
    s_deaths.erase(std::make_pair(botGuid, questId));
    s_fails.erase(std::make_pair(botGuid, questId));
    auto it = s_seekers.find(botGuid);
    if (it != s_seekers.end() && it->second == questId)
    {
        s_seekers.erase(it);
        s_trigger.erase(botGuid);
    }
}

void ClearBot(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(s_deathLock);
    s_deaths.erase(s_deaths.lower_bound({ botGuid, 0 }), s_deaths.upper_bound({ botGuid, UINT32_MAX }));
    s_fails.erase(s_fails.lower_bound({ botGuid, 0 }), s_fails.upper_bound({ botGuid, UINT32_MAX }));
    s_seekers.erase(botGuid);
    s_trigger.erase(botGuid);
}

void Update(uint32 diff)
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.AI.Party.Enabled", false)
        || sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterDeaths", 0) > 0
        || sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterFails", 3) > 0;
    if (!enabled)
        return;
    ReadOpts();
    s_formAccMs += diff;
    if ((s_accMs += diff) < s_opt.IntervalMs)
        return;
    uint32 const dtSec = std::max<uint32>(1, s_accMs / 1000);
    s_accMs = 0;
    Step(dtSec);
    if (s_formAccMs >= s_opt.FormEveryMs)
    {
        s_formAccMs = 0;
        TryForm();
    }
}
}
