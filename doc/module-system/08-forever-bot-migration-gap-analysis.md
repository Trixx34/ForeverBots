# Module System — Forever Bot Migration Gap Analysis

Status: analysis complete, no decisions made yet on the open items below.
Builds on `00`-`07` (full module system design) + investigation of `forever` branch bot system (`src/server/game/Bots/`, ~43,500 lines, 154 files).

## Current state of the bot system (as of `forever`)

- Custom-built (not mod-playerbots — see `docs/playerbots/feasibility-report.md` for that rejection), fully merged into the core `game` static lib via `CollectAndAddSourceFiles` glob (no isolated script module, no CMake bot-specific entries).
- Integration is **4 direct core-file edits**: `World.cpp` (`sBotMgr->Update(diff)` unconditional call), `Player.cpp`/`Unit.cpp`/`Spell.cpp` (inline `GetSession()->GetBotAI()` checks at ~10+ gameplay call sites: damage taken, spell cast, aura change, death, level-up, quest given), `WorldSession.h` (extended with `BotAI* _botAI` member).
- Bots are literal `WorldSession` objects (no socket), owned by `BotMgr`, ticked inline via `Player::Update` — no separate scheduler.
- Central classes operate directly on engine objects throughout, e.g.:
  ```cpp
  void BotAI::OnDamageTaken(Player* bot, Unit* attacker, uint32 damage, ..., SpellInfo const* spell);
  void BotAI::OnSpellCast(Player* bot, SpellInfo const* spell, uint32 power, Unit* target, ...);
  ```
- One partially reusable exception: ~18 `BotXxxPlan.{h,cpp}` files (movement, loot, auction, duel, repair, reputation) are documented as pure-data decision logic over callbacks — the closest existing thing to a serialization boundary, but a minority of the system.
- Direct SQL against `CharacterDatabase`/`WorldDatabase` plus its own `forever_botlog` database (7 tables: `bot`, `bot_event`, `bot_event_hot`, `bot_event_hourly`, `bot_death_daily`, `bot_pos`, `bot_event_rollup`).
- Dedicated `botserver.conf.dist` (`Bot.*` keys).

## Compatibility assessment against the designed module architecture (docs 01-07)

### Already compatible, no new design needed

| Item | Why it fits |
|---|---|
| Bot identity/ticking | Bots are already core-owned sessions ticked by core — matches "module only supplies decisions, core ticks the session" exactly. |
| Movement execution | Core already does `PathGenerator`/navmesh pathing; a module just needs to emit `MOVE_TO` with a destination — already in the Phase 1 `BotAction` enum. |
| GM bot commands (`cs_bot.cpp`) | Already proper `CommandScript` hooks — fits the Tier 1 `EventRequest`/`EventReply` pattern (Phase 1) directly, no new concept required. |
| Config (`Bot.*` settings) | Fits `ModuleConfig` delivery over Tier 1 (Phase 1 addendum) as-is. |

### Real gaps — need new design before porting is feasible

1. **Most touch points already have a generic `ScriptMgr` hook — bots just aren't using them.** Confirmed by direct check against `src/server/game/Scripting/ScriptMgr.h`:

   | Bot's ad-hoc call | Existing generic hook |
   |---|---|
   | `OnDamageTaken` | `UnitScript::OnDamage(Unit* attacker, Unit* victim, uint32& damage)` (`:428`) |
   | heal equivalent | `UnitScript::OnHeal` (`:425`) |
   | `OnSpellCast` | `PlayerScript::OnSpellCast(Player*, Spell*, bool)` (`:757`) |
   | death | `PlayerScript::OnPVPKill`/`OnCreatureKill`/`OnPlayerKilledByCreature` (`:702-708`) |
   | level-up | `PlayerScript::OnLevelChanged(Player*, uint8 oldLevel)` (`:711`) |
   | quest given/status | `PlayerScript::OnQuestStatusChange` (`:787`), `ServerScript::OnQuestAccept`/`OnQuestStatusChange` (`:399`, `:956`) |

   **One genuine gap**: `OnAuraChange` (apply/remove) has no generic subscribable hook — only the per-spell `AuraScript` mechanism, which isn't equivalent. This needs an actual new `ScriptMgr` category.

   **Revised scope**: formalizing hooks is mostly "replace ~10 hardcoded inline `GetSession()->GetBotAI()` calls with proper `ScriptObject` subclass registrations against hooks that already exist," plus **one** new hook category for aura changes — not "invent 15-20 new categories" as originally estimated. Confirms the decision to formalize via `ScriptMgr` (rather than keep scattered inline edits) is cheaper than it first looked.

