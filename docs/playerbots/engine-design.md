# Bot AI engine (Phase 2) - design

Files: `src/server/game/Bots/BotEngine.{h,cpp}` (core classes, registries, one engine), `BotAI.{h,cpp}` (per-bot AI, state
machine, logging, factory), `BotStrategies.cpp` (built-in test strategies), `BotQuestLog.{h,cpp}` (quest step events).
Hooks: `Player::Update` (tick), `WorldSession` (owns the AI), `BotMgr::FinishLogin/LogoutBot` (create/destroy),
`Player::AddQuest/CompleteQuest/RewardQuest/FailQuest/AbandonQuest/SetQuestObjectiveData` (quest log, bot-guarded).
Design follows the structure of mod-playerbots (strategy/trigger/action, values, multipliers, three engines); no code copied.

## Class diagram in words
- `BotAI` (one per bot) owns three `BotEngine` (NonCombat, Combat, Dead) and the per-bot caches of instantiated
  `Trigger`, `Action`, `Multiplier` and `UntypedValue` objects (by name, created lazily from the registry, never freed
  before the AI). It owns the state machine, the decision ring (last 8 decisions), the trace flag and the AI clock.
- `BotEngine` holds the active `Strategy` objects of one state, the merged trigger->actions wiring (`_nodes`), the active
  `Multiplier`s and the reusable action queue.
- `Strategy` = name + `InitTriggers(vector<BotTriggerNode>&)` (trigger name -> {action name, relevance}) +
  `InitMultipliers(names)`. Several are active at once; their wiring is merged (same trigger twice = one check).
- `Trigger`: `IsActive()` must be O(1) and read cached values; `Check()` adds a `checkIntervalMs` rate limit (between
  checks it reports false).
- `Action`: `IsPossible()`, `IsUseful()`, `Execute()`; reports its outcome with `SetResult(reason, summary, detailsJson)`;
  flags (`ACTION_FLAG_NOISY`, `ACTION_FLAG_QUIET_LOG`).
- `Value<T>`: cached `Calculate()` with `checkIntervalMs` (0 = every call).
- `Multiplier::GetValue(Action const&)`: 1 = unchanged, 0 = forbid. Decides on action flags, never on strategy names.
- `BotRegistry`: name -> creator tables for strategies (with the state mask they apply to), triggers, actions, values,
  multipliers. Built once on first use (on the world thread, forced by `BotAI::Create`), read-only afterwards.
  Built-ins are registered in `RegisterBuiltinBotObjects` (BotStrategies.cpp).

## Relevance bands
| band | value | band | value |
|---|---|---|---|
| Default | 5 | Raid | 60 |
| Normal | 10 | Emergency | 90 |
| High | 20 | PullLow / Mid / High | 105 / 106 / 107 |
| Move | 30 | Interrupt | 40 |
| Dispel | 50 | | |

Effective relevance = base relevance x all active multipliers. The queue is sorted by it; the first action that is
multiplier-positive, `IsPossible`, `IsUseful` and whose `Execute()` returns true runs. One action per AI tick per bot.
The rest of the queue is recorded as `alternatives` with a result (`MULTIPLIED_TO_ZERO`, `NOT_POSSIBLE`, `NOT_USEFUL`,
`EXECUTE_FAILED`, `NOT_REACHED`).

## Lifecycle and threading
- Created in `BotMgr::FinishLogin` (world thread) via `BotAI::Create` (the AI factory: applies the default strategy set per
  state, see `DefaultStrategies` in BotAI.cpp; today only the test strategies when `Bot.AI.TestStrategy=1`).
- Owned by the bot's `WorldSession` (`SetBotAI`, deleted in `~WorldSession` and explicitly in `BotMgr::LogoutBot` before
  `LogoutPlayer`), so it always dies with the session and cannot outlive a removed bot. It never keeps a `Player*` across
  ticks (`Player*` is passed into `Update`, `GetBot()` is valid only during a tick) and its destructor never touches the player.
- Ticked from `Player::Update` (after the `!IsInWorld()` return, so also while dead) by `if (GetSession()->GetBotAI())`: a
  null-pointer check for real players. `Update` accumulates `diff`, ticks every `Bot.AI.TickMs` (random start offset per bot so
  180 bots do not tick on the same world tick), skips ticks while the player is being teleported, invalidates cached values when
  the map id changes.
- Map threads: each bot is updated by exactly one map thread; the AI touches only its own state, relaxed atomics for the global
  counters, and `BotMgr::LogEvent` (mutex-protected buffer, flushed by the world thread). No database access.
- Console commands (`bot strategy`, `bot trace`, `bot ai ...`) run in `World::ProcessCliCommands`, which is after
  `MapManager::Update` has joined its worker threads (`m_updater.wait()`), so they can change AI state without locks.
  Keep it that way: do not call the control methods from a map thread.
- State changes happen only in `BotAI::ChangeState`. Desired state: forced (test) > `!IsAlive()` -> Dead > `IsInCombat()` ->
  Combat > NonCombat. Cause codes: `AI_START`, `DIED`, `REVIVED`, `COMBAT_START`, `COMBAT_END`, `FORCED`, `FORCED_END`.
  Entering a state calls `OnStateEnter()` on every cached object.

