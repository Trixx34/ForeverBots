# Module System — First Steps Plan

Status: Phase 0 scoping decisions resolved. No implementation yet.
See `00-investigation.md` for the codebase findings this plan is based on.
See `02-phase0-decisions.md` for the full decision log and rationale.

## Architecture direction (resolved in Phase 0)

This is an **open-source, multi-author module ecosystem** (not every operator will audit module source before deploying), so the design does **not** build on the core's existing in-process `ScriptReloadMgr` dynamic `.dll`/`.so` loading — that mechanism offers no real trust boundary (loaded code shares the process, full memory/DB/secrets access). Instead:

- **True process isolation**: each module runs as its own OS process, not an in-process `.dll`/`.so`.
- **Two-tier hook/communication model**:
  - **Tier 1 — event hooks** (low-frequency, general-purpose): mirrors existing `ScriptMgr` categories (`PlayerScript`, `CommandScript`, `QuestScript`, `AuctionHouseScript`, `GuildScript`, `WorldScript`, `AccountScript`, etc.). Transport: request/response IPC messages (`boost::asio` local sockets/pipes + protobuf, both already core dependencies).
  - **Tier 2 — per-tick decision loop** (bot AI / combat logic, hot-path): **not** one IPC call per event. Core writes a batched world-state snapshot once per tick into a `boost::interprocess` shared-memory ring buffer (positions, health, threat, cooldowns for the module's bots); module process reads it, computes decisions for all its bots in one pass, writes an action queue back into shared memory. Core applies returned actions via existing in-core APIs next tick.
  - Hot-path `ScriptMgr` categories (e.g. per-tick `UnitScript` damage hooks, `CreatureScript` AI ticks) are **not** exposed via Tier 1 — only through the batched Tier 2 path, because per-call IPC at that frequency would cripple tick rate.
- **Language scope**: C++-only for v1 (cross-language support deferred — revisit once protocol is stable).
- **Build/distribution**: module author's repo is `git clone`d into a new top-level `modules/<name>/` directory in the core checkout. Core's top-level `CMakeLists.txt` gains a discovery loop (`add_subdirectory()` per cloned module) — this is the `modules/` convention this fork currently lacks (confirmed in investigation §2). Each module's own `CMakeLists.txt` builds a **standalone executable** (not linked into the core monolith) against a pinned SDK version (fetched via `FetchContent`/`CPM.cmake` at a specific tag, independent of core version). A rebuild of the server picks up newly cloned modules automatically. At runtime, the built module executable still launches as an **isolated process** — build-time convenience does not compromise runtime isolation.
- **Versioning**: SDK/protocol version match between module and running core must be **exact**. Any mismatch → refuse to load, clear log error. No compat shims in v1.
- **Config**: reuse `ConfigMgr::LoadAdditionalDir` for a `modules.d/<name>.conf` convention (core-side only — the module process itself receives its config over Tier 1 IPC at startup handshake, not by reading core's config files directly, to preserve isolation).

## Phase 1 — SDK & protocol foundations

Nothing can be built until the wire contract exists.

- Define the **Tier 1 message schema** (protobuf): handshake, event dispatch (one message type per `ScriptMgr` category exposed), responses, heartbeat, shutdown.
- Define the **Tier 2 shared-memory layout**: snapshot struct (per-bot world-state fields needed for decisions) and action-queue struct (commands the module can issue). Version this layout explicitly (it's a binary struct, not protobuf — changes are breaking by nature).
- Decide and document the **initial Tier 1 hook subset** (start small: `PlayerScript::OnLogin/OnLogout`, `CommandScript`, `WorldScript` are good first candidates — low frequency, easy to test end-to-end).
- Publish the **SDK package**: protocol headers + generated protobuf code + shared-memory struct headers + a thin client library (handshake, heartbeat, reconnect, typed send/receive helpers). Tag it with a version (e.g. `sdk-v0.1.0`) — this is the version module authors pin against.
- No actual module process or core-side host yet — this phase is the contract only.

## Phase 2 — Core-side module host (supervisor)

- New subsystem (e.g. `src/server/game/Modules/ModuleMgr.cpp`/`.h`) responsible for:
  - Scanning a configurable `Modules.Dir` for module manifests + their built executables (populated by the `modules/` build-time convention above).
  - Spawning each module process, performing the version-exact handshake, rejecting on mismatch with a clear log error.
  - Heartbeat monitoring; restart-on-crash policy (define: max restart attempts before giving up and logging a permanent failure).
  - Resource limits on spawned processes (Job Objects on Windows, cgroups on Linux) — scope this as a hard requirement given untrusted-author modules, not a nice-to-have.
  - Routing Tier 1 events from `ScriptMgr`'s existing hook-firing call sites to the IPC layer for subscribed modules.
  - Owning/publishing the Tier 2 shared-memory regions and the per-tick snapshot/action-queue exchange loop.
- This is the biggest net-new engineering piece — no existing code to extend here (unlike the original `ScriptReloadMgr`-based plan).

## Phase 3 — `modules/` build-time discovery

- Add top-level `modules/` directory convention: glob subdirectories, `add_subdirectory()` each.
- Each module repo template provides: `CMakeLists.txt` (executable target + pinned SDK fetch), `module.json` manifest (name, version, SDK version, declared Tier 1 hook subscriptions, declared Tier 2 participation yes/no), source.
- Post-build step copies the module's built executable + manifest into the runtime `Modules.Dir` so `ModuleMgr` (Phase 2) picks it up without a manual copy step.
- Write the **"Hello Module" walkthrough**: `git clone` template into `modules/`, implement one Tier 1 hook (e.g. a chat command), rebuild core, see it load/handshake in the worldserver log — no core source edits.

## Phase 4 — Permission manifest & audit logging

- Module manifest declares intended capabilities (`tier1.hooks: [...]`, `tier2.participates: bool`, any future capability flags).
- Log declared capabilities at load time so operators can see what a module claims to need before/while running it.
- Not an enforcement mechanism by itself (Tier 1/Tier 2 APIs are the real enforcement, since the module process has no other way to reach core memory/DB) — this is about operator visibility and an audit trail, useful groundwork for a future community module registry/review process.

## Phase 5 — Bot AI as a reference Tier 2 module

- Once Tier 2 is working end-to-end (Phase 2 shared-memory loop + Phase 3 build integration), build a minimal bot-AI module as the first real Tier 2 consumer — validates the per-tick snapshot/action-queue design against real combat-decision latency requirements before wider adoption.
- Treat this as the acceptance test for the whole architecture: if bot AI can't hit acceptable tick-rate behavior through Tier 2, the shared-memory design needs revision before anything else builds on it.

## Deferred / explicitly out of scope for v1

- Cross-language module support (C++-only for now).
- Semantic-version compatibility ranges (exact-match only).
- Any in-process `.dll`/`.so` loading path (the original `ScriptReloadMgr`-based approach) — superseded by process isolation.

## Immediate next action

Start **Phase 1** (SDK & protocol foundations) — specifically the Tier 1 message schema and Tier 2 shared-memory layout definitions, since every later phase depends on that contract being settled first.
