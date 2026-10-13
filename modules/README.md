# `modules/` — Module Source Directory

Clone a module's git repository into `modules/<name>/`. The directory name must exactly match the `"name"` field in that module's
`module.json`. A rebuild of the core (`cmake --build .`) discovers and builds it through `cmake/modules/BuildModules.cmake`, so no
change to this repository's source is needed. A module that fails to build is logged as a warning and skipped; it does not stop the
rest of the core build (see `doc/module-system/05-phase3-build-discovery-design.md`, sections 2 and 3).

A module is a separate process, not part of the worldserver: the core starts it, talks to it over a local socket, and, for bots, a
shared-memory region. See `doc/module-system/` for the design and `doc/module-system/14-implementation-status.md` for what exists.
Modules are written against the [forever-module-sdk](https://github.com/Trixx34/forever-module-sdk) and configured with
`Module.<name>.<Key>` entries in `worldserver.conf.d/modules.d/<name>.conf`.

## Modules

| Directory | Repository | What it does |
|---|---|---|
| `forever-bots` | [MOD-Forever-Bots](https://github.com/Trixx34/MOD-Forever-Bots) | Bot AI: combat, rest, vendor visits, travel, looting, parties |
| `auto-vendor-trash` | [MOD-auto-vendor-trash](https://github.com/Trixx34/MOD-auto-vendor-trash) | Sells looted grey items and tells the player |
| `quest-group-loot` | [MOD-quest-group-loot](https://github.com/Trixx34/MOD-quest-group-loot) | Shares a looted quest item with the party members who need it |

The directory name is each module's `module.json` name, which may differ from its repository name (check `module.json`).
