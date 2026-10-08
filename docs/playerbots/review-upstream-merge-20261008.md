# Review: upstream merge 3f3bbd53c5 and bot behavior

Scope: merge commit `3f3bbd53c5` (into `forever`), bringing in `bf91743f0f`, `fee0064375`, `277e70a5ed`, `0baf81b1c0`, `12491bd554`.
Method: read the code diff of the merge against its first parent (`fc54d291`), then searched `src/server/game/Bots` for every symbol, spell and data
source the diff touches. No code was changed and nothing was built or run; the findings are from reading only.

## Summary

No conflict and no compile-level breakage with `src/server/game/Bots` was found. The bots do not use pets, the block value, Conflagrate, or
`spell_quest.cpp`. Two commits change combat outcomes the bots feel indirectly (block clamp, weapon-spell block), one changes a spell the bot
class table casts (Immolate), and the data imports change the spawn and quest-level data the quest index reads at start-up. Nothing needs a
code change before merging further work; the follow-ups below are mostly verification and regression tests.

| # | Severity | Area | Finding |
|---|----------|------|---------|
| 1 | Medium | Immolate script on a bot-cast Dot | Warlock bots cast Immolate; the merge adds a script effect to it. Needs an in-game check. |
| 2 | Medium | Spawn data re-import | About 1900 creature rows removed and re-added with new guids; quest spawn index and density depend on them. |
| 3 | Low-Medium | Block / crit-block (positive) | Bots with shields now take less weapon-spell damage; a wraparound on crit block is fixed. Combat tuning baselines shift. |
| 4 | Low | Hunter pet happiness | Bots have no pet logic; if a hunter bot ever owns a pet it will lose happiness and never feed it. |
| 5 | Low | `Powers` enum grew 26 to 28 | Bot code only uses a small switch with a default; no array sized by the enum. |
| 6 | Low | Taming quests and `spell_quest.cpp` | Bots already classify these quests as blocked; no interaction. |
| 7 | Info | Quest level rows added | New `quest_template_classic_level` rows can change bot quest-level gating. |

## Findings

### 1. Medium - Immolate now has a script effect (warlock bot Dot)

- Bot side: `src/server/game/Bots/BotCombat.cpp:168` lists Immolate (root 348) as `Kind::Dot`; the cast goes through `TryCast`
  (`BotCombat.cpp:1020`, `CastSpell(..., TRIGGERED_NONE)`) with the highest known rank. The re-cast guard is
  `target->HasAura(r.Id, bot->GetGUID())` (`BotCombat.cpp:1141`).
- Merge side: `src/server/scripts/Custom/Classic/world/classic_spell_scripts.cpp` (`classic_spell_warl_immolate`, registered for the eight rank ids in
  `sql/custom/world/2026_10_08_01_world_classic_warlock_immolate.sql`) makes the target cast the hidden marker aura 1282590 on itself on each hit.
- Why it matters: the bot's guard checks the rank id owned by the bot, so a second aura on the target does not break the Dot logic. The risks are
  indirect: (a) `Validate` requires `EFFECT_2` on every scripted rank; if any rank in the client data lacks it the script is skipped or logged at start-up,
  and Conflagrate would stay uncastable (bots do not cast it, so only players are affected); (b) a hostile creature casting a triggered spell on itself
  could touch threat or combat state, which the bot's fight loop reads (`GetPvECombatRefs`, `BotCombat.cpp` PickTarget).
- Follow-ups: start the server and confirm the log has no Validate error for the eight ranks; run one warlock bot against a few mobs and compare
  cast/fail counts for Immolate (`CastFails` log lines) with the previous run; confirm the mob does not enter or leave combat because of the marker.

### 2. Medium - creature spawn data re-imported (density cap, new guids)

- Merge side: `sql/custom/world/2026_10_03_00_world_forever_sniff_maps.sql` is regenerated in `277e70a5ed` (about 1900 rows removed, 1940 added;
  most are guid renumbering inside the 21200000 block, some are the density cap on Zephras Isle). The same commit regenerates the other sniff imports.
- Bot side: `src/server/game/Bots/BotQuest.cpp:543-555` builds the spawn index from `GetAllCreatureData()` and `GetAllGameObjectData()` at start-up;
  `HasSpawn` (`BotQuest.cpp:231`) feeds the static blockers `NO_TARGET_SPAWN` / `NO_STARTER_SPAWN` / `NO_ENDER_SPAWN` (`BotQuest.cpp:348-400`).
  `SpawnId` is used only for game objects (`BotQuest.cpp:2792-2813`), kept in memory, and not stored; bot quest-log keys use the bot guid and quest id
  (`BotQuestLog.cpp:39`), so guid renumbering does not invalidate saved state.
- Why it matters: a cap that lowers the number of spawns of a quest target on the Skyborne start island means fewer targets per area, so more
  contention between bots and slower kill/collect objectives. An entry that lost its last spawn would turn from plannable to `NO_TARGET_SPAWN`.
- Follow-ups: after the next start, compare the blocker counts against `docs/playerbots/quest-coverage-20261007.md` and against the previous run;
  re-run the Skyborne start-zone bots and look at time-to-complete for kill quests; if the cap has a configurable limit, record it in the notes.

### 3. Low-Medium - block value and critical-block clamp (positive, shifts baselines)

