# Bot group control: crowd control, interrupts, dispels (2026-10-08)

Bots in a group interrupt, dispel and control mobs together instead of each acting alone. Everything is off unless `Bot.AI.GroupControl.Enabled = 1` (settings in `botserver.conf.dist`, section BOT GROUP CONTROL). Requested by **Trixx34**.

## What the bots do

| Feature | Behaviour |
|---|---|
| Interrupt | Any enemy of the group (not only the bot's own fight target) that casts a spell worth stopping is interrupted by **one** bot that can reach it: Kick, Counterspell, Hammer of Justice, Earth Shock. Worth stopping: heals and stun/fear/root/charm/silence casts always, other casts of at least `Interrupt.MinCastMs` with at least `Interrupt.MinRemainingMs` left, never a spell that cannot be interrupted. A second bot takes over (backup) if the cast is still running after `Interrupt.HoldMs`. |
| Dispel | Priest (Dispel Magic, Cure Disease), paladin (Purify, Cleanse), druid (Cure Poison, Remove Curse; only unshifted), shaman (Cure Poison, Cure Disease), mage (Remove Lesser Curse) remove harmful effects from group members and themselves. Order: stun/polymorph/fear/root/silence (100), damage over time (40), slow (20), other (10); lowest health first on a tie. In a fight only effects of at least `Dispel.MinPriority`; out of a fight everything harmful (`Dispel.OutOfCombat`). One bot per member (`Dispel.HoldMs`). |
| Crowd control | A player marks mobs with raid target icons (default moon, square, triangle, diamond: `Control.Marks`). Bots that can control the mob (Polymorph, Shackle Undead, Hibernate, Banish, Scare Beast, Sap) do it, one bot per mob, any number of marked mobs, strongest first, before the pull (`Control.PrePull`, spell range only: bots do not walk up to the mob) or during the fight. Without a mark the old automatic control (`Bot.AI.Rotation.CrowdControl`) still controls the strongest extra attacker, at most `Control.MaxAuto`. |
| Leave controlled mobs alone | Target choice skips controlled or marked mobs while others are left, and a bot drops such a fight target. While a mob other than the fight target is controlled within 30 yards, rotation rows that hit several enemies (Thunder Clap, Cleave, Multi-Shot, Frost Nova, Cone of Cold, Psychic Scream, Consecration) are skipped. |
| Chat orders (`Bot.Chat.Orders.Enabled`) | `cc`, `interrupt`, `dispel` on the issuer's target. Example: select a mob, say `mage cc`. Codes: `OK`, `NO_SPELL`, `WRONG_TYPE`, `OUT_OF_RANGE`, `NO_LOS`, `ALREADY_CONTROLLED`, `ASSIGNED` (another bot took it), `NOT_CASTING`, `NOTHING_TO_DISPEL`, `NOT_HOSTILE`, `NOT_FRIENDLY`, `NO_POWER`, `COOLDOWN`, `BUSY`, `CAST_FAILED`. Handler: `BotCombatOrder` (BotCombat.cpp), a single case in `BotChat.cpp`. |

Log rows (category combat): `INTERRUPT_CAST`, `DISPEL_CAST`, `CC_CAST` (in a fight), `CC_PREPULL`.

## Code

* `BotGroupControl.h/.cpp`: pure decisions, unit tested in `tests/game/BotGroupControl.cpp` (`[BotGroupControl]`): `ClaimBoard` (shared claims so two bots never answer the same cast, effect or mob, with expiry for a backup), `WorthInterrupting`, `AuraPriority`, `PickDispelTarget`, `ParseMarkList`, `PickControlTarget`, `ReservedForControl`.
* `BotCombat.cpp`: fills the facts from the game (`CastFactsOf`, `TryInterrupt`, `TryDispel`, `PlanControl`/`TryGroupControl`), runs them before the rotation (`GroupControlStep`) and out of a fight (strategy `groupcontrol`, action `combat_group_support`).
* `Group::GetTargetIcon` (Group.h): read access to the raid target icons.

## Limits

* No sim server run yet; the container build compiles and the Catch2 tests pass, spell ids are checked against the DB at startup like the rest of the spell table (a wrong id or name is dropped and logged).
* Sap works only from stealth within 10 yards; bots do not stealth or walk up for it.
* Warriors (Pummel/Shield Bash need a stance or a shield), warlock pets (Spell Lock) and Silence are not used as interrupts.
* Enemy buffs are not dispelled (only harmful effects on friends).
* Frost Nova rooted mobs count as controlled (the core flags them breakable by damage) and are fought last.
