# Module System — Phase 5 Design: Reference Bot-AI Module (Acceptance Test)

Status: design drafted, not yet implemented. This is the architecture's acceptance test — if it fails, earlier phases get revised, not patched around.
Builds on all prior docs (`00` through `06`).

## Scope & non-goals

- **Goal**: prove the full pipeline end-to-end (clone → build → approve → handshake → Tier 1/Tier 2 traffic → bot behavior visible in-game → graceful failure handling) using the *smallest possible* bot-AI logic. This is a plumbing test, not a combat-AI quality test.
- **Non-goal**: sophisticated rotations, threat tables, positioning logic, anything resembling real `docs/playerbots`-level AI. That's future work built *on top of* a validated pipeline, not part of this acceptance test.

## Reference module behavior (deliberately trivial)

- Declares `tier2.participates = true`, small bounds (`max_bots = 5`, `max_nearby_units = 10`, `max_actions = 5`), configurable `tick_interval` (swept across test runs, see §3).
- Declares minimal Tier 1 hooks: `WorldScript.OnStartup`/`OnShutdown` only (lifecycle logging) — no command/player hooks needed for this test.
- Decision loop per owned bot, per tick snapshot received:
  - If a hostile `NearbyUnit` exists in the snapshot → emit `ATTACK_TARGET` action.
  - Else if a configured "leader" GUID is in range → emit `FOLLOW` action.
  - Else → emit `STOP`.
- On startup, issues `ControlRequest` for N pre-existing test-realm bot GUIDs (configured via its own `ModuleConfig`-delivered settings, not hardcoded) — exercises the dynamic bot-ownership flow from Phase 2 §6 rather than assuming bots spawn themselves.

## Acceptance test matrix

**A. Functional (happy path)**
1. Clone reference module into `modules/reference-bot/`, rebuild core → confirm it's isolated-built and copied into `Modules.Dir` (Phase 3).
2. First load → confirm `pending_approval` status + correct capability summary logged (Phase 4); run `module approve reference-bot`; restart/reload → confirm it now spawns.
3. Confirm handshake succeeds, `ModuleConfig` delivered, Tier 2 shared-memory region allocated with the negotiated bounds.
4. Confirm `ControlRequest` for configured bot GUIDs is granted; confirm `module info reference-bot` reflects owned bot count.
5. In a test realm: spawn a hostile near an owned bot → confirm it attacks; remove the hostile and place a "leader" unit nearby → confirm it follows; isolate it from both → confirm it stops. This confirms snapshot→decision→action round-trips actually work, not just that messages are exchanged.

**B. Resilience**
6. Kill the module process mid-run → confirm heartbeat timeout triggers the restart policy (Phase 2 §5) with correct backoff/attempt counting; confirm the core itself never crashes or stalls.
7. Exceed `Modules.MaxRestartAttempts` (repeatedly crash it) → confirm it correctly escalates to `permanently_failed` with a clear log entry rather than restart-looping forever.
8. Force a bot GUID to be deleted/logged out while owned → confirm `ControlRevoked` is sent and the module's own bookkeeping doesn't leave a dangling reference (no crash, no stuck "ghost" control claim).

**C. Build/approval isolation**
9. Deliberately break the reference module's `CMakeLists.txt` (bad syntax) → confirm the **core build still completes successfully** (Phase 3 isolation) with a clear warning naming the module.
10. Bump `tier2.requested_max_bots` in `module.json` without re-approving → confirm load is blocked with a correct capability-diff log (Phase 4), and that `Modules.AutoApproveLocal=1` bypasses it as intended for local dev.

**D. Multi-module coexistence**
11. Run two independent reference-module instances (different names, under different `modules/<name>/` dirs) each claiming **distinct** bot GUID sets → confirm both run concurrently without interference.
12. Have both attempt `ControlRequest` on the **same** GUID → confirm the second request is correctly rejected with a clear reason (Phase 2 §6 ownership conflict handling).

**E. Performance measurement (resolves deferred numeric defaults)**

This is where the Phase 1/2 deferred values get real answers instead of guesses:

| Deferred value | Measurement approach |
|---|---|
| `tick_interval` default | Sweep 1 (every tick), 5, 10, 20 across 1/5/20 controlled bots; measure server main-loop tick-time delta vs. baseline (no module) and subjective bot responsiveness. Pick the smallest interval that keeps tick-time overhead negligible (e.g. <1-2% of tick budget). |
| `Modules.HeartbeatIntervalMs` / `HeartbeatTimeoutMs` | Measure actual round-trip heartbeat latency under load; set timeout comfortably above worst observed latency plus margin, not an arbitrary guess. |
| `Modules.CommandRequestTimeoutMs` (200ms default) | Not heavily exercised by this module (no Tier 1 command hooks) — validate separately with a trivial command-hook test module if needed, or accept the 200ms default as reasonable and revisit only if real usage shows otherwise. |
| `MAX_TRACKED_COOLDOWNS` cap | Not meaningfully tested by this trivial module (no spell casting logic) — needs a follow-up test module that actually casts spells before this cap can be picked from real data; flag as still-open after Phase 5. |
| `Modules.ResourceLimits.MemoryMB`/`CpuPercent` | Measure actual reference module process RSS/CPU under the above sweep; set defaults with headroom above observed peak. |

## Exit criteria — what "Phase 5 passing" means

- All of A-D pass without requiring a core restart/crash at any point other than the deliberate ones in the test matrix.
- Performance sweep (E) produces concrete, justified default values (not guesses) for every deferred config key from Phases 1-2, **except** `MAX_TRACKED_COOLDOWNS`, which is explicitly carried forward as still-open (needs a spell-casting test module, out of scope for this trivial reference module).
- If any test in A-D fails in a way that implicates the *architecture* (not just a reference-module bug) — e.g. non-blocking Tier 2 staleness turns out to make bots feel unacceptably laggy even at `tick_interval=1`, or process-spawn overhead per module turns out too high for realistic bot-AI-module counts — **the relevant earlier phase doc gets revised**, not patched around in the reference module. This is the explicit "architecture isn't finalized until this passes" gate mentioned at the top.

## What comes after Phase 5

This is the last planning phase — everything from here is implementation:
- Build the actual SDK (Phase 1 contract) as real code.
- Build `ModuleMgr`/core module host (Phase 2) as real code.
- Build `BuildModules.cmake` + template repo (Phase 3) as real code.
- Build the approval/audit system (Phase 4) as real code.
- Build and run the reference module test matrix (Phase 5) above, feeding real measurements back into whichever earlier doc has deferred values.

No further design docs are expected after this one unless Phase 5 testing surfaces an architectural revision.
