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


#ifndef TRINITY_BOT_TRAVEL_PLAN_H
#define TRINITY_BOT_TRAVEL_PLAN_H

// Travel and leveling progression for the bots (Bot.AI.Travel.*): which zone a bot should level in next, and how it gets there
// (walk, flight path, boat, zeppelin, tram, portal). Pure functions over plain data: no Player, no Map, no DBC. The glue (BotTravel.cpp)
// feeds in the known flight paths; everything else comes from the static tables in BotTravelPlan.cpp. Randomness is a hash of
// (bot, level, attempt), so results are reproducible in tests and two bots in the same spot do not all pick the same zone.
// See docs/playerbots/feature-bot-travel-20261008.md.

#include "Define.h"
#include <functional>
#include <utility>
#include <vector>

namespace BotTravel
{
    // Bot.AI.Travel.*: every part is off unless Enabled is set (default off); read once by BotTravel.cpp.
    struct Config
    {
        bool Enabled = false;
        bool Zones = true;          // pick the next leveling zone by level and faction when the local zone is done
        bool Taxi = true;           // discover and use flight paths
        bool Transport = true;      // boats and zeppelins (simulated ride between two docks)
        bool Hearth = true;         // hearthstone and inn binding
        uint32 CheckSec = 10;       // seconds between two checks "does the current zone still fit"
        uint32 MaxRouteSteps = 8;   // longest route that is planned
        uint32 RecentZones = 4;     // zones remembered per bot, so it does not bounce back
        uint32 LeaveMarginLevels = 0; // levels above the band maximum before the bot leaves
        uint32 DiscoverRadius = 500;  // yards: unknown flight masters this close are visited once
        uint32 TripMinutes = 25;    // a trip that takes longer is abandoned
    };
    TC_GAME_API Config const& Cfg();

    enum class Team : uint8 { Alliance = 1, Horde = 2 };
    // bit mask of the modes a bot may use
    enum Mode : uint8
    {
        MODE_WALK = 1, MODE_TAXI = 2, MODE_BOAT = 4, MODE_ZEPPELIN = 8, MODE_TRAM = 16, MODE_PORTAL = 32,
        MODE_ALL = 63,
    };
    TC_GAME_API char const* ModeName(Mode m);

    // One leveling zone. Area = AreaTable id, Map = continent map id. Teams: bit mask of Team values that quest there (3 = both).
    struct Zone
    {
        uint32 Area = 0;
        uint32 Map = 0;
        char const* Name = "";
        uint8 MinLevel = 0;
        uint8 MaxLevel = 0;
        uint8 Teams = 3;
    };
    TC_GAME_API std::vector<Zone> const& Zones();
    TC_GAME_API Zone const* FindZone(uint32 area);

    // One connection between two zones. Both directions are listed as separate edges. Seconds includes waiting for the transport.
    struct Edge
    {
        uint32 From = 0;
        uint32 To = 0;
        Mode M = MODE_WALK;
        float Sec = 0.0f;
        uint8 Teams = 3;            // who may use it (a Horde zeppelin is no use to an Alliance bot)
    };
    // walking links between neighbouring zones, boats, zeppelins, trams, portals
    TC_GAME_API std::vector<Edge> const& StaticEdges();

    struct Route
    {
        bool Found = false;
        float TotalSec = 0.0f;
        std::vector<Edge> Steps;
    };
    // Cheapest route (Dijkstra on seconds) from one zone to another over StaticEdges() plus `extra` (known flight paths), using only
    // the modes in `modes` and edges open to `team`. At most maxSteps edges. From == To is a found route with no steps.
    // `banned` lists (from, to) zone pairs that must not be used (a ride or flight that failed for this bot).
    using Banned = std::vector<std::pair<uint32, uint32>>;
    TC_GAME_API Route PlanRoute(uint32 from, uint32 to, Team team, uint32 modes, std::vector<Edge> const& extra = {}, uint32 maxSteps = 8, Banned const& banned = {});

