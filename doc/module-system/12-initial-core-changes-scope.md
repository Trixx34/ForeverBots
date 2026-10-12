# Module System — Initial Core-Repo Changes Scope

Status: scope drafted, not yet implemented. Mirrors `forever-module-sdk`'s skeleton depth
(structure + declarations + stubs, no working network/IPC logic yet).
Builds on `01`-`11`. Verified against the actual `foreverModules` checkout (not assumed).

## Inventory of files to add/change

### 1. New `UnitScript::OnAuraChange` hook (doc `09`)

- **Modify** `src/server/game/Scripting/ScriptMgr.h` — add `virtual void OnAuraChange(Unit* target, AuraApplication* aurApp, bool apply);` to `class TC_GAME_API UnitScript` (alongside existing `OnDamage`/`OnHeal`, `:425-431`).
- **Modify** `src/server/game/Scripting/ScriptMgr.cpp` — add the `ScriptMgr::OnAuraChange(...)` dispatcher function (mirrors the existing `OnDamage`/`OnHeal` wrapper pattern, loops `ScriptRegistry<UnitScript>`). No new `template class ScriptRegistry<T>` specialization needed — `UnitScript` already has one.
- **Modify** `src/server/game/Entities/Unit/Unit.cpp` — add `sScriptMgr->OnAuraChange(...)` calls at the 3 confirmed sites: `_ApplyAura` (`:3629`, `apply=true`), both `_UnapplyAura` overloads (`:3686`, `:3778`, `apply=false`).

This slice is small and self-contained — can be implemented and tested independently of everything else below.

### 2. New `ModuleMgr` subsystem (doc `04`)

- **New directory** `src/server/game/Modules/` (picked up automatically by the existing `CollectAndAddSourceFiles` glob in `src/server/game/CMakeLists.txt` — confirmed no CMake changes needed here, same mechanism the `Bots/` directory already relies on per the gap analysis `08` §6).
- **New** `src/server/game/Modules/ModuleMgr.h` / `.cpp` — singleton (`sModuleMgr`), skeleton methods per doc `04`: `LoadModules()`, `OnWorldTick(uint64 tick)`, handshake/spawn/restart-policy method stubs, bot-ownership map (`std::unordered_map<uint64, ModuleId>`) declared but unpopulated logic. **Skeleton depth**: method bodies are `TODO`-stubbed, no real `boost::process` spawning or `boost::asio` socket server yet — matches the SDK's current stub depth so both sides can be iterated together later.
- **Integration point confirmed**: `World::Update` already has the exact pattern to follow — `sBotMgr->Update(diff)` is called unconditionally at `World.cpp:2303` (found during the `08` investigation). `ModuleMgr::OnWorldTick(...)` gets called the same way, right where that bot-system call sits today (useful marker for later when `08`'s actual migration work begins — this call site is literally the handoff point between old and new systems).

### 3. `modules/` build-time discovery (doc `05`)

- **New top-level directory** `modules/` (empty at this stage — add a `.gitkeep` or short `README.md` explaining the clone-here convention, since an empty dir isn't tracked by git).
- **New** `cmake/modules/BuildModules.cmake` — the isolated per-module configure/build script (warning-not-fatal-error on failures, per doc `05` §3). Skeleton: the loop structure and `message(WARNING ...)` failure handling can be written now; it simply does nothing (zero modules found) until someone actually clones a module into `modules/`.
- **Modify** top-level `CMakeLists.txt` — insert `add_custom_target(build_modules ALL COMMAND ${CMAKE_COMMAND} ... -P cmake/modules/BuildModules.cmake)` right after the existing `add_subdirectory(src)` line (confirmed actual location in the current file — `include(CTest)` follows it).

### 4. Config keys (doc `04` §7, doc `06`)

- **Modify** `src/server/worldserver/worldserver.conf.dist` — add the `Modules.*` block (`Enabled`, `Dir`, `HandshakeTimeoutMs`, `MaxHandshakeAttempts`, `HeartbeatIntervalMs`, `HeartbeatTimeoutMs`, `MaxRestartAttempts`, `RestartBackoffMs`, `CommandRequestTimeoutMs`, `ResourceLimits.MemoryMB`, `ResourceLimits.CpuPercent`, `AutoApproveLocal`) per doc `04`'s proposed defaults — these are config **declarations** or so `ConfigMgr` has something to load; `ModuleMgr`'s stub methods don't need to act on all of them yet.
- New dedicated `modules` logger channel (doc `06` §4) — add alongside the existing logger channel examples in `worldserver.conf.dist`'s `Logger.*`/`Appender.*` section (exact existing format needs to be checked at implementation time — not critical to pin down for scoping).

### 5. Capability approval + module commands (doc `06`)

- **New** `src/server/scripts/Commands/cs_module.cpp` — standard `CommandScript` (same convention as `cs_bot.cpp`, confirmed console commands route through the same `ChatCommand`/`Chat.h` system `CliRunnable.cpp` already includes — **no `CliRunnable.cpp` changes needed**, smaller scope than doc `06` originally implied by calling these "console commands"). Implements `module approve <name>`, `module list`, `module info <name>` (doc `06` §3).
- `approved_modules.json` itself is a **runtime artifact**, not a repo file — no file to add, just document its location (core config directory) in `ModuleMgr`'s own comments/README when implemented.

## What's deliberately NOT in this initial scope

- Any real `boost::asio`/`boost::process`/`boost::interprocess` implementation inside `ModuleMgr` — matches the SDK's current stub depth.
- The `forever-module-sdk` FetchContent wiring inside a real module's `CMakeLists.txt` — there's no module to clone into `modules/` yet, so this has nothing to attach to until a reference/example module exists.
- Anything from the bot migration itself (docs `08`-`10`) — that's a separate, later effort gated on Phase 5, not part of standing up the module system's skeleton.

## Suggested implementation order (if/when this proceeds)

1. `OnAuraChange` hook (§1) — smallest, self-contained, testable independently, zero dependency on anything else.
2. `modules/` + `BuildModules.cmake` + top-level `CMakeLists.txt` wiring (§3) — safe to add even with zero modules present (the loop just finds nothing).
3. Config keys (§4) — declarative, no behavior yet.
4. `ModuleMgr` skeleton (§2) — the biggest piece; depends on config keys existing to read from, even as stubs.
5. `cs_module.cpp` commands (§5) — depends on `ModuleMgr` existing (even as a stub) to call into.

## Next action

Confirm how deep to build this initial slice — same skeleton-vs-full-implementation choice already made for the SDK — before writing actual code into the core repo.