## Adding a strategy, trigger and action (about 10 lines)
```cpp
class HpLowTrigger : public Trigger { public: explicit HpLowTrigger(BotAI* ai) : Trigger(ai, "hp_low", 500) {}
    bool IsActive() override { return GetBot()->GetHealthPct() < 40.0f; } };          // O(1)
class EatAction : public Action { public: explicit EatAction(BotAI* ai) : Action(ai, "eat") {}
    bool IsUseful() override { return !GetBot()->IsInCombat(); }
    bool Execute() override { /* one job */ SetResult("EAT", "started eating"); return true; } };
// in RegisterBuiltinBotObjects (or a new class file's registration function called from it):
r.AddTrigger("hp_low", MakeTrigger<HpLowTrigger>()); r.AddAction("eat", MakeAction<EatAction>());
r.AddStrategy("rest", BotStateBit(BotState::NonCombat), []{ return std::make_unique<SimpleStrategy>("rest",
    [](auto& t, auto&){ t.push_back({ "hp_low", { { "eat", BotRelevance::Normal } } }); }); });
```
Then `bot strategy <bot> +rest`, or add `"rest"` to `DefaultStrategies` for the state. New `.cpp` files in `Bots/` are picked up
by CMake after a re-configure (`cmake build`).

## Log events emitted (forever_botlog.bot_event)
| event_type | reason | when | details (JSON) |
|---|---|---|---|
| state_change | AI_START, DIED, REVIVED, COMBAT_START, COMBAT_END, FORCED, FORCED_END | engine state changed (`details.kind="engine"`; lifecycle events by BotMgr keep their own reasons) | from, to, cause, ai_ms; entering Dead adds `death:true` and `recent_decisions` (last 8: ms_ago, action, reason) |
| decision | action's own code (e.g. TEST_IDLE) | an action executed | engine, trigger, action, relevance, band, effective, alternatives[], detail (action's JSON) |
| trace | TRACE_EVAL | trace on and the queue had entries but nothing ran | engine, considered[] |
| strategy_change | STRATEGY_ADDED / STRATEGY_REMOVED | `bot strategy` | strategy, source, engines |
| error | ENGINE_UNKNOWN_NAME | a strategy referenced an unregistered trigger/action/value/multiplier (once per name, WARN) | key |
| quest | QUEST_ACCEPTED, QUEST_PROGRESS, QUEST_COMPLETE, QUEST_REWARDED, QUEST_ABANDONED, QUEST_FAILED | quest steps, `quest_id` column set | see below |

Trace rows are written as INFO (not TRACE) on purpose: `BotLog.MinSeverity` defaults to 1 and would drop severity 0 rows, and
the per-bot trace switch is the explicit opt-in. Reason is cut to 63 chars and summary to 250 before queueing (column limits).
Trace also turns on actions flagged `ACTION_FLAG_QUIET_LOG` and every QUEST_PROGRESS step.

Quest event details: QUEST_ACCEPTED {title, quest_level, min_level, suggested_players, group_quest, giver{type,entry}, flags,
objectives}; QUEST_PROGRESS {objective_id, objective_type, object_id, old, new, required, objective_done} (logged for the first
change, the completing change, and everything under trace); QUEST_COMPLETE {common fields, objectives[{id,type,object_id,have,
required}]}; QUEST_REWARDED {common, turn_in{type,entry}, choice_item{item,name,count}, fixed_items[], xp, money, reputation[{faction,
value_id,override}], accept_to_reward_s (null when the accept happened before a restart)}; QUEST_ABANDONED {common}; QUEST_FAILED
(WARN) {common, cause, time_limit_s, alive, engine (last engine state)}. `group_quest` is true when `suggested_players > 1`. Every quest event carries `details.source`: `"bot"` for real bot
activity (the strategy name once Phase 8 exists) and `"test_command"` for events caused by `bot quest ...`, so analysts can filter
test noise (`details->>'$.source' = 'bot'`).

QUEST_FAILED `cause` codes (set by `BotQuestLog::Scope`, a thread-local RAII marker placed at the core's FailQuest call sites;
the innermost scope wins): `timeout` (timed-quest timer expiry in Player::Update; also the fallback for an unscoped failure of a
quest with a time limit), `died` (death path, `FailQuestsWithFlag(COMPLETION_NO_DEATH)`), `logout` (`FAIL_ON_LOGOUT` quests),
`escort_failed` (ScriptedEscortAI, ScriptedFollowerAI, SmartAI escort end with failure), `event_failed` (SmartAI action, map
script, quest-fail spell effect), `test_command` (`bot quest fail`), `other` (anything unscoped, e.g. custom scripts that call
FailQuest directly). Reputation holds the quest
template's reward ids, not the final standing change. The accept time is kept in memory only.

## Console commands (RBAC_PERM_COMMAND_BOT, Console::Yes)
`bot strategy <name|all> [+x|-x[,+y]]`, `bot trace <name|all> on|off`, `bot ai status|reset|on|off`,
`bot ai force <name|all> noncombat|combat|dead|auto` (pins the engine, not the game state), `bot kill <name>` (test aid),
`bot quest add|complete|reward|abandon|fail <bot> <questId> [choiceItemId]` (test aids using the normal Player quest APIs).
Use the existing `revive <name>` to revive a bot.

## Config (worldserver.conf.dist, PLAYER BOTS block)
`Bot.AI.Enabled`, `Bot.AI.TickMs`, `Bot.AI.TestStrategy`, `Bot.AI.Test.IdleSec`, `Bot.AI.Test.CombatSec`. Read once at first
use (not reloadable); `bot ai on|off` pauses ticking at runtime.
