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


#include "BotTravelPlan.h"
#include "BotMovePlan.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace BotTravel
{
namespace
{
constexpr uint8 A = 1, H = 2, AH = 3;
constexpr uint32 EK = 0, KAL = 1;

// Leveling zones of the two factions (vanilla world), level 1 to 60 (area ids from AreaTable.dbc). Bands follow the usual quest ranges; a zone is in
// the table for the team that has quests there. Cities are not zones: a transport that leaves from a city is attached to the zone of the
// city (Orgrimmar to Durotar, Undercity to Tirisfal Glades, Stormwind to Elwynn, Ironforge to Dun Morogh, Darnassus to Teldrassil).
std::vector<Zone> const g_zones =
{
    // Eastern Kingdoms
    { 1,    EK,  "Dun Morogh",           1, 12, A },
    { 12,   EK,  "Elwynn Forest",        1, 10, A },
    { 85,   EK,  "Tirisfal Glades",      1, 10, H },
    { 40,   EK,  "Westfall",            10, 20, A },
    { 38,   EK,  "Loch Modan",          10, 20, A },
    { 130,  EK,  "Silverpine Forest",   10, 20, H },
    { 44,   EK,  "Redridge Mountains",  15, 25, A },
    { 10,   EK,  "Duskwood",            18, 30, A },
    { 267,  EK,  "Hillsbrad Foothills", 20, 30, AH },
    { 11,   EK,  "Wetlands",            20, 30, A },
    { 45,   EK,  "Arathi Highlands",    30, 40, AH },
    { 33,   EK,  "Stranglethorn Vale",  30, 45, AH },
    { 3,    EK,  "Badlands",            35, 45, AH },
    { 8,    EK,  "Swamp of Sorrows",    35, 45, AH },
    { 47,   EK,  "The Hinterlands",     40, 50, AH },
    { 51,   EK,  "Searing Gorge",       43, 50, AH },
    { 4,    EK,  "Blasted Lands",       45, 55, AH },
    { 46,   EK,  "Burning Steppes",     50, 58, AH },
    { 28,   EK,  "Western Plaguelands", 51, 58, AH },
    { 139,  EK,  "Eastern Plaguelands", 53, 60, AH },
    // Kalimdor
    { 141,  KAL, "Teldrassil",           1, 10, A },
    { 14,   KAL, "Durotar",              1, 10, H },
    { 215,  KAL, "Mulgore",              1, 10, H },
    { 148,  KAL, "Darkshore",           10, 20, A },
    { 17,   KAL, "The Barrens",         10, 25, H },
    { 406,  KAL, "Stonetalon Mountains",15, 27, AH },
    { 331,  KAL, "Ashenvale",           18, 30, AH },
    { 400,  KAL, "Thousand Needles",    25, 35, AH },
    { 405,  KAL, "Desolace",            30, 40, AH },
    { 15,   KAL, "Dustwallow Marsh",      35, 45, AH },
    { 357,  KAL, "Feralas",             40, 50, AH },
    { 440,  KAL, "Tanaris",             40, 50, AH },
    { 16,   KAL, "Azshara",             45, 55, AH },
    { 361,  KAL, "Felwood",             48, 55, AH },
    { 490,  KAL, "Un'Goro Crater",      48, 55, AH },
    { 618,  KAL, "Winterspring",        53, 60, AH },
    { 1377, KAL, "Silithus",            55, 60, AH },
};

std::vector<Edge> BuildEdges()
{
    std::vector<Edge> v;
    auto both = [&v](uint32 a, uint32 b, Mode m, float sec, uint8 teams = AH)
    {
        v.push_back({ a, b, m, sec, teams });
        v.push_back({ b, a, m, sec, teams });
    };
    constexpr float W = 300.0f;   // walking from the middle of one zone to the middle of the next

    // --- walking, Eastern Kingdoms ---
    both(1, 38, MODE_WALK, W);      // Dun Morogh - Loch Modan
    both(38, 11, MODE_WALK, W);     // Loch Modan - Wetlands
    both(38, 3, MODE_WALK, W);      // Loch Modan - Badlands
    both(3, 51, MODE_WALK, W);      // Badlands - Searing Gorge
    both(51, 46, MODE_WALK, W);     // Searing Gorge - Burning Steppes
    both(12, 40, MODE_WALK, W);     // Elwynn - Westfall
    both(12, 44, MODE_WALK, W);     // Elwynn - Redridge
    both(12, 10, MODE_WALK, W);     // Elwynn - Duskwood
    both(44, 10, MODE_WALK, W);     // Redridge - Duskwood
    both(44, 46, MODE_WALK, 360.0f);// Redridge - Burning Steppes
    both(44, 8, MODE_WALK, W);      // Redridge - Swamp of Sorrows
    both(40, 10, MODE_WALK, W);     // Westfall - Duskwood
    both(10, 33, MODE_WALK, 360.0f);// Duskwood - Stranglethorn
    both(8, 4, MODE_WALK, W);       // Swamp of Sorrows - Blasted Lands
    both(11, 45, MODE_WALK, W);     // Wetlands - Arathi
    both(45, 267, MODE_WALK, W);    // Arathi - Hillsbrad
    both(45, 47, MODE_WALK, W);     // Arathi - Hinterlands
    both(47, 267, MODE_WALK, W);    // Hinterlands - Hillsbrad
    both(267, 130, MODE_WALK, 360.0f); // Hillsbrad - Silverpine
    both(130, 85, MODE_WALK, W);    // Silverpine - Tirisfal
    both(85, 28, MODE_WALK, W);     // Tirisfal - Western Plaguelands
    both(28, 139, MODE_WALK, W);    // Western - Eastern Plaguelands

    // --- walking, Kalimdor ---
    both(148, 331, MODE_WALK, W);   // Darkshore - Ashenvale
    both(331, 406, MODE_WALK, W);   // Ashenvale - Stonetalon
    both(331, 17, MODE_WALK, W);    // Ashenvale - Barrens
    both(331, 16, MODE_WALK, W);    // Ashenvale - Azshara
    both(331, 361, MODE_WALK, W);   // Ashenvale - Felwood
    both(361, 618, MODE_WALK, W);   // Felwood - Winterspring
    both(406, 17, MODE_WALK, W);    // Stonetalon - Barrens
    both(406, 405, MODE_WALK, W);   // Stonetalon - Desolace
    both(405, 357, MODE_WALK, W);   // Desolace - Feralas
    both(17, 14, MODE_WALK, W);     // Barrens - Durotar
    both(17, 215, MODE_WALK, W);    // Barrens - Mulgore
    both(17, 400, MODE_WALK, W);    // Barrens - Thousand Needles
    both(17, 15, MODE_WALK, W);     // Barrens - Dustwallow Marsh
    both(14, 16, MODE_WALK, 420.0f);// Durotar - Azshara
    both(400, 357, MODE_WALK, W);   // Thousand Needles - Feralas
    both(400, 440, MODE_WALK, W);   // Thousand Needles - Tanaris
    both(440, 490, MODE_WALK, W);   // Tanaris - Un'Goro
    both(440, 1377, MODE_WALK, W);  // Tanaris - Silithus
    both(490, 1377, MODE_WALK, W);  // Un'Goro - Silithus

    // --- boats, zeppelins, trams, portals ---
    both(11, 148, MODE_BOAT, 240.0f, A);       // Menethil Harbor - Auberdine
    both(11, 15, MODE_BOAT, 240.0f, A);        // Menethil Harbor - Theramore
    both(148, 141, MODE_BOAT, 200.0f, A);      // Auberdine - Rut'theran Village
    both(33, 17, MODE_BOAT, 240.0f, AH);       // Booty Bay - Ratchet
    both(14, 85, MODE_ZEPPELIN, 240.0f, H);    // Orgrimmar - Undercity
    both(14, 33, MODE_ZEPPELIN, 270.0f, H);    // Orgrimmar - Grom'gol Base Camp
    both(85, 33, MODE_ZEPPELIN, 270.0f, H);    // Undercity - Grom'gol Base Camp
    both(1, 12, MODE_TRAM, 180.0f, A);         // Ironforge - Stormwind (Deeprun Tram)
    return v;
}

// Dijkstra graph, rebuilt per call from the (small) edge lists
struct Item
{
    float Cost;
    uint32 Area;
    bool operator>(Item const& o) const { return Cost > o.Cost; }
};
}   // namespace

char const* ModeName(Mode m)
{
    switch (m)
    {
        case MODE_WALK: return "walk";
        case MODE_TAXI: return "taxi";
        case MODE_BOAT: return "boat";
        case MODE_ZEPPELIN: return "zeppelin";
        case MODE_TRAM: return "tram";
        case MODE_PORTAL: return "portal";
        default: return "?";
    }
}

char const* WhyName(Why w)
{
    switch (w)
    {
        case Why::Stay: return "STAY";
        case Why::NoCandidate: return "NO_CANDIDATE";
        case Why::Outgrown: return "OUTGROWN";
        case Why::TooLow: return "TOO_LOW";
        case Why::Exhausted: return "EXHAUSTED";
    }
    return "?";
}

std::vector<Zone> const& Zones() { return g_zones; }

Zone const* FindZone(uint32 area)
{
    for (Zone const& z : g_zones)
        if (z.Area == area)
            return &z;
    return nullptr;
}

std::vector<Edge> const& StaticEdges()
{
    static std::vector<Edge> const edges = BuildEdges();
    return edges;
}

Route PlanRoute(uint32 from, uint32 to, Team team, uint32 modes, std::vector<Edge> const& extra, uint32 maxSteps)
{
    Route r;
    if (from == to)
    {
        r.Found = true;
        return r;
    }
    uint8 const teamBit = uint8(team);
    std::unordered_map<uint32, std::vector<Edge const*>> adj;
    auto add = [&](std::vector<Edge> const& list)
    {
        for (Edge const& e : list)
            if ((modes & e.M) && (e.Teams & teamBit))
                adj[e.From].push_back(&e);
    };
    add(StaticEdges());
    add(extra);

    struct Node { float Cost = 1e30f; Edge const* Via = nullptr; uint32 Prev = 0; uint32 Steps = 0; };
    std::unordered_map<uint32, Node> best;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    best[from].Cost = 0.0f;
    pq.push({ 0.0f, from });
    while (!pq.empty())
    {
        Item const it = pq.top();
        pq.pop();
        Node const cur = best[it.Area];
        if (it.Cost > cur.Cost)
            continue;
        if (it.Area == to)
            break;
        if (cur.Steps >= maxSteps)
            continue;
        auto a = adj.find(it.Area);
        if (a == adj.end())
            continue;
        for (Edge const* e : a->second)
        {
            float const c = cur.Cost + e->Sec;
            Node& n = best[e->To];
            if (c < n.Cost)
            {
                n.Cost = c;
                n.Via = e;
                n.Prev = it.Area;
                n.Steps = cur.Steps + 1;
                pq.push({ c, e->To });
            }
        }
    }
    auto end = best.find(to);
    if (end == best.end() || !end->second.Via)
        return r;
    r.Found = true;
    r.TotalSec = end->second.Cost;
    for (uint32 a = to; a != from; a = best[a].Prev)
        r.Steps.push_back(*best[a].Via);
    std::reverse(r.Steps.begin(), r.Steps.end());
    return r;
}

float TaxiSeconds(float yards)
{
    return 45.0f + std::max(0.0f, yards) / 32.0f;
}

namespace
{
bool Open(Zone const& z, Team t) { return (z.Teams & uint8(t)) != 0; }
bool Recent(Query const& q, uint32 area) { return std::find(q.Recent.begin(), q.Recent.end(), area) != q.Recent.end(); }
}

Choice PickZone(Query const& q)
{
    Choice c;
    c.Area = q.CurrentArea;
    int32 const lvl = int32(q.Level);
    Zone const* cur = FindZone(q.CurrentArea);

    // does the current zone still fit?
    if (cur && Open(*cur, q.BotTeam))
    {
        if (lvl > int32(cur->MaxLevel) + int32(q.LeaveMargin))
            c.Reason = Why::Outgrown;
        else if (lvl < int32(cur->MinLevel) - 2)
            c.Reason = Why::TooLow;
        else if (q.LocalExhausted && lvl >= (int32(cur->MinLevel) + int32(cur->MaxLevel)) / 2)
            c.Reason = Why::Exhausted;
        else
            return c;   // Stay
    }
    else
        c.Reason = Why::Outgrown;   // unknown zone or one of the wrong faction: treat as "go somewhere that fits"

    // candidates: zones of the team whose band holds the level, reachable within the step limit
    struct Cand { Zone const* Z; Route R; float Score; };
    std::vector<Cand> cands;
    for (int pass = 0; pass < 2 && cands.empty(); ++pass)
    {
        int32 const slack = pass == 0 ? 0 : 3;   // second pass: accept zones a few levels off
        for (Zone const& z : g_zones)
        {
            if (!Open(z, q.BotTeam) || z.Area == q.CurrentArea)
                continue;
            if (q.CurrentArea == 0 && z.Map != q.CurrentMap)
                continue;   // unknown position (a city): only zones on the same continent
            if (lvl < int32(z.MinLevel) - slack || lvl > int32(z.MaxLevel) + slack)
                continue;
            Route r = PlanRoute(q.CurrentArea, z.Area, q.BotTeam, q.Modes, q.Extra, q.MaxSteps);
            if (!r.Found && q.CurrentArea == 0)
            {
                // unknown position (a city or a zone outside the table): assume a typical trip of one walking leg
                r.Found = true;
                r.TotalSec = 600.0f;
            }
            if (!r.Found)
                continue;
            float const mid = 0.5f * float(z.MinLevel + z.MaxLevel);
            float const half = 0.5f * float(z.MaxLevel - z.MinLevel) + 3.0f;
            float score = 1.0f - std::fabs(float(lvl) - mid) / half;               // level fit: 1 in the middle of the band
            score -= r.TotalSec / 1200.0f;                                           // 20 min of travel cancels a perfect fit
            if (z.Map == q.CurrentMap)
                score += 0.15f;
            if (Recent(q, z.Area))
                score -= 0.6f;
            score += BotMove::Range(BotMove::Mix(q.BotKey, q.Attempt, z.Area), 0.0f, 0.25f);
            cands.push_back({ &z, std::move(r), score });
        }
    }
    if (cands.empty())
    {
        c.Reason = Why::NoCandidate;
        return c;
    }
    auto best = std::max_element(cands.begin(), cands.end(), [](Cand const& a, Cand const& b) { return a.Score < b.Score; });
    c.Area = best->Z->Area;
    c.RouteSec = best->R.TotalSec;
    c.R = std::move(best->R);
    return c;
}

bool ShouldHearth(HearthQuery const& q, float* savedSec)
{
    if (savedSec)
        *savedSec = 0.0f;
    if (!q.HearthReady || !q.Bind || q.Bind == q.Current || q.Target == q.Current)
        return false;
    Route const direct = PlanRoute(q.Current, q.Target, q.BotTeam, q.Modes, q.Extra);
    Route const viaBind = PlanRoute(q.Bind, q.Target, q.BotTeam, q.Modes, q.Extra);
    if (!viaBind.Found)
        return false;
    float const directSec = direct.Found ? direct.TotalSec : 1e9f;
    float const saved = directSec - (q.CastSec + viaBind.TotalSec);
    if (savedSec)
        *savedSec = std::min(saved, 1e8f);
    return saved >= q.MinSavingSec;
}

bool ShouldRebind(RebindQuery const& q)
{
    if (!q.InnArea || q.InnArea == q.Bind)
        return false;
    if (!q.Bind)
        return true;
    Route const here = PlanRoute(q.InnArea, q.Next, q.BotTeam, q.Modes, q.Extra);
    if (!here.Found)
        return false;
    Route const old = PlanRoute(q.Bind, q.Next, q.BotTeam, q.Modes, q.Extra);
    float const oldSec = old.Found ? old.TotalSec : 1e9f;
    return oldSec - here.TotalSec >= q.MinSavingSec;
}
}   // namespace BotTravel
