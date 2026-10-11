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

#include "BotBattleground.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotDungeonRun.h"
#include "BotMgr.h"
#include "BotParty.h"
#include "BotPet.h"
#include "BotTravel.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>

using Trinity::StringFormat;

namespace BotBattleground
{
Config const& Cfg()
{
    static Config cfg;
    static std::once_flag once;
    std::call_once(once, []()
    {
        auto opt = [](char const* k, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(k, def), lo, hi); };
        cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Battleground.Enabled", false);
        cfg.QueueIntervalMs = uint32(opt("Bot.AI.Battleground.QueueIntervalSec", 10, 2, 120)) * 1000;
        cfg.FillTo = uint32(opt("Bot.AI.Battleground.FillTo", 0, 0, 40));
        cfg.MaxBotsPerQueue = uint32(opt("Bot.AI.Battleground.MaxBotsPerQueue", 20, 0, 80));
        cfg.JoinPerStepMax = uint32(opt("Bot.AI.Battleground.JoinPerStep", 3, 1, 20));
        cfg.JoinDelaySec = uint32(opt("Bot.AI.Battleground.JoinDelaySec", 8, 0, 300));
        cfg.IdleDropSec = uint32(opt("Bot.AI.Battleground.IdleDropSec", 120, 10, 3600));
        cfg.MinLevel = uint32(opt("Bot.AI.Battleground.MinLevel", 10, 1, 80));
        cfg.AcceptMinSec = uint32(opt("Bot.AI.Battleground.AcceptMinSec", 3, 0, 60));
        cfg.AcceptMaxSec = uint32(opt("Bot.AI.Battleground.AcceptMaxSec", 15, int32(cfg.AcceptMinSec), 70));
        cfg.LeaveMinSec = uint32(opt("Bot.AI.Battleground.LeaveMinSec", 6, 0, 300));
        cfg.LeaveMaxSec = uint32(opt("Bot.AI.Battleground.LeaveMaxSec", 25, int32(cfg.LeaveMinSec), 600));
        cfg.EmptyLeaveSec = uint32(opt("Bot.AI.Battleground.EmptyLeaveSec", 90, 0, 3600));
        cfg.StepMs = uint32(opt("Bot.AI.Battleground.StepMs", 1000, 250, 5000));
        cfg.OffensePct = uint32(opt("Bot.AI.Battleground.OffensePct", 40, 0, 100));
        cfg.DefensePct = uint32(opt("Bot.AI.Battleground.DefensePct", 25, 0, int32(100 - cfg.OffensePct)));
        cfg.EngageYards = float(opt("Bot.AI.Battleground.EngageYards", 28, 5, 60));
        cfg.DefendYards = float(opt("Bot.AI.Battleground.DefendYards", 15, 6, 40));
        cfg.EscortYards = float(opt("Bot.AI.Battleground.EscortYards", 6, 2, 20));
        cfg.ReturnYards = float(opt("Bot.AI.Battleground.ReturnYards", 60, 10, 150));
    });
    return cfg;
}

namespace
{
    // Warsong Gulch (the only battleground in this version). The flag objects are looked up by entry (WarsongGulch script); the
    // positions are the flag stands, used when the object is not loaded yet.
    constexpr uint32 ENTRY_FLAG[TEAMS] = { 227741, 227740 };   // Alliance, Horde flag in its base
    struct Pos { float X, Y, Z; };
    constexpr Pos FLAG_POS[TEAMS] = { { 1540.423f, 1481.325f, 351.8284f }, { 916.0226f, 1434.405f, 345.413f } };
    constexpr float USE_YARDS = 4.5f;

    struct BotState
    {
        uint32 InviteSeenMs = 0, AcceptAtMs = 0;   // invitation noticed / accept moment (0 = none)
        uint32 LeaveAtMs = 0, EmptySinceMs = 0;
        uint32 NextStepMs = 0, UseAtMs = 0, HealerTryMs = 0;
        Order Last = Order::Hold;
        bool Entered = false, HealerSet = false;
    };

