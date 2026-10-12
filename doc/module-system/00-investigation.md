# Module System — Codebase Investigation

Branch: `foreverModules` (mirrors `advocaite/TrinityCore:forever`).
Goal: let developers add functionality via **separate git repos**, with hooks/extension points, external plugin loading, and security boundaries — without touching core source.

## 1. ScriptMgr / hook system

- `src/server/game/Scripting/ScriptMgr.h` / `.cpp` — central hook registry. Base `ScriptObject`; subclasses register via `ScriptRegistry<T>::AddScript(this)` in their constructor.
- ~35 hook categories: `ServerScript`, `WorldScript`, `FormulaScript`, `*MapScript` (via `MapScript<T>`), `ItemScript`, `UnitScript`, `CreatureScript`, `GameObjectScript`, `AreaTriggerScript`/`AreaTriggerEntityScript`, `BattlefieldScript`, `OutdoorPvPScript`, `CommandScript`, `WeatherScript`, `AuctionHouseScript`, `ConditionScript`, `VehicleScript`, `DynamicObjectScript`, `TransportScript`, `AchievementScript`/`AchievementCriteriaScript`, `PlayerScript`, `AccountScript`, `GuildScript`, `GroupScript`, `ConversationScript`, `SceneScript`, `QuestScript`, `WorldStateScript`, `EventScript`, `PlayerChoiceScript`, plus generic/factory convenience variants (~line 1386+).
- Documented extension contract (`ScriptMgr.h` ~L112-150): subclass `ScriptObject`, register in ctor, add `template class ScriptRegistry<T>;` specialization at bottom of `ScriptMgr.cpp`.
- Registry population is not purely static — see §3 (dynamic loading already exists).

## 2. Existing module precedent

- No `modules/` dir at repo root (dirs: `.circleci .github cmake contrib dep doc sql src tests`). No AzerothCore-style external-module convention.
- TrinityCore's own concept: each subdir of `src/server/scripts/` (27 dirs — `Commands`, `Spells`, `World`, `Custom`, `EasternKingdoms`, etc.) is an independently selectable "script module" via CMake `SCRIPTS` cache var — buildable **static**, **dynamic**, or **disabled** per module (`cmake/macros/ConfigureScripts.cmake` → `GetScriptModuleList()`).
- No git-submodule/subrepo auto-discovery (`modules/CMakeLists.txt` loop) — `GetScriptModuleList()` only globs the fixed in-tree path.

## 3. Build system — dynamic loading already exists

- `src/server/scripts/CMakeLists.txt` — discovers modules, either folds sources into the monolithic `scripts` static lib, or creates an independent `add_library(... SHARED)` target per module (dynamic linkage), linked against `game` + `trinity-core-interface`.
- `cmake/macros/ConfigureScripts.cmake` — `GetScriptsBasePath`, `GetScriptModuleList`, `GetNativeSharedLibraryName` (`.dll`/`lib*.so`/`lib*.dylib`), `GetInstallOffset` (`bin/scripts`).
- `src/server/scripts/ScriptLoader.cpp.in.cmake` — CMake-templated `ScriptLoader.cpp` per module. Dynamic build defines `TRINITY_IS_DYNAMIC_SCRIPTLOADER`, exports `extern "C"` ABI: `GetScriptModuleRevisionHash()`, `GetScriptModule()`, `AddScripts()`, `GetBuildDirective()`.
- `cmake/options.cmake` (`SCRIPTS` = `none|static|dynamic|minimal-static|minimal-dynamic`); `cmake/showoptions.cmake` defines `TRINITY_API_USE_DYNAMIC_LINKING` globally when dynamic scripts selected.
- **`src/server/game/Scripting/ScriptReloadMgr.cpp` (1626 lines)** — full hot-swap dynamic module loader (`HotSwapScriptReloadMgr`, `#ifdef TRINITY_API_USE_DYNAMIC_LINKING`):
  - `boost::dll` + raw `LoadLibrary`/`dlopen` per platform.
  - Watches `HotSwap.ScriptDir` (default `"scripts"`) + source tree via `efsw` (`LibraryUpdateListener`, `SourceUpdateListener`); can optionally trigger CMake/MSBuild recompile (`HotSwap.EnableReCompiler`).
  - `GetLibraryDirectory()` resolves load dir (absolute or exe-relative) — **a compiled external `.dll`/`.so` dropped there is already picked up**, no core CMake changes needed, provided it exports the 4 ABI functions and links against core's `game`/`trinity-core-interface` ABI.
  - Config: `src/server/worldserver/worldserver.conf.dist` L1557-1623 (`HotSwap.Enabled`, `HotSwap.ScriptDir`, `HotSwap.EnableReCompiler`, `HotSwap.EnableEarlyTermination`, `HotSwap.EnableBuildFileRecreation`, `HotSwap.EnableInstall`, `HotSwap.EnablePrefixCorrection`, `HotSwap.ReCompilerBuildType`).
