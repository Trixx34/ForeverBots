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

#include "BotPlayerLed.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotDungeon.h"
#include "BotMgr.h"
#include "BotPlayerLedPlan.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using Trinity::StringFormat;

namespace BotPlayerLed
{
namespace
{
    struct Opts
    {
        bool Enabled = false;          // Bot.AI.Dungeon.PlayerLed.Enabled
        uint32 IntervalMs = 2000;      // Bot.AI.Dungeon.PlayerLed.IntervalSec
        bool Notify = true;            // Bot.AI.Dungeon.PlayerLed.Notify: tell the leader which bots rest
        Config Plan;
    };

    struct LeaderTrack
    {
        uint32 MapId = 0xFFFFFFFFu, InstanceId = 0;
        uint32 SinceMs = 0;            // on this map instance since
        bool Needy = false;
        uint32 NoticeMs = 0;
        uint32 Seen = 0;
    };

    struct BotTrack
    {
        uint32 FarSinceMs = 0;         // 0 = not far
        uint32 DeadSinceMs = 0;        // 0 = alive
        uint32 ActMs = 0;              // 0 = never
        uint32 Seen = 0;
    };

    Opts s_opt;
    bool s_optRead = false;
    uint32 s_accMs = 0, s_nowMs = 1, s_pass = 0;
    std::unordered_map<uint64, LeaderTrack> s_leaders;
    std::unordered_map<uint64, BotTrack> s_bots;
    std::mutex s_busyLock;
    std::unordered_set<uint64> s_busy;

    void ReadOpts()
    {
        if (s_optRead)
            return;
        s_optRead = true;
        auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(k, def), lo, hi); };
        s_opt.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Dungeon.PlayerLed.Enabled", false);
        s_opt.IntervalMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.IntervalSec", 2, 1, 30)) * 1000;
        s_opt.Notify = sConfigMgr->GetBoolDefault("Bot.AI.Dungeon.PlayerLed.Notify", true);
        s_opt.Plan.EnterDelayMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.EnterDelaySec", 3, 0, 60)) * 1000;
        s_opt.Plan.CatchUpYards = float(opt("Bot.AI.Dungeon.PlayerLed.CatchUpYards", 60, 20, 300));
        s_opt.Plan.CatchUpMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.CatchUpSec", 20, 5, 600)) * 1000;
        s_opt.Plan.RaiseDelayMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.RaiseDelaySec", 10, 0, 600)) * 1000;
        s_opt.Plan.ActCooldownMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.ActCooldownSec", 15, 1, 600)) * 1000;
        s_opt.Plan.NoticeRepeatMs = uint32(opt("Bot.AI.Dungeon.PlayerLed.NoticeRepeatSec", 60, 10, 3600)) * 1000;
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

    // Bots of the player's group, by leader. Only groups led by a connected player (not a bot) with at least one bot member.
    struct LedGroup
    {
        Player* Leader = nullptr;
        std::vector<Player*> Bots;
    };

    std::vector<LedGroup> CollectGroups()
    {
        std::unordered_map<uint64, size_t> index;
        std::vector<LedGroup> out;
        for (Player* bot : sBotMgr->GetOnlineBotPlayers())
        {
            Group* g = bot->GetGroup();
            if (!g || !bot->IsInWorld())
                continue;
            Player* leader = ObjectAccessor::FindConnectedPlayer(g->GetLeaderGUID());
            if (!leader || !leader->IsInWorld() || !leader->GetSession() || leader->GetSession()->IsBot() || leader->GetGroup() != g)
                continue;
            auto [it, fresh] = index.try_emplace(leader->GetGUID().GetCounter(), out.size());
            if (fresh)
                out.push_back({ leader, {} });
            out[it->second].Bots.push_back(bot);
        }
        return out;
    }

    bool InFiveMan(Map const* m) { return m && m->IsNonRaidDungeon(); }
    bool OnWorldMap(Map const* m) { return m && !m->Instanceable(); }

