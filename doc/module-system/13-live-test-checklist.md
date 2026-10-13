# 13. Live test checklist: bot module against a running worldserver

Purpose: first end-to-end run of the module system with the bot lifecycle (`BotMgr`), Tier 2 snapshots and actions,
and the `forever-bots` module. Work through the steps in order. Each step lists what to see. A step that fails stops
the run: record the step number and the log lines around it.

## 0. What you need

- Core branch `bots-actions-config` (or later), built as `worldserver`.
- The module repo `Trixx34/MOD-Forever-Bots`, `main`.
- A world database with the bot accounts and characters the lifecycle expects (`BOT0001`... accounts, see `BotMgr`).
- A dev machine where `Modules.AutoApproveLocal = 1` is acceptable (it skips the capability check; not for production).
- Logs for the `modules` and `server.worldserver` categories at info level, and `modules` at debug level for the
  action lines in step 8. Set the `Logger.*` entries in `worldserver.conf` (the level numbers are in the comments there).

## 1. Build

1. Core: `cmake --build` on `bots-actions-config`. Expect no new warnings from `Modules/` or `Bots/`.
2. Module: `cmake -S . -B build -DFOREVER_MODULE_SDK_SOURCE=<sdk checkout>`, then build and `ctest --test-dir build`.
   Expect `roster` and `policy` to pass. Note the SDK commit (`40b8cf8` or later for the nearby-unit view).
3. Install the module where the core looks: `<Modules.Dir>/forever-bots/` (default `modules_runtime/forever-bots/`)
   must contain `module.json` and the executable `forever-bots` (`forever-bots.exe` on Windows).

## 2. Configure

1. `worldserver.conf`: `Modules.Enabled = 1`, `Modules.AutoApproveLocal = 1`. Leave the `Modules.Tier2.*` bounds
   at their defaults for the first run.
2. `worldserver.conf.d/modules.d/forever-bots.conf`, under the config directory (`worldserver.conf.d`, relative to the
   directory the worldserver starts in). Copy the example from the module repo
   (`config/modules.d/forever-bots.conf.example`) and set `Module.forever-bots.MaxBots = 1`.
3. Expect no config errors at start-up.

## 3. Start and discovery

1. Start `worldserver`.
2. Expect `modules`: `started 'forever-bots' (pid ...), waiting for its handshake.`
3. Expect no `not loaded` or `pending approval` lines for `forever-bots`.

## 4. Handshake and Tier 2

1. Module stderr: no `could not connect to the core` line.
2. Module stderr: no `no Tier 2 region` line. If it appears, the reason is printed after it; the core's
   `Modules.Tier2.*` bounds or the region creation failed.
3. Core log: the handshake is accepted, and a Tier 2 region is created for the module (`ModuleTier2Region::Create`
   has no error).

## 5. Config delivery

1. Set `Module.forever-bots.MaxBots = 2` and restart. Expect the module to request control of two bots and log two
   `controlling bot` lines (MaxBots is read from config; the Tier 2 request ceiling is fixed at 16).
2. Set it back to `1`. Expect one `controlling bot` line.
3. Set `Module.forever-bots.Race = 0`. Expect a config warning in the module's stderr and the default race used.

## 6. Bot spawn and login

1. Expect core: `BotMgr: created bot BOTnnnn (guid ..., race ..., class ...)` on first run, or no line on a run that
   reuses an existing bot.
2. Expect core: `BotMgr: bot BOTnnnn (guid ...) is in the world.` within a few seconds.
3. Expect module: `controlling bot <guid>`, with the same guid as the core's line.
4. Check the database: the character exists, its account is locked, and its last login is recent.
5. Check the logs: no `login of bot ... failed` line. A failure there is a lifecycle bug, not a module bug.

## 7. Snapshots

1. Place the bot near a hostile mob (GM teleport or `.tele` as a player next to it), then restart the test.
2. Enable a temporary debug print in the module's pump (not committed) that logs `tick`, bot count and nearby count
   each time the tick changes. Expect the tick to advance, the bot to be listed, and the nearby count to be the number
   of units the core found within 40 yards.
3. Expect each nearby entry to have a plausible health and a hostile flag that matches the mob.
4. Spell cooldowns are not filled (they read zero). That is expected, not a failure.

## 8. Actions

1. Expect the bot to attack the nearby hostile mob. The module publishes `kAttackTarget`; the core applies it.
2. Expect no `refused` debug lines for a valid mob. Look for these texts:
   - `target is not hostile to the bot`: the module targeted a non-hostile unit, a policy or flag bug.
   - `target is not a live unit on this bot's map`: the unit moved to another map, died, or was not found.
   - `not online; dropped`: the bot was logged out between snapshot and action.
3. Move the mob out of range (it should leave the 40-yard snapshot). Expect the bot to stop attacking and then follow
   the closest player, or stop if there is none (`kFollow` or `kStop`).
4. The `unknown spell` and `does not know spell` refusals are not exercised by the module today (the policy never
   casts). Check them by reading `ModuleBotActions.cpp`, not in the live run.

## 9. Revocation and shutdown

1. Stop the module process (kill it). Expect the core to report the disconnect, and to restart the module within the
   restart policy (`Modules.RestartBackoffMs`, `Modules.MaxRestartAttempts`).
2. On the restarted module, expect the bot to be granted again (`controlling bot` with the same guid) once the core has
   released the old ownership on disconnect.
3. Shut down `worldserver` cleanly. Expect `LogoutAll` lines (`logged out (LOGOUT_SHUTDOWN)`) for each bot that was in
   the world, and the module to receive `Shutdown` and exit.

## 10. Failure paths (optional, after the above passes)

1. Protocol mismatch: change `protocol_version` in `module.json` to `2`. Expect a permanent failure, no restart attempts.
2. Tier 2 refused: set `Modules.Tier2.MaxBots = 0`. Expect the module to log `no Tier 2 region` and not drive bots.
3. Action flood: the policy publishes at most one set per tick, so a flood is not expected. Check that the core's
   action queue does not grow across ticks.

## Recording the result

For each step, note pass or fail and paste the first failing log line. Commit any debug print you added only after
removing it.
