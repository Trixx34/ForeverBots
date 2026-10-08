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


// Travel and leveling progression, see BotTravel.h. Layout:
//   1. config and helpers (zones, flight nodes)
//   2. per-bot state (travel_ctx value)
//   3. travel_tick action: decide when to leave a zone, run the trip step by step, learn flight masters, hearth and bind
//   4. registration of the "travel" strategy

#include "BotTravel.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotMgr.h"
#include "BotMovePlan.h"
#include "BotQuest.h"
#include "BotTravelPlan.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellHistory.h"
#include "StringFormat.h"
#include "TaxiPathGraph.h"
#include "TransportMgr.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_map>

namespace BotTravel
{
namespace
{
using Trinity::StringFormat;

constexpr uint32 SPELL_HEARTHSTONE = 8690;
constexpr uint32 ITEM_HEARTHSTONE = 6948;
constexpr float FM_INTERACT_YARDS = 4.0f;

// ---------------------------------------------------------------------------------------------------------------------
// 1. config and helpers
// ---------------------------------------------------------------------------------------------------------------------
Config s_cfg;
std::once_flag s_cfgOnce;

// area (possibly a sub-zone) to the zone of the table; 0 when it is none of the leveling zones (a city, a dungeon, ...)
uint32 ResolveZone(uint32 area)
{
    for (uint32 guard = 0; area && guard < 4; ++guard)
    {
        if (FindZone(area))
            return area;
        AreaTableEntry const* a = sAreaTableStore.LookupEntry(area);
        if (!a)
            return 0;
        area = a->ParentAreaID;
    }
    return 0;
}

Team TeamOf(Player const* bot) { return bot->GetTeamId() == TEAM_ALLIANCE ? Team::Alliance : Team::Horde; }

bool NodeVisible(TaxiNodesEntry const* n, Player const* bot)
{
    if (!n || n->GetFlags().HasFlag(TaxiNodeFlags::IgnoreForFindNearest))
        return false;
    return n->GetFlags().HasFlag(bot->GetTeamId() == TEAM_ALLIANCE ? TaxiNodeFlags::ShowOnAllianceMap : TaxiNodeFlags::ShowOnHordeMap);
}

// zone of a flight node, filled lazily from the map the bot stands on (the terrain of other continents is not at hand)
std::mutex s_nodeMx;
std::unordered_map<uint32, uint32> s_nodeZone;

uint32 NodeZone(Player* bot, TaxiNodesEntry const* n)
{
    if (!n || n->ContinentID != bot->GetMapId() || !bot->GetMap())
        return 0;
    {
        std::lock_guard<std::mutex> lk(s_nodeMx);
        auto it = s_nodeZone.find(n->ID);
        if (it != s_nodeZone.end())
            return it->second;
    }
    uint32 const zone = ResolveZone(bot->GetMap()->GetZoneId(bot->GetPhaseShift(), n->Pos.X, n->Pos.Y, n->Pos.Z));
    std::lock_guard<std::mutex> lk(s_nodeMx);
    s_nodeZone[n->ID] = zone;
    return zone;
}

float NodeDist(TaxiNodesEntry const* a, TaxiNodesEntry const* b)
{
    float const dx = a->Pos.X - b->Pos.X, dy = a->Pos.Y - b->Pos.Y;
    return std::sqrt(dx * dx + dy * dy);
}

// --- boats and zeppelins ---
// Dock stops of every transport, computed once from the core's transport templates (the pause waypoints of the path), and the walkable
// anchors (flight master positions) near them. The ride itself is simulated: the bot waits at the anchor for the real transport to stand at
// its stop, then, after the ride time of the template, is placed at the anchor of the destination stop.
struct TransportInfo
{
    uint32 Entry = 0;
    uint32 CycleMs = 0;
    std::vector<Stop> Stops;
};
std::vector<TransportInfo> s_transports;
std::vector<Anchor> s_anchors;
std::once_flag s_transportOnce;

void BuildTransportIndex()
{
    for (auto const& [entry, tmpl] : sObjectMgr->GetGameObjectTemplates())
    {
        if (tmpl.type != GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT)
            continue;
        TransportTemplate const* tt = sTransportMgr->GetTransportTemplate(entry);
        if (!tt || !tt->TotalPathTime)
            continue;
        TransportInfo info;
        info.Entry = entry;
        info.CycleMs = tt->TotalPathTime;
        for (TransportPathLeg const& leg : tt->PathLegs)
            for (TransportPathSegment const& seg : leg.Segments)
            {
                if (!seg.Delay)
                    continue;
                Optional<Position> pos = tt->ComputePosition(seg.SegmentEndArrivalTimestamp + 1, nullptr, nullptr);
                if (pos)
                    info.Stops.push_back({ seg.SegmentEndArrivalTimestamp % info.CycleMs, leg.MapId, pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ() });
            }
        std::sort(info.Stops.begin(), info.Stops.end(), [](Stop const& a, Stop const& b) { return a.TimeMs < b.TimeMs; });
        if (info.Stops.size() >= 2)
            s_transports.push_back(std::move(info));
    }
    for (TaxiNodesEntry const* n : sTaxiNodesStore)
        if (n && n->IsPartOfTaxiNetwork())
            s_anchors.push_back({ n->ID, n->ContinentID, n->Pos.X, n->Pos.Y, n->Pos.Z });
}

// Flight edges between zones from the nodes the bot knows on its map. Pairs are checked with the real route search of the core, so
// only flights the bot can take are offered. Cost: pairs of known nodes; called once per decision to leave a zone.
std::vector<Edge> TaxiEdges(Player* bot)
{
    std::vector<TaxiNodesEntry const*> known;
    for (TaxiNodesEntry const* n : sTaxiNodesStore)
        if (NodeVisible(n, bot) && n->ContinentID == bot->GetMapId() && bot->m_taxi.IsTaximaskNodeKnown(n->ID) && NodeZone(bot, n))
            known.push_back(n);
    std::map<std::pair<uint32, uint32>, float> best;
    std::vector<uint32> path;
    for (TaxiNodesEntry const* a : known)
        for (TaxiNodesEntry const* b : known)
        {
            uint32 const za = NodeZone(bot, a), zb = NodeZone(bot, b);
            if (a == b || za == zb)
                continue;
            path.clear();
            if (!TaxiPathGraph::GetCompleteNodeRoute(a, b, bot, path) || path.size() < 2)
                continue;
            float yards = 0.0f;
            for (size_t i = 1; i < path.size(); ++i)
                if (TaxiNodesEntry const* p = sTaxiNodesStore.LookupEntry(path[i - 1]))
                    if (TaxiNodesEntry const* q = sTaxiNodesStore.LookupEntry(path[i]))
                        yards += NodeDist(p, q);
            float const sec = TaxiSeconds(yards);
            auto key = std::make_pair(za, zb);
            auto it = best.find(key);
            if (it == best.end() || sec < it->second)
                best[key] = sec;
        }
    std::vector<Edge> out;
    for (auto const& [k, sec] : best)
        out.push_back({ k.first, k.second, MODE_TAXI, sec, 3 });
    return out;
}

TaxiNodesEntry const* NearestNode(Player* bot, float x, float y, uint32 zone)
{
    TaxiNodesEntry const* r = nullptr;
    float rd = 0.0f;
    for (TaxiNodesEntry const* n : sTaxiNodesStore)
    {
        if (!NodeVisible(n, bot) || n->ContinentID != bot->GetMapId() || (zone && NodeZone(bot, n) != zone))
            continue;
        float const dx = n->Pos.X - x, dy = n->Pos.Y - y;
        float const d = dx * dx + dy * dy;
        if (!r || d < rd)
        {
            r = n;
            rd = d;
        }
    }
    return r;
}

// nearest alive, friendly creature with the npc flag within `range`
Creature* FindNpc(Player* bot, NPCFlags flag, float range)
{
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    std::vector<Creature*> list;
    bot->GetCreatureListWithOptionsInGrid(list, range, options);
    Creature* best = nullptr;
    float bd = 0.0f;
    for (Creature* c : list)
    {
        if (!c->HasNpcFlag(flag) || !bot->IsFriendlyTo(c))
            continue;
        float const d = bot->GetDistance(c);
        if (!best || d < bd)
        {
            best = c;
            bd = d;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------
// 2. per-bot state
// ---------------------------------------------------------------------------------------------------------------------
enum class Kind : uint8 { Zone, Discover };

class TravelCtx : public UntypedValue
{
public:
    explicit TravelCtx(BotAI* ai) : UntypedValue(ai, "travel_ctx", 0) { }

    uint32 NextCheckMs = 0;
    uint32 Attempt = 0;
    uint32 ExhaustedChecks = 0;       // consecutive checks with "nothing to quest for here"
    uint32 TaxiBlockedUntilMs = 0;    // after a flight failed (money, ...): walk only for a while
    uint32 HearthCoolUntilMs = 0;
    uint32 NextDiscoverMs = 0;
    uint32 BanClearMs = 0;
    Banned Avoid;                     // zone pairs whose ride or flight failed lately; cleared every 20 minutes
    std::vector<uint32> Recent;       // zones left or failed lately, oldest first
    std::vector<uint32> Tried;        // flight nodes visited or given up on

    struct Trip
    {
        bool Active = false;
        Kind K = Kind::Zone;
        Choice C;
        size_t Idx = 0;
        uint32 StartMs = 0, StepMs = 0, GoalMs = 0, Tries = 0;
        bool Flying = false;
        bool Hearthing = false;
        uint32 Node = 0;              // discover: node to visit
        uint32 FromZone = 0;
        // boat or zeppelin leg
        uint8 Ride = 0;               // 0 not started, 1 walking to the dock, 2 waiting for the transport, 3 riding
        uint32 RideEntry = 0, RideMs = 0, RideEndMs = 0, RideWaitMs = 0;
        Stop Dock, Landing;           // stops of the transport (dock, destination)
        Anchor DockAnchor, LandAnchor; // walkable places next to them
        bool Rode = false, Tail = false;
    } T;

    void Reset() { T = Trip(); }
    void Remember(uint32 area)
    {
        if (!area)
            return;
        std::erase(Recent, area);
        Recent.push_back(area);
        while (Recent.size() > std::max<uint32>(1, s_cfg.RecentZones))
            Recent.erase(Recent.begin());
    }
};

TravelCtx* Ctx(BotAI* ai) { return static_cast<TravelCtx*>(ai->GetValueRaw("travel_ctx")); }

void Emit(BotAI* ai, Player* bot, char const* reason, std::string summary, std::string details = std::string(), uint8 sev = BOTLOG_INFO)
{
    ai->EmitEvent(bot, "decision", sev, reason, std::move(summary), std::move(details));
}

std::string StepsJson(Route const& r)
{
    std::string out;
    for (Edge const& e : r.Steps)
        out += StringFormat(R"({}{{"mode":"{}","from":{},"to":{},"sec":{:.0f}}})", out.empty() ? "" : ",", ModeName(e.M), e.From, e.To, e.Sec);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// 3. travel_tick
// ---------------------------------------------------------------------------------------------------------------------
class TravelTickAction : public Action
{
public:
    explicit TravelTickAction(BotAI* ai) : Action(ai, "travel_tick", ACTION_FLAG_QUIET_LOG) { }

    bool IsPossible() override
    {
        Player* bot = GetBot();
        return s_cfg.Enabled && bot->IsAlive() && !bot->GetSession()->IsAltBot();
    }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        TravelCtx* cp = Ctx(ai);
        if (!cp)
            return false;
        TravelCtx& c = *cp;
        uint32 const now = ai->GetNowMs();

        if (c.T.Active)
            return Run(ai, bot, c, now);

        if (int32(c.NextCheckMs - now) > 0)
            return false;
        c.NextCheckMs = now + s_cfg.CheckSec * 1000 + uint32(BotMove::Range(BotMove::Mix(bot->GetGUID().GetCounter(), now / 1000), 0.0f, 2000.0f));
        if (!CanTravel(bot))
            return false;
        Decide(ai, bot, c, now);
        return false;
    }

private:
    static bool CanTravel(Player* bot)
    {
        return bot->IsAlive() && !bot->IsInCombat() && !bot->IsInFlight() && !bot->GetGroup() && bot->GetMap() && !bot->GetMap()->Instanceable()
            && !bot->InBattleground() && !bot->IsBeingTeleported();
    }

    uint32 Modes(TravelCtx const& c, uint32 now) const
    {
        uint32 m = MODE_WALK;
        if (s_cfg.Taxi && int32(c.TaxiBlockedUntilMs - now) <= 0)
            m |= MODE_TAXI;
        if (s_cfg.Transport)
            m |= MODE_BOAT | MODE_ZEPPELIN;
        return m;
    }

    // check: does the zone still fit; start a trip, bind at an inn, or learn a flight master
    void Decide(BotAI* ai, Player* bot, TravelCtx& c, uint32 now)
    {
        uint32 const cur = ResolveZone(bot->GetZoneId());
        bool const grinding = BotQuest::IsGrinding(ai);
        c.ExhaustedChecks = grinding ? c.ExhaustedChecks + 1 : 0;

        if (s_cfg.Zones)
        {
            // a bot in a city or a zone outside the table only leaves when it has nothing to quest for
            if (cur || c.ExhaustedChecks >= 2)
            {
                Query q;
                q.Level = bot->GetLevel();
                q.BotTeam = TeamOf(bot);
                q.CurrentArea = cur;
                q.CurrentMap = bot->GetMapId();
                q.LocalExhausted = c.ExhaustedChecks >= 2;
                q.BotKey = bot->GetGUID().GetCounter();
                q.Attempt = c.Attempt;
                q.Modes = Modes(c, now);
                q.Recent = c.Recent;
                if (int32(c.BanClearMs - now) <= 0)
                {
                    c.Avoid.clear();
                    c.BanClearMs = now + 20 * 60 * 1000;
                }
                q.Avoid = c.Avoid;
                q.MaxSteps = s_cfg.MaxRouteSteps;
                q.LeaveMargin = s_cfg.LeaveMarginLevels;
                if (s_cfg.Taxi && (q.Modes & MODE_TAXI))
                    q.Extra = TaxiEdges(bot);
                Choice choice = PickZone(q);
                if (choice.Reason != Why::Stay)
                    ++c.Attempt;
                if (choice.Reason != Why::Stay && choice.Reason != Why::NoCandidate && choice.Area != cur)
                {
                    StartTrip(ai, bot, c, now, cur, std::move(choice), q.Extra);
                    return;
                }
                if (choice.Reason == Why::NoCandidate && c.ExhaustedChecks == 2)
                    Emit(ai, bot, "TRAVEL_NO_CANDIDATE", StringFormat("zone {} is done but no reachable zone fits level {}", cur, bot->GetLevel()),
                        StringFormat(R"({{"zone":{},"level":{}}})", cur, bot->GetLevel()));
                MaybeRebind(ai, bot, c, now, cur, choice.Area ? choice.Area : cur, q.Extra);
            }
        }
        if (s_cfg.Taxi)
            MaybeDiscover(ai, bot, c, now);
    }

    void StartTrip(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, uint32 cur, Choice choice, std::vector<Edge> const& extra)
    {
        c.T = TravelCtx::Trip();
        c.T.Active = true;
        c.T.K = Kind::Zone;
        c.T.FromZone = cur;
        c.T.StartMs = c.T.StepMs = now ? now : 1;
        // unknown start (a city): one walking leg to the target
        if (choice.R.Steps.empty() && choice.Area != cur)
            choice.R.Steps.push_back({ cur, choice.Area, MODE_WALK, choice.RouteSec, 3 });
        c.T.C = std::move(choice);
        c.Remember(cur);
        Emit(ai, bot, "TRAVEL_START", StringFormat("leaving zone {} ({}), going to zone {}", cur, WhyName(c.T.C.Reason), c.T.C.Area),
            StringFormat(R"({{"from":{},"to":{},"why":"{}","level":{},"route_sec":{:.0f},"steps":[{}]}})", cur, c.T.C.Area, WhyName(c.T.C.Reason), bot->GetLevel(),
                c.T.C.RouteSec, StepsJson(c.T.C.R)));

        // hearthstone first when the way from the bind is clearly shorter
        if (s_cfg.Hearth && TryHearth(ai, bot, c, now, cur, extra))
            return;
    }

    bool TryHearth(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, uint32 cur, std::vector<Edge> const& extra)
    {
        if (int32(c.HearthCoolUntilMs - now) > 0 || bot->IsInCombat() || !bot->HasItemCount(ITEM_HEARTHSTONE, 1) || bot->GetSpellHistory()->HasCooldown(SPELL_HEARTHSTONE))
            return false;
        HearthQuery h;
        h.Target = c.T.C.Area;
        h.Current = cur;
        h.Bind = ResolveZone(bot->m_homebindAreaId);
        h.HearthReady = true;
        h.BotTeam = TeamOf(bot);
        h.Modes = Modes(c, now);
        h.Extra = extra;
        float saved = 0.0f;
        if (!h.Bind || bot->m_homebind.GetMapId() != bot->GetMapId() || !ShouldHearth(h, &saved))
            return false;
        Item* stone = bot->GetItemByEntry(ITEM_HEARTHSTONE);
        if (!stone)
            return false;
        BotMotion::Halt(bot);
        SpellCastResult const res = bot->CastSpell(bot, SPELL_HEARTHSTONE, CastSpellExtraArgs(TRIGGERED_NONE).SetCastItem(stone));
        c.HearthCoolUntilMs = now + 600 * 1000;
        if (res != SPELL_CAST_OK)
            return false;
        c.T.Hearthing = true;
        c.T.StepMs = now;
        Emit(ai, bot, "TRAVEL_HEARTH", StringFormat("using the hearthstone, home is zone {} (saves about {:.0f} s)", h.Bind, saved),
            StringFormat(R"({{"bind":{},"from":{},"to":{},"saved_sec":{:.0f}}})", h.Bind, cur, h.Target, saved));
        return true;
    }

    // at an innkeeper: bind when it shortens the way back to where the bot levels next
    void MaybeRebind(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, uint32 cur, uint32 next, std::vector<Edge> const& extra)
    {
        (void)c;
        if (!s_cfg.Hearth || !cur || bot->m_homebind.GetMapId() != bot->GetMapId())
            return;
        Creature* inn = FindNpc(bot, UNIT_NPC_FLAG_INNKEEPER, 20.0f);
        if (!inn || !bot->GetNPCIfCanInteractWith(inn->GetGUID(), UNIT_NPC_FLAG_INNKEEPER, UNIT_NPC_FLAG_2_NONE))
            return;
        RebindQuery r;
        r.InnArea = cur;
        r.Bind = ResolveZone(bot->m_homebindAreaId);
        r.Next = next;
        r.BotTeam = TeamOf(bot);
        r.Modes = Modes(c, now);
        r.Extra = extra;
        if (!ShouldRebind(r))
            return;
        bot->GetSession()->SendBindPoint(inn);
        Emit(ai, bot, "TRAVEL_REBIND", StringFormat("bound the hearthstone at the inn in zone {} (was {})", cur, r.Bind),
            StringFormat(R"({{"zone":{},"old":{},"next":{}}})", cur, r.Bind, next));
    }

    // an unknown flight master close by: walk there once and learn it
    void MaybeDiscover(BotAI* ai, Player* bot, TravelCtx& c, uint32 now)
    {
        if (int32(c.NextDiscoverMs - now) > 0 || bot->GetLevel() < 5)
            return;
        c.NextDiscoverMs = now + 60 * 1000;
        TaxiNodesEntry const* pick = nullptr;
        float pd = float(s_cfg.DiscoverRadius) * float(s_cfg.DiscoverRadius);
        for (TaxiNodesEntry const* n : sTaxiNodesStore)
        {
            if (!NodeVisible(n, bot) || n->ContinentID != bot->GetMapId() || bot->m_taxi.IsTaximaskNodeKnown(n->ID)
                || std::find(c.Tried.begin(), c.Tried.end(), n->ID) != c.Tried.end() || !sObjectMgr->GetNearestTaxiNode(n->Pos.X, n->Pos.Y, n->Pos.Z, n->ContinentID, bot->GetTeam()))
                continue;
            float const dx = n->Pos.X - bot->GetPositionX(), dy = n->Pos.Y - bot->GetPositionY();
            float const d = dx * dx + dy * dy;
            if (d < pd)
            {
                pick = n;
                pd = d;
            }
        }
        if (!pick)
            return;
        c.Tried.push_back(pick->ID);
        c.T = TravelCtx::Trip();
        c.T.Active = true;
        c.T.K = Kind::Discover;
        c.T.Node = pick->ID;
        c.T.StartMs = c.T.StepMs = now ? now : 1;
        Emit(ai, bot, "TRAVEL_DISCOVER", StringFormat("walking to the flight master of node {} ({:.0f} yd)", pick->ID, std::sqrt(pd)),
            StringFormat(R"({{"node":{},"dist":{:.0f}}})", pick->ID, std::sqrt(pd)));
    }

    void End(BotAI* ai, Player* bot, TravelCtx& c, char const* reason, bool ok, uint32 blame = 0)
    {
        Emit(ai, bot, ok ? "TRAVEL_DONE" : "TRAVEL_ABORT", StringFormat("trip {}: {}", ok ? "finished" : "stopped", reason),
            StringFormat(R"({{"reason":"{}","kind":"{}","zone":{},"took_s":{}}})", reason, c.T.K == Kind::Zone ? "zone" : "discover", bot->GetZoneId(), (ai->GetNowMs() - c.T.StartMs) / 1000),
            ok ? BOTLOG_INFO : BOTLOG_WARN);
        BotMotion& m = ai->Motion();
        if (m.HasGoal() && !std::strncmp(m.GetTag(), "travel", 6))
            m.ClearGoal();
        if (!ok && blame)
            c.Remember(blame);
        c.NextCheckMs = ai->GetNowMs() + (ok ? 3000 : 60000);
        c.Reset();
    }

    bool Run(BotAI* ai, Player* bot, TravelCtx& c, uint32 now)
    {
        if (!bot->IsAlive())
        {
            End(ai, bot, c, "bot_died", false);
            return false;
        }
        if (bot->IsInFlight())
            return true;    // the flight path moves the bot; nothing else may run
        if (bot->IsInCombat() || bot->IsBeingTeleported())
            return false;   // the combat engine runs; the trip waits
        if (bot->GetGroup() || bot->GetMap()->Instanceable())
        {
            End(ai, bot, c, "grouped_or_instance", false);
            return false;
        }
        if (now - c.T.StartMs > s_cfg.TripMinutes * 60 * 1000)
        {
            End(ai, bot, c, "timeout", false, c.T.C.Area);
            return false;
        }
        if (c.T.K == Kind::Discover)
            return RunDiscover(ai, bot, c, now);
        return RunZone(ai, bot, c, now);
    }

    bool RunDiscover(BotAI* ai, Player* bot, TravelCtx& c, uint32 now)
    {
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(c.T.Node);
        if (!node || bot->m_taxi.IsTaximaskNodeKnown(c.T.Node))
        {
            End(ai, bot, c, node ? "already_known" : "no_node", true);
            return false;
        }
        if (now - c.T.StepMs > 4 * 60 * 1000)
        {
            End(ai, bot, c, "step_timeout", false);
            return false;
        }
        if (Creature* fm = FindNpc(bot, UNIT_NPC_FLAG_FLIGHTMASTER, 40.0f))
        {
            if (bot->GetDistance(fm) <= FM_INTERACT_YARDS && bot->GetNPCIfCanInteractWith(fm->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE))
            {
                BotMotion::Halt(bot);
                bot->GetSession()->SendLearnNewTaxiNode(fm);
                End(ai, bot, c, bot->m_taxi.IsTaximaskNodeKnown(c.T.Node) ? "learned" : "learned_other_node", true);
                return false;
            }
            return Walk(ai, bot, c, now, fm->GetPositionX(), fm->GetPositionY(), fm->GetPositionZ(), FM_INTERACT_YARDS - 1.0f, "travel_fm");
        }
        return Walk(ai, bot, c, now, node->Pos.X, node->Pos.Y, node->Pos.Z, 12.0f, "travel_fm");
    }

    // Issues the goal (again) when none is active; counts re-issues so a path that never works ends the trip.
    bool Walk(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, float x, float y, float z, float arrive, char const* tag)
    {
        BotMotion& m = ai->Motion();
        if (m.HasGoal() && !std::strcmp(m.GetTag(), tag))
            return false;   // the goto strategy walks
        if (m.HasGoal())
            return false;   // another owner of the movement slot (flee, follow ...)
        if (now - c.T.GoalMs < 1500)
            return false;
        if (c.T.Tries >= 6)
        {
            End(ai, bot, c, "walk_failed", false, c.T.C.Area);
            return false;
        }
        ++c.T.Tries;
        c.T.GoalMs = now;
        m.SetGoal(bot->GetMapId(), x, y, z, arrive, tag);
        return false;
    }

    bool RunZone(BotAI* ai, Player* bot, TravelCtx& c, uint32 now)
    {
        TravelCtx::Trip& t = c.T;
        if (t.Hearthing)
        {
            // the cast takes 10 s; afterwards the bot stands at its bind: plan again from there
            if (bot->IsNonMeleeSpellCast(false))
                return true;
            if (now - t.StepMs < 25000 && ResolveZone(bot->GetZoneId()) == t.FromZone)
                return true;
            End(ai, bot, c, "hearthed", true);
            return false;
        }
        uint32 const zone = ResolveZone(bot->GetZoneId());
        // jump over steps already done (a path through a zone that was not planned counts too)
        for (size_t i = t.Idx; i < t.C.R.Steps.size(); ++i)
            if (zone == t.C.R.Steps[i].To)
            {
                t.Idx = i + 1;
                t.StepMs = now;
                t.Tries = 0;
                t.Flying = false;
                break;
            }
        if (zone != t.C.Area && t.Idx >= t.C.R.Steps.size() && t.Rode && !t.Tail)
        {
            // stepped off a transport in a city or at a dock outside the zone table: walk on to the target zone
            t.Tail = true;
            t.C.R.Steps.push_back({ zone, t.C.Area, MODE_WALK, 300.0f, 3 });
            t.StepMs = now;
            t.Tries = 0;
        }
        if (zone == t.C.Area || t.Idx >= t.C.R.Steps.size())
        {
            if (zone == t.C.Area)
                c.Remember(zone);
            End(ai, bot, c, zone == t.C.Area ? "arrived" : "route_done_elsewhere", zone == t.C.Area, zone == t.C.Area ? 0 : t.C.Area);
            return false;
        }
        if (now - t.StepMs > 8 * 60 * 1000)
        {
            End(ai, bot, c, "step_timeout", false, t.C.Area);
            return false;
        }
        Edge const& e = t.C.R.Steps[t.Idx];
        if (e.M == MODE_TAXI)
            return RunTaxi(ai, bot, c, now, e);
        if (e.M == MODE_BOAT || e.M == MODE_ZEPPELIN)
            return RunRide(ai, bot, c, now, e);
        if (e.M != MODE_WALK)
        {
            End(ai, bot, c, "unsupported_mode", false, t.C.Area);
            return false;
        }
        // walking: head for the quest hub of the farthest zone ahead on the route that has one
        for (size_t k = t.C.R.Steps.size(); k-- > t.Idx;)
        {
            if (t.C.R.Steps[k].M != MODE_WALK)
                continue;
            bool walkOnly = true;
            for (size_t j = t.Idx; j <= k; ++j)
                walkOnly &= t.C.R.Steps[j].M == MODE_WALK;
            float x, y, z;
            if (walkOnly && BotQuest::FindHubInZone(bot, t.C.R.Steps[k].To, x, y, z))
                return Walk(ai, bot, c, now, x, y, z, 30.0f, "travel");
        }
        End(ai, bot, c, "no_hub_in_zone", false, e.To);
        return false;
    }

    // ---- boat or zeppelin leg ----
    uint32 ZoneAt(Player* bot, Stop const& s) const
    {
        return bot->GetMap() ? ResolveZone(bot->GetMap()->GetZoneId(bot->GetPhaseShift(), s.X, s.Y, s.Z)) : 0;
    }

    // Picks the transport, its dock stop in the zone the bot stands in, and the stop that comes next on the destination continent.
    bool ResolveRide(Player* bot, TravelCtx::Trip& t, Edge const& e)
    {
        std::call_once(s_transportOnce, BuildTransportIndex);
        Zone const* to = FindZone(e.To);
        if (!to)
            return false;
        uint32 bestMs = 0xFFFFFFFFu;
        bool found = false;
        for (TransportInfo const& info : s_transports)
            for (size_t i = 0; i < info.Stops.size(); ++i)
            {
                Stop const& from = info.Stops[i];
                if (from.Map != bot->GetMapId() || ZoneAt(bot, from) != e.From)
                    continue;
                uint32 ride = 0;
                int const j = NextStop(info.Stops, info.CycleMs, i, [&](Stop const& s)
                {
                    return s.Map == to->Map && (s.Map != bot->GetMapId() || ZoneAt(bot, s) == e.To);
                }, &ride);
                if (j < 0 || ride >= bestMs)
                    continue;
                int const a = NearestAnchor(s_anchors, from.Map, from.X, from.Y, 250.0f);
                int const b = NearestAnchor(s_anchors, info.Stops[j].Map, info.Stops[j].X, info.Stops[j].Y, 250.0f);
                if (a < 0 || b < 0)
                    continue;
                bestMs = ride;
                found = true;
                t.RideEntry = info.Entry;
                t.RideMs = ride;
                t.Dock = from;
                t.Landing = info.Stops[size_t(j)];
                t.DockAnchor = s_anchors[size_t(a)];
                t.LandAnchor = s_anchors[size_t(b)];
            }
        return found;
    }

    bool RunRide(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, Edge const& e)
    {
        TravelCtx::Trip& t = c.T;
        if (!t.Ride)
        {
            if (!ResolveRide(bot, t, e))
            {
                c.Avoid.push_back({ e.From, e.To });
                End(ai, bot, c, "no_transport_stop", false);
                return false;
            }
            t.Ride = 1;
            t.StepMs = now;
            t.Tries = 0;
            Emit(ai, bot, "TRAVEL_RIDE_PLAN", StringFormat("taking transport {} from zone {} to zone {} ({} s ride)", t.RideEntry, e.From, e.To, t.RideMs / 1000),
                StringFormat(R"({{"transport":{},"from":{},"to":{},"ride_s":{},"dock_node":{},"landing_node":{}}})", t.RideEntry, e.From, e.To, t.RideMs / 1000, t.DockAnchor.Id, t.LandAnchor.Id));
        }
        if (t.Ride == 1)
        {
            float const dx = bot->GetPositionX() - t.DockAnchor.X, dy = bot->GetPositionY() - t.DockAnchor.Y;
            if (dx * dx + dy * dy <= 25.0f * 25.0f)
            {
                BotMotion& m = ai->Motion();
                if (m.HasGoal() && !std::strcmp(m.GetTag(), "travel_dock"))
                    m.ClearGoal();
                t.Ride = 2;
                t.RideWaitMs = now;
                return false;
            }
            return Walk(ai, bot, c, now, t.DockAnchor.X, t.DockAnchor.Y, t.DockAnchor.Z, 12.0f, "travel_dock");
        }
        if (t.Ride == 2)
        {
            GameObject* ship = bot->FindNearestGameObject(t.RideEntry, 300.0f);
            bool const docked = ship && ship->GetExactDist2d(t.Dock.X, t.Dock.Y) <= 60.0f;
            if (docked)
            {
                BotMotion::Halt(bot);
                t.Ride = 3;
                t.RideEndMs = now + std::min<uint32>(t.RideMs, 600000u);
                t.Rode = true;
                Emit(ai, bot, "TRAVEL_RIDE_START", StringFormat("boarding transport {}, arrives in {} s", t.RideEntry, t.RideMs / 1000),
                    StringFormat(R"({{"transport":{},"ride_s":{},"waited_s":{}}})", t.RideEntry, t.RideMs / 1000, (now - t.RideWaitMs) / 1000));
                return true;
            }
            // no transport object around at all after 90 s: it is not spawned in this world; otherwise wait one full cycle
            if (now - t.RideWaitMs > (ship ? 7u * 60u * 1000u : 90u * 1000u))
            {
                c.Avoid.push_back({ e.From, e.To });
                End(ai, bot, c, ship ? "transport_never_docked" : "transport_not_spawned", false);
            }
            return false;
        }
        // riding: hold the bot, then put it at the landing
        if (int32(t.RideEndMs - now) > 0)
            return true;
        t.Ride = 0;
        t.Idx++;
        t.StepMs = now;
        t.Tries = 0;
        bot->TeleportTo(t.LandAnchor.Map, t.LandAnchor.X, t.LandAnchor.Y, t.LandAnchor.Z, bot->GetOrientation());
        Emit(ai, bot, "TRAVEL_RIDE_END", StringFormat("left transport {} at node {}", t.RideEntry, t.LandAnchor.Id),
            StringFormat(R"({{"transport":{},"node":{},"map":{}}})", t.RideEntry, t.LandAnchor.Id, t.LandAnchor.Map));
        return true;
    }

    bool RunTaxi(BotAI* ai, Player* bot, TravelCtx& c, uint32 now, Edge const& e)
    {
        TravelCtx::Trip& t = c.T;
        if (t.Flying)
        {
            // landed somewhere else than planned: the loop above re-checks the zone; give up when it does not match
            End(ai, bot, c, "flight_ended_elsewhere", false, t.C.Area);
            return false;
        }
        Creature* fm = FindNpc(bot, UNIT_NPC_FLAG_FLIGHTMASTER, 120.0f);
        if (!fm)
        {
            // walk to the known node of this zone, the flight master stands there
            TaxiNodesEntry const* n = NearestNode(bot, bot->GetPositionX(), bot->GetPositionY(), e.From);
            if (!n || !bot->m_taxi.IsTaximaskNodeKnown(n->ID))
            {
                End(ai, bot, c, "no_flight_master", false, e.To);
                return false;
            }
            return Walk(ai, bot, c, now, n->Pos.X, n->Pos.Y, n->Pos.Z, 20.0f, "travel_fm");
        }
        if (bot->GetDistance(fm) > FM_INTERACT_YARDS || !bot->GetNPCIfCanInteractWith(fm->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE))
            return Walk(ai, bot, c, now, fm->GetPositionX(), fm->GetPositionY(), fm->GetPositionZ(), FM_INTERACT_YARDS - 1.0f, "travel_fm");

        BotMotion::Halt(bot);
        BotMotion& m = ai->Motion();
        if (m.HasGoal())
            m.ClearGoal();
        bot->GetSession()->SendLearnNewTaxiNode(fm);
        uint32 const fromId = sObjectMgr->GetNearestTaxiNode(fm->GetPositionX(), fm->GetPositionY(), fm->GetPositionZ(), fm->GetMapId(), bot->GetTeam());
        TaxiNodesEntry const* from = sTaxiNodesStore.LookupEntry(fromId);
        std::vector<uint32> best;
        for (TaxiNodesEntry const* n : sTaxiNodesStore)
        {
            if (!from || !NodeVisible(n, bot) || n->ContinentID != bot->GetMapId() || !bot->m_taxi.IsTaximaskNodeKnown(n->ID) || NodeZone(bot, n) != e.To)
                continue;
            std::vector<uint32> path;
            if (TaxiPathGraph::GetCompleteNodeRoute(from, n, bot, path) && path.size() >= 2 && (best.empty() || path.size() < best.size()))
                best = std::move(path);
        }
        if (best.empty() || !bot->ActivateTaxiPathTo(best, fm))
        {
            c.TaxiBlockedUntilMs = now + 15 * 60 * 1000;   // money, a closed path ...: walk for a while
            c.Avoid.push_back({ e.From, e.To });
            End(ai, bot, c, best.empty() ? "no_flight_route" : "flight_refused", false);
            return false;
        }
        t.Flying = true;
        t.StepMs = now;
        Emit(ai, bot, "TRAVEL_FLIGHT", StringFormat("flying from node {} to node {} (zone {} to {})", best.front(), best.back(), e.From, e.To),
            StringFormat(R"({{"from_node":{},"to_node":{},"from":{},"to":{},"hops":{}}})", best.front(), best.back(), e.From, e.To, best.size()));
        return true;
    }
};

class TravelDueTrigger : public Trigger
{
public:
    explicit TravelDueTrigger(BotAI* ai) : Trigger(ai, "travel_due", 2000) { }
    bool IsActive() override { return s_cfg.Enabled; }
};

class TravelStrategy : public Strategy
{
public:
    TravelStrategy() : Strategy("travel") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // above the quest layer (it yields to Busy() anyway); the action returns false unless it holds the bot, so other actions still run
        t.push_back({ "travel_due", { { "travel_tick", BotRelevance::Move + 3.0f } } });
    }
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------------------------------------------------
Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Travel.Enabled", false);
        s_cfg.Zones = sConfigMgr->GetBoolDefault("Bot.AI.Travel.Zones", true);
        s_cfg.Taxi = sConfigMgr->GetBoolDefault("Bot.AI.Travel.Taxi", true);
        s_cfg.Transport = sConfigMgr->GetBoolDefault("Bot.AI.Travel.Transport", true);
        s_cfg.Hearth = sConfigMgr->GetBoolDefault("Bot.AI.Travel.Hearth", true);
        s_cfg.CheckSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.CheckSec", 10), 2, 600));
        s_cfg.MaxRouteSteps = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.MaxRouteSteps", 8), 1, 20));
        s_cfg.RecentZones = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.RecentZones", 4), 1, 20));
        s_cfg.LeaveMarginLevels = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.LeaveMarginLevels", 0), 0, 10));
        s_cfg.DiscoverRadius = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.DiscoverRadius", 500), 50, 5000));
        s_cfg.TripMinutes = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Travel.TripMinutes", 25), 2, 240));
    });
    return s_cfg;
}

void OnLogout(BotAI* ai, Player* bot)
{
    (void)bot;
    if (TravelCtx* c = Ctx(ai))
        c->Reset();
}

bool Busy(BotAI* ai)
{
    if (!s_cfg.Enabled)
        return false;
    TravelCtx* c = Ctx(ai);
    return c && c->T.Active;
}
} // namespace BotTravel

void RegisterTravelBotObjects(BotRegistry& r)
{
    BotTravel::Cfg();
    r.AddValue("travel_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotTravel::TravelCtx>(ai); });
    r.AddTrigger("travel_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotTravel::TravelDueTrigger>(ai); });
    r.AddAction("travel_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotTravel::TravelTickAction>(ai); });
    r.AddStrategy("travel", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotTravel::TravelStrategy>(); });
}