    void Apply(Act act, Player* leader, Player* bot, uint32 index, Decision const& d)
    {
        float dx = 0, dy = 0;
        Spread(index, dx, dy);
        uint64 const g = bot->GetGUID().GetCounter();
        Map const* lm = leader->GetMap();
        std::string const where = StringFormat("{} ({:.0f}, {:.0f})", lm ? lm->GetMapName() : "?", leader->GetPositionX(), leader->GetPositionY());
        switch (act)
        {
            case Act::Raise:
                bot->ResurrectPlayer(0.5f);
                bot->SpawnCorpseBones();
                Log(g, "DUNGEON_PL_RAISED", StringFormat("raised next to {} at {}", leader->GetName(), where));
                break;
            case Act::Enter:
                Log(g, "DUNGEON_PL_ENTER", StringFormat("follows {} into {}", leader->GetName(), where));
                break;
            case Act::Leave:
                Log(g, "DUNGEON_PL_LEAVE", StringFormat("follows {} out to {}", leader->GetName(), where));
                break;
            case Act::CatchUp:
                Log(g, "DUNGEON_PL_CATCHUP", StringFormat("{:.0f} yards behind {} for a while, joins at {}", bot->GetExactDist(leader), leader->GetName(), where));
                break;
            default:
                return;
        }
        if (BotAI* ai = AiOf(bot))
            ai->Motion().ClearGoal();
        bot->TeleportTo(leader->GetMapId(), leader->GetPositionX() + dx, leader->GetPositionY() + dy, leader->GetPositionZ() + 0.5f, leader->GetOrientation());
        (void)d;
    }

