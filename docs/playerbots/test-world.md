# Bot test world (simulation environment)

**Date:** 2026-10-06. Part of phase 11b in [feature-plan.md](feature-plan.md) (owner agent: `bot-sim`; results read by
`bot-analyst`; single-phase verification by `bot-qa`). This is a design and requirements note, nothing here is built.

Goal: a separate world where bots run many dungeons and quest chains unattended, so behavior can be tuned from data
instead of from watching bots.

## 1. Shape of the environment
- **A second worldserver process on the Forever VM (192.168.34.32)**, its own config copy, its own ports and
  `RealmID`, sharing the same `run\data` game data (read-only). Bots have no network socket, so **no `bnetserver`
  and no client are needed**: the sim server needs only worldserver and its databases.
- **Databases (NAS MySQL), minimal new data:**
  - `forever_sim_auth` and `forever_sim_characters`: new, small; sim accounts, bot characters, instance state. Wiped or
    restored from a snapshot between batches.
  - `forever_sim_log`: bot events for sim runs (or `forever_botlog` with a `run_id`; a separate database keeps real
    play data clean and lets whole batches be dropped).
  - `forever_world` and `forever_hotfixes`: **shared read-only with the main realm** (worldserver only writes a
    version row on start; run the sim with `Updates.EnableDatabases = 0`). This avoids copying a database of about
    1 GB and keeps sim and main content identical. A frozen copy (`forever_sim_world`) is the alternative if a batch
    must not move with merges.
- Every NAS database or user creation is a write: it needs the owner's explicit request and a confirm, as always.

## 2. What already exists in the core (read from the source tree)
- GM commands usable from a harness: `.instance unbind|listbinds|stats|setbossstate|getbossstate`,
  `.character level`, `.group level`, `.reset`, `.tele`, `.go`, `.group` commands, `.server` commands.
- Vanilla dungeon content: the converted VMaNGOS world (`sql/custom/world/*_vanilla_*`, instance portals
  `..._classic_instance_portals.sql`, areatrigger teleports); the extracted data includes vmaps for all maps and
  mmaps for 69 maps once generation finishes.
- A death recap table and entries already exist in the world SQL (`death_recap`), which may feed the `death` events.
- Config knobs: `MapUpdate.Threads` (default 1, raise for the sim), `ThreadPool`, `Instance.UnloadDelay`,
  `AccountInstancesPerHour` (default 10: **must be raised or disabled** or bots hit the hourly instance limit),
  `Instance.IgnoreLevel`, `MaxCoreStuckTime`, `mmap.enablePathFinding`, `vmap.enableLOS`.

## 3. What has to be built (none of it exists)
| Piece | Notes | Owner |
|---|---|---|
| Bot sessions/login (phase 1) and the engine (phase 2) | hard prerequisite: no bots, no sim | `plumber` |
| Party builder | create a 5-bot party by class/role template, enter a dungeon, leave it | `plumber` + `class-ai` |
| Bot factory | create, level, gear (by stat weights/premades), train spells, to a target level | `class-ai` |
| Run controller | start run, detect end (clear, wipe, timeout, stuck), record outcome, reset, start next | `bot-sim` + `plumber` |
| Instance reset and respawn | reset instance state programmatically between runs; resurrect and teleport bots to the entrance; ignore the hourly limit | `plumber` |
| Run id and outcome in the log | `run_id`, `run_outcome`, duration, party composition, strategy/rules version; encounter and aura events | `plumber` |
| Batch runner and A/B tooling | N parallel runs, equal sample sizes per variant, collect results | `bot-sim` |
| Analysis | post-mortems, rule proposals, A/B comparison | `bot-analyst` |

## 4. Capacity on this machine (measured today, estimates are guesses until phase 2 gives a per-bot cost)
- Xeon E5-2695 v4, 4 cores / 8 logical, 20 GB RAM (11 GB free), 37 GB free disk. The mmaps run is currently using
  about half the CPU.
- A worldserver loads a lot of static data (expect a few GB RAM per process); prefer one sim process with several map
  threads over many processes. RAM allows two or three processes at most.
- 180 bots = about 36 parties. Whether that runs in parallel on 8 logical cores depends on per-bot tick cost, which we
  do not know. **Measure first** (idle bots, then bots in combat), then size parallelism. Rough expectation: this VM
  supports on the order of 5 to 15 simultaneous dungeon runs, not 36; a day for 1000 runs is a stretch here.
- If more throughput is needed, options are a bigger or dedicated sim host, a time-compression mode (core changes,
  risky), or smaller dungeons and quest chains first.

## 5. Order of work
1. Plumbing: bot log verified, phase 1 (login) and phase 2 (engine) with measured tick cost for 180 idle bots.
2. Sim server skeleton: second worldserver config, sim databases, run controller with a trivial scenario (bots enter an
   instance, wait, leave, reset): proves reset and unattended looping before any AI exists.
3. Quest-chain scenario first (phase 8 behavior, levels 1-10, open world): cheaper and more parallel than dungeons.
4. Dungeon scenarios, starting with Ragefire Chasm or Deadmines, once bots can sometimes clear.
5. Batches and A/B tests with `bot-analyst` reading the results.

## 6. Risks and open questions
- No bot behavior exists yet; early batches will mostly measure bot bugs, not strategy quality.
- Core changes may be needed for reliable instance reset and for stopping the hourly instance limit cleanly.
- The sim must never touch the main realm's databases or ports; separate config files and distinct `RealmID`.
- Decisions for the owner: shared versus frozen `forever_world` for the sim; which scenarios first (quests or
  dungeons); whether to buy or borrow more compute than this VM.
