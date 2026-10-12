# `modules/` — Module System Source Directory

**Status: skeleton.** This directory is the clone target for the module system designed in
`doc/module-system/`. See `doc/module-system/05-phase3-build-discovery-design.md` for the full
design and `doc/module-system/12-initial-core-changes-scope.md` for what's implemented so far.

## Convention

Clone a module's git repository into `modules/<name>/`. The directory name must exactly match the
`"name"` field in that module's `module.json`. A rebuild of the core (`cmake --build .`) discovers
and builds it automatically via `cmake/modules/BuildModules.cmake` — no changes to this repo's
source are needed.

A broken/failing module build does not block the rest of the core build (it's logged as a
warning and skipped) — see `doc/module-system/05-phase3-build-discovery-design.md` §2-3.

## Status of the discovery mechanism

`cmake/modules/BuildModules.cmake` currently contains the isolated per-module
configure/build/copy loop. It safely does nothing when this directory is empty (as it is right
now) — nothing to clone here yet, this is scaffolding ahead of any real module existing.