2. **Tier 2 schema is far too narrow.** Current `BotSnapshot`/`BotAction` (health/mana/cooldowns/nearby-units/attack-move-follow-stop) covers only the combat+movement slice (`BotCombat.cpp` + `BotBehavior.cpp`, ~245KB of the 1.8MB total). Covers none of quests, loot, gear, group, auction, talents, dungeons, chat.

3. **Reframing that narrows the real gap**: most of the system by volume (quest decisions, loot rolls, auction/trade, gear changes, talent picks, dungeon logic, chat/whisper orders) is **event-driven and low-frequency**, not per-tick. These fit the **already-designed Tier 1 request/reply pattern**, using the existing `ScriptMgr` hooks identified in #1 as their event source — no Tier 2 redesign needed for them. Only combat+movement genuinely requires the Tier 2 shared-memory loop.

4. **Module DB access — resolved by reframing, not a real either/or.** Core-owned data (inventory, quests, character state) should never be touched via raw SQL by a module regardless of credentials — those are *actions* a module requests (equip item, accept quest) that core executes through its own existing in-process APIs, same path a real player's input takes. That's just more Tier 1 hook/request categories (#1/#3), not a separate DB design problem. The only real module DB need is **module-private bookkeeping** (e.g. `forever_botlog`-style analytics/internal state) — **resolved: dedicated module DB/schema with its own credentials**, delivered via `ModuleConfig`, module manages its own migrations. Requires migrating `forever_botlog` + any bot-related `CharacterDatabase` tables (e.g. bot-alt tables) that are genuinely module-private (not core character state) to this module-owned schema.

5. **Scale**: ~43,500 lines is a full reimplementation against new interfaces, not a refactor. The existing code is valuable as a **reference for decision logic** (what a bot should decide given a situation), but nearly every file's *mechanism* (direct `Player*`/`Unit*`/`Group*`/`SpellInfo*` manipulation) must be rewritten against serialized Tier 1 messages / Tier 2 snapshots. The `BotXxxPlan` pattern (~18 files) is the best starting reference for what a ported, data-driven version should look like.

## Decisions (resolved)

- **Core-edit posture**: formalize as proper `ScriptMgr` hook categories (reusing existing ones per #1, adding the one new `OnAuraChange`-equivalent category) — smaller effort than first estimated, and keeps the diff against `advocaite/TrinityCore` clean/reusable by non-bot modules.
- **DB access model**: dedicated module DB/schema for module-private data; core-owned data goes through Tier 1 action requests (#4).
- **Migration end goal**: **full migration** — everything including combat/movement eventually becomes a module, with `src/server/game/Bots/` ultimately removable from core. This raises the importance of Phase 5 (reference module) actually proving Tier 2 viable for real combat decision-making before full migration can proceed with confidence — if Tier 2 can't hit acceptable combat responsiveness, this goal needs revisiting, not quietly downgraded to partial migration without saying so.

## Recommended path forward

1. Add the one new `OnAuraChange`-equivalent `ScriptMgr` hook category; redirect bot integration to use it + the existing hooks from #1 (replacing the 4 hardcoded core-file edit sites).
2. Stand up the dedicated module DB/schema for bot-private bookkeeping; migrate `forever_botlog` + genuinely module-private tables to it.
3. Port the **low-frequency subsystems first** (quest, loot, auction, gear, chat orders) as Tier 1 modules — validates the "majority of the system fits Tier 1" reframing with real code, independent of Tier 2 being proven.
4. Port **combat + movement last**, once Phase 5's reference module has validated the Tier 2 pipeline at realistic scale — since full migration is now the explicit goal, Phase 5's exit criteria (`07-phase5-reference-module-design.md`) should be treated as a hard gate before committing engineering time to porting `BotCombat.cpp`/`BotBehavior.cpp`.
5. Expect this to be a **multi-month, incremental rewrite effort** — run the ported module system alongside the existing in-core bot system during transition (feature-flag/config-gated), not a hard cutover. Given full migration is the goal, plan the in-core `Bots/` removal as the final step once the last subsystem (combat) is validated in production, not a step taken on faith.

