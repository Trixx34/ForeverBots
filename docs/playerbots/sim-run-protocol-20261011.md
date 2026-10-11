# Run protocol for the next sim (N-10)

Not bot code. Goal: turn R-V2-01 to R-V2-03 and N-01 to N-09 into measured results instead of guesses. Basis: `sim-run-800-analysis-20261011.md`, `rules/additional-20261011.md`.

## Setup

1. Same server build and data for the whole run, same zone mix and start levels as the 800 bot run, **800 bots, 6 hours**. Spawn with the same seed and the same `bot spawn` commands; do not change anything while it runs.
2. Arms are assigned by guid. In `botserver.conf` set `Bot.Rules.ArmPct = 50`: bots with guid counter modulo 100 below 50 are **arm B** (rules on), the others **arm A** (control). The same split is used by `Bot.AI.Flee.AbPct` (`BotAvoid::FleeModeFor`), so do not set that option in this run, or the flee experiment mixes into the comparison.
3. Arm B gets: N-01 to N-07 and N-09 on (their defaults), N-08 off (it needs this run to be sized, see "After the run"). The R-V2-01 to R-V2-03 switches (`Bot.AI.Weapon.Restore`, `Bot.AI.Combat.ApproachEscalate.Enabled`, `Bot.Quest.TripGuard.Enabled`) are not tied to the arm: for the run either leave them on for all bots (then the comparison measures N-rules only) or switch them off in a first, control-only half. Write down which one was used.
4. N-09 (log diet) is global, not per arm: both arms write the diet rows, so row counts are comparable. Keep `Bot.Log.Categories` at its default.
5. Telemetry on: `Bot.Log.*` defaults, bot log database reachable, `Bot.Log.RepeatCap` as in the previous run. Record the config file with the results.

## What to measure (per arm, per class, per hour)

| Metric | Query basis |
|---|---|
| Bots with at least one XP event per hour (progress, the primary metric) | `bot_event` type `xp`, distinct bots per hour |
| XP per hour per bot | sum of `xp.amount` per bot and hour |
| Deaths per bot-hour | `death` rows over online hours (`login` to `logout`) |
| Full-bag bot-hours | `BAG_FULL` and `VENDOR_RESULT` free slots |
| Stall recoveries | count of `STALL_XP` by step, bots with more than one per hour |
| Rule side effects | count of `ZONE_FENCE`, `PULL_BLOCKED`, `FIGHT_NO_DAMAGE`, `ZONE_LEAVE`, `CROWD_SPREAD`, `BAG_PRESSURE` per bot-hour |
| Log rows per bot-hour | rows in `bot_event` and `bot_event_hot` per arm |

Split every metric by class (hunter, warlock, rogue, warrior are the weak four) and by level band (1-10, 11-20, 21+). Ignore the first 30 minutes (ramp up, logins spread by `Bot.Login.MaxPerTick`).

## Judging

- Arm B wins when bots with an XP event per hour is higher by a margin that is larger than the difference between the two halves of arm A (split arm A by guid parity to see the noise).
- A rule is kept when its own rows show it fired and the metric it targets moved in the right direction; a rule that never fired (zero rows) is untested, not good.
- Watch for harm: more `STALL_XP` relogs per hour than before, `ZONE_FENCE` dropping a quest that many bots need (check the top quests), `PULL_BLOCKED` with the same block for more than 10 minutes, XP per hour falling in arm B.

## After the run

- Size N-08 from the deaths per pick by level difference and class (arm A rows), then run a second 6 h run with `Bot.Rules.ClassMargin.Enabled = 1` in arm B.
- Check the per-hour log diet caps against the analysis (`TARGET_PICKED` and `AGGRO_AVOID` rows per bot-hour) and move them if the analysis lost something it needed.
- Put the numbers in `docs/playerbots/sim-run-<date>-analysis.md` with the config used.
