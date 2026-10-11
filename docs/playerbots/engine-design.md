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

## Config (botserver.conf.dist, PLAYER BOTS block)
`Bot.AI.Enabled`, `Bot.AI.TickMs`, `Bot.AI.TestStrategy`, `Bot.AI.Test.IdleSec`, `Bot.AI.Test.CombatSec`. Read once at first
use (not reloadable); `bot ai on|off` pauses ticking at runtime.

## Phase 3: non-combat basics (BotBehavior.cpp/.h)

Everything is a strategy/trigger/action in the Phase 2 engine and is switchable with `bot strategy <name> +/-<strategy>`.
Defaults come from config: NonCombat `rest,goto,follow`, Combat empty, Dead `recover`.

| Strategy | Engine | Trigger -> action (relevance) |
|---|---|---|
| goto | NonCombat | has_goal -> move_to_goal (Move) |
| follow | NonCombat | follow_active -> follow_leader (Move); starts above 10 yd, arrives at 5 yd |
| stay | NonCombat | hold_active -> stop_moving (High) + multiplier hold_position (blocks MOVES actions) |
| rest | NonCombat | need_eat/need_drink -> eat/drink (Rest = 35); resting -> rest_tick (34) + multiplier resting_hold |
| recover | Dead | recover_tick -> release_spirit 90, reclaim_corpse 89, spirit_heal 88, corpse_run 87 |

- Movement: `BotMotion` (one per bot) holds a goal; `Step` issues `PathGenerator` (mmaps) paths through `MoveSplineInit::MovebyPath`,
  arrival by distance, "moving" = spline not finalized. Stuck = no 2 yd progress for `Bot.AI.Move.StuckSec` (8 s): re-path, after
  `StuckRepaths` (3) episodes give up (`stuck` UNREACHABLE_TARGET). `EnsureGrids` loads the grids along the line first (a destination in an
  unloaded grid has no navmesh tile and looks like MMAP_MISSING). PathType NOPATH, or NOT_USING_PATH with FARFROMPOLY = off navmesh -> NO_PATH;
  NOT_USING_PATH alone = MMAP_MISSING. A partial path whose goal is far from every poly (FARFROMPOLY_END) fails fast with `path_fail`
  PATH_PARTIAL_FAR; a complete path replaces the goal z by the navmesh height of its end (z-aware arrival).
- Recovery state is derived from game state every tick (ghost flag, HasCorpse, corpse location); only timers are stored (`BotRecover`).
  release after a random `Release.MinSec..MaxSec` (3-8 s) via the real `HandleRepopRequest`; corpse on another map, no corpse, path
  unreachable/partial, off navmesh or farther than `Recover.MaxCorpseRunYards` (1200) -> spirit healer (`HandleSpiritHealerActivate`);
  otherwise walk to within 20 yd of the corpse, `HandleReclaimCorpse` once in 35 yd and the core reclaim delay has passed.
  Resurrection sickness is read from the revive spell and logged in the REVIVED/SPIRIT_HEALED details. Hardcore realm: logged
  RECOVER_REFUSED_HARDCORE once, no recovery.
- Rest: free food/drink spells (433-435, 1127, 1129, 2639 / 430-432, 1133, 1135, 1137) by level, validated at registry build (accepted table
  logged once). Placeholder until the economy phase. Eats below 60 % health, drinks below 40 % mana, stands at 95 %.
- Teleports: the AI tick runs inside `Player::Update`'s `SetCanDelayTeleport(true)` region, so teleports an action requests are delayed
  to the end of the update; `BotMgr::ProcessBotTeleports` (world thread) then plays the client side (near: synthesized MoveTeleportAck;
  far: HandleMoveWorldportAck). Far teleports are untested.
