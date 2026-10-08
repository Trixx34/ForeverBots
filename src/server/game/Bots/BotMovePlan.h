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

#ifndef TRINITY_BOT_MOVE_PLAN_H
#define TRINITY_BOT_MOVE_PLAN_H

// Natural-looking movement for the bots (Bot.AI.Move.Natural.*): decision logic as pure functions over plain data, used by BotMotion
// (BotBehavior.cpp) and the idle behavior. No Player, no map: line-of-sight and path queries come in as callbacks. Randomness is a
// hash of (bot, target, attempt) so every result is reproducible in tests and two bots never share a roll.
// See docs/playerbots/feature-bot-pets-movement-loot-20261008.md.

#include "Define.h"
#include <functional>
#include <vector>

namespace BotMove
{
    // Bot.AI.Move.Natural.*: every part is off unless Enabled is set (default off); read once by BotBehavior.cpp.
    struct NaturalConfig
    {
        bool Enabled = false;
        bool Shortcut = true;        // string pulling of the computed path
        bool RoundCorners = true;    // chamfered corners and a smooth spline
        bool SpeedVariation = true;  // slow drift of the walking speed per bot
        bool Pacing = true;          // start delay, arrival pause, pauses after combat, loot and quest givers
        bool Spread = true;          // per-bot offset of the approach point
        bool Idle = true;            // look around, sit, wander while nothing else is to do
        bool Metrics = true;         // MOVE_METRICS rows
        uint32 MetricsSec = 60;
        uint32 IdleMaxStandSec = 25;
        uint32 MaxShortcutYards = 45;
    };
    TC_GAME_API NaturalConfig const& Natural();

    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    TC_GAME_API float Dist2D(Vec3 const& a, Vec3 const& b);
    TC_GAME_API float Dist3D(Vec3 const& a, Vec3 const& b);
    TC_GAME_API float PathLength(std::vector<Vec3> const& path);
    // Angle in degrees (0..180) between the direction a->b and b->c; 0 = straight on.
    TC_GAME_API float TurnAngleDeg(Vec3 const& a, Vec3 const& b, Vec3 const& c);

    // splitmix64 over the three keys: the one source of "randomness" of this module, uniform in [0,1) after Unit()
    TC_GAME_API uint64 Mix(uint64 a, uint64 b = 0, uint64 c = 0);
    inline float Unit(uint64 h) { return float(h >> 40) / float(1ull << 24); }
    inline float Range(uint64 h, float lo, float hi) { return lo + (hi - lo) * Unit(h); }

    // true when a walker can go straight from a to b (ground contact, no obstacle); supplied by the caller
    using LosFn = std::function<bool(Vec3 const&, Vec3 const&)>;

    // --- smooth paths ---
    struct ShortcutConfig
    {
        float MaxSegment = 45.0f;     // longest straight leg created by a shortcut
        uint32 Lookahead = 10;        // how many path points ahead are tried
    };
    // String pulling: removes path points whose neighbours see each other. First and last point are kept. A path of two points or
    // fewer comes back unchanged.
    TC_GAME_API std::vector<Vec3> ShortcutPath(std::vector<Vec3> const& path, LosFn const& los, ShortcutConfig const& cfg = {});

    struct CornerConfig
    {
        float MinTurnDeg = 25.0f;     // corners sharper than this are rounded
        float MaxCut = 3.5f;          // yards cut from each side of a corner
        float CutFraction = 0.4f;     // never more than this fraction of the adjoining leg
    };
    // Replaces each sharp interior corner by two points on its legs (a chamfer), so the bot turns over a few yards instead of snapping.
    // A chamfer is only used when the new short leg passes the line-of-sight check.
    TC_GAME_API std::vector<Vec3> RoundCorners(std::vector<Vec3> const& path, LosFn const& los, CornerConfig const& cfg = {});

    // --- approach points and spread ---
    struct ApproachParams
    {
        uint64 BotKey = 0;
        uint64 TargetKey = 0;
        uint32 Attempt = 0;
        float Radius = 2.0f;          // largest offset from the target point, yards
        float BearingToBot = 0.0f;    // radians: direction from the target towards where the bot comes from
        float ArcRad = 2.0f;          // total width of the arc the offset may fall in, centred on BearingToBot
    };
    // Offset (dx, dy) from the target point. Same inputs give the same offset; another bot, target or attempt gives another one.
    TC_GAME_API Vec3 ApproachOffset(ApproachParams const& p);

    // --- pacing ---
    enum class Pause : uint8 { StartDelay, Arrive, QuestGiver, Loot, PostCombat, Look };
    // Duration of a pause in ms; `salt` makes repeated pauses of one bot differ.
    TC_GAME_API uint32 PauseMs(Pause kind, uint64 botKey, uint32 salt);
    // Speed factor around 1.0 (amplitude = 0.04 means 0.96..1.04) that drifts slowly: constant within an epoch, blended across epochs.
    TC_GAME_API float SpeedFactor(uint64 botKey, uint32 nowMs, float amplitude, uint32 epochMs = 45000);

    // --- failed paths ---
    enum class Reroute : uint8 { Retry, SideRoute, Wait, GiveUp };
    // What a mover does after `fails` consecutive failed or stalled attempts at the same leg.
    TC_GAME_API Reroute AdviceFor(uint32 fails);
    // Delay before the next attempt: base * 2^(fails-1), capped, +-25% jitter per bot.
    TC_GAME_API uint32 BackoffMs(uint32 fails, uint64 botKey, uint64 legKey, uint32 baseMs = 1500, uint32 capMs = 30000);

