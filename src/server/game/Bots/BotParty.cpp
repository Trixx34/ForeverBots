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
#include "BotAI.h"
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
    return !BotDungeonRun::Busy(ai) && !BotTravel::Busy(ai) && !BotPet::Busy(ai) && !Busy(ai);
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
    uint32 quest = 0, deaths = 0;
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        auto it = s_seekers.find(guid);
        if (it == s_seekers.end())
            return;
        quest = it->second;
        s_seekers.erase(it);
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
    ev.Summary = StringFormat("died {} times on quest {}: now grouped with leader {} to continue it", deaths, quest, leader);
    ev.Details = StringFormat(R"({{"quest":{},"deaths":{},"threshold":{},"leader":{},"is_leader":{},"step":"grouped"}})", quest, deaths, s_opt.GroupAfterDeaths, leader, guid == leader ? "true" : "false");
    sBotMgr->LogEvent(std::move(ev));
}

void TryForm()
{
    if (s_parties.size() >= s_opt.MaxParties)
        return;
    std::map<uint64, uint32> seekers;
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        seekers = s_seekers;
    }
    if (!s_opt.General && seekers.empty())
        return;
    std::vector<Candidate> cands;
    for (Player* p : sBotMgr->GetOnlineBotPlayers())
    {
        if (!Free(p, s_opt.MinLevel))
            continue;
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
                ClearQuest(c.Guid, sk->second);   // the quest left the log without a hook
        }
        if (!c.Quests.empty())
            cands.push_back(std::move(c));
    }
    if (cands.size() < 2)
        return;

    Config plan = s_opt.Plan;
    if (!s_opt.General)
        plan.FormChancePct = 0;   // only the bots that want a group on their quest seed a party
    for (Formed const& f : FormParties(std::move(cands), plan, ++s_salt))
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

void NoteQuestDeath(Player* bot, uint32 questId)
{
    static uint32 const threshold = uint32(std::max(0, std::min(100, sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterDeaths", 0))));
    if (!threshold || !bot || !questId)
        return;
    uint64 const guid = bot->GetGUID().GetCounter();
    uint32 n;
    {
        std::lock_guard<std::mutex> lock(s_deathLock);
        n = ++s_deaths[{ guid, questId }];
        if (n < threshold || s_seekers.count(guid))
            return;
        s_seekers[guid] = questId;
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
    ev.Summary = StringFormat("died {} times on quest {}: seeks a group to continue it", n, questId);
    ev.Details = StringFormat(R"({{"quest":{},"deaths":{},"threshold":{},"step":"seek"}})", questId, n, threshold);
    sBotMgr->LogEvent(std::move(ev));
}

void ClearQuest(uint64 botGuid, uint32 questId)
{
    std::lock_guard<std::mutex> lock(s_deathLock);
    if (s_deaths.empty() && s_seekers.empty())
        return;
    s_deaths.erase(std::make_pair(botGuid, questId));
    auto it = s_seekers.find(botGuid);
    if (it != s_seekers.end() && it->second == questId)
        s_seekers.erase(it);
}

void ClearBot(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(s_deathLock);
    s_deaths.erase(s_deaths.lower_bound({ botGuid, 0 }), s_deaths.upper_bound({ botGuid, UINT32_MAX }));
    s_seekers.erase(botGuid);
}

void Update(uint32 diff)
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.AI.Party.Enabled", false)
        || sConfigMgr->GetIntDefault("Bot.Quest.GroupAfterDeaths", 0) > 0;
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
