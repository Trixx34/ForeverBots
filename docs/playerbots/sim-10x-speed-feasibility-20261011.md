# Sim at ~10x game speed: feasibility (research only, 2026-10-11)

Goal: a 12 h wall run covering ~120 h of bot time. Nothing was built or changed; the running sim DBs were not touched. Numbers come from the source, `C:\ForeverSim\server\logs\Server.log` (1319 `BOT_TICK_STATS` lines) and a read-only look at the running process.

## Verdict

- A true 10x at 800 bots is not feasible on this VM (4 cores / 8 threads). Realistic ceiling is roughly 1.5-2.5x at 800 bots, or ~10x only at about 80-100 bots.
- The reason is CPU, not code: the sim is already tick-bound at 1x.
- Recommended: a measured hybrid (section c): optional fixed-step simulated clock, plus Rate.* approximations for the slow "bulk" activities, at 300-400 bots.

## Headroom at 800 bots (evidence)

- `MinWorldUpdateTime` = 1 ms (default), so the world loop spins and `diff` equals the previous tick's processing time. Sim time == wall time always. Tick cost vs bots from Server.log (avg serverDiffMs): 150 bots 3 ms, 360 bots 13, 480 bots 72, 600 bots 80, 720 bots 266, 800 bots 600-1470 ms (rising over the run).
- At 800 bots a world tick takes ~1 s. So the sim already runs on 1-second time steps: movement, melee and AI see coarse diffs. Fidelity is already degraded at 1x. Do not make this worse.
- `BotMgr::Update` itself is cheap (about 0.5-1.7 ms avg, up to 7 ms max). The cost is Map/Session/Player updates inside the map threads (`MapUpdate.Threads = 4`) plus bot AI and pathing.
- Machine now: worldserver (14 threads) with total CPU about 40% of 8 logical processors, so ~3 cores busy. Perfectly parallel scaling would cap at ~2.5x; the serial world-thread portion makes 1.5-2x more likely. The superlinear tick-cost growth (720 to 800 bots) suggests contention or a serial hotspot, which nobody has profiled.
- Unknown: cost per simulated second at a small fixed diff (say 100-200 ms). Per-tick fixed cost drops with fewer, longer ticks, so I cannot derive it from the log. A short measurement is the first step (see effort).

## What is time-based in the fork

Three clock families, which matters for any time-scale design:

1. Diff-driven (`WorldUpdateLoop` -> `World::Update(diff)` -> `Map::Update` -> `Player/Unit/Creature::Update(diff)`): spells (cast time, aura ticks, durations), regen, combat swing timers, creature AI timers, motion/spline progress, respawn check timers, `WUPDATE_*` world timers (auctions, player save `PlayerSaveInterval` = 90000, AHBot), grid relocation, weather. Scale automatically if diff is scaled.
2. `GameTime::*` and `getMSTime()` (`Timer.h`/`GameTime.cpp`, 693 uses in src; `UpdateGameTimers()` refreshes them once per world tick from `time()`, `getMSTime()`, system/steady clock): `SpellHistory` cooldowns (uses `GameTime::GetTime<Clock>()`), respawn times, corpse decay, item/aura persistence, mail, auctions, daily reset, game events, WoW time of day, session idle timers. Do NOT scale with diff; need a clock hook.
3. Direct wall clock (`steady_clock::now`, `system_clock::now`, `time(nullptr)`, ~299 uses outside Bots): profiling timers, DB latency, Bot log timestamps. Mostly should stay wall time.

