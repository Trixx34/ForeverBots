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

#ifndef TRINITY_BOT_ENGINE_H
#define TRINITY_BOT_ENGINE_H

// Bot AI engine core (Phase 2), see docs/playerbots/engine-design.md.
// Strategy -> Trigger -> Action, plus Values (cached calculations) and Multipliers (scale relevance).
// Everything here runs on the map thread that updates the bot's Player (except registry construction).

#include "Define.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class BotAI;
class Player;

// The three engines of a bot. Exactly one is active; BotAI::ChangeState is the only place that switches.
enum class BotState : uint8
{
    NonCombat = 0,
    Combat    = 1,
    Dead      = 2
};
constexpr uint32 BOT_STATE_COUNT = 3;
constexpr uint32 BotStateBit(BotState s) { return 1u << uint32(s); }
constexpr uint32 BOT_STATES_ALL = 7;
char const* BotStateName(BotState state);

// Named relevance bands. The highest relevance (after multipliers) runs first.
namespace BotRelevance
{
    constexpr float Default   = 5.0f;
    constexpr float Normal    = 10.0f;
    constexpr float High      = 20.0f;
    constexpr float Move      = 30.0f;
    constexpr float Rest      = 35.0f; // eat/drink outranks walking (the rest hold multiplier then blocks movement)
    constexpr float Interrupt = 40.0f;
    constexpr float Dispel    = 50.0f;
    constexpr float Raid      = 60.0f;
    constexpr float Emergency = 90.0f;
    constexpr float PullLow   = 105.0f;
    constexpr float PullMid   = 106.0f;
    constexpr float PullHigh  = 107.0f;
    char const* BandName(float relevance); // name of the highest band not above the value, for logs
}

enum BotActionFlags : uint32
{
    ACTION_FLAG_NONE  = 0,
    ACTION_FLAG_NOISY = 0x1,  // chatty/cosmetic, suppressed by quiet-style multipliers
    ACTION_FLAG_QUIET_LOG = 0x2, // executing it is only logged when the bot trace switch is on (it reports its own transitions)
    ACTION_FLAG_MOVES = 0x4     // starts or continues walking; multipliers (stay, resting) can forbid it
};

// Base of everything a bot AI instantiates: knows its name and its BotAI.
class BotAiObject
{
public:
    BotAiObject(BotAI* ai, char const* name) : _ai(ai), _name(name) { }
    virtual ~BotAiObject() = default;
    BotAiObject(BotAiObject const&) = delete;
    BotAiObject& operator=(BotAiObject const&) = delete;

    std::string const& GetName() const { return _name; }
    BotAI* GetAI() const { return _ai; }
    Player* GetBot() const; // valid only during a tick

    // Called when the active engine changes (all cached objects of the bot get it).
    virtual void OnStateEnter() { }

private:
    BotAI* _ai;
    std::string _name;
};

// A cached calculation. Get() recalculates at most every checkIntervalMs of AI time.
class UntypedValue : public BotAiObject
{
public:
    UntypedValue(BotAI* ai, char const* name, uint32 checkIntervalMs) : BotAiObject(ai, name), _interval(checkIntervalMs) { }
    void Invalidate() { _valid = false; }
protected:
    bool NeedsRefresh();
    uint32 _interval;
    uint32 _lastMs = 0;
    bool _valid = false;
};

template<typename T>
class Value : public UntypedValue
{
public:
    Value(BotAI* ai, char const* name, uint32 checkIntervalMs = 0) : UntypedValue(ai, name, checkIntervalMs) { }
    T Get()
    {
        if (NeedsRefresh())
            _cached = Calculate();
        return _cached;
    }
protected:
    virtual T Calculate() = 0;
private:
    T _cached{};
};

// A cheap condition. IsActive() must be O(1) and read cached values only. Check() rate-limits with checkIntervalMs:
// between checks it reports false.
class Trigger : public BotAiObject
{
public:
    Trigger(BotAI* ai, char const* name, uint32 checkIntervalMs = 0) : BotAiObject(ai, name), _interval(checkIntervalMs) { }
    bool Check();
    virtual bool IsActive() = 0;
protected:
    uint32 _interval;
private:
    uint32 _lastMs = 0;
    bool _checkedOnce = false;
};

// One job. IsPossible/IsUseful gate any expensive work in Execute. Execute reports its outcome through SetResult
// (reason code for the decision log, optional summary and JSON object text for details).
class Action : public BotAiObject
{
public:
    Action(BotAI* ai, char const* name, uint32 flags = ACTION_FLAG_NONE) : BotAiObject(ai, name), _flags(flags) { }
    virtual bool IsPossible() { return true; }
    virtual bool IsUseful() { return true; }
    virtual bool Execute() = 0;

    uint32 GetFlags() const { return _flags; }
    std::string const& GetReason() const { return _reason; }
    std::string const& GetSummary() const { return _summary; }
    std::string const& GetDetails() const { return _details; }
    void ClearResult() { _reason.clear(); _summary.clear(); _details.clear(); }
protected:
    void SetResult(std::string reason, std::string summary = std::string(), std::string detailsJson = std::string())
    {
        _reason = std::move(reason);
        _summary = std::move(summary);
        _details = std::move(detailsJson);
    }
private:
    uint32 _flags;
    std::string _reason, _summary, _details;
};

// Scales the relevance of an action (return 1 = unchanged, 0 = forbid). Must not branch on strategy names.
class Multiplier : public BotAiObject
{
public:
    using BotAiObject::BotAiObject;
    virtual float GetValue(Action const& action) = 0;
};

struct BotActionDef
{
    std::string Action;
    float Relevance;
};

struct BotTriggerNode
{
    std::string Trigger;
    std::vector<BotActionDef> Actions;
};

