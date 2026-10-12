# Module System — Phase 0 Decision Log

Context: open-source project, module authors are external/untrusted by default (not everyone will audit module code before deploying it).

## 1. Trust boundary

**Decision: True process isolation.** Each module runs as a separate OS process, not an in-process `.dll`/`.so`.

Rationale: in-process loading (the core's existing `ScriptReloadMgr` dynamic-loading mechanism) gives loaded code full memory/DB/secrets access with no enforceable boundary — a "mediated API" convention can always be bypassed by a module author who links the raw headers directly. For an ecosystem where not every module gets audited before use, only a separate-process boundary provides real enforcement.

## 1b. Hook coverage vs. performance

**Decision: Two-tier architecture.**
- Tier 1 (event hooks, low-frequency): request/response IPC, mirrors existing `ScriptMgr` categories.
- Tier 2 (per-tick decisions, hot-path — bot AI/combat): batched shared-memory snapshot + action-queue loop, not per-event IPC calls.

Rationale: intended first consumers include bot AI/combat logic (confirmed), which needs per-tick decision frequency. Per-event IPC at that frequency would cripple tick rate. Low-frequency hooks (commands, quests, account/guild/world events, auction house) are fine as individual IPC messages.

## Module language scope

**Decision: C++-only for v1.** Revisit opening the protocol to other languages once it's stable.

Rationale: shared-memory ring buffers (Tier 2) are far simpler same-language/same-ABI-ish than cross-language; smaller v1 scope.

## 2. Distribution model

**Decision: Clone into `modules/<name>/` in the core checkout; a core rebuild picks it up.**

Core's top-level `CMakeLists.txt` gets a discovery loop (`add_subdirectory()` per cloned module dir) — the `modules/` auto-discovery convention this fork currently lacks (AzerothCore has it, this fork doesn't, per investigation §2). Each module builds a standalone executable (not linked into the core monolith). Runtime isolation is unaffected by this build-time convenience — the built binary still launches as its own process.

## 2b. SDK packaging

**Decision: Versioned SDK package, pinned per-module.**

Module's own `CMakeLists.txt` fetches a specific SDK release/tag (e.g. via `FetchContent`/`CPM.cmake`), independent of whatever core version it happens to be cloned into. SDK = protocol headers, shared-memory layout headers, generated protobuf code, thin client library.

## 3. API / protocol versioning

**Decision: Exact version match required.** Module declares an SDK version; if it doesn't exactly match what the running core supports, refuse to load with a clear log error. No compatibility shims in v1.

Rationale: simplest, most predictable; avoids maintaining backward-compat shims while the protocol is still new/evolving. Revisit semantic-version ranges once the protocol stabilizes.

## 4. Platform / transport

**Decision: `boost::interprocess` (Tier 2 shared memory) + `boost::asio` (Tier 1 sockets/pipes).**

Both Windows and Linux supported via one dependency family already present in the core (`boost` is an existing dependency per investigation §4/§3). Avoids hand-rolling `CreateFileMapping`/`shm_open` platform-specific code separately, though the existing `ScriptReloadMgr` precedent shows per-platform code paths (`<windows.h>` vs `<dlfcn.h>`) are already a normal pattern in this codebase if needed elsewhere.

## Net effect on original investigation assumptions

The investigation (`00-investigation.md`) found that in-process dynamic loading (`ScriptReloadMgr`) already works and assumed a module system would extend it. Phase 0 explicitly rejects that path in favor of process isolation, given the open-source/untrusted-author context. This means the "first steps" plan (`01-first-steps-plan.md`) builds substantially new infrastructure (IPC host, shared-memory loop, `modules/` build discovery) rather than extending `ScriptReloadMgr` — a deliberate, larger v1 scope than the investigation's initial read suggested.
