# `forever` branch — facts relevant to a bot implementation

Captured 2026-10-05 while researching player-bot feasibility. These are the
specifics of this fork that any bot-implementation approach (ported or native)
needs to account for.

## What this fork is

- Remote: `https://github.com/advocaite/TrinityCore.git`
- Branch `forever` runs **TrinityCore master** source against the **WoW Classic
  beta client 1.60.1.70245** (`_classic_beta_\WowB.exe`), not WotLK/retail.
- World database: a **vanilla (1.12-era) world converted from VMaNGOS**, plus
  original custom content ("Skyborne" / Zephras Isle).
- No MySQL/manual setup needed for players — a Windows repack handles it. For
  building from source, `worldserver` auto-applies `sql/custom` files via
  `Updates.EnableDatabases = 15`.

## Core code has already diverged from stock TrinityCore/WotLK behavior

Recent commit history shows combat/stat/leveling systems rewritten toward
vanilla semantics, e.g.:

- "vanilla stat and combat formulas"
- "base stats are set, not added, on level up"
- "round melee damage instead of truncating, armor never takes a hit below 1"
- "vanilla creature melee damage and player base health"

This means `Player::UpdateStats`, melee damage calculation, and related
formulas in `src/server/game` no longer match stock TrinityCore/WotLK math —
**any bot AI tuned against unmodified TrinityCore/WotLK stat math (e.g.
mod-playerbots' threat/DPS/healing thresholds) would need re-tuning here even
if its code could be made to compile.**

## Build system / extensibility

- Standard TrinityCore drop-in script-module mechanism exists: CMake
  `SCRIPT_MODULE_LIST`, built via `-DSCRIPT="custom"`, with
  static/dynamic build options (see `cmake/options.cmake`,
  `cmake/macros/ConfigureScripts.cmake`). Scripts live under
  `src/server/scripts/<module>`.
- **No deep plugin/hook-loader abstraction** beyond that — nothing like a
  `modules/` directory, and no existing bot/AI code
  (`grep -ri playerbot src/` returns nothing). Deep gameplay hooks (spell
  casting, movement, group/raid logic, loot) still require editing core files
  directly, same as upstream TrinityCore.

## Why this matters for mod-playerbots specifically

mod-playerbots targets a custom AzerothCore fork
(`mod-playerbots/azerothcore-wotlk`, `Playerbot` branch — 680 commits / 133
files diverged from AzerothCore master), not TrinityCore. AzerothCore itself
forked from TrinityCore around 2016 and has since diverged in module system,
naming, hooks, and build tooling. Porting its bot logic here means reconciling
**three-way divergence**: AzerothCore's fork ↔ TrinityCore master ↔ this fork's
vanilla rewrite — not a two-party merge. See
[feasibility-report.md](feasibility-report.md) for the full analysis and
recommended alternatives.

## Quick reference: where to check core drift before any future attempt

- `src/server/game/Entities/Player/Player.cpp` — `Player::UpdateStats` and
  related stat/leveling code (already rewritten here).
- `src/server/game/Entities/Unit/Unit.cpp` — `Unit::CalculateMeleeDamage` and
  combat math (already rewritten here).

If a future bot project (ported or native) hooks either of these, diff this
fork's version against stock TrinityCore master at the same functions first —
that diff size predicts how much bot-AI tuning will need redoing.