- Events: decision rows GOTO_START/GOTO_ARRIVED/FOLLOW_*/EAT_START/DRINK_START/EAT_DONE/DRINK_DONE/REST_END, RELEASE_SPIRIT,
  RECLAIM_WAIT, CORPSE_RUN_START, CORPSE_RECLAIMED, SPIRIT_HEALER_PLAN/FOUND, SPIRIT_HEALED, state_change REVIVED, death DIED;
  `stuck` (NO_PROGRESS, UNREACHABLE_TARGET, NO_SPIRIT_HEALER), `path_fail` (NO_PATH, MMAP_MISSING, WRONG_MAP). Plan reasons: NO_CORPSE,
  CORPSE_OTHER_MAP, CORPSE_NEAR, CORPSE_TOO_FAR, CORPSE_PATH_OK, CORPSE_UNREACHABLE, CORPSE_OFF_NAVMESH, MMAP_MISSING, CORPSE_RUN_FAILED.
  `path_fail` also emits PATH_PARTIAL_FAR (partial path, goal far from a walkable poly). Quest codes (quest_blocked reasons such as
  NO_QUEST_AVAILABLE, GIVER_NOT_INTERACTABLE, NO_STARTER_SPAWN, QUEST_LOG_FULL, BAG_FULL, NEEDS_EVENT, OBJECTIVE_UNSUPPORTED, and decision
  tags QUEST_PICK, QUEST_PICK_FAR, QUEST_WORK, QUEST_TURNIN_PLAN, QUEST_TURNED_IN, QUEST_PULL, QUEST_REWARD_EQUIPPED) are registered in
  quest-design.md 6.2.1; economy/hub/survival codes (TRAINED, TRAIN_*, VENDOR_*, BAG_*, QUEST_HUB_*, QUEST_QUARANTINED*, *_GAVE_UP) in 6.2.2 and social event types (invite, trade, quest_share, alt_command) in 6.2.3. There is no code-side registry:
  reasons are free strings in the VARCHAR(64) column.
- Test commands: `bot goto <name> x y z [arrive]`, `bot follow <name> <leader|off>`, `bot stay <name|all> on|off`, `bot hurt <name> hp% [mana%]`,
  `bot root <name> on|off`, `bot level <name> lvl`, `bot tele <name> map x y z [force]` (refused inside the start zone of the other faction without force, logged as TELE_REFUSED_FACTION), `bot state <name>`, `bot path <name> x y z`.
- Config: `Bot.AI.Default.NonCombat/Combat/Dead`, `Bot.AI.Rest.EatBelowPct/DrinkBelowPct/DonePct/FreeFood`, `Bot.AI.Release.MinSec/MaxSec`,
  `Bot.AI.Recover.MaxCorpseRunYards`, `Bot.AI.Move.StuckSec/StuckRepaths` (see botserver.conf.dist).

## Death and combat telemetry
BotAI keeps ring buffers of damage taken, damage dealt and 1 Hz vitals; `Unit::Kill` calls `OnDying` before `setDeathState` strips auras/power, and `SnapshotDeath` builds the death details JSON. Fights are tracked by `UpdateFight` (fight_id, 2 s coalescing). See progress.md, "Death post-mortem and combat events".

## Log additions from the sim analysis (session id, XP, spells, caps, MySQL plumbing)
All rows below go to `bot_event`; `bot_guid`, `level`, map/zone and position are filled by `MakeEvent` as for every BotAI event.

### New events and reasons
| event_type | reason | when | details (JSON) |
|---|---|---|---|
| (lifecycle) | BOT_LOGIN | a bot logs in | now also starts the session: `session_seq` is allocated here (`BotMgr::BeginLogSession`, unix seconds x 1000 + counter, so unique across restarts) |
| xp | XP_GAIN | `Player::GiveXP` (hook `BotAI::OnXpGain`) | source (`kill`, `quest`, `explore`, `other`), amount, bonus, xp_now, xp_max, level, elapsed_s (since login), victim{entry,level,name} for kills; `target_entry` column = victim, `quest_id` column = quest. Quest/explore sources are tagged by `NoteXpSource` just before the core's `GiveXP` |
| level_up | LEVEL_UP | end of `Player::GiveLevel` | from, to, elapsed_s (since login), level_time_s (since the previous level-up or login), kill_xp_total |
| spells | SPELLS_KNOWN | first AI tick after login, and at every level-up | cause (`LOGIN`/`LEVEL_UP`), count, spellbook_total, spells[{id,name,rank}] (max 80, highest rank of each chain, non-passive, class families only). The level-up row is written before trainer-learned spells exist: the next login row is the full picture |
| ai_stats | AI_TICK_STATS | every `Bot.Log.AiTickStatsSec` (60) per bot | window_s, engines{noncombat/combat/dead:{n,avg_ms,max_ms}} |
| decision | LOG_SUPPRESSED | a repeat-capped row was suppressed and a different row ended the run, or at logout | total, cap, suppressed[{type,reason,quest_id,target_entry,count}] |
| log | LOG_DROPPED | the database accepted a batch again after rows were dropped (bot_guid 0, WARN) | events_dropped, pos_dropped, rows_in_failed_batches, window_start, window_end, window_s |