Bots specifically:
- `BotAI` accumulates `_nowMs` from the diff passed by `Player::Update`, and buckets by `Bot.AI.TickMs` = 500. So bot decisions scale with diff and tick at most once per world tick. At diff 1 s a bot gets <=1 decision per tick already. At 10x with an unchanged loop, diff would be ~10 s.
- `BotQuest.cpp` uses `getMSTime()` for dead-zone memory (`g_deadly`, `DeadlySec`), chest quarantine (`g_goBad`, `QuarantineSec`), hub-fail (`Bot.Quest.HubFailGlobalSec` 1800 s), spawn claim and cooldown. These are wall-clock based and would not speed up with a diff scale unless `getMSTime` itself is scaled. Also `BotChat` (`GetGameTimeMS`) and `BotAuction` (`GetGameTime`).
- Logging timestamps (`BotMgr.cpp` `system_clock`): wall time. Analysis tooling would need a sim-time column or an offset.
- `BotPopulation`, `BotSocial`, `BotParty`, `BotDungeonRun` take `diff` from `BotMgr::Update`; they scale with diff.

## Option (a): true global time scale

Two sub-designs.

a1. Multiply diff by K, keep the loop free-running. Cheap (one line in `WorldUpdateLoop`) but wrong: at K=10 and 1 s ticks the world steps in 10 s increments. Splines overshoot waypoints, melee/aura ticks are skipped or batched, bots stop reacting before dying, LoS/aggro checks are missed, `MaxCoreStuckTime` watchdog logic sees nothing odd but creature evade/leash behave erratically. Rejected.

a2. Fixed-step simulated clock ("as fast as possible"): `WorldUpdateLoop` calls `World::Update(STEP)` with STEP = 100-200 ms of sim time with no sleep, and a single hook makes `getMSTime`, `UpdateGameTimers` (`time()`, system/steady points) and the bot wall-clock users return sim time (sim clock = start + accumulated STEP). Needs:
- Central sim clock in `Timer.h`/`GameTime.cpp`, switchable by config (`Sim.TimeScale`/`Sim.FixedStepMs`), default off so production behaviour is untouched.
- Replace the ~300 direct `time(nullptr)`/`system_clock`/`steady_clock` uses that mean "game time" (audit; many are profiling and must stay wall). Items, mail, auctions, quest reset, SpellHistory and holidays go through `GameTime`, so mostly covered by the hook.
- Bot code: `getMSTime()` in BotQuest etc. is covered if the hook is in `getMSTime`; but `getMSTime` is also used by the log flusher, DB ping and network timers, so a second function (`getSimMSTime`) is cleaner than hooking the global.
- Real-time consumers that break or misbehave: DB sessions/ping timers, `MaxCoreStuckTime` freeze detector (watchdog is wall-clock; fine), `BotMgr` TCP probe, async DB ops completing in wall time (a result may arrive "10 sim minutes later"), the `ws-host.py` console, sim controller timeouts (`run-12h.ps1`), anything waiting for a bot to finish an action in wall seconds.
- Speed achieved = what the CPU gives (about 1.5-2.5x at 800 bots), not a chosen 10x. A real 10x needs ~80-100 bots.
Risks: the cost of ticks is nonlinear in bots; fixed-step makes more ticks per sim second (each tick has fixed overhead: visibility, AI bucket checks, session updates), so the gain per tick is smaller than expected. Hidden wall-clock use in 299 call sites can cause subtle bugs (e.g. cooldown expiring never or immediately). Results become CPU-dependent. DB write volume per wall second rises by the speedup (bot_event batches: `BotLog.MaxBatch` 500 per `FlushIntervalMs` 1000 = 500 rows/s cap, would need raising, and the sim MySQL on 127.0.0.1:3307 has to keep up).

## Option (b): config-rate approximations (no code)

All in `worldserver.conf` of the sim; each only accelerates one aspect and distorts others:

