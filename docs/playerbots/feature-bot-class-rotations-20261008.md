# Bot class rotations (2026-10-08)

Branch `feature/bot-class-rotations`, started from `forever`. Until now the bots cast only three or four fixed spells per class (the table in `BotCombat.cpp`); everything else they trained was never cast. This adds conditional rows to that table. Switch: `Bot.AI.Rotation.Enabled` (default **off**, `botserver.conf.dist`). With the switch off every conditional row is skipped, so the old behaviour is unchanged.

## How it works

* `SpellDef` has a new last field `When` (`BotRotation::Rule`: a condition and a parameter). Existing rows keep condition `Always` and run as before.
* `BotRotation.h/.cpp` is the pure part: `Allowed(rule, facts)` over plain data, unit tested (`tests/game/BotRotation.cpp`, tag `[BotRotation]`).
* `CastAction` fills the facts once per tick (`RotationFacts`: own health and power percent, target health, hostile units attacking the bot, target casting / fleeing / elite / level difference, time in the fight) and skips a row whose condition fails. Table order is still the priority order.
* New spell kind `Debuff`: cast while the target lacks the spell's aura (Sunder Armor, Demoralizing Shout, Hunter's Mark, Faerie Fire). Finishers skip themselves while their aura runs (Slice and Dice).
* A row is dropped at startup (and logged) when its spell id is missing or the name differs; a spell the bot has not learned is skipped. Wrong ids therefore cost nothing but the missing spell.

## Conditions

TargetHpAbove / TargetHpBelow, SelfHpBelow, SelfPowerBelow / SelfPowerAbove, EnemiesAtLeast, TargetCasting, TargetFleeing, Opener (first N seconds), TargetStrong (elite or N levels above), LifeTapSafe.

## Rows added

| Class | Rows (condition) |
|---|---|
| Warrior | Charge (opener), Bloodrage (rage under 20%), Execute (target under 20%), Hamstring (fleeing), Thunder Clap (2+ enemies), Demoralizing Shout (3+), Sunder Armor (strong target), Cleave (2+) |
| Rogue | Kick (target casting), Evasion (health under 50%), Slice and Dice (2 combo points, target above 40%) |
| Paladin | Divine Protection (health under 35%), Hammer of Justice (target casting), Consecration (3+ enemies) |
| Hunter | Hunter's Mark (strong target), Concussive Shot and Wing Clip (fleeing), Multi-Shot (2+) |
| Mage | Counterspell (casting), Frost Nova (2+), Cone of Cold (3+), Mana Shield (health under 40%), Ice Barrier (under 70%) |
| Priest | Power Word: Shield (under 70%), Inner Fire (opener), Psychic Scream (3+), Mind Blast (mana above 25%) |
| Warlock | Death Coil (under 40%), Drain Life (under 50%), Life Tap (health above 60%, mana under 30%), Curse of Agony (target above 40%) |
| Shaman | Lightning Shield (opener), Flame Shock (target above 40%), Stormstrike |
| Druid | Barkskin (under 50%), Thorns (opener), Faerie Fire (strong target), Entangling Roots (fleeing) |

## Not in this step

Out-of-combat buffs (the self-buff throttle is one shared timer for the whole fight, so more than a few buffs would starve each other), totems, druid forms and stances, pet-class abilities beyond the pet strategy, healing other players, interrupt timing beyond "the target is casting". Spec and talents are not read; talent spells are simply used when known. Overlap with open PR #7 (wand result 32, seal recast) is limited to adjacent lines in `BotCombat.cpp`.

## Verification

`BotRotation` tests pass (compiled standalone). `BotCombat.cpp` and `BotRotation.cpp` compile in the full cmake tree. Nothing was run on a server: rank-1 spell ids were written from memory and are validated by name at startup (look for the "class spells validated, dropped:" line), so check that line first. To check in the sim with the switch on: cast counts per new spell, `CAST_FAILED` reasons (ONLY_SHAPESHIFT / stance results on warrior rows are expected to back off for 60 s), deaths per class against a run with the switch off.