    std::unordered_map<uint64, BotState> s_bot;
    uint32 s_nowMs = 0, s_queueAccMs = 0, s_botAccMs = 0;
    uint32 s_noHumanSinceMs[MAX_BATTLEGROUND_BRACKETS] = { };   // now + 1 when the queue of the bracket last had no waiting player, 0 = it has one
    bool s_joinFailLogged = false;

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

    bool IsBot(Player* p) { return AiOf(p) != nullptr; }

    uint8 TeamIdx(Team t) { return t == ALLIANCE ? 0 : 1; }

    bool OtherBusy(BotAI* ai)
    {
        return BotDungeonRun::Busy(ai) || BotTravel::Busy(ai) || BotPet::Busy(ai) || BotParty::Busy(ai);
    }

    BattlegroundQueueTypeId QueueId()
    {
        return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_WS, BattlegroundQueueIdType::Battleground, false, 0);
    }

    // CMSG_BATTLEFIELD_PORT for the queue slot: accept the invitation or leave the queue.
    void Port(Player* bot, uint32 slot, bool accept)
    {
        WorldPackets::Battleground::BattlefieldPort packet{ WorldPacket(CMSG_BATTLEFIELD_PORT) };
        packet.Ticket.Id = slot;
        packet.AcceptedInvite = accept;
        bot->GetSession()->HandleBattleFieldPortOpcode(packet);
    }

    void LeaveQueueSlots(Player* bot)
    {
        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
            if (bot->GetBattlegroundQueueTypeId(slot) != BATTLEGROUND_QUEUE_NONE && !bot->IsInvitedForBattlegroundQueueType(bot->GetBattlegroundQueueTypeId(slot)))
                Port(bot, slot, false);
    }

    bool Eligible(Player* p)
    {
        Config const& cfg = Cfg();
        if (!p || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || p->IsInFlight() || p->GetGroup() || p->IsBeingTeleported())
            return false;
        BotAI* ai = AiOf(p);
        if (!ai || p->GetSession()->IsAltBot() || sBotMgr->IsActiveAlt(p->GetGUID().GetCounter()))
            return false;
        if (p->GetLevel() < cfg.MinLevel || p->InBattleground() || p->InBattlegroundQueue() || p->IsDeserter())
            return false;
        Map const* m = p->GetMap();
        if (!m || m->Instanceable())
            return false;
        return !OtherBusy(ai);
    }

    // ---- queues -----------------------------------------------------------------------------------------------------------------
    void UpdateQueues()
    {
        Config const& cfg = Cfg();
        BattlegroundTemplate const* tmpl = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BATTLEGROUND_WS);
        if (!tmpl || tmpl->MapIDs.empty())
            return;
        BattlegroundQueueTypeId const qid = QueueId();
        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(qid);
        uint32 const nowGame = GameTime::GetGameTimeMS();

        std::vector<Player*> bots;   // filled on first need
        bool botsLoaded = false;

        for (uint32 b = 0; b < MAX_BATTLEGROUND_BRACKETS; ++b)
        {
            QueueFacts f;
            f.MinPerTeam = tmpl->GetMinPlayersPerTeam();
            f.MaxPerTeam = tmpl->GetMaxPlayersPerTeam();
            std::vector<Player*> waitingBots;
            bool any = false;
            for (uint32 g = 0; g < BG_QUEUE_GROUP_TYPES_COUNT; ++g)
                for (GroupQueueInfo* gi : queue.m_QueuedGroups[b][g])
                {
                    uint8 const t = TeamIdx(gi->Team);
                    for (auto const& [guid, info] : gi->Players)
                    {
                        Player* p = ObjectAccessor::FindPlayer(guid);
                        if (!p)
                            continue;
                        any = true;
                        if (IsBot(p))
                        {
                            ++f.BotsQueued[t];
                            if (!gi->IsInvitedToBGInstanceGUID)
                                waitingBots.push_back(p);
                        }
                        else if (!gi->IsInvitedToBGInstanceGUID)
                        {
                            ++f.HumansWaiting[t];
                            f.LongestHumanWaitSec = std::max(f.LongestHumanWaitSec, (nowGame - gi->JoinTime) / 1000);
                        }
                    }
                }

            if (f.HumansWaiting[0] + f.HumansWaiting[1])
                s_noHumanSinceMs[b] = 0;
            else if (!s_noHumanSinceMs[b])
                s_noHumanSinceMs[b] = s_nowMs + 1;
            f.NoHumanSec = s_noHumanSinceMs[b] ? (s_nowMs + 1 - s_noHumanSinceMs[b]) / 1000 : 0;
            if (!any)
                continue;

            QueueNeed const need = PlanQueue(f, cfg);
            if (need.DropBots)
            {
                for (Player* p : waitingBots)
                {
                    Log(p->GetGUID().GetCounter(), "BG_QUEUE_DROP", "leaves the Warsong Gulch queue: no player is waiting any more");
                    LeaveQueueSlots(p);
                }
                continue;
            }
            if (!need.Add[0] && !need.Add[1])
                continue;

            if (!botsLoaded)
            {
                bots = sBotMgr->GetOnlineBotPlayers();
                botsLoaded = true;
            }
            std::vector<Candidate> cands;
            std::unordered_map<uint64, Player*> byGuid;
            for (Player* p : bots)
            {
                if (!Eligible(p))
                    continue;
                PVPDifficultyEntry const* br = DB2Manager::GetBattlegroundBracketByLevel(tmpl->MapIDs.front(), p->GetLevel());
                if (!br || uint32(br->GetBracketId()) != b)
                    continue;
                Candidate c;
                c.Guid = p->GetGUID().GetCounter();
                c.Team = TeamIdx(p->GetTeam());
                c.Level = uint8(p->GetLevel());
                cands.push_back(c);
                byGuid[c.Guid] = p;
            }
            for (uint64 guid : PickBots(cands, need.Add, uint64(s_nowMs / 1000)))
            {
                Player* p = byGuid[guid];
                WorldPackets::Battleground::BattlemasterJoin packet{ WorldPacket(CMSG_BATTLEMASTER_JOIN) };
                packet.QueueIDs.push_back(qid.GetPacked());
                p->GetSession()->HandleBattlemasterJoinOpcode(packet);
                if (p->InBattlegroundQueueForBattlegroundQueueType(qid))
                    Log(guid, "BG_QUEUE_JOIN", StringFormat("queues for Warsong Gulch next to a waiting player (bracket {}, level {})", b, uint32(p->GetLevel())));
                else if (!s_joinFailLogged)
                {
                    s_joinFailLogged = true;
                    Log(guid, "BG_QUEUE_FAILED", "the Warsong Gulch queue refused the bot (battleground disabled, missing permission or deserter?)", BOTLOG_WARN);
                }
            }
        }
    }

    // A bot with an invitation accepts after its delay when it is free, else declines.
    void AnswerInvites(Player* bot, BotState& st)
    {
        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
        {
            BattlegroundQueueTypeId const q = bot->GetBattlegroundQueueTypeId(slot);
            if (q == BATTLEGROUND_QUEUE_NONE || !bot->IsInvitedForBattlegroundQueueType(q))
                continue;
            uint64 const guid = bot->GetGUID().GetCounter();
            if (!st.InviteSeenMs)
            {
                st.InviteSeenMs = s_nowMs + 1;
                st.AcceptAtMs = s_nowMs + AcceptDelayMs(Cfg(), guid);
            }
            if (int32(s_nowMs - st.AcceptAtMs) < 0)
                return;
            st.InviteSeenMs = 0;
            bool const free = !bot->GetGroup() && !bot->IsInCombat() && !OtherBusy(AiOf(bot)) && !bot->IsDeserter();
            Log(guid, free ? "BG_ACCEPT" : "BG_DECLINE", free ? "accepts the battleground invitation" : "declines the battleground invitation: busy", free ? BOTLOG_INFO : BOTLOG_WARN);
            Port(bot, slot, free);
            return;
        }
        st.InviteSeenMs = 0;
    }

    // ---- inside the battleground --------------------------------------------------------------------------------------------------
    GameObject* FlagObject(Player* bot, uint8 team)
    {
        return bot->FindNearestGameObject(ENTRY_FLAG[team], 3000.0f, false);
    }

    Flag FlagOf(GameObject* go)
    {
        return go ? Flag(uint8(go->GetFlagState())) : Flag::None;
    }

    // The flag of `team` lying on the ground, if any (the drop object's owner is the flag object).
    GameObject* DroppedFlag(Player* bot, uint8 team)
    {
        std::list<GameObject*> list;
        FindGameObjectOptions options;
        options.GameObjectType = GAMEOBJECT_TYPE_NEW_FLAG_DROP;
        bot->GetGameObjectListWithOptionsInGrid(list, 3000.0f, options);
        for (GameObject* go : list)
            if (GameObject* owner = bot->GetMap()->GetGameObject(go->GetOwnerGUID()))
                if (owner->GetEntry() == ENTRY_FLAG[team])
                    return go;
        return nullptr;
    }

    void LeaveBattleground(Player* bot, BotState& st, char const* why)
    {
        Log(bot->GetGUID().GetCounter(), "BG_LEAVE", StringFormat("leaves the battleground ({})", why));
        st = BotState();
        WorldPackets::Battleground::BattlefieldLeave packet{ WorldPacket(CMSG_BATTLEFIELD_LEAVE) };
        bot->GetSession()->HandleBattlefieldLeaveOpcode(packet);
    }

    void SetGoalIfNew(BotAI* ai, Player* bot, float x, float y, float z, float arrive)
    {
        BotMotion& m = ai->Motion();
        if (m.HasGoal() && !std::strcmp(m.GetTag(), "bg") && std::hypot(m.GoalX() - x, m.GoalY() - y) < 4.0f)
            return;
        m.SetFollow(ObjectGuid::Empty);
        m.SetGoal(bot->GetMapId(), x, y, z, arrive, "bg");
    }

    void Stop(BotAI* ai)
    {
        BotMotion& m = ai->Motion();
        m.SetFollow(ObjectGuid::Empty);
        if (m.HasGoal() && !std::strcmp(m.GetTag(), "bg"))
            m.ClearGoal();
    }

    // Spirit healers of a battleground raise the ghosts waiting at them in waves; a ghost only has to register with one.
    void WaitForHealer(Player* bot, BotState& st)
    {
        if (st.HealerSet || int32(s_nowMs - st.HealerTryMs) < 0)
            return;
        st.HealerTryMs = s_nowMs + 3000;
        std::list<Creature*> list;
        FindCreatureOptions options;
        options.IgnorePhases = true;
        bot->GetCreatureListWithOptionsInGrid(list, MAX_AREA_SPIRIT_HEALER_RANGE, options);
        for (Creature* c : list)
            if (c->IsAreaSpiritHealer())
            {
                bot->SetAreaSpiritHealer(c);
                st.HealerSet = true;
                Log(bot->GetGUID().GetCounter(), "BG_SPIRIT_WAIT", "waits for the battleground spirit healer");
                return;
            }
    }

    void PlayStep(Player* bot, BotAI* ai, BotState& st, Battleground* bg)
    {
        Config const& cfg = Cfg();
        uint64 const key = bot->GetGUID().GetCounter();
        BattlegroundStatus const status = bg->GetStatus();

        if (!st.Entered)
        {
            st.Entered = true;
            Log(key, "BG_ENTER", StringFormat("is in the battleground (team {})", TeamIdx(bg->GetPlayerTeam(bot->GetGUID())) ? "Horde" : "Alliance"));
        }

        if (status == STATUS_WAIT_LEAVE)
        {
            if (!st.LeaveAtMs)
                st.LeaveAtMs = s_nowMs + 1 + LeaveDelayMs(cfg, key);
            else if (int32(s_nowMs - st.LeaveAtMs) >= 0)
                LeaveBattleground(bot, st, "the match is over");
            return;
        }

        // roster of the battleground: players on the other side, bots of the own side (for the roles), any player at all
        uint8 const mine = TeamIdx(bg->GetPlayerTeam(bot->GetGUID()));
        std::vector<uint64> ownBots;
        std::vector<Player*> foes;
        uint32 teammates = 0;
        bool human = false;
        for (auto const& [guid, info] : bg->GetPlayers())
        {
            Player* p = ObjectAccessor::FindPlayer(guid);
            if (!p)
                continue;
            if (!IsBot(p))
                human = true;
            if (TeamIdx(info.Team) == mine)
            {
                ++teammates;
                if (IsBot(p))
                    ownBots.push_back(guid.GetCounter());
            }
            else if (p->IsInWorld() && p->IsAlive() && p->GetMap() == bot->GetMap())
                foes.push_back(p);
        }

        if (!human && cfg.EmptyLeaveSec)
        {
            if (!st.EmptySinceMs)
                st.EmptySinceMs = s_nowMs + 1;
            else if (s_nowMs + 1 - st.EmptySinceMs >= cfg.EmptyLeaveSec * 1000)
            {
                LeaveBattleground(bot, st, "no player is left in it");
                return;
            }
        }
        else
            st.EmptySinceMs = 0;

        if (!bot->IsAlive())
        {
            Stop(ai);
            if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
                WaitForHealer(bot, st);
            return;
        }
        st.HealerSet = false;

        if (bot->IsInCombat() || bot->IsBeingTeleported() || bot->IsNonMeleeSpellCast(false, false, true))
            return;   // the Combat engine fights; the next step decides again

        std::sort(ownBots.begin(), ownBots.end());
        uint32 const rank = uint32(std::find(ownBots.begin(), ownBots.end(), key) - ownBots.begin());

        uint8 const enemy = mine ^ 1;
        GameObject* ownGo = FlagObject(bot, mine);
        GameObject* enemyGo = FlagObject(bot, enemy);
        GameObject* ownDrop = DroppedFlag(bot, mine);
        GameObject* enemyDrop = DroppedFlag(bot, enemy);
        auto posOf = [bot](GameObject* go, uint8 team) -> Pos { return go ? Pos{ go->GetPositionX(), go->GetPositionY(), go->GetPositionZ() } : FLAG_POS[team]; };
        Pos const ownPos = posOf(ownGo, mine), enemyPos = posOf(enemyGo, enemy);

        Facts f;
        f.InProgress = status == STATUS_IN_PROGRESS;
        f.Alive = true;
        f.MyRole = RoleFor(rank, uint32(ownBots.size()), cfg);
        f.OwnFlag = FlagOf(ownGo);
        f.EnemyFlag = FlagOf(enemyGo);
        f.Teammates = teammates;
        f.DistOwnRoom = bot->GetExactDist2d(ownPos.X, ownPos.Y);
        if (ownDrop)
            f.DistOwnDropped = bot->GetExactDist2d(ownDrop);
        if (enemyDrop)
            f.DistEnemyDropped = bot->GetExactDist2d(enemyDrop);
        Player* teamCarrier = nullptr;
        if (enemyGo && !enemyGo->GetFlagCarrierGUID().IsEmpty())
        {
            if (enemyGo->GetFlagCarrierGUID() == bot->GetGUID())
                f.IAmCarrier = true;
            else if ((teamCarrier = ObjectAccessor::GetPlayer(*bot, enemyGo->GetFlagCarrierGUID())))
            {
                f.TeamCarrier = true;
                f.DistCarrier = bot->GetExactDist2d(teamCarrier);
            }
        }
        Player* foeCarrier = nullptr;
        if (ownGo && f.OwnFlag == Flag::Taken)
            if ((foeCarrier = ObjectAccessor::GetPlayer(*bot, ownGo->GetFlagCarrierGUID())) && foeCarrier->IsAlive() && foeCarrier->GetMap() == bot->GetMap())
                f.EnemyCarrierSeen = true;
            else
                foeCarrier = nullptr;

        Order const order = PlanStep(f, cfg);
        if (order != st.Last)
        {
            Log(key, "BG_ORDER", StringFormat("{} (role {})", OrderName(order), f.MyRole == Role::Offense ? "offense" : f.MyRole == Role::Defense ? "defense" : "roam"), BOTLOG_TRACE);
            st.Last = order;
        }

        // an enemy to fight: the nearest, or the flag carrier we hunt
        Player* target = nullptr;
        float targetYd = -1.0f;
        if (order == Order::HuntCarrier && foeCarrier)
        {
            target = foeCarrier;
            targetYd = bot->GetExactDist2d(foeCarrier);
        }
        else
            for (Player* p : foes)
            {
                float const d = bot->GetExactDist2d(p);
                if ((targetYd < 0.0f || d < targetYd) && bot->IsValidAttackTarget(p) && bot->CanSeeOrDetect(p))
                {
                    target = p;
                    targetYd = d;
                }
            }
        if (target && ShouldEngage(order, targetYd, cfg))
        {
            Stop(ai);
            if (targetYd > 4.0f)   // the Combat engine walks the rest; attacking puts the bot into it
                BotMotion::Halt(bot);
            bot->Attack(target, true);
            return;
        }

        auto reached = [bot](Pos const& p) { return bot->GetExactDist2d(p.X, p.Y) <= USE_YARDS; };
        switch (order)
        {
            case Order::Hold:
                Stop(ai);
                break;
            case Order::GetEnemyFlag:
            case Order::PickUpFlag:
            {
                // the flag on the ground when the carrier died, else the one on its stand
                if (enemyDrop && bot->GetExactDist2d(enemyDrop) <= USE_YARDS)
                {
                    if (int32(s_nowMs - st.UseAtMs) >= 0)
                    {
                        st.UseAtMs = s_nowMs + 1500;
                        enemyDrop->Use(bot);
                        Log(key, "BG_FLAG_USE", "takes the enemy flag from the ground");
                    }
                }
                else if (!enemyDrop && enemyGo && f.EnemyFlag == Flag::InBase && reached(enemyPos))
                {
                    if (int32(s_nowMs - st.UseAtMs) >= 0)
                    {
                        st.UseAtMs = s_nowMs + 1500;
                        enemyGo->Use(bot);
                        Log(key, "BG_FLAG_USE", "takes the enemy flag from its stand");
                    }
                }
                else if (enemyDrop)
                    SetGoalIfNew(ai, bot, enemyDrop->GetPositionX(), enemyDrop->GetPositionY(), enemyDrop->GetPositionZ(), 2.5f);
                else
                    SetGoalIfNew(ai, bot, enemyPos.X, enemyPos.Y, enemyPos.Z, 3.0f);
                break;
            }
            case Order::CarryHome:
                SetGoalIfNew(ai, bot, ownPos.X, ownPos.Y, ownPos.Z, 2.5f);
                break;
            case Order::WaitHome:
                if (f.DistOwnRoom > 10.0f)
                    SetGoalIfNew(ai, bot, ownPos.X, ownPos.Y, ownPos.Z, 8.0f);
                else
                    Stop(ai);
                break;
            case Order::ReturnOwnFlag:
                if (ownDrop && bot->GetExactDist2d(ownDrop) <= USE_YARDS)
                {
                    if (int32(s_nowMs - st.UseAtMs) >= 0)
                    {
                        st.UseAtMs = s_nowMs + 1500;
                        ownDrop->Use(bot);
                        Log(key, "BG_FLAG_RETURN", "touches its own flag to send it back");
                    }
                }
                else if (ownDrop)
                    SetGoalIfNew(ai, bot, ownDrop->GetPositionX(), ownDrop->GetPositionY(), ownDrop->GetPositionZ(), 2.5f);
                break;
            case Order::HuntCarrier:
                if (foeCarrier)
                    SetGoalIfNew(ai, bot, foeCarrier->GetPositionX(), foeCarrier->GetPositionY(), foeCarrier->GetPositionZ(), 4.0f);
                break;
            case Order::Escort:
                if (teamCarrier && f.DistCarrier > cfg.EscortYards)
                {
                    if (ai->Motion().GetFollow() != teamCarrier->GetGUID())
                    {
                        ai->Motion().ClearGoal();
                        ai->Motion().SetFollow(teamCarrier->GetGUID());
                    }
                }
                else
                    Stop(ai);
                break;
            case Order::Defend:
            {
                Point const p = RingPoint(ownPos.X, ownPos.Y, 4.0f, cfg.DefendYards, key, s_nowMs);
                if (std::hypot(bot->GetPositionX() - p.X, bot->GetPositionY() - p.Y) > 6.0f)
                    SetGoalIfNew(ai, bot, p.X, p.Y, ownPos.Z, 3.0f);
                else
                    Stop(ai);
                break;
            }
            case Order::Roam:
            {
                Point const p = RingPoint((ownPos.X + enemyPos.X) * 0.5f, (ownPos.Y + enemyPos.Y) * 0.5f, 3.0f, 30.0f, key, s_nowMs);
                if (std::hypot(bot->GetPositionX() - p.X, bot->GetPositionY() - p.Y) > 8.0f)
                    SetGoalIfNew(ai, bot, p.X, p.Y, (ownPos.Z + enemyPos.Z) * 0.5f, 4.0f);
                else
                    Stop(ai);
                break;
            }
        }
    }
}