// A named bundle of trigger->action wiring and multipliers. Several are active at once per engine.
class Strategy
{
public:
    explicit Strategy(char const* name) : _name(name) { }
    virtual ~Strategy() = default;
    std::string const& GetName() const { return _name; }
    virtual void InitTriggers(std::vector<BotTriggerNode>& triggers) = 0;
    virtual void InitMultipliers(std::vector<std::string>& multiplierNames) { (void)multiplierNames; }
private:
    std::string _name;
};

// Name-based creator tables. Built once on first use, read-only afterwards (safe to read from map threads).
class TC_GAME_API BotRegistry
{
public:
    static BotRegistry const& instance();

    using StrategyCreator = std::function<std::unique_ptr<Strategy>()>;
    using TriggerCreator = std::function<std::unique_ptr<Trigger>(BotAI*)>;
    using ActionCreator = std::function<std::unique_ptr<Action>(BotAI*)>;
    using ValueCreator = std::function<std::unique_ptr<UntypedValue>(BotAI*)>;
    using MultiplierCreator = std::function<std::unique_ptr<Multiplier>(BotAI*)>;

    struct StrategyEntry { uint32 StateMask; StrategyCreator Create; };

    // registration (only while the registry is being built, see RegisterBuiltinBotObjects)
    void AddStrategy(std::string name, uint32 stateMask, StrategyCreator c) { _strategies[std::move(name)] = { stateMask, std::move(c) }; }
    void AddTrigger(std::string name, TriggerCreator c) { _triggers[std::move(name)] = std::move(c); }
    void AddAction(std::string name, ActionCreator c) { _actions[std::move(name)] = std::move(c); }
    void AddValue(std::string name, ValueCreator c) { _values[std::move(name)] = std::move(c); }
    void AddMultiplier(std::string name, MultiplierCreator c) { _multipliers[std::move(name)] = std::move(c); }

    StrategyEntry const* FindStrategy(std::string const& n) const { auto i = _strategies.find(n); return i == _strategies.end() ? nullptr : &i->second; }
    TriggerCreator const* FindTrigger(std::string const& n) const { auto i = _triggers.find(n); return i == _triggers.end() ? nullptr : &i->second; }
    ActionCreator const* FindAction(std::string const& n) const { auto i = _actions.find(n); return i == _actions.end() ? nullptr : &i->second; }
    ValueCreator const* FindValue(std::string const& n) const { auto i = _values.find(n); return i == _values.end() ? nullptr : &i->second; }
    MultiplierCreator const* FindMultiplier(std::string const& n) const { auto i = _multipliers.find(n); return i == _multipliers.end() ? nullptr : &i->second; }

    std::vector<std::string> StrategyNames() const;

private:
    BotRegistry();
    std::map<std::string, StrategyEntry> _strategies;
    std::map<std::string, TriggerCreator> _triggers;
    std::map<std::string, ActionCreator> _actions;
    std::map<std::string, ValueCreator> _values;
    std::map<std::string, MultiplierCreator> _multipliers;
};

// Defined in BotStrategies.cpp: registers the built-in test strategies and their parts.
void RegisterBuiltinBotObjects(BotRegistry& registry);
// Defined in BotBehavior.cpp (Phase 3): movement, follow/stay, rest (eat/drink) and death recovery.
void RegisterPhase3BotObjects(BotRegistry& registry);
// Defined in BotCombat.cpp: the basic combat strategy ("combat").
void RegisterCombatBotObjects(BotRegistry& registry);
void RegisterQuestBotObjects(BotRegistry& registry);
// Defined in BotPet.cpp: hunter pet upkeep and taming (strategy "pet").
void RegisterPetBotObjects(BotRegistry& registry);
void RegisterTravelBotObjects(BotRegistry& registry);
// Defined in BotConsumables.cpp: potions in a fight (strategy "consumables").
void RegisterConsumableBotObjects(BotRegistry& registry);
// Defined in BotDummy.cpp: training dummy runs (strategy "dummy").
void RegisterDummyBotObjects(BotRegistry& registry);

// One engine: the active strategies of one BotState, the merged trigger/action wiring and the action queue.
class BotEngine
{
public:
    BotEngine(BotAI* ai, BotState state) : _ai(ai), _state(state) { }

    bool AddStrategy(std::string const& name);      // false when unknown or already active
    bool RemoveStrategy(std::string const& name);   // false when not active
    bool HasStrategy(std::string const& name) const;
    std::vector<std::string> const& GetStrategies() const { return _strategyNames; }

    // Evaluates the triggers, builds the merged queue and executes the best possible+useful action. Returns true when one ran.
    bool DoNextAction();

    bool IsEmpty() const { return _strategyNames.empty(); }

    // Ticks since the last call on which no action ran, by reason, as a JSON object ("" when there were none); resets the counters.
    // Reasons: no_trigger (nothing relevant fired), then the outcome of the best queued action (MULTIPLIED_TO_ZERO, NOT_POSSIBLE,
    // NOT_USEFUL, EXECUTE_FAILED).
    std::string TakeIdleJson();

private:
    struct Slot { Action* Act; float Relevance; Trigger* Trig; };
    struct Node { Trigger* Trig; std::vector<std::pair<Action*, float>> Actions; };

    void Rebuild();

    BotAI* _ai;
    BotState _state;
    bool _dirty = false;
    std::vector<std::string> _strategyNames;
    std::vector<std::unique_ptr<Strategy>> _strategies;
    std::vector<Node> _nodes;
    std::vector<Multiplier*> _multipliers;
    std::vector<Slot> _queue; // reused every tick
    uint32 _idleNoTrigger = 0, _idleMultiplied = 0, _idleNotPossible = 0, _idleNotUseful = 0, _idleExecFailed = 0;
};

#endif