    void Tell(LedGroup const& lg, LeaderTrack& lt, std::vector<BotDungeon::MemberState> const& states)
    {
        Player* leader = lg.Leader;
        bool const inside = InFiveMan(leader->GetMap());
        std::vector<Need> needs = inside ? ListNeeds(states, BotDungeon::Cfg().Ready) : std::vector<Need>();
        bool const needy = !needs.empty();
        uint32 const since = lt.NoticeMs ? s_nowMs - lt.NoticeMs : 0xFFFFFFFFu;
        BotPlayerLed::Notice n = NoticeDecision(lt.Needy, needy, leader->IsInCombat(), since, s_opt.Plan.NoticeRepeatMs);
        if (n == BotPlayerLed::Notice::None)
        {
            if (!leader->IsInCombat())
                lt.Needy = needy;
            return;
        }
        lt.Needy = needy;
        lt.NoticeMs = s_nowMs;
        std::string text;
        if (n == BotPlayerLed::Notice::Ready)
            text = "Bots: everybody is ready.";
        else
        {
            text = "Bots not ready:";
            for (Need const& need : needs)
            {
                Player* p = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(need.Guid));
                text += StringFormat(" {} ({}{});", p ? p->GetName() : "?", NeedText(need.Why), need.Value ? StringFormat(" {}%", need.Value) : std::string());
            }
        }
        Log(leader->GetGUID().GetCounter(), n == BotPlayerLed::Notice::Ready ? "DUNGEON_PL_READY" : "DUNGEON_PL_REST", text);
        if (s_opt.Notify && leader->GetSession())
            ChatHandler(leader->GetSession()).SendSysMessage(text);
    }

    void Pass()
    {
        ++s_pass;
        std::unordered_set<uint64> busy;
        for (LedGroup& lg : CollectGroups())
        {
            Player* leader = lg.Leader;
            Map const* lm = leader->GetMap();
            if (!lm || leader->IsBeingTeleported())
                continue;
            LeaderTrack& lt = s_leaders[leader->GetGUID().GetCounter()];
            lt.Seen = s_pass;
            if (lt.MapId != leader->GetMapId() || lt.InstanceId != lm->GetInstanceId())
            {
                lt.MapId = leader->GetMapId();
                lt.InstanceId = lm->GetInstanceId();
                lt.SinceMs = s_nowMs;
            }

            std::vector<BotDungeon::MemberState> states;
            uint32 moved = 0;
            for (Player* bot : lg.Bots)
            {
                BotAI* ai = AiOf(bot);
                Map const* bm = bot->GetMap();
                if (!ai || !bm || bm->IsBattlegroundOrArena() || bm->IsRaid())
                    continue;
                bool const follows = ai->Motion().GetFollow() == leader->GetGUID();
                BotTrack& bt = s_bots[bot->GetGUID().GetCounter()];
                bt.Seen = s_pass;

                Facts f;
                f.Follows = follows;
                f.BotAlive = bot->IsAlive();
                f.BotInCombat = bot->IsInCombat();
                f.LeaderAlive = leader->IsAlive();
                f.LeaderInCombat = leader->IsInCombat();
                f.LeaderInDungeon = InFiveMan(lm);
                f.LeaderOnWorldMap = OnWorldMap(lm);
                f.BotInDungeon = InFiveMan(bm);
                f.BotInInstanceOfLeader = bot->GetMapId() == leader->GetMapId() && bm->GetInstanceId() == lm->GetInstanceId();
                f.Distance = f.BotInInstanceOfLeader ? bot->GetExactDist(leader) : 0.0f;
                f.LeaderMapSinceMs = s_nowMs - lt.SinceMs;
                if (f.BotAlive)
                {
                    bt.DeadSinceMs = 0;
                    if (f.BotInInstanceOfLeader && f.Distance > s_opt.Plan.CatchUpYards)
                    {
                        if (!bt.FarSinceMs)
                            bt.FarSinceMs = s_nowMs;
                    }
                    else
                        bt.FarSinceMs = 0;
                }
                else
                {
                    bt.FarSinceMs = 0;
                    if (!bt.DeadSinceMs)
                        bt.DeadSinceMs = s_nowMs;
                }
                f.FarForMs = bt.FarSinceMs ? s_nowMs - bt.FarSinceMs : 0;
                f.DeadForMs = bt.DeadSinceMs ? s_nowMs - bt.DeadSinceMs : 0;
                f.SinceActMs = bt.ActMs ? s_nowMs - bt.ActMs : 0xFFFFFFFFu;

                if (follows && (f.LeaderInDungeon || f.BotInDungeon))
                    busy.insert(bot->GetGUID().GetCounter());

                if (follows)
                {
                    BotDungeon::MemberState st;
                    st.Guid = bot->GetGUID().GetCounter();
                    st.AsRole = BotDungeon::PreferredRole(bot->GetClass());
                    st.Alive = f.BotAlive;
                    st.Present = f.BotInInstanceOfLeader && f.Distance <= 40.0f;
                    st.InCombat = f.BotInCombat;
                    st.HealthPct = int32(bot->GetHealthPct());
                    st.ManaPct = bot->GetMaxPower(POWER_MANA) ? int32(100.0f * bot->GetPower(POWER_MANA) / bot->GetMaxPower(POWER_MANA)) : 100;
                    states.push_back(st);
                }

                if (bot->IsBeingTeleported() || bot->IsInFlight())
                    continue;
                Decision const d = Decide(f, s_opt.Plan);
                if (d.What == Act::None)
                    continue;
                Apply(d.What, leader, bot, moved++, d);
                bt.ActMs = s_nowMs;
                bt.FarSinceMs = 0;
            }
            Tell(lg, lt, states);
        }

        // forget leaders and bots that were not seen this pass
        for (auto it = s_leaders.begin(); it != s_leaders.end();)
            it = it->second.Seen != s_pass ? s_leaders.erase(it) : std::next(it);
        for (auto it = s_bots.begin(); it != s_bots.end();)
            it = it->second.Seen != s_pass ? s_bots.erase(it) : std::next(it);

        std::lock_guard<std::mutex> lock(s_busyLock);
        s_busy.swap(busy);
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
    ReadOpts();
    s_nowMs += diff;
    if (!s_opt.Enabled)
        return;
    if ((s_accMs += diff) < s_opt.IntervalMs)
        return;
    s_accMs = 0;
    Pass();
}
}
