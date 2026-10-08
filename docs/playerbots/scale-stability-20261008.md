# Scale and stability (item 7), 2026-10-08

Status: code and SQL written, NOT compiled, NOT sim-verified. Covers next-plan steps F (24 h soak) and H (main-realm deploy).

## What changed
- `bot_pos` retention is configurable: new `botlog_roll_partitions3(keep, hot, pos_days)` (forever-botlog-setup.sql and forever-botlog-migrate-1.sql). `botlog_roll_partitions2` is now a wrapper with pos_days = 2, so behaviour is unchanged by default. To keep 7 days: `ALTER EVENT botlog_daily_roll DO CALL botlog_roll_partitions3(14, 5, 7);` (drop of old partitions is by name comparison, so lengthening only needs the new call; shortening drops on the next run). Re-apply forever-botlog-migrate-1.sql on existing databases.
- `BOT_TICK_STATS` server log line every `Bot.Log.ServerStatsSec` (default 60, 0 = off; BotMgr.cpp `LogTickStats`): bots online, world update diff avg/max (ms, from `sWorldUpdateTime.GetLastUpdateTime()` sampled each tick) and `BotMgr::Update` cost avg/max (us). Does not need the bot log DB. This is the A/B handle for the visibility fix: run the same bot count on a build with and without the `SetPlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME)` line (CharacterHandler.cpp, HandleBotPlayerLogin) and compare the serverDiffMs lines. `BotMgr::Update` excludes per-bot AI (ticked in Player::Update; see AI_TICK_STATS) and map visibility work, so the server diff is the number that matters.
- Visibility switch: NOT added. Without that flag `Player::CanNeverSee` hides everything from a bot, so target validation and spells fail against every creature (progress.md, 2026-10 entry). A config switch would only produce a broken bot, so the A/B must be done with two builds.

## Needs sim verification
Compile; BOT_TICK_STATS appears and averages are sane; botlog_roll_partitions3 on a scratch DB (partitions kept = pos_days + 3 ahead); the visibility A/B numbers (none exist yet; the only data point is diff 2 ms at 180 idle bots).

## Step F: 24 h soak runbook (180 bots, sim)
Setup: fresh sim DB backup, botlog DB with migrate-1 applied, `Bot.Log.ServerStatsSec = 60`, `Bot.Log.AiTickStatsSec = 60`, `Bot.Log.HotSplit = 1`, 180 bots spawned (`bot spawn`), event_scheduler ON.
1. T0: record `server info`, RSS of worldserver, row counts of bot_event, bot_event_hot, bot_pos, size of forever_botlog (information_schema).
2. Hourly: grep BOT_TICK_STATS; run `SELECT COUNT(*) FROM bot WHERE ...` equivalents: bots online (`.bot status`), crashes, `error` rows (`SELECT reason, COUNT(*) FROM bot_event_all WHERE event_type='error' AND ts > NOW() - INTERVAL 1 HOUR GROUP BY reason`), log_dropped rows, LOG_SUPPRESSED.
3. T+24 h (after the 05 min daily roll has run once): compare against T0, check partitions (`information_schema.PARTITIONS`) and that the roll dropped nothing newer than its retention.
Acceptance:
- Zero worldserver crashes or assertion/fatal log lines; 180 bots still online (logouts only with a logged reason).
- serverDiffMs avg <= 50 ms and max (per window) <= 500 ms; no growth trend across 24 h (hour 24 avg within 20 percent of hour 2). BotMgr::Update avg < 1000 us.
- RSS growth < 10 percent between hour 2 and hour 24 (no leak).
- No `error` severity rows beyond a known list; no log_dropped rows; bot log DB within retention: bot_pos <= pos_days + 1 day of partitions, bot_event_hot <= 6, bot_event <= 15; total DB size stable day over day after day 2 (no unbounded growth).
- No unexplained `stuck` clusters (more than 3 percent of bots per hour).
(Thresholds are proposals, not measured; adjust after the first soak and record the baseline in progress.md.)

## Step H: main-realm deploy runbook
Pre-flight: soak (F) passed on the sim with the same commit; schedule the restart window (a restart drops players), announce it.
1. Full backup of auth, characters, world, hotfixes; confirm restore works (restore to a scratch instance).
2. Create the bot log DB on the main DB host (forever-botlog-setup.sql; real password only in the untracked botserver.conf), check event_scheduler.
3. Build the release commit; keep the previous binary for rollback. Start with `Bot.Enabled = 1` but zero bots: `Bot.Log.ServerStatsSec = 60`. Confirm baseline serverDiffMs with real players only (record 30 min).
4. Ramp: 20 bots, 30 min; 60, 30 min; 180. At each step compare serverDiffMs avg/max and player-facing latency to the baseline. Bot accounts are BOTnnnn; bots never queue ahead of players (check queue/session limits: bots count toward max players, raise `PlayerLimit` or confirm they bypass).
5. Watch: BOT_TICK_STATS, worldserver log, `error` rows, DB connection counts, disk of the log DB.
Acceptance:
- With 180 bots: serverDiffMs avg rises by <= 15 ms and max by <= 100 ms over the player-only baseline, and the avg stays <= 50 ms.
- No player-visible harm: no new player crashes, no queue caused by bots, no bots in player-facing exploit situations (griefing is checked via bot_event rows reviewed after 24 h).
- Bot log DB growth within the retention estimate; 7 days later no partitions older than retention.
Rollback triggers: any crash loop, serverDiffMs avg > 100 ms for 10 min, or player reports of lag attributable to bots. Rollback: `bot despawn all` / `Bot.Enabled = 0` and restart, or restore the previous binary; restore the DB backup only if character data is damaged.
