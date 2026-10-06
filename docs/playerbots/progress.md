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

## Phase 3: non-combat basics (verified on the sim 2026-10-06, not committed)

- Walking with mmaps, arrival, NO_PATH, stuck (NO_PROGRESS then UNREACHABLE_TARGET), eat, drink, eat+drink together (EAT_START/DRINK_START/
  EAT_DONE/DRINK_DONE/REST_END rows), follow, stay, bot_pos moving flag: all seen in bot_event/bot_pos.
- Dead recovery: kill -> DIED -> RELEASE_SPIRIT (3-8 s) -> CORPSE_RUN_START -> CORPSE_RECLAIMED -> REVIVED; spirit healer fallback
  (SPIRIT_HEALER_PLAN -> FOUND -> SPIRIT_HEALED -> REVIVED, sickness logged); unreachable/off-navmesh corpse -> healer. The 23 bots that
  were dead before the deploy recovered. Restart while dead: two bots killed, worldserver restarted within seconds, `bot spawn 180`: both
  logged in dead with a fresh AI and recovered (one by corpse reclaim, one by spirit healer, CORPSE_UNREACHABLE plan).
- Tick cost, 180 bots (re-measured with a true idle window, no bot moving): idle 3.4 us avg, max 299 us (the Phase 2 / telemetry baseline
  was about 2.1 us; the rest/goto/follow/hold triggers evaluated every tick account for the difference). Burst of 120 simultaneous gotos:
  first 6.5 s avg 10.9 us (max 0.9 ms), over 22 s (path queries plus walking) avg 21.7 us, worst single tick 4.4 ms (a tick that
  loaded grids and ran a path query), total 223 ms in 22 s = about 1 % of a core. Tail after the burst 4.7 us. Process CPU 34-37 % of one
  core in all windows (dominated by the core: 180 sessions, maps, movement), the AI is not visible in it.
- Goto burst outcome (120 bots, goal 108 yd away, random start positions): 128 GOTO_START, 56 GOTO_ARRIVED, 43 NO_PATH (path_type 8 = core
  NOPATH, off_navmesh false), 16 UNREACHABLE_TARGET. The 16 stuck bots are all dwarf/gnome race-3 bots stacked at one spawn point
  (-6094, 818.8, 429.7), 46 yd above the grounded goal z: `bot path` from there gives a zero-length path to its own spot and NOPATH to
  anything 36+ yd away, so their start poly is isolated: a test artifact of the sim spawn position, not a stuck-detection defect (the
  detector fired as designed). **Superseded**: see "Goto navigation fixes" below; the causes were position drift from `bot nudge` and goals over
  navmesh holes/ledges, not isolated spawn polys.
- Not verified: far (cross-map) teleport handshake, instance/battleground deaths, Hardcore path, killer capture in the death event, real
  consumables from bags (FreeFood placeholder), grid-load memory for many far goals, follow across maps, a long partial-path goto.

## Death post-mortem and combat events (verified on the sim 2026-10-06, not committed)

- `death` rows (reason DIED) carry: `killer` (guid, entry, name, level, rank, type creature/player/environment/self, last hit melee/spell, lvl_diff, hp_before), `damage` (last 10 s per source and per spell, hits, total, fight_s, started_by, first_hit_s_before_death), `hp_traj` ([ms before death, hp]), `auras` and `cc`, `resources` (power at death and 10 s ago, armor, gear summary), `combat` (attackers, hostiles_30yd, dealt_10s, strategies per engine, activity), `bots_30yd` and `bots_30yd_fighting`, `repeat` (per bot and per killer entry+zone), `source` (`test_command` for bot kill/hurt/aggro/envdmg, else `bot`), `fight_id`. Header: zone_id, level, target_entry = killer entry. `recent_decisions` limited to 15 s / 10 entries.
- `combat` rows: COMBAT_START (target, first=bot/mob, hp, power, hostiles_30yd, dist, activity) and COMBAT_END (outcome died/target_killed/fled/reset/other, duration_s, dealt, taken, hp_start/end/min, kills), linked by `fight_id = "<botguid>-<unix_ms>"`; END after 2 s without combat or hits (flapping coalesced).
- REVIVED rows carry `dead_s`, `attempts`, `recovery` (corpse_run or spirit_healer), `plan_reason`.
- Tracking: per-bot ring buffers (hits 32, dealt 32, vitals 12 at 1 Hz); heavy work only at death (snapshot avg 146 us). Hooks in Unit::DealDamage / Unit::Kill, env type stashed in Player::EnvironmentalDamage; no core header changed.
- Test aids: `bot aggro <name> [radius]`, `bot envdmg <name> <type 0-6> <amount>`.
- Cost, 180 bots (`bot ai status`): tick steady windows 3.28 us before, 3.17-3.42 us after; hooks outside the tick avg 4.76 us/call; death row 0.8-2.1 KB.