| Setting | Effect | Distorts |
|---|---|---|
| `Rate.XP.Kill`/`Rate.XP.Quest` (x5-10) | levels faster, covers 120 h of progression | Quest/level pacing per hour of travel and fights; bots hit later zones with less time spent per zone; gear/skill lag behind level; wrong "time per quest" and economy. Best single lever for progression studies. |
| `Rate.Health`/`Rate.Mana`/`Rate.Energy`/`Rate.Rage.*` regen | less downtime between pulls | Fights unchanged; food/drink and rest logic becomes unrepresentative. |
| `Rate.Creature.Damage.*` / `HP.*` | shorter fights | Shifts death rate and kill speed; breaks death/risk analysis. Avoid for death-focused studies. |
| `Rate.MoveSpeed` (min 0.01) | faster travel | Affects players and creatures (mobs chase and flee faster); path/stuck/leash behaviour changes; spline step length per tick grows, interacts badly with 1 s ticks; client-visible speeds. Travel timing results no longer representative. |
| `Rate.Rest.*`, `Rate.Corpse.Decay.*`, `Respawn` knobs | quicker respawn / corpse decay | Mob density, competition for quest mobs, camping. Useful because bot crowding is a main sim question; distorts if you test crowding realism. |
| `Rate.Auction.Time`, auction deposit/cut | faster AH expiry | Market dynamics. |
| `Bot.AI.TickMs` (50-10000) | not a speedup | Smaller = more CPU; larger = coarser decisions. |
| `MinWorldUpdateTime`, `MapUpdate.Threads`, `MapUpdateInterval` | only CPU tuning | n/a; useful to get a smoother 1x. |

There is no config key that scales the clock. Other fork Bot.* settings (214 time keys in `botserver.conf.dist`: `Bot.Quest.HubFailGlobalSec`, `DeadlySec`, `QuarantineSec`, etc.) can be divided by K by hand to match, but you must decide which ones are "game time".

## Option (c): hybrid (recommended)

1. Keep the wall clock real (1x tick semantics) but lower the load per sim-second: fewer bots (300-400), `Bot.AI.TickMs` as is, per-tick detail logging off.
2. Use config rates for the slow bulk activities: `Rate.XP.Kill/Quest` x5-x10, moderate regen rates (x2-3), shorter respawn and corpse decay. Do not touch damage, HP or MoveSpeed.
3. Optionally add a2 (fixed-step sim clock) later only if a CPU benchmark shows a worthwhile speedup.

Expected: about 3-5x progression per wall hour via XP rates; combat, travel, death, stuck and path behaviours remain representative; economy and mail/auction timing distort.

## Representative vs not (under a2 and under hybrid)

- Representative under a2 (fixed step, 100-200 ms): combat, spells, deaths, pathing/stuck, travel, quest logic, AI decisions. Not representative: anything with real-time dependencies (DB latency, TCP probe), and logs need sim-time stamps.
- Under hybrid: representative combat, pathing, deaths, travel durations. Not representative: levelling pace per hour (intentionally), regen/rest, respawn density, auction/mail expiry timing.
- Under a1 (diff x K): nothing about movement/combat is representative. Do not use.

## Effort estimate

- Step 0, benchmark (about 0.5 day, sim only): run 400 bots with a hack-free config at `MinWorldUpdateTime` 1 and record `BOT_TICK_STATS`; then compare CPU per tick to confirm what the 800-bot superlinear growth is (profile the world thread with a sampling profiler). Also check if the 1 s ticks are already a fidelity problem (a quick look at stuck/death ratio vs bot count).
- Option (b) config only: 0.5 day (choose rates, run a short A/B vs 1x on a 150-bot sample).
- Option a2: 3-5 days: sim clock in Timer/GameTime (config-switchable), world loop fixed step, audit of ~300 wall-clock calls, bot wall-clock users (BotQuest, BotChat, BotAuction, BotMgr timestamps), sim-time column in bot_event or a conversion, controller scripts, and a validation run versus 1x. High risk of subtle bugs; payoff limited by CPU.
- Option (c): (b) plus Step 0 = 1 day; add a2 only after.

## Decisions for the caller

1. Is 10x at about 80-100 bots acceptable, or must the population stay at 800? (If 800 is required, plan for 1.5-2x at best.)
2. Which analysis questions matter: progression pacing (XP rates are fine) versus deaths/stuck/crowding (needs honest time).
3. OK to profile the sim's world thread (bot-sim/plumber) before investing in a2?
4. If a2 is built, bot_event timestamps: store sim time, wall time, or both.
