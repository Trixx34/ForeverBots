# Bot watchdog (suggestion 1)

A bot can end up doing nothing for a long time: stuck in geometry, waiting for a quest target that never spawns, or looping on a
refused action. The goal-level stuck handling in `BotMotion` only sees a walk that does not advance. The watchdog looks at the bot as a
whole and recovers it in steps.

## Progress

A bot made progress when any of these changed since the last mark: it moved 10 yards or changed map, its experience or level, its
money, its quest log (ids and states, plus the count of rewarded quests). It is also not stalled while it is dead, in combat, on a
taxi, being teleported, or inside a dungeon or battleground (those have their own recovery).

## Steps

| Stall | Step | Reason code |
|---|---|---|
| `StallSec` (600 s) | drop the movement goal and halt, so the planners choose again | `WATCHDOG_CLEAR_GOAL` |
| `StallSec * HearthMul` (2x) | hearthstone, if the bot has one off cooldown | `WATCHDOG_HEARTH`, or `WATCHDOG_HEARTH_SKIPPED` |
| `StallSec * HomeMul` (4x) | teleport to the home bind; the stall clock restarts | `WATCHDOG_HOME` / `WATCHDOG_HOME_SKIPPED` |

Each step is handed out once per stall. Hearthstone and home steps are capped at `MaxPerHour` per bot; past the cap only
`WATCHDOG_EXHAUSTED` is written, which is the signal that a bot needs a human or a log analysis.

## Config

`Bot.AI.Watchdog.Enabled` (default 0), `StallSec`, `HearthMul`, `HomeMul`, `Hearth`, `Home`, `MoveYards`, `MaxPerHour`; see
`botserver.conf.dist`.

## Code

- `BotWatchdogPlan.h/.cpp`: the `Tracker`, pure over plain data. Unit tests in `tests/game/BotWatchdogPlan.cpp`.
- `BotWatchdog.h/.cpp`: reads the snapshot, runs the steps, registers the `watchdog` strategy (NonCombat).

## Not verified yet

Nothing has run on a sim server. Look for the `WATCHDOG_*` rows after a long run; a high rate of `WATCHDOG_CLEAR_GOAL` means a planner
idles legitimately (raise `StallSec` or add the case to the progress signals).
