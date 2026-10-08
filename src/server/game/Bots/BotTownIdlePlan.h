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

#ifndef TRINITY_BOT_TOWN_IDLE_PLAN_H
#define TRINITY_BOT_TOWN_IDLE_PLAN_H

// Town idling (Bot.AI.TownIdle.*, BotBehavior.cpp IdleStep). A bot that has nothing to do inside a city or an inn does not stand on
// one spot: it drifts between the places where players gather (innkeeper, bank, auction house, flight master, mailbox), lingers
// there, looks around, emotes and now and then sits down. Pure over plain data so it is unit tested without a map or a Player. See
// docs/playerbots/feature-bot-town-idle-20261008.md.

#include "Define.h"
#include <vector>

namespace BotTownIdle
{
    struct Config
    {
        bool Enabled = false;
        float SearchYards = 70.0f;       // radius around the bot in which hotspots are looked for
        uint32 LingerMinSec = 15;        // least time spent at one spot before moving on
        uint32 LingerMaxSec = 70;        // standing still longer than a per-bot value in this range forces a move
        uint32 MinGapMs = 4000;          // least time between two idle actions
        uint32 SitMinMs = 8000, SitMaxMs = 30000;
        uint32 EmotePct = 22;            // chance per decision to emote
        uint32 SitPct = 10;              // chance per decision to sit (more when hurt or drained)
        float StandOffMin = 2.5f, StandOffMax = 6.0f;   // distance at which a bot stands from a hotspot
    };

    enum class SpotKind : uint8 { Inn, Bank, Auction, Flight, Mail, Vendor };
    constexpr uint8 SPOT_KINDS = 6;

    struct Spot
    {
        float X = 0.0f, Y = 0.0f;
        SpotKind Kind = SpotKind::Vendor;
    };

    enum class Act : uint8 { Stay, Look, Emote, Sit, StandUp, GoSpot, Wander };

    // The emotes a bot picks from; the glue maps them to game emote ids.
    enum class EmoteKind : uint8 { Talk, Wave, Laugh, Yes, No, Dance };
    constexpr uint8 EMOTE_KINDS = 6;

    struct Facts
    {
        uint32 StillMs = 0;              // standing without any movement or action
        uint32 SinceActionMs = 0;        // since the last idle action was started
        bool Sitting = false;
        uint32 SittingMs = 0;
        bool Threat = false;             // anything hostile or an aggro warning nearby, or in combat
        bool HurtOrDrained = false;
        int32 CurrentSpot = -1;          // index into the spot list the bot is at or last walked to, -1 when none
        bool AtSpot = false;             // within StandOffMax + 2 yards of CurrentSpot
    };

    struct Plan
    {
        Act What = Act::Stay;
        float TurnRad = 0.0f;            // Look: facing change, radians (signed)
        EmoteKind Emote = EmoteKind::Talk;
        int32 Spot = -1;                 // GoSpot: index into the spot list
        float StandOff = 0.0f;           // GoSpot / Wander: distance from the spot (Wander: yards to walk)
        float Bearing = 0.0f;            // GoSpot: bearing from the spot to the standing point; Wander: absolute bearing
    };

    // Relative weight of a spot kind when a bot picks where to go (inn and bank draw the most idlers, vendors the least).
    TC_GAME_API uint32 SpotWeight(SpotKind kind);

    // Index of the spot a bot walks to next: weighted by kind, never the current one unless it is the only one; -1 for an empty list.
    TC_GAME_API int32 PickSpot(std::vector<Spot> const& spots, int32 current, uint64 botKey, uint32 nowMs);

    // Per-bot linger time in ms, within [LingerMinSec, LingerMaxSec].
    TC_GAME_API uint32 LingerMs(Config const& cfg, uint64 botKey);

    // Bot.AI.TownIdle.*, read once (BotBehavior.cpp).
    TC_GAME_API Config const& Cfg();

    TC_GAME_API Plan PlanStep(Facts const& f, Config const& cfg, std::vector<Spot> const& spots, uint64 botKey, uint32 nowMs);
}

#endif
