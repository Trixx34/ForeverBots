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

#include "BotLfg.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotChat.h"
#include "BotDungeonPlan.h"
#include "BotDungeonRun.h"
#include "BotLfgPlan.h"
#include "BotMgr.h"
#include "BotParty.h"
#include "BotPet.h"
#include "BotPlayerLed.h"
#include "BotTravel.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using Trinity::StringFormat;

namespace BotLfg
{
namespace
{
using BotDungeon::Role;

struct Opts
{
    Config Plan;
    uint32 IntervalMs = 3000;
    uint32 MaxQueued = 20;
    bool Notify = true;
};

struct Entry
{
    ObjectGuid Player;
    Role AsRole = Role::Dps;           // what the player plays
    bool Searching = true;
    uint32 AgeSec = 0;
    uint32 GoneSec = 0;
    uint32 Missing = 0;
    std::vector<uint64> Bots;          // brought by this search and still held
    std::unordered_map<uint64, Role> BotRoles;
    uint32 LastMissingKey = 0xFFFFFFFFu;
};

Opts s_opt;
bool s_optRead = false;
uint32 s_accMs = 0;
std::unordered_map<uint64, Entry> s_entries;   // by player guid counter
std::mutex s_busyLock;
std::unordered_set<uint64> s_busy;

void ReadOpts()
{
    if (s_optRead)
        return;
    s_optRead = true;
    auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(k, def), lo, hi); };
    Config& c = s_opt.Plan;
    c.Enabled = sConfigMgr->GetBoolDefault("Bot.LFG.Enabled", false);
    c.GroupSize = uint32(opt("Bot.LFG.GroupSize", 5, 2, 5));
    c.MaxLevelSpread = uint32(opt("Bot.LFG.MaxLevelSpread", 4, 0, 20));
    c.MinLevel = uint32(opt("Bot.LFG.MinLevel", 10, 1, 80));
    c.Teleport = sConfigMgr->GetBoolDefault("Bot.LFG.Teleport", true);
    c.MaxDistance = float(opt("Bot.LFG.MaxDistance", 1500, 20, 20000));
    c.TimeoutSec = uint32(opt("Bot.LFG.TimeoutSec", 180, 10, 3600));
    c.ReleaseSec = uint32(opt("Bot.LFG.ReleaseSec", 60, 5, 3600));
    s_opt.IntervalMs = uint32(opt("Bot.LFG.IntervalSec", 3, 1, 60)) * 1000;
    s_opt.MaxQueued = uint32(opt("Bot.LFG.MaxQueued", 20, 1, 500));
    s_opt.Notify = sConfigMgr->GetBoolDefault("Bot.LFG.Notify", true);
}

