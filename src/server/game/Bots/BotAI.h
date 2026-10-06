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

#ifndef TRINITY_BOT_AI_H
#define TRINITY_BOT_AI_H

#include "BotBehavior.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include <array>
#include <atomic>
#include <optional>
#include <string>

struct BotAIConfig
{
    bool Enabled = true;          // Bot.AI.Enabled (also switchable at runtime with `bot ai on|off`)
    uint32 TickMs = 500;          // Bot.AI.TickMs
    bool TestStrategy = false;    // Bot.AI.TestStrategy: new bots get the built-in test strategies
    uint32 TestIdleSec = 30;      // Bot.AI.Test.IdleSec
    uint32 TestCombatSec = 5;     // Bot.AI.Test.CombatSec
    // Phase 3 behaviors
    std::string DefaultNonCombat = "rest,goto,follow"; // Bot.AI.Default.NonCombat (comma separated strategy names)
    std::string DefaultCombat;                         // Bot.AI.Default.Combat
    std::string DefaultDead = "recover";               // Bot.AI.Default.Dead
    uint32 EatBelowPct = 60;      // Bot.AI.Rest.EatBelowPct: start eating below this health percent (out of combat)
    uint32 DrinkBelowPct = 40;    // Bot.AI.Rest.DrinkBelowPct: start drinking below this mana percent
    uint32 RestDonePct = 95;      // Bot.AI.Rest.DonePct: stop resting at this percent
    bool FreeFood = true;         // Bot.AI.Rest.FreeFood: eat/drink without carrying items (until the economy phase), see engine-design.md
    uint32 ReleaseMinSec = 3;     // Bot.AI.Release.MinDelaySec / MaxDelaySec: delay between death and releasing the spirit
    uint32 ReleaseMaxSec = 8;
    uint32 MaxCorpseRunYards = 1200; // Bot.AI.Recover.MaxCorpseRunYards: farther corpses use the spirit healer
    uint32 StuckSec = 8;          // Bot.AI.Move.StuckSec: seconds without progress before a stuck episode
    uint32 StuckRepaths = 3;      // Bot.AI.Move.StuckRepaths: episodes (each re-issues the path) before the goal is given up
};

// Process-wide counters (written by map threads, relaxed atomics).
struct BotAIStats
{
    std::atomic<uint64> Ticks{0};         // AI ticks run (one per bot per TickMs)
    std::atomic<uint64> TickNs{0};        // wall time spent inside ticks
    std::atomic<uint64> MaxTickNs{0};
    std::atomic<uint64> LogNs{0};         // part of TickNs spent building and queueing log events
    std::atomic<uint64> PosNs{0};         // wall time spent in SamplePosition (position telemetry, part of TickNs)
    std::atomic<uint64> PosSamples{0};    // bot_pos samples queued
    std::atomic<uint64> ActionsRun{0};
    std::atomic<uint64> StateChanges{0};
    std::atomic<uint64> Events{0};        // log events produced by the engine
};

// One per bot, owned by the bot's WorldSession (so it dies with the session), ticked from Player::Update on the map thread.
class TC_GAME_API BotAI
{
public:
    // --- configuration and global state ---
    static BotAIConfig const& Config();   // loads worldserver.conf values on first use
    static bool IsEnabled();
    static void SetEnabled(bool on);
    static BotAIStats& Stats();
    static void SetTraceAll(bool on);     // also applies to bots created later
    static bool GetTraceAll();

    // AI factory: creates the AI and applies the default strategy set for each state. Never null.
    static std::unique_ptr<BotAI> Create(Player* bot);

    explicit BotAI(Player* bot);
    ~BotAI(); // never touches the Player

    // Map thread, from Player::Update (only for bots, only while in world). Accumulates time and ticks every Bot.AI.TickMs.
    void Update(Player* bot, uint32 diff);

    // --- control (world thread, outside map updates; see engine-design.md) ---
    bool AddStrategy(Player* bot, std::string const& name, char const* source = "command");
    bool RemoveStrategy(Player* bot, std::string const& name, char const* source = "command");
    std::vector<std::string> DescribeStrategies() const; // one line per engine
    void SetForcedState(std::optional<BotState> state) { _forced = state; _forcedChanged = true; }
    void SetTrace(bool on) { _trace = on; }
    bool IsTrace() const { return _trace; }
    BotState GetState() const { return _state; }
    std::optional<BotState> GetForcedState() const { return _forced; }
    uint64 GetGuid() const { return _guid; }

