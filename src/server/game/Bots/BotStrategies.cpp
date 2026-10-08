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

// Built-in test strategies of the bot engine (Phase 2). They prove every part of the engine (values, triggers, actions,
// the merged queue with two relevances, multipliers, all three engines) without needing movement or combat.
// Strategies: test_idle (non-combat), test_combat (combat), test_dead (dead), test_quiet (multiplier, all engines).

#include "BotAI.h"
#include "Player.h"
#include "StringFormat.h"

namespace
{
// Value: whole seconds since the bot last executed any action (cached for 1 s).
class IdleSecValue : public Value<uint32>
{
public:
    explicit IdleSecValue(BotAI* ai) : Value<uint32>(ai, "idle_sec", 1000) { }
protected:
    uint32 Calculate() override { return GetAI()->GetIdleMs() / 1000; }
};

// Trigger: idle for Bot.AI.Test.IdleSec seconds. O(1): reads the cached value.
class IdleTrigger : public Trigger
{
public:
    explicit IdleTrigger(BotAI* ai) : Trigger(ai, "idle", 1000), _idle(ai->GetValue<uint32>("idle_sec")) { }
    bool IsActive() override { return _idle && _idle->Get() >= BotAI::Config().TestIdleSec; }
private:
    Value<uint32>* _idle;
};

// Trigger: fires every Bot.AI.Test.CombatSec seconds while the combat engine is active.
class CombatPeriodicTrigger : public Trigger
{
public:
    explicit CombatPeriodicTrigger(BotAI* ai) : Trigger(ai, "combat_periodic", BotAI::Config().TestCombatSec * 1000) { }
    bool IsActive() override { return true; }
};

// Trigger: the dead engine is active (after the bot died, or when the state was forced for a test).
class DeadEngineTrigger : public Trigger
{
public:
    explicit DeadEngineTrigger(BotAI* ai) : Trigger(ai, "dead_engine", 1000) { }
    bool IsActive() override { return GetAI()->GetState() == BotState::Dead; }
};

// Higher relevance: only useful in even seconds of AI time, so both outcomes of the queue show up in the log.
class IdleNoteAction : public Action
{
public:
    explicit IdleNoteAction(BotAI* ai) : Action(ai, "test_idle_note", ACTION_FLAG_NOISY) { }
    bool IsUseful() override { return (GetAI()->GetNowMs() / 1000) % 2 == 0; }
    bool Execute() override
    {
        SetResult("TEST_IDLE_NOTE", "idle note (preferred action)", Trinity::StringFormat(R"({{"idle_reason":"TEST_STRATEGY","idle_sec":{}}})", GetAI()->GetIdleMs() / 1000));
        return true;
    }
};

class IdleLogAction : public Action
{
public:
    explicit IdleLogAction(BotAI* ai) : Action(ai, "test_idle_log", ACTION_FLAG_NOISY) { }
    bool Execute() override
    {
        SetResult("TEST_IDLE", "idle log (fallback action)", Trinity::StringFormat(R"({{"idle_reason":"TEST_STRATEGY","idle_sec":{}}})", GetAI()->GetIdleMs() / 1000));
        return true;
    }
};

class CombatLogAction : public Action
{
public:
    explicit CombatLogAction(BotAI* ai) : Action(ai, "test_combat_log", ACTION_FLAG_NOISY) { }
    bool Execute() override
    {
        SetResult("TEST_COMBAT", "combat engine tick", Trinity::StringFormat(R"({{"engine_age_ms":{}}})", GetAI()->GetStateAgeMs()));
        return true;
    }
};

// Once per death (re-armed when the dead engine is entered).
class DeadLogAction : public Action
{
public:
    explicit DeadLogAction(BotAI* ai) : Action(ai, "test_dead_log") { }
    void OnStateEnter() override { _logged = false; }
    bool IsUseful() override { return !_logged; }
    bool Execute() override
    {
        _logged = true;
        Player* bot = GetBot();
        SetResult("TEST_DEAD", "dead engine active", Trinity::StringFormat(R"({{"alive":{},"health":{}}})", bot->IsAlive() ? "true" : "false", bot->GetHealth()));
        return true;
    }
private:
    bool _logged = false;
};

// Zeroes every action flagged noisy (a passive/quiet mode). Never looks at strategy names.
class QuietNoisyMultiplier : public Multiplier
{
public:
    explicit QuietNoisyMultiplier(BotAI* ai) : Multiplier(ai, "quiet_noisy") { }
    float GetValue(Action const& action) override { return (action.GetFlags() & ACTION_FLAG_NOISY) ? 0.0f : 1.0f; }
};

class SimpleStrategy : public Strategy
{
public:
    using Init = void (*)(std::vector<BotTriggerNode>&, std::vector<std::string>&);
    SimpleStrategy(char const* name, Init init) : Strategy(name), _init(init) { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override { std::vector<std::string> m; _init(t, m); }
    void InitMultipliers(std::vector<std::string>& m) override { std::vector<BotTriggerNode> t; _init(t, m); }
private:
    Init _init;
};

template<typename T>
BotRegistry::TriggerCreator MakeTrigger() { return [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<T>(ai); }; }
template<typename T>
BotRegistry::ActionCreator MakeAction() { return [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<T>(ai); }; }
}

void RegisterBuiltinBotObjects(BotRegistry& r)
{
    r.AddValue("idle_sec", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<IdleSecValue>(ai); });
    r.AddTrigger("idle", MakeTrigger<IdleTrigger>());
    r.AddTrigger("combat_periodic", MakeTrigger<CombatPeriodicTrigger>());
    r.AddTrigger("dead_engine", MakeTrigger<DeadEngineTrigger>());
    r.AddAction("test_idle_note", MakeAction<IdleNoteAction>());
    r.AddAction("test_idle_log", MakeAction<IdleLogAction>());
    r.AddAction("test_combat_log", MakeAction<CombatLogAction>());
    r.AddAction("test_dead_log", MakeAction<DeadLogAction>());
    r.AddMultiplier("quiet_noisy", [](BotAI* ai) -> std::unique_ptr<Multiplier> { return std::make_unique<QuietNoisyMultiplier>(ai); });

    r.AddStrategy("test_idle", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<SimpleStrategy>("test_idle", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "idle", { { "test_idle_note", BotRelevance::Normal }, { "test_idle_log", BotRelevance::Default } } });
        });
    });
    r.AddStrategy("test_combat", BotStateBit(BotState::Combat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<SimpleStrategy>("test_combat", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "combat_periodic", { { "test_combat_log", BotRelevance::Normal } } });
        });
    });
    r.AddStrategy("test_dead", BotStateBit(BotState::Dead), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<SimpleStrategy>("test_dead", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "dead_engine", { { "test_dead_log", BotRelevance::Emergency } } });
        });
    });
    r.AddStrategy("test_quiet", BOT_STATES_ALL, []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<SimpleStrategy>("test_quiet", [](std::vector<BotTriggerNode>&, std::vector<std::string>& m)
        {
            m.push_back("quiet_noisy");
        });
    });

    RegisterPhase3BotObjects(r);
    RegisterCombatBotObjects(r);
    RegisterQuestBotObjects(r);
    RegisterPetBotObjects(r);
    RegisterTravelBotObjects(r);
}
