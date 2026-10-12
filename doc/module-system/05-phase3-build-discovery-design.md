# Module System — Phase 3 Design: `modules/` Build-Time Discovery

Status: design drafted, not yet implemented.
Builds on `00-investigation.md`, `02-phase0-decisions.md`, `01-first-steps-plan.md`, `03-phase1-protocol-design.md`, `04-phase2-module-host-design.md`.

Goal: `git clone <module-repo> modules/<name>` + rebuild the core → module is live in `Modules.Dir`, no core source edits, **and** a broken module can't block the rest of the build (per the isolation decision below).

## 1. Discovery convention

- New top-level `modules/` directory (sits alongside existing `src`, `cmake`, `contrib`, etc. — confirmed absent today per investigation §2).
- Each subdirectory `modules/<name>/` is a cloned module repo containing at minimum `CMakeLists.txt` + `module.json` at its root.
- `module.json`'s `"name"` field **must exactly match** the directory name. Mismatch → skip with a warning (keeps build-path/runtime-identity bookkeeping unambiguous; avoids a module silently masquerading under a different directory name than its declared identity).
- Missing `CMakeLists.txt` or `module.json` → skip with a warning, same isolation treatment as a build failure (§3).

## 2. Why NOT plain `add_subdirectory()`

Plain `add_subdirectory()` folds each module's CMake configure pass into the **same** configure run as the core — a syntax error or missing dependency in one module's `CMakeLists.txt` aborts the entire core configure step, which violates the isolation decision. Each module instead needs to be configured and built as an **independent CMake project**, so its failures are contained.

## 3. Isolated per-module build mechanism

- New script `cmake/modules/BuildModules.cmake`, invoked from the top-level `CMakeLists.txt` via:
  ```cmake
  add_custom_target(build_modules ALL
      COMMAND ${CMAKE_COMMAND}
          -DCORE_BUILD_DIR=${CMAKE_BINARY_DIR}
          -DMODULES_SOURCE_DIR=${CMAKE_SOURCE_DIR}/modules
          -DMODULES_RUNTIME_DIR=${MODULES_RUNTIME_DIR}  # = Modules.Dir, same value used in worldserver.conf
          -P ${CMAKE_SOURCE_DIR}/cmake/modules/BuildModules.cmake
  )
  ```
- `BuildModules.cmake` logic, per discovered module directory:
  1. Validate `module.json` presence + name match (§1) — `message(WARNING ...)` and skip on failure, **never** `message(FATAL_ERROR)`.
  2. `execute_process(COMMAND ${CMAKE_COMMAND} -S modules/<name> -B ${CORE_BUILD_DIR}/modules/<name> ... RESULT_VARIABLE configure_result)` — configure the module as its own independent CMake project (its `CMakeLists.txt` is free to `FetchContent` the pinned SDK version, per Phase 0 §2b).
  3. On non-zero `configure_result`: `message(WARNING "Module <name> failed to configure, skipping")`, continue to the next module.
  4. `execute_process(COMMAND ${CMAKE_COMMAND} --build ${CORE_BUILD_DIR}/modules/<name> ... RESULT_VARIABLE build_result)`.
  5. On non-zero `build_result`: same warning-and-skip treatment.
  6. On success: copy the built executable + `module.json` into `${MODULES_RUNTIME_DIR}/<name>/`.
- Because every failure path uses `message(WARNING ...)` (never `FATAL_ERROR`) and the script only ever reads `RESULT_VARIABLE`s without propagating them, `BuildModules.cmake` itself always completes, so `add_custom_target(build_modules ALL ...)` always reports success to the parent build — **the overall `cmake --build .` exit code reflects only the core's own targets, never a module's failure.** Failed modules are simply absent from `Modules.Dir`, so Phase 2's `ModuleMgr::LoadModules()` never sees them (no special-casing needed on the runtime side).
- All per-module configure/build warnings print to the normal build console output, so a broken module is loud and visible — just not blocking.

## 4. Module template repo

A separate repo (not in this core repo) that module authors fork/copy as their starting point:

```
<module-name>/
  CMakeLists.txt       # FetchContent(sdk, GIT_TAG <pinned-version>) + add_executable + target_link_libraries(sdk)
  module.json          # manifest — see schema below
  src/
    main.cpp           # SDK client bootstrap: connect, handshake, hook registration, main loop
    hooks/
      OnCommand.cpp     # example Tier 1 hook implementation
  README.md             # points back to the "Hello Module" walkthrough
```

`module.json` schema:
```json
{
  "name": "my-module",
  "version": "0.1.0",
  "protocol_version": "1",
  "sdk_package_version": "0.1.0",
  "tier1": {
    "hooks": ["CommandScript", "PlayerScript.OnLogin"]
  },
  "tier2": {
    "participates": false,
    "requested_max_bots": 0,
    "requested_max_nearby_units": 0,
    "requested_max_actions": 0,
    "requested_tick_interval": 1
  }
}
```
`protocol_version` is the exact-match gate against core's compiled-in wire protocol (Phase 0 §3); `sdk_package_version` records which tagged SDK release the module built against, informational only — see `11-sdk-scope.md` for the full split rationale.

`CMakeLists.txt` responsibilities: pin the SDK package version via `FetchContent_Declare(module_sdk GIT_REPOSITORY <sdk-repo> GIT_TAG v0.1.0)`, build a standalone executable (not a library — this process runs independently at runtime per the process-isolation decision), link against the SDK's client library + generated protobuf code.

## 5. "Hello Module" walkthrough (to be written as the template repo's README)

1. `git clone <template-repo-url> modules/hello-module`
2. Edit `modules/hello-module/module.json` → set `name` to `hello-module` (must match directory name).
3. Implement one hook in `src/hooks/` — e.g. a `CommandScript` handler for a `.hello` chat command.
4. From the core's build directory: `cmake --build .` (normal rebuild — no special module-specific command needed; `build_modules` target runs automatically as part of `ALL`).
5. Confirm `modules_runtime/hello-module/` now contains the built executable + `module.json` (copied automatically).
6. Start/restart worldserver, confirm in the log: module discovered → process spawned → handshake accepted → `.hello` command works in-game.
7. No core source files were touched at any point.

## Deferred from this phase

- Exact `FetchContent`/`CPM.cmake` choice for SDK fetching in the template (either works — pick whichever the SDK repo's own release tagging is simpler to consume from; not a core-repo decision).
- Incremental rebuild behavior when only one module's source changed (should `BuildModules.cmake` re-run `cmake --build` unconditionally every time, or track per-module source timestamps to skip unchanged ones?) — a build-speed optimization, not needed for correctness; revisit once module count/build time becomes a real annoyance.
- Module removal flow (deleting `modules/<name>/` and rebuilding should also remove it from `Modules.Dir` — needs a cleanup step comparing the previous build's module list to the current discovery pass).

## Next action

Phase 4: permission manifest surfacing + audit logging (already scoped at a high level in `01-first-steps-plan.md`) — mostly about making the `tier1.hooks`/`tier2.participates` declarations from `module.json` visible in operator-facing logs at load time, building on the manifest schema finalized here.