void Update(uint32 diff)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled)
        return;
    s_nowMs += diff;
    s_queueAccMs += diff;
    s_botAccMs += diff;

    if (s_queueAccMs >= cfg.QueueIntervalMs)
    {
        s_queueAccMs = 0;
        UpdateQueues();
    }

    if (s_botAccMs < 500)
        return;
    s_botAccMs = 0;

    std::unordered_map<uint64, BotState> keep;
    for (Player* bot : sBotMgr->GetOnlineBotPlayers())
    {
        if (!bot || !bot->IsInWorld())
            continue;
        BotAI* ai = AiOf(bot);
        uint64 const guid = bot->GetGUID().GetCounter();
        if (bot->InBattleground())
        {
            Battleground* bg = bot->GetBattleground();
            if (!ai || !bg || bg->GetTypeID() != BATTLEGROUND_WS)
                continue;
            BotState& st = s_bot[guid];
            if (int32(s_nowMs - st.NextStepMs) >= 0)
            {
                st.NextStepMs = s_nowMs + cfg.StepMs;
                PlayStep(bot, ai, st, bg);
            }
            keep[guid] = s_bot[guid];
        }
        else if (bot->InBattlegroundQueue(true))
        {
            BotState& st = s_bot[guid];
            AnswerInvites(bot, st);
            keep[guid] = st;
        }
    }
    s_bot.swap(keep);
}

bool Busy(BotAI* ai)
{
    Player* bot = ai ? ai->GetTickBot() : nullptr;
    return bot && Cfg().Enabled && bot->InBattleground();
}
}