    // Phase 3 per-bot behavior state (map thread during ticks; the goal/follow setters from console commands on the world thread)
    BotMotion& Motion() { return _motion; }
    BotRest& Rest() { return _rest; }
    BotRecover& Recover() { return _recover; }
    // Logs an event of any type for this bot (stuck, path_fail, decision, ...), position and level filled in from the bot.
    void EmitEvent(Player* bot, char const* type, uint8 severity, std::string reason, std::string summary, std::string detailsJson = std::string());

    // --- for engine objects (valid during a tick or a control call) ---
    Player* GetTickBot() const { return _tickBot; }
    uint32 GetNowMs() const { return _nowMs; }
    uint32 GetIdleMs() const { return _nowMs - _lastActionMs; }
    uint32 GetStateAgeMs() const { return _nowMs - _stateSinceMs; }
    Trigger* GetTrigger(std::string const& name);
    Action* GetAction(std::string const& name);
    Multiplier* GetMultiplier(std::string const& name);
    UntypedValue* GetValueRaw(std::string const& name);
    template<typename T>
    Value<T>* GetValue(std::string const& name) { return dynamic_cast<Value<T>*>(GetValueRaw(name)); }

    // Called by BotEngine after it executed an action.
    void OnActionExecuted(BotState engine, Action& action, Trigger* trigger, float baseRelevance, float effectiveRelevance,
        std::vector<std::string> const& alternatives);
    // Trace-only record of one evaluation (what fired, what was considered). Cheap no-op unless the trace switch is on.
    void TraceEvaluation(BotState engine, std::string const& summary, std::string const& detailsJson);
    // Engine-level warning (unknown name etc.), logged once per name.
    void WarnOnce(std::string const& key, std::string const& message);

    BotEvent MakeEvent(Player* bot, char const* type, uint8 severity, std::string reason, std::string summary) const;

private:
    void Tick(Player* bot);
    BotState DesiredState(Player* bot, char const*& cause) const;
    void SamplePosition(Player* bot);  // bot_pos telemetry, see docs/playerbots/progress.md
    void ChangeState(Player* bot, BotState to, char const* cause); // the ONLY place the engine state changes
    void Emit(BotEvent&& event);
    std::string RecentDecisionsJson() const;

    struct Decision { uint32 Ms; Action const* Act; char Reason[24]; };
    static constexpr uint32 DECISION_RING = 8;

    uint64 _guid;
    Player* _tickBot = nullptr;
    uint32 _phaseMs;          // random per-bot phase so bots do not tick on the same world tick
    uint32 _lastBucket;
    uint32 _nowMs = 0;        // AI clock: sum of diffs while the bot is in world
    uint32 _lastActionMs = 0;
    uint32 _stateSinceMs = 0;
    uint16 _lastMapId = 0xFFFF;
    bool _posSampled = false;       // a sample was taken since login
    uint16 _posMapId = 0, _posZoneId = 0;
    uint32 _posLastMs = 0;
    float _posX = 0.0f, _posY = 0.0f, _posZ = 0.0f;
    BotState _state = BotState::NonCombat;
    bool _started = false;
    bool _trace;
    bool _forcedChanged = false;
    std::optional<BotState> _forced;

    std::array<std::unique_ptr<BotEngine>, BOT_STATE_COUNT> _engines;
    std::unordered_map<std::string, std::unique_ptr<Trigger>> _triggers;
    std::unordered_map<std::string, std::unique_ptr<Action>> _actions;
    std::unordered_map<std::string, std::unique_ptr<Multiplier>> _multipliers;
    std::unordered_map<std::string, std::unique_ptr<UntypedValue>> _values;
    std::vector<std::string> _warned;

    BotMotion _motion;
    BotRest _rest;
    BotRecover _recover;

    std::array<Decision, DECISION_RING> _ring{};
    uint32 _ringNext = 0;
    uint32 _ringCount = 0;
};

#endif
