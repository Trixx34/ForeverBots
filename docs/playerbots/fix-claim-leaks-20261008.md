# Fixes for the claim and shared-state leaks (scan of 2026-10-08)

Branch `fix/claim-leaks`, started from `review/claim-leaks`. Findings refer to `scan-claim-leaks-20261008.md`.

## Status per finding

| # | Status | What changed |
|---|---|---|
| 1 | Fixed | `~BotQuestCtx` releases the loot claim. `BotQuestCtx::OnStateEnter` releases it and drops the loot task when the bot enters `Dead`. `BotQuest::OnLogout` is called from `BotAI::OnLogout`. `RunLoot` checks ownership every tick with `GoGateState`; if the spawn is held by someone else (or cooling) it is released without cooling and another is picked, and when the claim has only lapsed it is re-taken with `GoTryClaim`. |
| 2 | Fixed | New owner-checked `GoRenew`; `RunLoot` renews about every 30 s (`GO_RENEW_MS`), also while the bot is in combat. The 150 s limit stays, so a dead owner still expires. |
| 3 | Fixed | `GoSpawnFailed` takes an `objectCause` flag. "not spawned" and "no loot" cool the spawn for everybody; "no approach progress", "cannot interact" and "not active for this quest" release with `coolMs = 0` and only set the per-bot `GoIgnore` entry. |
| 4 | Fixed | While waiting on `r == 2`, `GoNextMs` is set (waiting time, clamped to 1-5 s) and the pick is skipped until then. `GoPickSpawn` collects the candidates first and reads their state with one `GoGateStateBatch` call (one `g_goMx` acquisition per pick). |
| 5 | Fixed | `Choose()` prunes expired `Ignore` entries every 5 s with `std::erase_if`. |
| 6 | Fixed | New `BotMgr::EraseLogState`, called at the end of `LogoutBot` (after the last row of the session, so those rows still get their session number) and on the create-failure `_bots.erase` path. It is the only `_bots.erase` in the file. |
| 7 | Fixed | `Task::GoMap` is stored at claim time. `RunLoot` finishes the task (which releases the claim) when the bot is on another map. `Execute` clears `Ignore` and `Seen` when the map id changes. Only loot tasks are finished on a map change; other task kinds were left alone (the scan's "apply generally at the top of `RunTask`" was not done). |
| 8 | Deferred | Asked in the thread; no profile exists, so no change. |
| 9 | Deferred | Same as 8. |
| 10 | Fixed | `BotQuestLog::OnLogout` erases all `_acceptTimes` entries of the bot; called from `BotMgr::LogoutBot`. Quests that leave the log some other way while the bot stays online still keep their entry until logout. |
| 11 | Fixed | `BotChat::OnPlayerLogout`, called from `WorldSession::LogoutPlayer`, erases the player's `s_verbose` and `s_unauth` entries. Both maps are also capped at 4096 entries (one arbitrary entry is dropped to make room). |
| 12 | Fixed | `_botDeaths` and `_killerZoneDeaths` are cleared when they reach 50000 entries and a new key arrives, so the "repeat" numbers restart at that point. The scan said no change was needed; this was added because the brief asked for a cap. |

Commits: steps 1 and 2 are in one commit (finding 4's batch helper sits in the same code as the claim helpers); step 3 and step 5 are one commit each.

## What was measured and what was not

* **Not built.** The container has no Boost or MySQL development files, so CMake cannot configure and `worldserver` was not compiled. The code has never been through a compiler. A build on a machine with the dependencies is the first thing to do.
* **No tests run.** The repo's fast checks are Catch2 tests that need the same build. None exist for the Bots code.
* **Nothing profiled or run.** No behaviour (claim lifetimes, lock contention, memory growth) was observed; every effect above is reasoned from the code.
* **Lock ordering, read by eye:** `g_goMx` is only taken inside the small helpers (`GoGateState`, `GoGateStateBatch`, `GoTryClaim`, `GoRenew`, `GoRelease`) and nothing inside them calls the AI or the map. `_logMutex` (`EraseLogState`) and `_acceptMutex` (`BotQuestLog::OnLogout`) are taken on their own in `LogoutBot`, not nested.
* **Open point:** `~BotQuestCtx` now touches `g_goMx` and `g_goGate`. If a bot AI were ever destroyed during static destruction after those globals, that would be a problem; the normal logout path destroys the AI long before that. Not checked at shutdown.
* `WorldSession::LogoutPlayer` is the one file outside `Bots/` that was touched (one call at the top of the `_player` block).
