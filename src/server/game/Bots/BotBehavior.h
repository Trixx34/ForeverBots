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

#ifndef TRINITY_BOT_BEHAVIOR_H
#define TRINITY_BOT_BEHAVIOR_H

// Per-bot state of the Phase 3 behaviors (movement goal, rest session, death recovery), see docs/playerbots/engine-design.md.
// Plain data owned by BotAI; the strategies/triggers/actions that use it live in BotBehavior.cpp. All of it is touched on the
// map thread during a tick, except the goal/follow setters which are called by console commands on the world thread
// (outside map updates, same rule as the strategy commands).

#include "Define.h"
#include <G3D/Vector3.h>
#include "ObjectGuid.h"
#include <string>

class BotAI;
class Player;

// Result of a one-off pathfinding query (used to classify goals and corpse runs before moving).
struct BotPathInfo
{
    bool Valid = false;        // a usable path (possibly partial) exists
    bool NoPath = false;       // PATHFIND_NOPATH
    bool MmapMissing = false;  // no navmesh for the map (straight line only)
    bool OffMesh = false;      // the start or the destination is not on the navmesh (water, inside terrain)
    bool Partial = false;      // PATHFIND_INCOMPLETE
    float Length = 0.0f;       // path length in yards
    G3D::Vector3 End;          // actual end of the computed path (navmesh height)
    float EndGap = 0.0f;       // distance between the path end and the requested target (2D)
    float EndGap3D = 0.0f;     // same in 3D (a ledge above or a floor below the goal shows only here)
    bool StartOffMesh = false; // NOPATH because the bot itself stands where there is no navmesh (no polygon within reach)
    bool GoalOffMesh = false;  // the destination is more than 7 yards (3D) from the nearest navmesh polygon (path is partial or absent)
    uint32 Type = 0;           // raw PathType bits
};

class BotMotion
{
public:
    enum class Result : uint8 { Idle, Moving, Arrived, Failed };

    // Sets a destination on the bot's current map. The next Step() call logs GOTO_START and starts moving.
    void SetGoal(uint32 mapId, float x, float y, float z, float arriveDist, char const* tag);
    void ClearGoal() { _active = false; }
    bool HasGoal() const { return _active; }
    char const* GetTag() const { return _tag; }
    float GoalX() const { return _x; }
    float GoalY() const { return _y; }
    float GoalZ() const { return _z; }

    // Follow target (a player or bot on the same map). Guid empty = none.
    void SetFollow(ObjectGuid guid) { _follow = guid; }
    ObjectGuid GetFollow() const { return _follow; }

    // Map thread: one movement step (arrival check, path start/re-issue, stuck detection). Emits GOTO_START, GOTO_ARRIVED and the
    // stuck/path_fail events. Returns Moving while the goal is active, Arrived/Failed once (the goal is then cleared).
    Result Step(BotAI* ai, Player* bot);

    // Stops the current movement (the goal stays unless clearGoal).
    static void Halt(Player* bot);

    static BotPathInfo QueryPath(Player* bot, float x, float y, float z);
    static void EnsureGrids(Player* bot, float x, float y);

private:
    Result Fail(BotAI* ai, Player* bot, char const* type, char const* reason, std::string const& summary, std::string const& extra);
    void Issue(Player* bot, uint32 now);

    bool _active = false;
    bool _fresh = false;
    uint32 _mapId = 0;
    float _x = 0.0f, _y = 0.0f, _z = 0.0f, _arrive = 3.0f;
    char _tag[24] = "goto";

    uint32 _startMs = 0, _issueMs = 0, _lastStepMs = 0, _bestMs = 0;
    float _bestDist = 0.0f;
    uint32 _issues = 0;
    uint32 _episodes = 0;       // stuck episodes of this goal
    bool _everMoved = false;
    ObjectGuid _follow;
};

// Eat/drink session of a bot (rest strategy).
struct BotRest
{
    enum : uint8 { EAT = 1, DRINK = 2 };
    uint8 Bits = 0;              // active kinds
    uint32 SinceMs = 0;
    uint32 FoodSpell = 0, DrinkSpell = 0;
    uint8 FoodCasts = 0, DrinkCasts = 0;
    bool Resting() const { return Bits != 0; }
};

// Death recovery of a bot (dead engine, `recover` strategy).
struct BotRecover
{
    enum class Mode : uint8 { None, CorpseRun, Healer };
    uint32 DiedMs = 0;           // AI clock at death
    uint32 ReleaseDelayMs = 0;   // random delay before releasing the spirit
    Mode Plan = Mode::None;
    char PlanReason[32] = "";
    uint32 NextTryMs = 0;
    uint32 Attempts = 0;
    bool ReleaseFailedLogged = false;
    bool WaitLogged = false;
    bool NoHealerLogged = false;
    bool HardcoreLogged = false;
    ObjectGuid Healer;
    float HealerX = 0.0f, HealerY = 0.0f, HealerZ = 0.0f;
    void Reset() { *this = BotRecover(); }
};

// Ends the eat/drink session (stand up when `standUp`, removes the food aura). Defined in BotBehavior.cpp.
void BotEndRest(BotAI* ai, Player* bot, char const* reason, bool standUp = true);

#endif
