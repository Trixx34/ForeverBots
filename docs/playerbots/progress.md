# Player bots - progress notes

Per-phase validation notes (what was verified, with numbers). Plan: [feature-plan.md](feature-plan.md).

## Phase 2 - Engine core (validated on the sim, 2026-10-06)

Design: [engine-design.md](engine-design.md). Verified on the sim worldserver (forever_sim_* databases, bot log forever_sim_log),
RelWithDebInfo build, 10 to 180 bots, never on the real realm.

**Engine and logging**
- 10 bots spawned, `bot strategy all +test_idle,+test_combat,+test_dead`: 30 STRATEGY_ADDED rows, then one `decision` row per bot
  every 10 s (TEST_IDLE_NOTE / TEST_IDLE alternate; the lower-relevance alternative shows `NOT_USEFUL` or `NOT_REACHED` in
  `details.alternatives`; the band, relevance and effective relevance are in the details).
- `test_quiet` (multiplier) on one bot: both idle actions `MULTIPLIED_TO_ZERO`; with `bot trace <bot> on` the bot wrote one
  `trace/TRACE_EVAL` row per evaluation (`considered[]`), other bots wrote none.
- Dead engine: `bot kill Botbelan` -> `state_change DIED (engine noncombat -> dead)` with the last 8 decisions in
  `details.recent_decisions`, then one `TEST_DEAD` decision from the dead engine; `revive Botbelan` -> `REVIVED (dead -> noncombat)`.
  Combat engine exercised with `bot ai force <bot> combat` (FORCED, TEST_COMBAT decisions, FORCED_END on `auto`); no real combat yet.
- Despawn/restart: `server exit` with bots online wrote LOGOUT_SHUTDOWN rows, characters.online = 0 afterwards, bots log in again
  with fresh AI (AI_START) and no duplicated or missing quest events; saved quest state persisted across the restart.
- Bug found and fixed during the test: a world stall made all bots tick in lockstep forever (accumulator reset to 0). Ticks now
  fire when a per-bot time bucket changes (fixed random phase), so bots spread again after a stall. After the fix 1800 decisions
  over 100 s landed in 524 distinct 50 ms slots.

**Quest log (owner addition)** `bot quest add|complete|reward|abandon|fail` on throwaway sim bots (quests 783, 7, 358, 9, 183, 788):
QUEST_ACCEPTED, QUEST_PROGRESS (first change and the completing change only), QUEST_COMPLETE, QUEST_REWARDED (xp, money, reputation
ids, choice item with name, accept_to_reward_s), QUEST_ABANDONED and QUEST_FAILED (WARN) all appeared in `bot_event` with `quest_id`,
level/map/zone/position, `details.source = test_command`, `cause`, `alive`, `engine`. Not exercised: a real (non-test) failure cause
(timeout, died, escort_failed, event_failed paths are wired but unrun), the `source = "bot"` rows (only produced by non-test flows),
a choice reward with fixed items, group quests.

**Tick cost (180 idle bots, 500 ms tick, sim worldserver process CPU, one core = 100%)**
| state | CPU |
|---|---|
| 0 bots, idle | 9.4 % |
| 180 bots, AI on (no strategies) | 36.4 / 38.2 / 36.5 % (three 60 s windows) |
| 180 bots, AI off (`bot ai off`) | 37.9 / 37.3 % |
| 180 bots, AI on + test strategies on all (idle 10 s, about 14 decision rows/s) | 39.6 % |
- External sampling cannot separate AI on from off (difference is inside the +-1 % noise). Internal timers: **0.96 us average per
  tick** (max 72 us in steady state, up to 1.5 ms when a login burst coincides), 360 ticks/s for 180 bots = **about 0.35 ms of CPU
  per second in total, 0.03 % of a core, about 2 us per bot per second**. With the test strategies the average is 3.2 us per tick
  (79 ms over about 75 s, of which logging 15 ms): about 0.1 % of a core.
- The cost of the bots themselves (login, map, updates) is about (37 - 9.4) % = 27.6 % of a core = 1.5 ms per bot per second, in line
  with the earlier baseline (1.3 ms); the AI adds about 0.1 % of that. Nothing dominates: an empty tick is one state check.
- Decision: `Bot.AI.Enabled` defaults to on (cost is negligible with no strategies). `Bot.AI.TestStrategy` defaults to off.

**Not done / notes**: no real behavior (movement, combat, release/revive are Phase 3/5); no killer/damage capture in the death
event; config is read once (no reload); trace rows are INFO severity by design (see engine-design.md); a build with the full
`scripts` target needs `/m:1 /p:CL_MPCount=2` after touching `WorldSession.h` (the `/m:2` run hit the compiler heap limit).

## Position telemetry (`bot_pos`, for the sim console map trails; verified on the sim 2026-10-06)

- Table `bot_pos` (forever-botlog-setup.sql, applied to `forever_sim_log`; NOT yet applied to `forever_botlog`): `ts DATETIME(3)`,
  `bot_guid`, `map_id`, `zone_id`, `x`, `y`, `z` (float), `flags` (bit 0 moving, bit 1 in combat, bit 2 dead). Indexes `(map_id, ts)` and
  `(bot_guid, ts)`. Partitioned by day, kept 2 days. `botlog_roll_partitions(keep)` now calls the new `botlog_roll_table(table, keep)` for
  `bot_event` (keep days) and `bot_pos` (2 days) and uses `DATABASE()` instead of a hard-coded schema, so the same script works for
  `forever_botlog` and `forever_sim_log`.
- Producer: `BotAI::SamplePosition` at the end of every AI tick (map thread) -> `BotMgr::LogPosition` (mutex-protected buffer, same pattern
  as `LogEvent`, capped at 100000 samples if the DB stalls) -> `BotMgr::FlushLog` on the world thread writes a separate async transaction
  (`BOTLOG_INS_POS`). Samples: first tick after login, map or zone change, then at most every `Bot.Log.PosIntervalSec` (default 5, 0 = off,
  read once at startup, forced to 0 when the bot log DB is not available) and only if the bot moved more than 2 yards since its last sample.
  Idle bots write nothing.
- Test aid: `bot nudge <name|all> <yards> [degrees]` (0 = +x, 90 = +y; default along facing) moves bots via `UpdatePosition` without a
  teleport handshake. `bot ai status` prints the sample count and time spent.
- Result (180 bots, sim): 180 login rows (180 distinct bots), then 60 s of `bot nudge all 5 90` once per second gave 2408 rows in 72 s
  (13.4 per bot, about one per 5.4 s = interval 5 s plus tick granularity); y advanced 25 yards per sample as expected; 53 s of idle after
  that added 0 rows. Tick cost: time inside `SamplePosition` about 0.5 us per tick while idle (7.3 ms over 14420 ticks), about 8 us per
  sample taken; at 360 ticks/s that is 0.2 ms of CPU per second (0.02 % of a core). Whole-tick average stayed about 2.1 us (it was 0.96 us
  in the Phase 2 run; the two extra clock reads and the position checks account for part of it, rest is noise between runs).
