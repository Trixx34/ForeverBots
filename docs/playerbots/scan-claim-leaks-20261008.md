# Scan: resource-claim and shared-state leaks in `src/server/game/Bots`

Date: 2026-10-08. Base: branch `forever`. Static read only; no code was changed and nothing was profiled or run, so costs below are estimates from the code, not measurements.

## Scope and method

All 19 files in `src/server/game/Bots` were read for:

1. claims, reservations, back-off timers and per-bot state held in global, static or thread-local containers;
2. whether each entry is released on death, logout, removal, task change and quest abandon;
3. raw pointers or GUIDs kept across ticks or map changes;
4. containers that grow with bot count or run time;
5. per-tick work that scales with the square of the bot count.

Reference pattern: the loot task added in `d3c1061a54` and `fc54d29110` (`BotQuest.cpp`: `g_goGate`, `GoTryClaim`, `GoRelease`, `Task::GoSpawn/GoBot`). Its design is: claim keyed by spawn, owner recorded, a time limit on the claim, release in `Finish()`, and a cooling period after a loot. The findings below are the places where the same discipline is missing or incomplete, including in the reference pattern itself.

## Summary

| # | Severity | Area | One line |
|---|---|---|---|
| 1 | High | Loot claim (reference pattern) | Claim is released only by `Finish()`; death, logout, removal and strategy removal leave it to expire, and a revived bot resumes without re-claiming |
| 2 | High | Loot claim | Claim time limit (150 s) is never renewed, but a task may legitimately run up to 12 min |
| 3 | Medium | Loot claim | A bot-specific failure writes a global cooling period that blocks every bot |
| 4 | Medium | Loot claim | While waiting for respawn, every tick scans all spawns and takes the global mutex once per spawn |
| 5 | Medium | Quest context | `Ignore` map grows without bound and is never pruned |
| 6 | Medium | Logging | `_botLogState` is never erased for logged-out or removed bots |
| 7 | Low-Medium | Task state | Loot task is not revalidated after a map change |
| 8 | Low-Medium | Per-tick scans | Crowding counts walk the whole map player list per fight row |
| 9 | Low-Medium | Per-tick scans | Per-bot per-second creature scans rebuild near-identical lists for clustered bots |
| 10 | Low | Quest log | `_acceptTimes` entries survive logout and non-reward quest removal |
| 11 | Low | Chat | `s_unauth` and `s_verbose` are keyed by issuer and never pruned |
| 12 | Low | AI | `_botDeaths` and `_killerZoneDeaths` are process-lifetime by design but uncapped |

No raw `Unit*`, `Creature*`, `Player*` or `GameObject*` is kept across ticks (see "Checked and found sound").

---

## Findings

### 1. High: loot claim is not released on death, logout, removal or strategy removal

* Claim state: `BotQuest.cpp:778-780` (`GoGate`, `g_goGate`), claim at `BotQuest.cpp:807-815`, release at `BotQuest.cpp:818-830`.
* The only caller that releases on a normal exit is `Finish()` at `BotQuest.cpp:1223-1228`, which runs from `RunTask`.
* `QuestThinkAction::Execute` returns early while the bot is dead (`BotQuest.cpp:1184-1189`) and never reaches `Finish()`. `BotAI::ChangeState` (`BotAI.cpp:343-367`) calls `OnStateEnter()` on every value, but `BotQuestCtx` (`BotQuest.cpp:914`) does not override it (hook declared at `BotEngine.h:88`).
* Logout: `BotMgr::LogoutBot` (`BotMgr.cpp:1517-1541`) calls `BotAI::OnLogout` (`BotAI.cpp:905`), which only closes the fight row, then drops the AI. `BotAI::~BotAI() = default` (`BotAI.cpp:197`) destroys `BotQuestCtx` with no release. The same happens when the quest strategy is removed (`BotAI.cpp:650`) or the AI is rebuilt.
* Effect: a spawn stays "Busy" for other bots until the 150 s limit runs out. Deaths were 960 per hour in the R5 note, so a claim held by a bot that died while on its way to a single-spawn chest is routine. The bot is also not safe to resume: its `Task` still holds `GoSpawn`, the claim may have expired and been taken by another bot, and `RunLoot` (`BotQuest.cpp:2827`) never calls `GoTryClaim` again, so two bots can work the same spawn.
* Fix:
  1. Add a `BotQuestCtx::~BotQuestCtx()` that calls `GoRelease(T.GoSpawn, T.GoBot, 0)` when `T.K == Kind::Loot && T.GoSpawn`.
  2. Override `BotQuestCtx::OnStateEnter()` so entering `BotState::Dead` releases the claim and drops the loot task (`T.GoSpawn = 0`, `Finish`), because a corpse run or spirit healer run changes position anyway.
  3. Add a `BotQuest::OnLogout(ai)` call from `BotAI::OnLogout` for the explicit logout path (the destructor covers the rest).
  4. In `RunLoot`, when `t.GoSpawn` is set, re-check ownership each tick with `GoGateState` and re-run `GoTryClaim`; if it returns false, drop the spawn and pick another.