void Log(uint64 guid, char const* code, std::string summary, uint8 sev = BOTLOG_INFO)
{
    BotEvent ev;
    ev.BotGuid = guid;
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

bool IsBot(Player* p) { return p && p->GetSession() && p->GetSession()->IsBot(); }

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

void Say(Player* player, std::string const& text)
{
    if (s_opt.Notify && player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(StringFormat("LFG: {}", text));
}

TeamId TeamOf(Player const* p) { return p->GetTeamId(); }

// A bot that can be called: alive, out of combat, ungrouped, in the open world and not held by another bot task.
bool Free(Player* p)
{
    if (!IsBot(p) || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || p->IsInFlight() || p->GetGroup() || p->IsBeingTeleported())
        return false;
    if (sBotMgr->IsActiveAlt(p->GetGUID().GetCounter()))
        return false;
    Map const* m = p->GetMap();
    if (!m || m->Instanceable())
        return false;
    BotAI* ai = AiOf(p);
    return ai && !BotDungeonRun::Busy(ai) && !BotTravel::Busy(ai) && !BotPet::Busy(ai) && !BotParty::Busy(ai) && !BotPlayerLed::Busy(ai) && !Busy(ai);
}

// Where a player can lead a search: the open world or a five-man dungeon, alone or as the leader of a party (checked all the time, a
// search ends when it stops being true).
char const* LeadRefusal(Player* player)
{
    Map const* m = player->GetMap();
    if (!m || m->IsBattlegroundOrArena() || m->IsRaid())
        return "Bots cannot be called here.";
    if (Group* g = player->GetGroup())
    {
        if (g->isRaidGroup())
            return "A raid group cannot be filled; leave the raid first.";
        if (!g->IsLeader(player->GetGUID()))
            return "Only the group leader can ask for bots.";
    }
    return nullptr;
}

// Checked when the request arrives.
char const* Refusal(Player* player)
{
    if (!player->IsInWorld() || player->IsBeingTeleported())
        return "Wait until you are in the world.";
    if (!player->IsAlive())
        return "Not while you are dead.";
    return LeadRefusal(player);
}

Role RoleOfMember(Entry const& e, ObjectGuid guid, uint8 classId, ObjectGuid player)
{
    if (guid == player)
        return e.AsRole;
    auto it = e.BotRoles.find(guid.GetCounter());
    return it != e.BotRoles.end() ? it->second : BotDungeon::PreferredRole(classId);
}

std::vector<Role> RolesInGroup(Entry const& e, Player* player)
{
    std::vector<Role> have;
    Group* g = player->GetGroup();
    if (!g)
    {
        have.push_back(e.AsRole);
        return have;
    }
    for (Group::MemberSlot const& slot : g->GetMemberSlots())
        have.push_back(RoleOfMember(e, slot.guid, slot._class, player->GetGUID()));
    return have;
}

void ReleaseBot(Entry& e, uint64 guid, char const* why)
{
    SetBusy(guid, false);
    if (Player* p = Find(guid))
    {
        if (BotAI* ai = AiOf(p))
            ai->Motion().SetFollow(ObjectGuid::Empty);
        if (Group* g = p->GetGroup())
            g->RemoveMember(p->GetGUID());
    }
    Log(guid, "LFG_RELEASED", StringFormat("leaves the group of player {}: {}", e.Player.GetCounter(), why));
    e.Bots.erase(std::remove(e.Bots.begin(), e.Bots.end(), guid), e.Bots.end());
    e.BotRoles.erase(guid);
}

void ReleaseAll(Entry& e, char const* why)
{
    std::vector<uint64> const bots = e.Bots;
    for (uint64 guid : bots)
        ReleaseBot(e, guid, why);
}

void Fill(Entry& e, Player* player)
{
    std::vector<Candidate> cands;
    for (Player* p : sBotMgr->GetOnlineBotPlayers())
    {
        if (!Free(p))
            continue;
        Candidate c;
        c.Guid = p->GetGUID().GetCounter();
        c.ClassId = p->GetClass();
        c.Level = p->GetLevel();
        c.Team = TeamOf(p) == TEAM_ALLIANCE ? 0 : 1;
        c.SameMap = p->GetMapId() == player->GetMapId();
        c.Distance = c.SameMap ? p->GetExactDist(player) : 1.0e6f;
        cands.push_back(c);
    }

    Wanted want;
    want.Team = TeamOf(player) == TEAM_ALLIANCE ? 0 : 1;
    want.Level = player->GetLevel();
    want.Have = RolesInGroup(e, player);
    FillResult const r = PlanFill(cands, want, s_opt.Plan);

    uint32 index = 0;
    for (Pick const& pick : r.Picks)
    {
        Player* bot = Find(pick.Guid);
        if (!bot || !Free(bot))
            continue;
        Group* group = player->GetGroup();
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
        if (group->IsFull() || !group->AddMember(bot))
            continue;
        group->BroadcastGroupUpdate();
        e.Bots.push_back(pick.Guid);
        e.BotRoles[pick.Guid] = pick.AsRole;
        SetBusy(pick.Guid, true);

        if (BotAI* ai = AiOf(bot))
        {
            ai->Motion().ClearGoal();
            ai->Motion().SetFollow(player->GetGUID());
        }
        if (s_opt.Plan.Teleport && !(bot->GetMapId() == player->GetMapId() && bot->GetExactDist(player) < 30.0f))
        {
            float dx = 0, dy = 0;
            BotChat::SpreadOffset(index++, dx, dy);
            bot->TeleportTo(player->GetMapId(), player->GetPositionX() + dx, player->GetPositionY() + dy, player->GetPositionZ() + 0.5f, player->GetOrientation());
        }
        Log(pick.Guid, "LFG_JOINED", StringFormat("joins {} as {} (level {}, search {}s old)", player->GetName(), RoleText(pick.AsRole), bot->GetLevel(), e.AgeSec));
        Say(player, StringFormat("{} ({}) joined your group.", bot->GetName(), RoleText(pick.AsRole)));
    }
    e.Missing = r.MissingTanks + r.MissingHealers + r.MissingDps;

    // tell the player what is still open when that changed
    uint32 const key = (r.MissingTanks << 16) | (r.MissingHealers << 8) | r.MissingDps;
    if (e.Missing && key != e.LastMissingKey)
        Say(player, StringFormat("still looking for {}.", DescribeMissing(r.MissingTanks, r.MissingHealers, r.MissingDps)));
    e.LastMissingKey = key;
}

// Bots that left the group (kicked, left by hand, disbanded) are forgotten.
void PruneBots(Entry& e, Player* player)
{
    Group* g = player ? player->GetGroup() : nullptr;
    std::vector<uint64> const bots = e.Bots;
    for (uint64 guid : bots)
    {
        Player* p = Find(guid);
        if (!p || !g || p->GetGroup() != g)
        {
            SetBusy(guid, false);
            if (p)
                if (BotAI* ai = AiOf(p))
                    ai->Motion().SetFollow(ObjectGuid::Empty);
            Log(guid, "LFG_GONE", StringFormat("no longer in the group of player {}", e.Player.GetCounter()));
            e.Bots.erase(std::remove(e.Bots.begin(), e.Bots.end(), guid), e.Bots.end());
            e.BotRoles.erase(guid);
        }
    }
}

void Step(uint32 dtSec)
{
    for (auto it = s_entries.begin(); it != s_entries.end();)
    {
        Entry& e = it->second;
        Player* player = ObjectAccessor::FindConnectedPlayer(e.Player);
        e.AgeSec += dtSec;
        bool const online = player && player->IsInWorld();
        e.GoneSec = online ? 0 : e.GoneSec + dtSec;

        if (online)
            PruneBots(e, player);
        if (online && player->IsBeingTeleported())
        {
            ++it;
            continue;
        }

        EntryFacts f;
        f.Searching = e.Searching;
        f.PlayerOnline = online;
        f.PlayerMayLead = !online || !LeadRefusal(player);
        f.AgeSec = e.AgeSec;
        f.GoneSec = e.GoneSec;
        if (online && e.Searching && f.PlayerMayLead)
        {
            Fill(e, player);
            f.Missing = e.Missing;
        }
        else
            f.Missing = e.Missing;

        Outcome const o = Evaluate(f, s_opt.Plan);
        if (o.What == Verdict::Release)
        {
            if (online)
                Say(player, "your search ended; the bots left your group.");
            ReleaseAll(e, o.Why);
            it = s_entries.erase(it);
            continue;
        }
        if (o.What == Verdict::Complete)
        {
            e.Searching = false;
            Log(e.Player.GetCounter(), "LFG_COMPLETE", StringFormat("group of {} filled after {}s with {} bots", e.Bots.size() + 1, e.AgeSec, e.Bots.size()));
            Say(player, "your group is full.");
        }
        else if (o.What == Verdict::Expire)
        {
            e.Searching = false;
            Log(e.Player.GetCounter(), "LFG_TIMEOUT", StringFormat("search ended after {}s, {} bots found, missing {}", e.AgeSec, e.Bots.size(), e.Missing));
            Say(player, StringFormat("no more bots found; you have {} bot{}.", e.Bots.size(), e.Bots.size() == 1 ? "" : "s"));
        }
        // a finished search with no bots left is nothing to keep
        if (!e.Searching && e.Bots.empty())
        {
            it = s_entries.erase(it);
            continue;
        }
        ++it;
    }
}

void Join(Player* player, Request const& req)
{
    if (char const* why = Refusal(player))
    {
        Say(player, why);
        return;
    }
    if (req.BadArgs)
    {
        Say(player, "use: lfg, lfg tank, lfg healer, lfg dps, lfg status or lfg off.");
        return;
    }
    Role role = BotDungeon::PreferredRole(player->GetClass());
    if (req.HasRole)
    {
        if (!(BotDungeon::RolesOfClass(player->GetClass()) & BotDungeon::RoleBit(req.AsRole)))
        {
            Say(player, StringFormat("your class cannot be a {}.", RoleText(req.AsRole)));
            return;
        }
        role = req.AsRole;
    }
    uint64 const key = player->GetGUID().GetCounter();
    if (!s_entries.contains(key))
    {
        uint32 searching = 0;
        for (auto const& kv : s_entries)
            searching += kv.second.Searching ? 1 : 0;
        if (searching >= s_opt.MaxQueued)
        {
            Say(player, "too many searches right now, try again in a minute.");
            return;
        }
    }
    Entry& e = s_entries[key];
    e.Player = player->GetGUID();
    e.AsRole = role;
    e.Searching = true;
    e.AgeSec = 0;
    e.GoneSec = 0;
    e.LastMissingKey = 0xFFFFFFFFu;
    Log(key, "LFG_QUEUED", StringFormat("{} asks for bots as {} (level {})", player->GetName(), RoleText(role), player->GetLevel()));
    Say(player, StringFormat("looking for bots; you are the {}.", RoleText(role)));
    Fill(e, player);   // the first bots come at once, not at the next pass
    if (!e.Missing)
    {
        e.Searching = false;
        Say(player, "your group is full.");
        if (e.Bots.empty())
            s_entries.erase(key);
    }
}

void Leave(Player* player)
{
    auto it = s_entries.find(player->GetGUID().GetCounter());
    if (it == s_entries.end())
    {
        Say(player, "you have no search.");
        return;
    }
    size_t const n = it->second.Bots.size();
    ReleaseAll(it->second, "cancelled");
    s_entries.erase(it);
    Say(player, StringFormat("cancelled; {} bot{} left your group.", n, n == 1 ? "" : "s"));
}

void Status(Player* player)
{
    auto it = s_entries.find(player->GetGUID().GetCounter());
    if (it == s_entries.end())
    {
        Say(player, "you have no search. Whisper \"lfg\" to a bot to start one.");
        return;
    }
    Entry const& e = it->second;
    std::string text = StringFormat("{} bot{} with you", e.Bots.size(), e.Bots.size() == 1 ? "" : "s");
    if (e.Searching)
        text += StringFormat(", still looking ({}s of {}s used).", e.AgeSec, s_opt.Plan.TimeoutSec);
    else
        text += ".";
    Say(player, text);
}

void Handle(Player* player, std::string_view text)
{
    ReadOpts();
    if (!s_opt.Plan.Enabled || !player || IsBot(player))
        return;
    Request const req = ParseRequest(text);
    switch (req.What)
    {
        case Action::Join:   Join(player, req); break;
        case Action::Leave:  Leave(player); break;
        case Action::Status: Status(player); break;
        default: break;
    }
}
}

void OnWhisper(Player* player, Player* receiver, std::string_view text)
{
    if (!IsBot(receiver) || receiver == player)
        return;
    Handle(player, text);
}

void OnPartyChat(Player* player, std::string_view text)
{
    Handle(player, text);
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
    ReadOpts();
    if (!s_opt.Plan.Enabled || s_entries.empty())
        return;
    if ((s_accMs += diff) < s_opt.IntervalMs)
        return;
    uint32 const dtSec = std::max<uint32>(1, s_accMs / 1000);
    s_accMs = 0;
    Step(dtSec);
}
}