    enum class Why : uint8
    {
        Stay,           // the current zone fits the level
        NoCandidate,    // nothing fits and is reachable: stay and grind
        Outgrown,       // level above the zone's band
        TooLow,         // level below the zone's band (e.g. arrived early)
        Exhausted,      // no quests left in the zone and the bot is past the middle of the band
    };
    TC_GAME_API char const* WhyName(Why w);

    struct Query
    {
        uint32 Level = 1;
        Team BotTeam = Team::Alliance;
        uint32 CurrentArea = 0;      // zone of the bot; 0 = unknown (not in the table, e.g. a city)
        uint32 CurrentMap = 0;
        bool LocalExhausted = false; // the quest layer found nothing to do here
        uint64 BotKey = 0;           // stable id of the bot (guid)
        uint32 Attempt = 0;          // number of earlier picks of this bot
        uint32 Modes = MODE_ALL;
        std::vector<uint32> Recent;  // areas visited lately, oldest first
        std::vector<Edge> Extra;     // known flight paths
        uint32 MaxSteps = 8;
        uint32 LeaveMargin = 0;
        Banned Avoid;                // edges not to use
    };

    struct Choice
    {
        Why Reason = Why::Stay;
        uint32 Area = 0;             // target zone; = CurrentArea when the bot stays
        float RouteSec = 0.0f;
        Route R;
    };
    // Should the bot leave the zone it is in, and where to? A bot that stays gets Area = CurrentArea and an empty route.
    TC_GAME_API Choice PickZone(Query const& q);

    // --- hearthstone and inn ---
    struct HearthQuery
    {
        uint32 Target = 0;           // zone the bot wants to go to
        uint32 Current = 0;
        uint32 Bind = 0;             // zone of the home bind; 0 = none
        bool HearthReady = false;    // item present, off cooldown, not in combat
        Team BotTeam = Team::Alliance;
        uint32 Modes = MODE_ALL;
        std::vector<Edge> Extra;
        float CastSec = 15.0f;       // cast time plus the load
        float MinSavingSec = 90.0f;  // not worth it for less
    };
    // True when casting the hearthstone and travelling from the bind is clearly faster than travelling from here.
    TC_GAME_API bool ShouldHearth(HearthQuery const& q, float* savedSec = nullptr);

    struct RebindQuery
    {
        uint32 InnArea = 0;          // the zone of the innkeeper the bot stands at
        uint32 Bind = 0;
        uint32 Next = 0;             // where the bot will level next (its current zone while it stays)
        Team BotTeam = Team::Alliance;
        uint32 Modes = MODE_ALL;
        std::vector<Edge> Extra;
        float MinSavingSec = 180.0f;
    };
    // True when binding here shortens the way back to the next leveling zone by more than MinSavingSec (or when there is no bind at all).
    TC_GAME_API bool ShouldRebind(RebindQuery const& q);

    // --- transports (boats and zeppelins) ---
    // One pause of a transport on its cycle, as the core's transport template gives it. TimeMs is inside [0, cycle).
    struct Stop
    {
        uint32 TimeMs = 0;
        uint32 Map = 0;
        float X = 0.0f, Y = 0.0f, Z = 0.0f;
    };
    // First stop after stops[from], walking the cycle forward and wrapping at cycleMs, for which pred is true. Returns its index (never
    // `from` itself) or -1. rideMs gets the time from stops[from] to it.
    TC_GAME_API int NextStop(std::vector<Stop> const& stops, uint32 cycleMs, size_t from, std::function<bool(Stop const&)> const& pred, uint32* rideMs = nullptr);

    // A walkable place near a dock (a flight master's position): where a bot waits for a ship and where it steps off.
    struct Anchor
    {
        uint32 Id = 0;
        uint32 Map = 0;
        float X = 0.0f, Y = 0.0f, Z = 0.0f;
    };
    // Nearest anchor on the map within maxYards (2D) of the point, or -1.
    TC_GAME_API int NearestAnchor(std::vector<Anchor> const& anchors, uint32 map, float x, float y, float maxYards);

    // --- flight masters ---
    // Cost in seconds of a flight of `yards` (taxi mounts fly at about 32 yd/s plus take-off); used for the extra edges.
    TC_GAME_API float TaxiSeconds(float yards);
}

#endif
