# Follow-ups to the upstream merge review (3f3bbd53c5)

Works through the follow-ups of `review-upstream-merge-20261008.md`. Nothing here was built or run; each section says what was verified from
the repo and what still needs a running server.

## 1. Immolate (finding 1)

### Validate for the eight ranks - not checkable from the repo

`classic_spell_warl_immolate::Validate` (`classic_spell_scripts.cpp:477`) needs `SPELL_EFFECT_SCRIPT_EFFECT` at `EFFECT_2` for each rank
(348, 707, 1094, 2941, 11665, 11667, 11668, 25309) and spell 1282590 to exist.

- The spell and effect rows come from the Classic client data (SpellEffect / Spell DB2), which is not in the repository. The hotfix SQL
  wipes the retail rows (`2026_09_28_00_hotfixes_classic_spell_duplicates.sql`) and its sniff import (`2026_10_01_02_hotfixes_forever_sniff.sql`,
  28 `spell_effect` rows) has no row for any of the eight ranks or for 1282590.
- Result: which ranks fail, if any, is **not known**. It needs the start-up log. A failing rank shows as a script-validation error naming the
  spell id, and the script is skipped for that rank only (the rank still casts as before).

### Can the marker self-cast change combat or threat state the bot reads?

Read only; no concrete failure path found, so no guard was added to `BotCombat.cpp`.

- `TryCast` (`BotCombat.cpp:1017`) casts Immolate normally. The script runs in `OnEffectHitTarget`, so only when Immolate hits; on a miss, resist
  or immune the marker is never cast.
- The marker is cast by the target on itself (`Spell.cpp:2771`): `SetInCombatWith(originalCaster)` runs only when the spell is not positive and
  has initial aggro or the target is already engaged. The target is engaged with the bot by the Immolate hit itself at that point, so the
  combat reference already exists and nothing new is added. A positive marker skips that line entirely.
- `PickTarget` (`BotCombat.cpp:912`) reads only `GetPvECombatRefs()`. The marker adds no new combat reference to the bot; it also does no
  damage, so it adds no threat.
- The re-cast guard (`BotCombat.cpp:1141`) is `target->HasAura(r.Id, bot->GetGUID())`: the marker has a different id, so it does not make the bot
  think Immolate is still up or stop it re-casting.
- Residual risk: spell 1282590 itself is not readable here (see above). If its data had a hostile effect or an `SPELL_ATTR` that forces combat on
  a non-engaged target, the mob would still already be in combat from Immolate. Confirm with one warlock bot run (needs a running server).

## 2. Spawn data (finding 2)

Check: `check_spawn_delta.py` (read-only, run `python3 -I docs/playerbots/check_spawn_delta.py` from the repo root); output saved in
`spawn-delta-20261008.txt`. It counts rows per (entry, map) in `2026_10_03_00_world_forever_sniff_maps.sql` at `fc54d291` and `3f3bbd53`.

- No entry lost a spawn: creature 181 -> 257 entries with spawns, 95 changed, all up, none to zero; game object 467 -> 964, 507 changed, all up,
  none to zero. The "removed and re-added" rows of the review were guid renumbering; per entry the file only gained spawns (about 1960 lines
  now against 1082).
- Quest-relevant changed entries (11 creatures; see the output file): 250282, 250283, 259433, 259434 and 275954 gained spawns;
  267006, 267310, 268682, 268684, 268701 and 275437 went from 0 to 1-5 spawns in this file. No game object entry with a quest row changed.
- So the import cannot create a new `NO_TARGET_SPAWN` from this file, and the Zephras Isle density cap named in the review does not show up as
  a lowered count here. "Quest-relevant" only covers quest rows in `sql/custom/world` (starter/ender rows and monster/game object
  objectives); quests whose rows live only in the base world database are not tagged.
- The 0 -> N entries could turn earlier `NO_TARGET_SPAWN` blockers into plannable quests; only the blocker counts from a running server
  (against `quest-coverage-20261007.md`) confirm that.

## 3. Quest levels (finding 7)

Check: `check_quest_levels.py` (read-only, `python3 -I docs/playerbots/check_quest_levels.py`), output in `quest-level-delta-20261008.txt`. It replays the
`quest_template_classic_level` and `quest_classic_level` statements in `sql/custom/world` in file name order at `fc54d291` and `3f3bbd53` and
compares the effective rows.

- 46 quests changed, all of them new rows (no existing row changed value): 79007, 79358, 79361, 86576, 86776, 91294, 91316, 92422, 92596, 92597,
  92699, 92701, 92706, 92709, 92727, 92741, 92840, 92849, 92850, 92911, 93461, 93739, 94413, 96057, 96102, 96604, 96874, 96899, 97003, 97242,
  97246, 97250, 97326, 97925, 97932, 97937, 97950, 97953, 97955, 97956, 97961, 97973, 98427, 99048, 99052, 99144. Per-quest level, minimum
  level and the first bot level that may take it are in the output file. (The 182 added lines of the diff are mostly moved identical rows.)
- Before, none of the 46 had a row in the repo SQL, so `GetQuestLevel` / `GetQuestMinLevel` fell back to ContentTuning (not in the repo, so
  the old value is not known here). Now every one has a minimum level and a quest level, so each can cross both bot gates:
  - `SatisfyQuestLevel` at `BotQuest.cpp:1604` blocks with `QUEST_LEVEL` while the bot level is below the minimum level;
  - `BotQuest.cpp:1611` and `:2022` skip the quest while its level is more than 2 above the bot (`GetQuestLevel > bot level + 2`).
  - The first bot level that may take each quest is `max(MinLevel, QuestLevel - 2)`: it ranges from 2 to 38 over the 46 quests, 17 of them
    at level 5 or below (for example 92597, first level 2, and 92596 and 94413, first level 4).
- The expected effect is a higher `QUEST_LEVEL` count for low bots and no change for bots at or above the thresholds. Confirm by comparing the
  blocker counts of the next run with `quest-coverage-20261007.md` (needs a running server).