## Goto navigation fixes (verified on the sim 2026-10-06, not committed)

- Root cause of the NO_PATH / stuck bursts: (1) `bot nudge` moved bots in straight lines off the navmesh and they were saved there (every later goto
  NO_PATH, `start_off_navmesh`); (2) burst goals at a fixed offset landed on navmesh holes (gnome, Skyborne) or on ledges 46 yd above/below
  the walkable surface (dwarf, orc: path 0x84, partial, goal far from poly), where the bot walked to the nearest poly and could never arrive
  (arrival needs |dz| < 25). Spawn points (`playercreateinfo`) are fine.
- Code: `PATH_PARTIAL_FAR` (path_fail) now fails fast at GOTO_START when the path is partial and the goal is > 7 yd (3D) from a walkable poly
  (details: path_type, end_gap, end_gap_3d, path_length, nav_end). The goal z is replaced by the navmesh height of the path end when the path is
  complete and within 10 yd, so arrival is judged against the walkable surface. NO_PATH details now carry `start_off_navmesh` /
  `goal_off_navmesh`; GOTO_START carries `end_gap_3d`, `goal_far_from_poly`. `bot nudge` now only moves a bot when the destination is a
  complete walkable path (otherwise skipped and reported). PATH_PARTIAL_FAR is a new reason code (plumber to register it in the schema docs).
- Sim tooling: `C:\ForeverSim\scripts\sim-burst.py` (`--mode valid` probes 16 headings per start and needs `type 0x01 valid 1 partial 0`, matched by
  goal coordinates; `--expect 180` refuses to run unless exactly 180 bots are online), `reset-bot-positions.py` (despawn, reset to
  playercreateinfo, spawn 180). The sim has 360 bot characters; `bot spawn 180` must only run with 0 online (it created 180 extra characters when
  run with 180 online). The sim worldserver is also restarted by other users of the sim (controller/console); check the uptime before a burst.
- Results (sim, 180 bots online, one terminal outcome per bot):
  - Fixed-offset burst, old code, same start positions: 91 arrived, 49 NO_PATH, 39 NO_PROGRESS then 39 UNREACHABLE_TARGET (stuck cycle 8 s x 3).
  - Fixed-offset burst, new code: 90 arrived, 50 NO_PATH (gnome 20 + Skyborne 30, goals on mesh holes), 39 PATH_PARTIAL_FAR (orc 18, dwarf 21)
    failed at GOTO_START instead of after about 24 s of walking; 0 stuck.
  - Valid-goal burst (goal probed as a complete path), 160 bots (the gnome start has no valid goal in 16 headings): old code 14:04 had 30 orcs
    stuck (path 0x01 but z mismatch), new code 159 GOTO_ARRIVED, 0 NO_PATH, 0 stuck; the 160th bot died on the way (spirit healer path).
- Not verified: the gnome start (-4983, 878, 274) has no complete path in any of 16 headings at 108 yd (not investigated); why some 400-500 yd
  paths return 0x0A; Skyborne goals at 108 yd (only holes tested).

