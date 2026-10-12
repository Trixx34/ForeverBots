# Module System — Phase 2 Design: Core-Side Module Host

Status: design drafted, not yet implemented.
Builds on `00-investigation.md`, `02-phase0-decisions.md`, `01-first-steps-plan.md`, `03-phase1-protocol-design.md`.

New subsystem: `src/server/game/Modules/ModuleMgr.{h,cpp}` (+ helper files as needed). Singleton (`sModuleMgr`), following the existing `sScriptMgr`/`sConfigMgr` convention.

## 1. Discovery & registry

- At worldserver startup, `ModuleMgr::LoadModules()` scans `Modules.Dir` (new config key) for `<name>/module.json` + its built executable (placed there by the Phase 3 `modules/` build-time copy step).
- Parses manifest: name, version, declared SDK version, declared Tier 1 hook subscriptions, Tier 2 participation + requested bounds/cadence.
- Compares declared SDK version against the core's compiled-in `kModuleProtocolVersion` constant — **exact match required** (Phase 0 §3). Mismatch → module marked **permanently failed**, never spawned, clear log error naming the expected vs. found version. (Distinct from a crash — restarting won't fix a version mismatch, so no restart attempts are wasted on it.)
- Valid modules go into a registry: `std::unordered_map<ModuleId, ModuleDescriptor>`.

## 2. Process spawning