### 2. High: claim time limit is never renewed

* `GO_CLAIM_MS = 150 * 1000` (`BotQuest.cpp:781`), set once in `GoTryClaim` (`BotQuest.cpp:812`). Nothing refreshes `ClaimUntil` while the bot walks.
* The task itself may run up to 12 min (`BotQuest.cpp:2854`) and the approach allows 60 s without progress, so a long walk from a far spawn exceeds 150 s. After that the spawn reads as free to others while the original bot is still on its way, which defeats the purpose of the claim on single-spawn objects.
* Fix: refresh `ClaimUntil` (owner-checked) from `RunLoot` about every 30 s while `t.GoSpawn` is set (a small `GoRenew(spawn, bot)` next to `GoTryClaim`). Keep the short limit so a dead owner still expires.

### 3. Medium: one bot's failure cools a spawn for every bot

* `GoSpawnFailed` (`BotQuest.cpp:2759-2772`) passes the respawn time as `coolMs` to `GoRelease` for every `why`, including "no approach progress", "cannot interact" and "not active for this quest". Those are bot-specific (path, level, quest state). The global cooling period at `BotQuest.cpp:828-829` then blocks all bots from that spawn for up to 600 s.
* Fix: use the global cooling period only for causes that describe the object (looted, not spawned, no loot). For bot-specific causes release with `coolMs = 0` and keep only the per-bot `GoIgnore` entry. Pass a flag or the `why` code into the helper.

### 4. Medium: respawn-wait polling takes the global mutex per spawn per tick

* `GoPickSpawn` (`BotQuest.cpp:2776-2825`) loops over every spawn of every source object and calls `GoGateState`, which locks `g_goMx` (`BotQuest.cpp:793-794`) on each spawn.
* When the result is `r == 2` (all cooling) the caller returns `false` without setting a next-check time (`BotQuest.cpp:2874-2890`), so the scan repeats every tick for up to 90 s per bot. With hundreds of bots on the same few quests this is O(bots x spawns) lock acquisitions per tick on one mutex.
* Fix: set `t.GoNextMs = now + 1000` (or the remaining wait, capped) when waiting, and lock once per pick instead of once per spawn (a `GoGateStateBatch` that copies the needed entries under one lock).

### 5. Medium: `BotQuestCtx::Ignore` grows without bound

