# Dynamic bot population (suggestion 8)

Bots are normally spawned by hand (`.bot spawn`). With `Bot.Population.Enabled = 1` the number of pool bots follows the server load.

- Target = (`Base` + `PerPlayerTenths`/10 per real player) x `HourPct[hour]`/100, clamped to `Min`..`Max`. Real players are the sessions with
  a character in the world; bots and alts are not counted.
- Every `IntervalSec` (60 s, after `StartDelaySec`) the population moves towards the target by at most `StepUp` (5) logins or `StepDown` (3)
  logouts, and not at all within `Hysteresis` (2) bots of it. Logins go through the normal queue (`Bot.Login.MaxPerTick`) and create
  balanced new characters when too few exist.
- Logouts take online pool bots that are not in combat and not grouped with a real player (`LOGOUT_POPULATION` in the lifecycle log). Alts are
  never touched.
- Each change is one server log line `Bot population: N bots, target T (P players), step S`.

Code: `BotPopulationPlan.h/.cpp` (pure, tests in `tests/game/BotPopulationPlan.cpp`), `BotPopulation.cpp` (glue called from `BotMgr::Update`),
`BotMgr::PoolBotCount` and `BotMgr::TrimPoolBots`.

Not verified on a sim server. Watch `BOT_TICK_STATS` while the population grows to confirm the step sizes keep the tick cost flat.
