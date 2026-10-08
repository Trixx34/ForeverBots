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