* Declared at `BotQuest.cpp:926`; written at `BotQuest.cpp:1895`, `3152`, `3242`; read at `3015`. Nothing erases entries (no `erase` or `erase_if` on any of the ctx maps in `BotQuest.cpp`).
* Keys are creature GUID counters, which keep increasing as creatures respawn, so each bot's map only grows over a long run. At 300 to 700 bots over days this is a slow linear leak. Expired entries are never read as live but are never freed either.
* The other per-bot maps (`Blacklist`, `HubBlack`, `SvcBlack`, `Repeats`, `Logged`, `GoIgnore`) are keyed by quest, hub, NPC entry or spawn id and are bounded by the size of the index. They are harmless in size.
* Fix: prune expired entries in `Choose()` every N seconds (`std::erase_if` on `Ignore` where `now >= value`), and clear `Ignore` and `Seen` when the map id changes (see #7).

### 6. Medium: `_botLogState` is never erased

* `BotMgr.h:295-298`, created at `BotMgr.cpp:535` and `585`. `FlushSuppressed` (`BotMgr.cpp:565-576`) reads it but never erases. `LogoutBot` (`BotMgr.cpp:1537`) and the `_bots.erase` paths (`BotMgr.cpp:1447` and the create-failure path) leave the entry behind.
* Entries hold two string-keyed maps (`Counts`, `Pending`). The key space per bot is bounded by the capped event types, but the entry count grows with every bot that ever logged in during the process lifetime, and `ApplyRepeatCap` (`BotMgr.cpp:535`) also creates an entry for any guid it sees, including guids that never log in again.
* Fix: after `FlushSuppressed` in `LogoutBot`, erase the entry under `_logMutex`; erase in the `_bots.erase` paths too. `BeginLogSession` already recreates it on login.

### 7. Low-Medium: loot task is not revalidated after a map change

* Spawns are filtered by map only at pick time (`BotQuest.cpp:2790`). After a map change (graveyard on another map, a teleport command, an instance), `RunLoot` continues with `t.GoX/GoY/GoZ` from the old map: `dSpawn` (`BotQuest.cpp:2906`) compares coordinates across maps, `Travel` is asked for a path on the wrong map, and the task ends after 60 s of "no approach progress" through `GoSpawnFailed`, which (with #3) also cools a valid spawn globally.
* Task `Target` and the other `ObjectGuid` fields (`BotQuest.cpp:705`) are resolved by lookup and fail safely, so there is no dangling pointer, only wasted time.
* Fix: store `GoMap` in `Task` and, when `bot->GetMapId() != t.GoMap`, call `Finish()` (which releases) and return. Apply the same check generally at the top of `RunTask`, and clear `Ignore` and `Seen` on a map change.

### 8. Low-Medium: crowding counts walk the whole map player list

* `BotAI.cpp:1017` (fight start) and `BotAI.cpp:1375` (fight end / death row) iterate `map->GetPlayers()` and test distance for each player, once per row.
* In a start zone with a few hundred bots on one map this is O(bots on map) per row, so O(n^2) when many fights start or end in the same window. Cost is limited to event rows, so it is not per-tick; estimated small per call, but it scales with the square of the zone population.
* Fix: use a grid-local search (`bot->GetPlayerListInGrid(list, 40.0f)`) or a per-tick neighbour count shared across the bots of one cell.

### 9. Low-Medium: per-bot creature scans repeat the same work for clustered bots

* `BotBehavior.cpp:254` (aggro scan, at most once per second per bot, radius 40 yd plus margin), `BotQuest.cpp:1869` and `3123` (target search lists), `BotBehavior.cpp:743` (400 yd spirit healer search, repeated each time `Healer` is empty and no healer was found).
* Each call is O(creatures in range) per bot. With hundreds of bots in the same cells the same creature lists are rebuilt per bot per second. Not measured.
* Fix: cache the creature list per map and cell for the current tick (or for 1 s), and for the healer search cache the nearest healer per map and zone once; throttle the 400 yd search with a next-try time when none is found.

### 10. Low: `_acceptTimes` survives logout and non-reward removal

* `BotQuestLog.cpp:39`; inserted at `138`; erased at `181` (reward), `225` (abandon) and `246`. A quest that leaves the log another way (timed quest failure, removal by command or script, character deleted, bot removed) keeps its entry for the process lifetime. In practice each bot holds at most the size of the quest log plus stale rows, so growth is slow.
* Fix: erase all entries for the bot's guid in `LogoutBot` (range erase on the ordered map), and erase on the quest-removal hook if one exists.

### 11. Low: chat maps keyed by issuer are never pruned

* `s_unauth` (`BotChat.cpp:281`, written at `644`) and `s_verbose` (`BotChat.cpp:273`, written at `666-668`) are keyed by the issuing player's GUID counter. Entries are added for every player who sends a bot command and are never removed. Growth follows the number of distinct players, not bots, so it is slow.
* Fix: erase the issuer's entries when the issuer logs out (or age out `s_unauth` records older than `UnauthLogSec`), and cap the map size.

### 12. Low: death counters are process-lifetime and uncapped

* `BotAI.cpp:774-775` (`_botDeaths`, `_killerZoneDeaths`). The comment states this is intended. `_botDeaths` is bounded by bots ever created, `_killerZoneDeaths` by killer entries times zones.
* Fix: none needed now. If the registry grows, add a size cap or reset on `bot clear`.

---

## Checked and found sound

* `g_dead` (`BotQuest.cpp:751`), `g_hubState` (`BotQuest.cpp:836`) and `g_goGate` (`BotQuest.cpp:780`): bounded by quests, hubs and spawns, time-limited, and not tied to a bot, so nothing there needs releasing. `g_goGate` itself is bounded at about 5,000 entries.
* `BotAlts::AltGuids` (`BotAlts.cpp:45`): add-only by design, bounded by alt characters.
* `OpenTrades` (`BotSocial.cpp:94`): entries are removed when the bot is gone, the trade is gone, or the timeout runs out (`BotSocial.cpp:232-253`).
* `CachedAnalysis` thread-local cache (`BotQuest.cpp:409`): bounded by quest count per map thread.
* `BotMgr` loops over `_bots` (`BotMgr.cpp:330`, `361`, and others) are linear per call. `ProcessBotTeleports` is O(bots) per tick with an early `_onlineCount` exit.
* No raw object pointers are kept across ticks. `Task::Target`, `BotCombat` `Target`, `ChaseGuid`, `Ignored`, the recover plan's `Healer` and the follow target are `ObjectGuid` values re-resolved on use. `BotAI::_tickBot` (`BotAI.h:251`) is set and cleared inside one tick (`BotAI.cpp:251`, `304`). `BotAggroHit::Mob` (`BotBehavior.h:53`) is a transient return value.

## Suggested order of work

1. Findings 1, 2 and 3 together (they share the claim helpers, about 40 lines): destructor and `OnStateEnter` release, renewal, owner-aware cooling.
2. Finding 4 (throttle and single lock).
3. Findings 5, 6 and 7 (pruning and map-change handling).
4. Findings 8 and 9 once a profile of a 300 to 700 bot run shows them; they are the O(n^2) candidates.
5. Findings 10 to 12 as housekeeping.

## Not verified

* No run or profile was done. Rates such as deaths per hour are taken from `quest-fix-r5-20261008.md`.
* The cost of findings 8 and 9 depends on how densely bots cluster; a log of fight rows per second and a per-tick timing of `BotAI` update would confirm or rule them out.