- Use `boost::process` (boost already a core dependency per investigation §4) to spawn each module's executable — avoids hand-rolling `CreateProcess`/`fork+exec` separately per platform, consistent with preferring boost for cross-platform concerns (Phase 0 §4 transport decision).
- Core generates a unique per-module Tier 1 endpoint (named pipe on Windows / Unix domain socket path on Linux) and passes it to the spawned process via command-line argument.
- **Parent-death binding**: on Windows, assign the process to a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` so module processes die if the core crashes/exits abnormally; on Linux, module's SDK client lib calls `prctl(PR_SET_PDEATHSIG, SIGTERM)` on its own startup as the equivalent. Prevents orphaned module processes lingering after a core crash.
- **Resource limits** (hard requirement per Phase 0 — untrusted-author modules): Job Object CPU/memory caps on Windows; cgroup (v2) CPU/memory limits on Linux where available, falling back to `setrlimit` if cgroups aren't set up on the host. Concrete default limits (e.g. memory cap MB) are an operational tuning value, not a protocol decision — expose as config (`Modules.ResourceLimits.MemoryMB`, `Modules.ResourceLimits.CpuPercent`), picked during implementation/testing.

## 3. Handshake sequencing

- After spawning, core waits up to `Modules.HandshakeTimeoutMs` (default proposed: 5000ms) for the module to connect and send `HandshakeRequest`.
- No handshake in time → kill process, log failure, apply restart policy (§5) as a transient failure (could be a slow machine/cold start, worth retrying a bounded number of times).
- Handshake received → version check (already done at discovery, but re-validated against the live message in case manifest and binary drifted) → `HandshakeResponse` with accept/reject. On accept with Tier 2 requested: allocate the shared-memory region per the Phase 1 layout, embed the region name in the response.

## 4. Threading model

- A dedicated `boost::asio::io_context` (own thread, **not** the main world-update thread) runs the Tier 1 socket server: accepting connections, async read/write of protobuf-framed messages, heartbeat tracking.
- **`EventNotify`** hooks (fire-and-forget) are posted onto this io_context asynchronously from whatever thread calls the `ScriptMgr`-equivalent hook site — no blocking on the caller.
- **`EventRequest`** hooks (e.g. `CommandScript`) are rare/non-hot-path by design (Phase 1 §Notify vs Request/Reply split), so a bounded **blocking** wait (the 200ms default) directly on the calling thread is an acceptable, simple trade-off — implemented via a condition variable signaled when the io_context thread receives the matching `EventReply` for that `message_id`. On timeout, return the "module did not respond" error established in Phase 1.
- **Tier 2 tick integration**: a new call site in the core's main world-update loop (needs locating the exact call site in `src/server/game/World/World.cpp` or equivalent `Map::Update` during implementation) — `ModuleMgr::OnWorldTick(tickNumber)` — iterates Tier-2-participating modules, checks each module's declared cadence (`tick_interval`), writes a filtered snapshot (only bots that module currently controls — see §6) when due, and reads/applies each module's currently-published `ActionQueue` (non-blocking, per Phase 0/Phase 1 design) by translating `BotAction` entries into existing core bot-control APIs.

## 5. Restart policy

Two failure classes, handled differently:

- **Permanent failure** (version mismatch, malformed manifest, repeated handshake timeout past max attempts): module marked failed, not retried, clear log entry; requires operator action (rebuild/fix/remove) to recover. Config: `Modules.MaxHandshakeAttempts` (default proposed: 3).
- **Transient failure** (process crash after a successful handshake, heartbeat silence past `Modules.HeartbeatTimeoutMs`): restart with backoff. Config: `Modules.MaxRestartAttempts` (default proposed: 5) within a rolling window (e.g. 10 minutes), `Modules.RestartBackoffMs` (exponential or fixed — pick during implementation). Exceeding max attempts within the window escalates to permanent failure with a clear log entry (don't retry forever on a module that's crash-looping).
- Heartbeat (`Modules.HeartbeatIntervalMs` default proposed: 5000ms) is the liveness signal driving this — independent of Tier 2 staleness (Phase 1 §Staleness handling), since a module can be Tier-2-slow but still alive.

## 6. Bot ownership — dynamic control requests

New Tier 1 message types (extend the `Envelope` oneof from Phase 1):

```proto
message ControlRequest {
  uint64 bot_guid = 1;        // 0 = "please spawn a new bot and give me control"
  SpawnParams spawn_params = 2;  // only used when bot_guid == 0
}
message ControlResponse {
  bool granted = 1;
  uint64 bot_guid = 2;        // assigned guid if newly spawned
  string reason = 3;          // populated when granted == false (e.g. "already controlled by module X")
}
message ReleaseControl {
  uint64 bot_guid = 1;
}
message ControlRevoked {      // core -> module notify
  uint64 bot_guid = 1;
  string reason = 2;          // e.g. "bot deleted", "GM command", "account logged out"
}
```

- `ModuleMgr` owns the authoritative map `std::unordered_map<uint64 /*bot_guid*/, ModuleId>`. A `ControlRequest` for a GUID already owned by another module is rejected (`granted=false`, reason populated) — this is how multiple bot-AI modules coexist without fighting over the same bot.
- `ControlRevoked` lets core take a bot back out from under a module (bot deleted, account logged out, GM intervention) — module must handle this notify and stop including that GUID in its own bookkeeping; no action required back from the module.
- Tier 2 snapshots for a module only ever include bots currently in that module's owned set — this is the "filtered view" mentioned in §4. If a module's owned-bot count exceeds its negotiated `max_bots` (Phase 1 handshake bound), additional `ControlRequest`s should be rejected with a clear reason (`"max_bots exceeded, request a larger bound at handshake"`) rather than silently truncating snapshots.

## 7. New config keys (worldserver.conf, loaded via existing `ConfigMgr`)

```
Modules.Enabled = 1
Modules.Dir = "modules_runtime"
Modules.HandshakeTimeoutMs = 5000
Modules.MaxHandshakeAttempts = 3
Modules.HeartbeatIntervalMs = 5000
Modules.HeartbeatTimeoutMs = 15000
Modules.MaxRestartAttempts = 5
Modules.RestartBackoffMs = 1000
Modules.CommandRequestTimeoutMs = 200
Modules.ResourceLimits.MemoryMB = 256
Modules.ResourceLimits.CpuPercent = 25
```

All values above are proposed defaults for implementation-time tuning, not final — flagging so they aren't mistaken for settled protocol decisions (unlike the Phase 0/1 items, which are).

## Deferred from this phase

- Exact world-update-loop call site for `OnWorldTick` integration — needs locating in the actual core source during implementation, not a design-level decision.
- `SpawnParams` message fields (race/class/level/team for a requested new bot) — straightforward but deferred until bot-spawning requirements are nailed down in Phase 5 reference module work.
- Admin-facing commands/tools to inspect module status, force-revoke bot control, or manually restart a module — useful operational tooling, not core-path-blocking, candidate for a later phase.

## Next action

Phase 3: `modules/` build-time discovery (CMake `add_subdirectory()` loop, module template repo, post-build copy into `Modules.Dir`) — the piece that makes "clone a repo into `modules/`, rebuild, it's live" actually true end-to-end.