COMBAT_START details gain `nearby_bots` (bots within 40 yd, `nearby_bots_yd`) and `claimed_by_other_bot` (the mob's tap list holds another bot; `claimed_by` = that guid). COMBAT_SUMMARY details gain
`spell_stats[{id,name,casts,hits,dmg,power}]` (top 16; id 0 = melee/auto attack, per-stay, filled by `Unit::DealDamage` and `Spell::cast` hooks), `power_type`
and `unused_spells[{id,name}]` (known class spells with no cast in that fight, max 15). No extra rows. `BotCombatCtx::End` (BotCombat.cpp) appends them via `BotAI::TakeSpellBreakdownJson`.

Death rows (`death`/`DIED`) always carry `details.killer{guid,entry,name,level,lvl_diff,resolved_by,...}`. `resolved_by` says how it was found: the lethal hit (`lethal_hit`), the attacker, a recent hit, the fight target or the attackers set;
`none` when nothing could be resolved.

### Repeat cap (`Bot.Log.RepeatCap`, default 3, 0 = off)
Per bot, per login (reset at BOT_LOGIN) and per key `type|reason|quest_id|target_entry`: the first N rows of `quest_blocked`, NO_PROGRESS, UNREACHABLE_TARGET, TEST_IDLE and
TEST_IDLE_NOTE are written, the rest are counted and flushed as one LOG_SUPPRESSED row. The cap sits in `BotMgr::LogEvent`, so direct emitters (BotQuest.cpp) are covered without changes.

### Schema (forever-botlog-setup.sql, upgrade with forever-botlog-migrate-1.sql)
- `bot_event.session_seq` (BIGINT UNSIGNED, set from BOT_LOGIN on; index `(bot_guid, session_seq)`).
- STORED generated columns, indexed: `killer_entry`, `killer_level` (`$.killer.entry/level`), `xp_amount` (xp rows), `spell_id` (`$.killer.spell` else `$.spell_id`). They are BIGINT on purpose:
  an out-of-range value in a narrower generated column raises ERROR 1264 and would fail the whole batch. VIRTUAL, not indexed: `outcome` (`$.outcome`), `lvl_diff` (`$.killer.lvl_diff` else `$.target.lvl_diff`).
  `JSON_VALUE ... RETURNING` yields NULL on a conversion error instead of failing.
- Index `idx_type_sev_ts (event_type, severity, ts)`. `idx_reason` and `idx_quest` are kept: the console filters by reason, the analysts group by reason and quest_id.
- `bot_event_hot` (same columns, `CREATE TABLE ... LIKE`, ids start at 10^12) and the view `bot_event_all` (`src` 0 = bot_event, 1 = hot). Query the view for anything that must see all rows.
- Summaries kept after partitions drop: `bot_event_hourly` (hour, src, bot, event_type, severity, n) and `bot_death_daily` (day, class, level, zone, killer_entry, n).
  `botlog_roll_table` summarises the 3 days before the cutoff (so a missed run is caught up; re-runs overwrite) and then drops the partitions.
- `botlog_roll_partitions2(keep, hot_keep)` is the daily call (14 and 5 days; bot_pos 2 days). `botlog_roll_partitions(keep)` is a compat wrapper.

### Hot/archive split (`Bot.Log.HotSplit`, default 0)
With 1, rows whose type is in `Bot.Log.HotTypes` (default decision,state_change,trace) go to `bot_event_hot` (5 days) instead of `bot_event` (14 days), except COMBAT_SUMMARY, LOG_SUPPRESSED and
LOG_DROPPED. Turn it on only after the console and analyst queries read `bot_event_all`. It is ignored with a warning when the table does not exist. State changes of the engine
stay queryable for 5 days; deaths, quests, xp, combat summaries stay 14 days.

### Summary rows (`Bot.Log.Summary.Enabled`, default 0)
High-volume events (types `Bot.Log.Summary.Types`, default cast,aura,trace; reason prefixes `Bot.Log.Summary.Reasons`, default GOTO_, QUEST_WALK_, QUEST_PULL, FOLLOW_, TOWN_IDLE_) are counted in memory and written as one `bot_event_rollup`
row per window (`Bot.Log.Summary.WindowSec`, 60) and key (bot, type, reason, severity, spell text for cast/aura, map, zone, quest, target): `n`, `first_ts`, `last_ts`, `level`, and the summary/details of the first
event as a sample. Rows at or above `Bot.Log.Summary.KeepSeverity` (2) and reasons with a `Bot.Log.Summary.Keep` prefix (DUNGEON_, TRAVEL_, DUMMY_, COMBAT_, BOT_, LOG_, CORPSE_, SPIRIT_, WATCHDOG_, PARTY_, BANK_, MAIL_)
stay detailed. Counts, per-bot/zone/quest/reason breakdowns and time series stay exact; per-event timing inside a window and per-event details beyond the first sample do not. Count queries use the view
`bot_event_counts_all` (rollup + detailed rows). Needs `forever-botlog-migrate-2.sql`; ignored with a warning when the table is missing. Rollup rows are kept 30 days (`botlog_rollup_prune_daily`).
Add the reason prefix of every new rare event to `Bot.Log.Summary.Keep` if its prefix would otherwise match a summarized one.

### Writer behaviour (BotMgr)
- `LogEvent`/`LogPosition` are non-blocking (mutex + vector push). The world thread flushes in `Update` every `BotLog.FlushIntervalMs`: one async transaction per flush (positions + events), at most 16 in flight.
- Buffers are capped (`Bot.Log.BufferMax` 200000 events, `Bot.Log.PosBufferMax` 100000). Over the cap rows are dropped and counted; one error is logged at the first drop and a `log_dropped` event is written when the
  database accepts a batch again. A failed transaction is resubmitted `Bot.Log.FlushRetries` times (3) and then counted as lost. At shutdown `FlushLog(true)` waits up to 15 s for the batches in flight.
- A configured but unreachable bot log database no longer aborts the worldserver start: it has its own loader, logging stays off and `.bot status` shows the reason. A missing hot table or session column is detected when the statements are prepared.
- `bot_pos` interval: `Bot.Log.PosIntervalSec` (5) up to `Bot.Log.PosScaleBots` (500) bots online, `Bot.Log.PosIntervalSlowSec` (15) above.

### Reachability probe (`Bot.Log.ProbeIntervalSec`, default 10)
The core DB layer calls ABORT() when a reconnect fails, so a database server outage mid-run would kill the worldserver if rows were still being submitted. `BotMgr::UpdateProbe` (world thread, helper thread
does a 3 s TCP connect to the `BotLogDatabaseInfo` host:port) switches logging off when the host stops answering (`.bot status` shows "off (bot log database host:port unreachable ...)")
and back on when it answers again; nothing is submitted while it is off, buffered rows are kept (capped) and written on recovery. Limits: it checks the port, not a SQL login, and a failure
in the seconds between two probes can still reach the core path. Skipped for socket or "." hosts and when set to 0.

## A3: alts as bots (BotAlts.cpp/.h, written, not yet run)

Design: `.bot alt add|remove|list <name>` (RBAC_PERM_COMMAND_BOT_ALT 1001, granted to players by `sql/custom/auth/2026_10_06_00_auth_rbac_bot_alt.sql`) calls `BotMgr::StartAlt`, which puts the character into `_bots` with `Alt = true` on its real account and queues it through the normal bot login (same `HandleBotPlayerLogin`, same `LogoutBot` save path). Rules: character must belong to the issuer's account (same refusal text for unknown names); refused while online as a player or while a real session of the account is mid-login; refused when already a bot; cap `Bot.Alt.MaxPerAccount` (4) of active alts; `Bot.Alt.Enabled`. `HandlePlayerLoginOpcode` refuses a player login of an active alt (DuplicateCharacter). Alts are skipped by `.bot spawn` reuse, `.bot despawn all` and `.bot stats`; `LogoutAll` (shutdown) saves them. Persistence (later change, see the social hooks section): alts are remembered in bot_alt and re-logged in at startup when the table exists. After login the alt joins its owner's group when the owner leads it or is alone (a group is created), because bots cannot accept invites. WorldSession gets `IsAltBot()`: logout then marks only that character offline (the stock statement marks every character of the account offline, which would hit the owner's own online character). All bot_event rows of an alt carry `"source":"alt"` in details (tagged in `BotMgr::LogEvent`). Reason codes in `alt_command` events: ALT_ADDED, ALT_REMOVED, ALT_REFUSED_NOT_OWNER, ALT_REFUSED_ONLINE, ALT_REFUSED_LOADING, ALT_REFUSED_ALREADY, ALT_REFUSED_CAP, ALT_REFUSED_BOT (to be accepted by bot infrastructure).

Test plan (sim, throwaway characters only; apply the auth SQL on the sim auth DB first; console has no session, so use the `test` word):
1. Create 2 throwaway accounts with 3 characters each (sim DB). `bot alt add <charA1> test`: bot login event with source alt, level/position kept, `.bot list` shows it online.
2. `bot alt add <charA1> test` again: ALT_REFUSED_ALREADY. Cap: add 5 alts of one account: the 5th gives ALT_REFUSED_CAP.
3. Not-owner and player-online: needs a client logged in as a character of the account (add it: ALT_REFUSED_ONLINE) and the same client trying to add a character of the other account from the in-game chat (ALT_REFUSED_NOT_OWNER). Player login of an active alt from a client: refused.
4. Save path: give the alt some change (`bot level`, move, loot), `bot alt remove`, compare `characters` row (level, xp, position, money, inventory count) before and after, and `online = 0`; restart-less relog shows the same state. Then a worldserver restart (sim, when free) with 2 alts online: rows saved, alts re-logged in at startup (when bot_alt exists).
5. Group and commands: alt joins the owner's group; `bot say <owner> party follow` obeyed; a non-leader is ignored and logged (A2 behavior).
6. Owner's other character online as a player while an alt logs out: its `characters.online` stays 1.

## Bot social hooks: invites, trade, quest share, alt persistence (BotSocial.*, BotAlts.*) - UNVERIFIED at runtime

Built, never run. Bot sessions have no socket, so the core handlers (world thread, PROCESS_THREADUNSAFE) call a hook at their end and the bot answers by driving its own session handler with a hand-built packet.

Hooks: GroupHandler (invite queued / already grouped), TradeHandler (initiate, player accept, before execute), QuestHandler (HandlePushQuestToParty, per bot receiver), BotMgr::Update (trade timeout, alt restore once), BotChat verb `share <questId>`.

Policies: invites accepted unless Bot.Invite.Enabled=0, alt bots only from the owner account, one group per bot (core refuses, logged INVITE_REFUSED_ALREADY_GROUPED). Trade: world bots refuse always, alts trade only with the owner account, bot offers nothing, an empty offer is refused, an open window is cancelled after 90 s. Quest push: same checks as the core loop, accepted quest goes to the log and is worked by BotQuest (alts only once a leader enables the quest strategy). Alts: idle defaults (Bot.Alt.Default.NonCombat), remembered in table bot_alt (forever-characters-bot-alt.sql, probe first, off with one warning when absent), `.bot despawn <alt>` owner only.

Test plan (real client needed where marked; sim GM injection cannot invite or trade):
1. Invite (client): invite a world bot -> joins, event INVITE_ACCEPTED. Invite a bot already grouped -> INVITE_REFUSED_ALREADY_GROUPED. Invite an alt of another account -> declined, INVITE_REFUSED_NOT_OWNER. Bot.Invite.Enabled=0 -> declined, INVITE_REFUSED_DISABLED.
2. Trade (client, alt of own account): open trade, put an item/gold, accept -> window completes, TRADE_ACCEPTED with gold_in and items_in, items present in the alt's bags after relog. Empty offer + accept -> TRADE_REFUSED_EMPTY. Idle 90 s -> TRADE_REFUSED_TIMEOUT. Full bags -> TRADE_REFUSED_CORE_FAILED. Trade a world bot -> TRADE_REFUSED_WORLD_BOT. Equipped and soulbound items: core refuses to place them.
3. Quest share: player shares a quest in a party with bots -> QUEST_SHARE_ACCEPTED or the specific QUEST_SHARE_REFUSED_* per bot (level, class, race, on quest, done, log full). Leader whispers `g1 share <questId>` -> bots with the quest push it, other bots log per-receiver results. Check the accepted quest is not skipped by BotQuest quarantine and that 40 bots sharing do not flood bot_event (each sharer logs one event per receiver).
4. Alt persistence (sim DB only after the SQL is applied there): `.bot alt add`, restart worldserver, alt comes back (log "restored N alt"), idle; it is grouped only if the core restored its old group (then an owner invite gives INVITE_REFUSED_ALREADY_GROUPED), otherwise ungrouped and the owner invites it -> joins. `.bot alt remove` or owner despawn deletes the row, shutdown does not. Without the table: one warning, no crash. GM of another account `.bot despawn <alt>` -> ALT_REFUSED_NOT_OWNER_DESPAWN.
5. Cost: tick cost at 180 bots with BotSocial::Update (empty vector normally, no cost).
