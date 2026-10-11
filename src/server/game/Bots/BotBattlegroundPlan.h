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

#ifndef TRINITY_BOT_BATTLEGROUND_PLAN_H
#define TRINITY_BOT_BATTLEGROUND_PLAN_H

// Battlegrounds for bots (Bot.AI.Battleground.*, BotBattleground.cpp). A player who queues for Warsong Gulch finds bots in the queue
// next to them: this file decides how many bots join each side, which bots, how long they wait before accepting the invite, which job
// each bot has on its team and what it does next (take the flag, bring it home, kill the enemy carrier, bring back its own flag,
// defend). Pure over plain data so it is unit tested without a map, a queue or a Player. See
// docs/playerbots/feature-bot-battlegrounds-20261008.md.

#include "Define.h"
#include <vector>

namespace BotBattleground
{
    struct Config
    {
        bool Enabled = false;
        uint32 QueueIntervalMs = 10000;   // how often the queues of the players are looked at
        uint32 FillTo = 0;                // players per side the queue is filled to; 0 = the battleground's minimum per side
        uint32 MaxBotsPerQueue = 20;      // bots waiting in one queue at most
        uint32 JoinPerStepMax = 3;        // bots added to one queue per look
        uint32 JoinDelaySec = 8;          // a player waits this long alone in the queue before bots join (bots do not answer at once)
        uint32 IdleDropSec = 120;         // bots leave a queue that has had no waiting player for this long
        uint32 MinLevel = 10;             // lowest bot level to be queued (the bracket must exist as well)
        uint32 AcceptMinSec = 3, AcceptMaxSec = 15;   // delay before a bot accepts the invitation (the invitation lasts 80 s)
        uint32 LeaveMinSec = 6, LeaveMaxSec = 25;     // delay before a bot leaves a finished battleground
        uint32 EmptyLeaveSec = 90;        // bots leave a battleground that has had no player for this long (0 = never)
        uint32 StepMs = 1000;             // one decision per bot per this long inside the battleground
        uint32 OffensePct = 40;           // share of a side that goes for the enemy flag
        uint32 DefensePct = 25;           // share of a side that stays at its own flag room
        float EngageYards = 28.0f;        // an enemy player this close is attacked
        float DefendYards = 15.0f;        // defenders stand within this distance of their flag room
        float EscortYards = 6.0f;         // escorts keep this close to their flag carrier
        float ReturnYards = 60.0f;        // a bot at most this far from a dropped flag of its side goes to take it back
    };

    // 0 = Alliance, 1 = Horde (TEAM_ALLIANCE / TEAM_HORDE).
    constexpr uint8 TEAMS = 2;

    // ---- queues -----------------------------------------------------------------------------------------------------------------
    struct QueueFacts
    {
        uint32 HumansWaiting[TEAMS] = { 0, 0 };   // players in the queue who have not been invited yet
        uint32 BotsQueued[TEAMS] = { 0, 0 };      // bots in the queue, invited or not
        uint32 LongestHumanWaitSec = 0;           // the oldest waiting player
        uint32 MinPerTeam = 5, MaxPerTeam = 10;   // of the battleground
        uint32 NoHumanSec = 0;                    // how long the queue has had no waiting player (0 while there is one)
    };

    struct QueueNeed
    {
        uint32 Add[TEAMS] = { 0, 0 };             // bots to put into the queue, per side
        bool DropBots = false;                    // the bots still waiting should leave the queue
    };

    // How many bots join each side. Nothing while no player waits or before JoinDelaySec; a side is filled to FillTo (clamped to the
    // battleground's min..max, FillTo = 0 means min) counting players and bots; at most JoinPerStepMax per look and MaxBotsPerQueue in all.
    TC_GAME_API QueueNeed PlanQueue(QueueFacts const& f, Config const& cfg);

    struct Candidate
    {
        uint64 Guid = 0;
        uint8 Team = 0;                           // 0 Alliance, 1 Horde
        uint8 Level = 0;
    };

