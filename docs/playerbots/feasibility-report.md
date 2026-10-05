# Feasibility: player bots for the `forever` branch

**Date:** 2026-10-05
**Reference project:** [mod-playerbots/mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
**Scope:** mod-playerbots was given as an example of a good *working* bot
implementation, not a mandatory port target. This report weighs porting it
against native/alternative approaches.

## Summary verdict

**Porting mod-playerbots directly is not advisable** — it would be a multi-month
rewrite disguised as an integration, for a reason more fundamental than this
fork's Classic customizations:

> **mod-playerbots targets AzerothCore, not TrinityCore.** Its own install docs
> say installations require a custom fork, `mod-playerbots/azerothcore-wotlk`
> (`Playerbot` branch), and that "the standard AzerothCore repository will not
> work."

AzerothCore forked from TrinityCore around 2016 and has since diverged in module
system, naming conventions, script hooks, and build tooling. So the task isn't
"apply a module to TrinityCore," it's "port a module between two
sibling-but-diverged emulator codebases" — *stacked on top of* "re-author WotLK
game data/mechanics for vanilla Classic," which is this fork's own problem on top
of that.

## Core architectural mismatch

1. **Codebase lineage mismatch.** mod-playerbots isn't additive-only — its
   companion core fork (`mod-playerbots/azerothcore-wotlk`, branch `Playerbot`)
   is **680 commits / 133 files changed** versus AzerothCore master, touching
   core gameplay classes directly (`Player`, `Group`, `WorldSession`, and
   battleground-adjacent logic, per the project's architecture notes on
   `PlayerbotAI`/`PlayerbotMgr`). None of that diff applies cleanly to
   TrinityCore's differently-shaped equivalents of those classes.
2. **WotLK data/mechanics mismatch.** The bot AI's talent/spec system is built
   entirely around WotLK talent trees and spell IDs — bots use stored spec
   numbers, premade specs defined in configuration, and `LearnTalent()` calls
   against WotLK talent strings. Vanilla Classic talent trees (level-60 cap,
   different nodes/ranks/spell IDs) share no data compatibility with this.
3. **Compounding with this fork's own rewrites.** `forever` has already
   rewritten core combat/stat code toward vanilla semantics — see
   [fork-context.md](fork-context.md) for specifics (base stats set not added on
   level up, rounded melee damage, vanilla stat/combat formulas). mod-playerbots'
   bot decision logic (threat thresholds, DPS rotations, healing logic) was tuned
   against TrinityCore/WotLK's *unmodified* stat/combat math. Even a
   hypothetically successful structural port would misbehave until every
   class's AI logic were re-tuned against this fork's formulas.

## Blockers vs. tedious-but-doable

| | |
|---|---|
| **Blocker** | Porting the ~133-file AzerothCore-specific diff onto TrinityCore master's differently-structured `Player`/`Group`/`WorldSession`/battleground classes. This is a from-scratch reimplementation of the hook layer, not a patch apply. |
| **Blocker** | Every class's spell/talent/strategy data is WotLK-specific and must be re-authored for vanilla Classic spell IDs and talent layout — effectively rewriting the AI's "brain" per class/spec. |
| **Tedious, not blocking** | Chat command parsing, basic movement/follow, loot/trade/auction-house interaction — more mechanical and less version-coupled. |

## Alternatives worth considering

- **[cmangos/playerbots](https://github.com/cmangos/playerbots) (ike3's Bot AI
  Core)** — explicitly supports cmangos-classic (1.12), TBC, and WotLK, so its
  talent/spell logic is *already vanilla-correct*. Still a different core
  lineage (MaNGOS vs. Trinity) so not directly portable, but a much better
  **architecture/strategy-logic reference** given this fork's world was also
  converted from VMaNGOS (a MaNGOS-classic derivative) — the conceptual distance
  is smaller than it looks.
- **[trickerer's NPCBots](https://github.com/trickerer/Trinity-Bots)**
  (TrinityCore/AzerothCore 3.3.5) — a simpler "hireable NPC companion" model
  rather than full player-replacement bots. Much smaller hook surface, easier to
  adapt to a modified core, but less capable (no full party-filling AI, no world
  population of "other players").
- **Minimal custom bot system** — a small follower/party-filler bot built on
  TrinityCore's existing `Creature`/`Unit` AI plus `Player`-mimicking stubs,
  scoped only to vanilla-appropriate behavior (tank/heal/DPS rotation for
  1–60 content), with no WotLK data dependency at all. Given the lineage
  mismatch above, **this is the recommended default direction** unless the
  validation step below changes the picture.

## Recommendation

Given the AzerothCore/TrinityCore lineage split (not just the Classic data
mismatch), lean toward **(a) borrow ike3's cmangos/playerbots as a design
reference for bot AI structure and strategy logic**, since it's already
vanilla-correct and closer in spirit to a VMaNGOS-derived world, **and (b)
implement natively on TrinityCore** rather than attempting to port either
project's source wholesale.

## Suggested first validation step

Don't start with mod-playerbots at all. Spend a day:

1. Diffing `mod-playerbots/azerothcore-wotlk`'s `Playerbot` branch against its
   own `master` to size the hook surface in AzerothCore's `Player.cpp`/
   `Group.cpp` equivalents.
2. Grepping this fork's `src/server/game/Entities/Player/Player.cpp` and
   `Unit.cpp` for how far they've drifted from stock TrinityCore master at the
   same hook points (e.g. `Player::UpdateStats`, `Unit::CalculateMeleeDamage`).

If those files are already 30–40%+ rewritten here, that confirms the native
route: reconciling three-way divergence (AzerothCore fork ↔ TrinityCore master ↔
this fork's vanilla rewrite) would cost more than building a small bot system
from scratch.