- **Conclusion**: both static and true dynamic (`dlopen`/`LoadLibrary`) plugin loading are first-class, working mechanisms already. Gaps: (a) no external-repo/git discovery convention, (b) no package/version contract beyond raw `GetScriptModuleRevisionHash()`/`GetBuildDirective()` ABI checks, (c) no sandboxing.

## 4. Existing plugin-like systems for precedent

- No Eluna (Lua) — zero matches. No embedded Lua/AngelScript; `dep/` has no scripting-language dependency (boost, mysql, openssl, protobuf, recastnavigation, efsw, CascLib, fmt, rapidjson only).
- Only native C++ `ScriptMgr` hooks (§1), plus `src/server/game/Scripting/v2/ScriptActions.h`/`.cpp` (data-driven quest/scene action sequences — content DSL, not general extensibility) and `ScriptSystem.cpp`/`.h` (loads `scripts.conf`/DB waypoint data — distinct from `ScriptMgr`).

## 5. Security-relevant touchpoints (list only — biggest design constraint)

- `src/server/database/Database/DatabaseWorkerPool.h` — raw `Execute(char const*)`, `PExecute`, `DirectExecute`, `Query`/`PQuery` (string-built SQL) alongside safer `PreparedStatement<T>`.
- `src/server/database/Database/MySQLConnection.h`/`.cpp`, `MySQLPreparedStatement.h`/`.cpp` — direct MySQL C API; live connection handles reachable via `LoginDatabase`/`WorldDatabase`/`CharacterDatabase`/`HotfixDatabase` globals (`DatabaseEnv.h`).
- `src/server/shared/Secrets/SecretMgr.h`/`.cpp` — holds `SECRET_TOTP_MASTER_KEY` etc. in memory, no barrier against in-process reads.
- `worldserver.conf.dist` / `bnetserver.conf.dist` — plaintext DB connection strings loaded into `ConfigMgr`, readable by anything linked into the process.
- `src/common/Configuration/Config.h`/`.cpp` (`ConfigMgr`/`sConfigMgr`) — single process-wide singleton, no ACL between core and module config.
- **Core constraint**: any `.dll`/`.so` loaded by `ScriptReloadMgr` runs fully in-process, no sandbox — full access to raw memory, all core globals, entire C++ ABI. Current system assumes trusted, same-origin modules. This is the primary gap an "external, untrusted-by-default" module system must address (or explicitly accept/scope as same-trust-level, same-origin-only).

## 6. Config loading pattern

- `src/common/Configuration/Config.h`/`.cpp` (`ConfigMgr`/`sConfigMgr`):
  - `LoadInitial(file, args, error)` — primary conf.
  - **`LoadAdditionalFile(file, keepOnReload, error)`** / **`LoadAdditionalDir(dir, keepOnReload, loadedFiles, errors)`** — existing precedent for supplementary config files/dirs with "keep on reload" semantics.
  - `OverrideWithEnvVariablesIfAny()`.
  - Flat accessors: `GetStringDefault`, `GetBoolDefault`, `GetIntDefault`, `GetInt64Default`, `GetFloatDefault`, `GetKeysByString`.
- `src/server/worldserver/Main.cpp` (~L141-227): `LoadInitial` → `LoadAdditionalDir(configDir, ...)` → `OverrideWithEnvVariablesIfAny()`.
- **Extension path**: reuse `LoadAdditionalDir` for a `modules.d/` or `modules/<name>/<name>.conf` convention — no new `ConfigMgr` code needed. No per-module key namespacing exists today; all keys share one flat space, so a module config convention needs a prefixing rule to avoid collisions.