- Merge side: `src/server/game/Entities/Unit/Unit.cpp:1266` (weapon-based spells now use `GetClassicShieldBlockValue()`; before, the fraction
  truncated to 0, so players never blocked them) and `Unit.cpp:1494` (`Blocked = min(Blocked, Damage)` so a doubled critical block cannot wrap the
  unsigned subtraction).
- Bot side: the bot code never reads block or crit-block (no hits in `src/server/game/Bots`), so there is no logic conflict. `Assess`
  (`BotCombat.cpp:481`) judges mobs by level and rank only.
- Why it matters: before the clamp, a critical block on a hit smaller than the doubled block value could wrap to a very large damage number. This could
  one-shot shield-wearing bots (warrior, paladin, shaman). Now they survive such hits, and take less damage from weapon-based spells. Flee thresholds
  and death-rate figures in `sim-run-analysis-20261007.md` were measured before this and may be a little pessimistic for shield classes.
- Follow-ups: re-run the shield-class sim and compare death and flee counts with the earlier run; no code change needed.

### 4. Low - hunter pet happiness

- Merge side: `Pet.cpp:340-346`, `Pet.cpp:486`, new `Pet::GetHappinessDamageMod` used in `Unit.cpp:7076` and `Unit.cpp:8224`. Tamed pets start unhappy
  (75% damage) and lose happiness over time, faster in combat. All of it applies to `HUNTER_PET` only.
- Bot side: no pet code exists; the hunter table (`BotCombat.cpp:151-156`) casts only Auto Shot, Serpent Sting, Arcane Shot, Raptor Strike. Bots never
  tame, call, feed, or dismiss a pet. The only pet references in `Bots` are `IsPet()` filters (`BotBehavior.cpp:233`, `BotQuest.cpp:3011`), which
  are unchanged.
- Why it matters: only if a hunter bot ends up with a pet (for example through a scripted quest reward). It would deal 75% damage after a short time
  and never recover. Players are not affected by this finding.
- Follow-up: when pet support for bots is planned, add feeding (Feed Pet and the new `Pet::GetFoodBenefit` rules) and a happiness check to the plan
  in `docs/playerbots/class-role-design.md`.

### 5. Low - `Powers` enum extended (26 to 28), `UnitMods` shifted

- Merge side: `SharedDefines.h` adds `POWER_UNUSED_26` and `POWER_HAPPINESS`; `Unit.h` inserts `UNIT_MOD_UNUSED_26` / `UNIT_MOD_HAPPINESS` before
  `UNIT_MOD_ARMOR`, so every later `UnitMods` value moves by two.
- Bot side: `PowerName` (`BotAI.cpp:713`) has a `default` branch, and nothing in `Bots` uses `MAX_POWERS` or `UNIT_MOD_*`. The log field simply reads
  "other" for the new powers.
- Follow-up: confirm a full rebuild (the header change touches most of the server); no code change needed.

### 6. Low - Taming the Beast quests and `spell_quest.cpp`

- Merge side: `spell_quest.cpp:73-113` replaces the Validate list with a per-spell lookup (`GetTameSpell`); `classic_spell_scripts.cpp:406-466` adds
  channel scripts for Tame Beast and six Taming Rod spells; `2026_10_07_10_world_classic_tame_beast.sql` registers them.
- Bot side: these quests start from an item or spell and carry completion-event flags, so `Analyze` marks them `NEEDS_EVENT`
  (`BotQuest.cpp:322`) and the other six rods' quests (94013, 94792, 94863, 94864, 94978, 94979) already sit in the no-starter-row list
  (`quest-design.md`, around lines 343 and 394). `spell_quest.cpp` has no code path the bots call.
- Follow-up: none for the merge. If taming quests are later enabled for bots, the 20-second channel needs the bot to stand still and not be
  interrupted (a stuck cast from the movement layer would cancel it); add that to the class-quest scope decision in `quest-design.md` section
  "Class quests".

### 7. Info - quest level rows

- `sql/custom/world/2026_10_06_03_world_forever_quest_levels_sniff.sql` gains rows (for example 99144 and 91294) and shifts some levels.
  The bot quest index reads `GetClassicQuestLevel()` (`BotQuest.cpp:573`) and selection checks `GetQuestLevel` (`BotQuest.cpp:1611`, `2022`). A changed
  level can move a quest in or out of the "needs a different level" gate.
- Follow-up: diff the `QUEST_LEVEL` blocker count between the last run and the next one.

## Items checked with no impact

- Random suffix greens (`2026_10_08_00_world_classic_random_suffix.sql`): item data only; the bots' gear logic does not read random properties
  (no references in `Bots`).
- `TraitHandler.cpp`: adds one log line on a rejected talent commit; bots do not send talent commits.
- Feed Pet (`Spell.cpp:6323`, `SpellEffects.cpp:3566`): only reached by a player casting Feed Pet.

## Suggested order of follow-ups

1. Start-up log check: no Validate errors for the eight Immolate ranks or the seven Tame scripts (items 1, 6).
2. One short warlock-bot run, then compare Immolate casts and fails (item 1).
3. Compare quest blocker counts (`NO_TARGET_SPAWN`, `QUEST_LEVEL`) before and after the data import (items 2, 7).
4. Re-run the shield-class simulation and note the new baseline (item 3).
5. Add pet support (feeding, dismiss) to the plan only if hunter pets become a bot goal (item 4).
