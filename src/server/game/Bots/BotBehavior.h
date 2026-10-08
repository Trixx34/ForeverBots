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
#include "BotMovePlan.h"
#include <optional>
#include <string>
#include <vector>

class BotAI;
class Creature;
class Player;

// Aggro awareness (Bot.AI.AggroAvoid.*): per-bot cache of the nearby hostile elites and higher-level mobs, refreshed at most once per
// second and shared by walking, resting and the target chooser.
struct BotAggro
{
    struct Threat { ObjectGuid Guid; uint32 Entry = 0; uint8 Level = 0; bool Elite = false; float Aggro = 0.0f; };
    std::vector<Threat> List;
    uint32 ScanMs = 0;
    bool Scanned = false;
    uint32 LogMs = 0;     // last AGGRO_AVOID row (per-bot rate cap)
    uint32 SinceMs = 0;   // the current goal has been held back since (0 = not held back)
    uint32 ClearMs = 0;   // since when no threat is near (resets SinceMs after a few seconds)
};

// The threat that is deepest inside its aggro radius plus margin.
struct BotAggroHit
{
    Creature* Mob = nullptr;
    uint32 Entry = 0;
    uint8 Level = 0;
    bool Elite = false;
    float Dist = 0.0f;    // 3D distance bot to mob
    float Aggro = 0.0f;   // the mob's aggro radius for this bot
    float Radius = 0.0f;  // Aggro + margin
};

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
    // R5: quest travel context for the log rows of this goal (BotQuest calls it after SetGoal with the "quest" tag).
    void SetQuestCtx(uint32 questId, uint32 entry, char const* task = nullptr) { _questId = questId; _questEntry = entry; _task = task; }

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

    // Natural movement (Bot.AI.Move.Natural.*): a pause stops walking and quest work until it ends (arrival, loot, quest giver, after combat).
    void Pause(uint32 nowMs, uint32 ms) { if (ms) _pauseUntil = nowMs + ms; }
    bool IsPaused(uint32 nowMs) const { return _pauseUntil && int32(_pauseUntil - nowMs) > 0; }
    // map thread, once per AI tick: samples the movement metrics and writes MOVE_METRICS rows
    void Tick(BotAI* ai, Player* bot);
    BotMove::Metrics& Metrics() { return _metrics; }
    // natural idling (strategy "natural_idle", NonCombat): look around, sit, wander a few yards; false when nothing was done
    bool IdleStep(BotAI* ai, Player* bot);

    BotAggro& Aggro() { return _aggro; }
    uint32 QuestEntry() const { return _questEntry; } // entry of the last quest/grind travel target (not a threat to avoid)

private:
    // Aggro steering of goto/quest goals (detour, back off, wait outside the aggro radius); nullopt = walk on normally.
    std::optional<Result> Steer(BotAI* ai, Player* bot, uint32 now);
    Result Fail(BotAI* ai, Player* bot, char const* type, char const* reason, std::string const& summary, std::string const& extra);
    enum class Launch : uint8 { Skipped, NoPath, Launched };
    Launch Issue(Player* bot, uint32 now);
    // natural movement helpers
    std::optional<Result> NaturalReissue(BotAI* ai, Player* bot, uint32 now, bool moving);
    bool NaturalStuck(BotAI* ai, Player* bot, uint32 now);
    void StartSideRoute(Player* bot, uint32 now, float yards);

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
    uint32 _lastFailMs = 0;     // identical failure rows (same tag and reason) are written once per minute
    uint32 _lastFailKey = 0;
    bool RepeatFail(char const* reason, uint32 now);
    void EmitMotion(BotAI* ai, Player* bot, char const* type, uint8 severity, char const* reason, std::string const& summary, std::string const& details);
    uint32 _questId = 0, _questEntry = 0;
    char const* _task = nullptr; // static string: quest task kind of the goal (log context)
    uint32 _lastWalkLogMs = 0;  // quest walk start rows: at most one per 10 s
    bool _walkLogged = false;   // a QUEST_WALK_START row was written for this goal (arrival is logged only then)
    uint32 _pathUs = 0;         // microseconds of the last path query of this goal
    ObjectGuid _follow;
    BotAggro _aggro;
    bool _detour = false;       // walking to a detour point instead of the goal
    float _dx = 0.0f, _dy = 0.0f, _dz = 0.0f;
    uint32 _detourMs = 0;

    // natural movement state (all untouched while the option is off)
    float _ox = 0.0f, _oy = 0.0f;       // approach offset added to the goal for the path target
    uint32 _pauseUntil = 0;
    uint32 _lastEndMs = 0;              // when the previous goal ended (a start delay only follows a real stop)
    uint32 _waitUntilMs = 0;            // back-off after a failed or repeated leg
    uint32 _legFails = 0;               // consecutive failed or repeated legs of this goal
    bool _sideOn = false;
    float _sx = 0.0f, _sy = 0.0f, _sz = 0.0f;
    uint32 _sideMs = 0;
    uint32 _trailMs = 0;
    BotMove::Vec3 _trail[4];            // positions sampled every 2 s while walking (step back goes along these)
    uint32 _trailN = 0;
    BotMove::FailTable _fails{12};
    BotMove::LegTracker _legs;
    BotMove::Metrics _metrics;
    // natural idling
    float _idleX = 0.0f, _idleY = 0.0f;
    uint32 _idleStillSince = 0, _idleLastAct = 0, _idleSatMs = 0;
    bool _idleSat = false;
};

// Hook for the danger gating of destinations (under-level bots, deadly areas). The movement code itself does not decide what is
// dangerous: a module that does installs a function here; idle wandering and the loot spawn choice ask it. Default: nothing is vetoed.
using BotDestinationVeto = bool (*)(Player* bot, uint32 mapId, float x, float y, float z);
void BotSetDestinationVeto(BotDestinationVeto fn);
bool BotDestinationVetoed(Player* bot, float x, float y, float z);

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
    uint32 CorpseFails = 0;      // failed corpse-run goals this death
    uint32 HealerFails = 0;      // failed spirit healer walks this death
    bool ReleaseFailedLogged = false;
    bool WaitLogged = false;
    bool NoHealerLogged = false;
    bool HardcoreLogged = false;
    ObjectGuid Healer;
    float HealerX = 0.0f, HealerY = 0.0f, HealerZ = 0.0f;
    void Reset() { *this = BotRecover(); }
};

// Hostile elite or higher-level mob deepest inside its aggro radius plus margin around the bot (false when avoidance is off or none
// is near). A mob of `ignoreEntry` (the current kill target) does not count.
bool BotAggroNear(BotAI* ai, Player* bot, uint32 ignoreEntry, BotAggroHit& out);
// True when a mob more dangerous than `target` stands close to it (the target chooser skips such targets, logged as AGGRO_AVOID).
bool BotAggroGuarded(BotAI* ai, Player* bot, Creature const* target);
// One AGGRO_AVOID decision row, at most one per bot per Bot.AI.AggroAvoid.LogIntervalSec.
void BotAggroLog(BotAI* ai, Player* bot, char const* action, BotAggroHit const& hit, char const* tag);

// Ends the eat/drink session (stand up when `standUp`, removes the food aura). Defined in BotBehavior.cpp.
void BotEndRest(BotAI* ai, Player* bot, char const* reason, bool standUp = true);

#endif