    // The bots to queue: per side the first `need` candidates of that side in an order that depends on `seed` and the bot only
    // (so one bot is not always the first and the choice is stable inside one look).
    TC_GAME_API std::vector<uint64> PickBots(std::vector<Candidate> const& cands, uint32 const (&need)[TEAMS], uint64 seed);

    // Milliseconds this bot waits before it accepts an invitation, and before it leaves a finished battleground (per-bot stable).
    TC_GAME_API uint32 AcceptDelayMs(Config const& cfg, uint64 botKey);
    TC_GAME_API uint32 LeaveDelayMs(Config const& cfg, uint64 botKey);

    // ---- play -------------------------------------------------------------------------------------------------------------------
    enum class Role : uint8 { Offense, Defense, Roam };

    // The jobs of a side: `rank` is the bot's place (0-based) in its side's bots sorted by guid, `count` the bots on the side.
    // The first OffensePct of a side attack, the next DefensePct defend, the rest roam; a side always has an attacker and a side of 3 or
    // more also a defender.
    TC_GAME_API Role RoleFor(uint32 rank, uint32 count, Config const& cfg);

    // Values of GameObject FlagState (InBase = 1 ...), None when the flag object was not found.
    enum class Flag : uint8 { None = 0, InBase = 1, Taken = 2, Dropped = 3, Respawning = 4 };

    struct Facts
    {
        bool InProgress = false;          // gates open, the match runs
        bool Alive = true;
        Role MyRole = Role::Roam;
        bool IAmCarrier = false;          // carrying the enemy flag
        bool TeamCarrier = false;         // another bot or player of the side carries the enemy flag
        Flag OwnFlag = Flag::None;        // the flag of the bot's side
        Flag EnemyFlag = Flag::None;
        float DistOwnDropped = -1.0f;     // to the dropped own flag, < 0 when it lies nowhere
        float DistEnemyDropped = -1.0f;   // to the dropped enemy flag
        float DistOwnRoom = 0.0f;         // to the own flag room (where a carried flag is scored)
        float DistCarrier = -1.0f;        // to the side's flag carrier (another one than the bot), < 0 when none
        bool EnemyCarrierSeen = false;    // the enemy carries our flag and the bot knows where
        uint32 Teammates = 1;             // bots and players of the side in the battleground
    };

    enum class Order : uint8
    {
        Hold,           // gates closed or nothing to do: stay where you are
        GetEnemyFlag,   // walk to the enemy flag room and take the flag
        PickUpFlag,     // take the enemy flag lying on the ground (the carrier died)
        CarryHome,      // run the flag to the own flag room
        WaitHome,       // own flag is away: wait in the own flag room with the flag for it to come back
        ReturnOwnFlag,  // touch the own flag lying on the ground to send it back
        HuntCarrier,    // kill the enemy flag carrier
        Escort,         // stay with the side's flag carrier
        Defend,         // stay at the own flag room
        Roam            // go to the middle of the field
    };

    // What the bot does next. The first line that applies wins:
    //   dead / gates closed -> Hold; carrier -> CarryHome (own flag in base) or WaitHome; own flag lying near -> ReturnOwnFlag;
    //   enemy flag lying near -> PickUpFlag (offense, roam); own flag taken -> HuntCarrier (defense, roam, and offense when the
    //   enemy flag is not in reach); a side member carries -> Escort (offense, roam) or Defend; offense -> GetEnemyFlag; defense -> Defend;
    //   roam -> Roam.
    TC_GAME_API Order PlanStep(Facts const& f, Config const& cfg);

    // True when the bot attacks an enemy player at `enemyYards` right now (it may already be fighting). Carriers and bots that are
    // about to touch a dropped flag fight only what is almost on them; others fight what comes within EngageYards.
    TC_GAME_API bool ShouldEngage(Order order, float enemyYards, Config const& cfg);

    // A standing point around `center`: a ring of radius [minYd, maxYd] at a bearing taken from the bot, stable for ~20 s.
    struct Point
    {
        float X = 0.0f, Y = 0.0f;
    };
    TC_GAME_API Point RingPoint(float cx, float cy, float minYd, float maxYd, uint64 botKey, uint32 nowMs);

    // Short name of an order for log rows.
    TC_GAME_API char const* OrderName(Order order);
}

#endif