    // Small table of goals that recently failed, keyed by a hash of the (rounded) goal; bounded, no allocation after reserve.
    class TC_GAME_API FailTable
    {
    public:
        struct Entry { uint64 Key = 0; uint32 Fails = 0; uint32 UntilMs = 0; uint32 LastMs = 0; };
        explicit FailTable(uint32 capacity = 12) : _cap(capacity) { _e.reserve(capacity); }
        bool Blocked(uint64 key, uint32 nowMs) const;
        uint32 Fails(uint64 key) const;
        uint32 UntilMs(uint64 key) const;
        // records a failure and returns the wait until the next attempt
        uint32 Fail(uint64 key, uint32 nowMs, uint64 botKey);
        void Success(uint64 key);
        void Clear() { _e.clear(); }
        size_t Size() const { return _e.size(); }
    private:
        uint32 _cap;
        std::vector<Entry> _e;
    };

    // Hash of a leg (rounded start and goal) for FailTable and repeated-leg counting.
    TC_GAME_API uint64 LegKey(Vec3 const& from, Vec3 const& to, float cell = 3.0f);

    // Counts the same leg being issued again within a short window (the "same leg every 400 ms" loop).
    class TC_GAME_API LegTracker
    {
    public:
        // returns how many times this leg was already issued inside the window (0 = first time)
        uint32 Issue(uint64 legKey, uint32 nowMs, uint32 windowMs = 8000);
        uint32 Repeats() const { return _repeats; }
        void Reset() { _n = 0; _repeats = 0; }
    private:
        struct Rec { uint64 Key; uint32 FirstMs; uint32 Count; };
        Rec _r[6] = {};
        uint32 _n = 0;
        uint32 _repeats = 0;
    };

    // --- stuck handling ---
    enum class StuckAct : uint8 { StepBack, SideRoute, GiveUp };
    struct StuckPlan
    {
        StuckAct Act = StuckAct::GiveUp;
        uint32 LookPauseMs = 0;       // look around (turn, stand) for this long first
        float TurnRad = 0.0f;         // facing change while looking, radians (signed)
        float StepYards = 0.0f;       // StepBack: how far to go back along the path just walked
        float SideYards = 0.0f;       // SideRoute: lateral offset of the intermediate point (signed)
    };
    // episode counts from 1. Episode 1 looks around and steps back, 2 tries a different route, later ones give up.
    TC_GAME_API StuckPlan PlanStuck(uint32 episode, uint64 botKey, uint32 nowMs);

    // --- idle behavior ---
    enum class IdleAct : uint8 { Stay, Look, Wander, Sit, StandUp };
    struct IdleFacts
    {
        uint32 StillMs = 0;           // standing without any movement or action
        uint32 SinceActionMs = 0;     // since the last idle action was started
        bool Sitting = false;
        uint32 SittingMs = 0;
        bool Threat = false;          // anything hostile or an aggro warning nearby
        bool HurtOrDrained = false;   // below full health or mana: sitting is worth it
    };
    struct IdleConfig
    {
        uint32 MaxStillMs = 25000;    // standing still longer than this forces a wander
        uint32 MinGapMs = 3000;       // least time between two idle actions
        uint32 SitMinMs = 6000, SitMaxMs = 20000;
        float WanderMin = 3.0f, WanderMax = 9.0f;
    };
    struct IdlePlan
    {
        IdleAct Act = IdleAct::Stay;
        float TurnRad = 0.0f;         // Look: facing change, radians (signed)
        float WanderYards = 0.0f;
        float WanderBearing = 0.0f;   // radians, absolute
    };
    TC_GAME_API IdlePlan PlanIdle(IdleFacts const& f, IdleConfig const& cfg, uint64 botKey, uint32 nowMs);

    // --- metrics for the sim ---
    struct MetricsSnapshot
    {
        uint32 WindowMs = 0;
        uint32 MovingMs = 0;
        uint32 IdleMs = 0;
        float IdleRatio = 0.0f;           // idle / window
        float DistanceYd = 0.0f;
        float TurnRateDegPerSec = 0.0f;   // summed absolute heading change per second of movement
        uint32 SharpTurns = 0;            // heading changes of 60 degrees or more between two samples
        uint32 RepeatedLegs = 0;
        uint32 StuckEvents = 0;
        uint32 PathFails = 0;
        uint32 LongestIdleMs = 0;
    };
    class TC_GAME_API Metrics
    {
    public:
        // one sample per AI tick; `moving` = the bot walks
        void Sample(uint32 nowMs, float x, float y, bool moving);
        void NoteStuck() { ++_stuck; }
        void NotePathFail() { ++_pathFails; }
        void NoteRepeatedLeg() { ++_repeated; }
        bool Due(uint32 nowMs, uint32 windowMs) const { return _startMs && nowMs - _startMs >= windowMs; }
        // snapshot of the window so far; the window restarts
        MetricsSnapshot Take(uint32 nowMs);
    private:
        uint32 _startMs = 0, _lastMs = 0;
        uint32 _moving = 0, _idle = 0, _idleRun = 0, _longestIdle = 0;
        float _dist = 0.0f, _turnSum = 0.0f;
        uint32 _sharp = 0, _stuck = 0, _pathFails = 0, _repeated = 0;
        bool _have = false, _haveHeading = false;
        float _px = 0.0f, _py = 0.0f, _heading = 0.0f;
    };
}

#endif
