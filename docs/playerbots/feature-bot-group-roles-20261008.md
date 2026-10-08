# Bot group roles: healing the group and assisting the tank (2026-10-08)

Branch `feature/bot-group-roles`, started from `forever`. First slice of the tank and healer combat roles in `class-role-design.md` ("Tank threat/taunt, healer triage, assist/focus rules"). Switch: `Bot.AI.Roles.Enabled` (default **off**); with it off nothing changes.

## What it does

* **Healer triage.** A bot with a heal spell (priest, paladin, shaman, druid: the one heal row of the spell table) that is in a group heals the member who needs it most: an emergency (under `EmergencyPct`, default 35) first, then the tank (under `TankHealBelowPct`, 90), then the lowest health percent under `AllyHealBelowPct` (80). Dead members, members out of heal range and members out of line of sight are skipped; with nobody reachable and the bot itself healthy it casts nothing. The heal trigger also fires for group members, and the group heal repeats every 1.5 s instead of 3 s. Log row `GROUP_HEAL` (self heals stay `SELF_HEAL`).
* **Assist.** Every bot that is not the tank picks, among the mobs already on it, the tank's current target, so threat builds on one mob. A mob that attacks another non-tank member is attacked instead when it is nearly dead (under 25%) or when the tank has no target among them. Mobs the bot would flee from are left out.
* The tank is the warrior of the group (the first living warrior on the map). No warrior means no tank: triage then treats everybody equally and nobody assists.
* The decisions are `BotGroupRoles.h/.cpp` (pure, tests `tests/game/BotGroupRoles.cpp`, tag `[BotGroupRoles]`); the glue is in `BotCombat.cpp` (`GroupAllies`, `NeedHealTrigger`, `HealAction`, `PickTarget`).

## Group combat, taunt and hold fire (second step, 2026-10-08)

* **Joining a fight.** A bot in a group that is not in combat itself but has a group member within 45 yards fighting a mob it could attack goes into the Combat engine (`GROUP_COMBAT`). Its target candidates are then the mobs the other members fight, so a healer heals and a damage dealer assists before any mob reaches it. This closes the gap listed below for the first step.
* **Tank.** The tank is the living warrior of the group with the lowest guid (same answer for every member). `combat_tank` runs before the engage and cast actions: Defensive Stance, then a Taunt on a mob that attacks another member (a mob on a healer first, then the weakest victim, then the mob with the most health; a mob already taunted by this bot is skipped), then Sunder Armor up to five stacks. Log rows `TANK_STANCE`, `TAUNT`.
* **Hold fire.** `Bot.AI.Roles.HoldSec` (3): a non-tank waits for the tank to gather threat before the first hit or cast, shorter (1.5 s) once the mob has turned to the tank, and not at all when the mob attacks the bot, there is no tank in range, or the tank fights another mob.
* Pure parts: `PickTauntTarget`, `HoldFire` in `BotGroupRoles` (tested). Glue in `BotCombat.cpp`, one hook in `BotAI::DesiredState`.

Limits: Taunt and Defensive Stance are the Classic ids resolved by name (a mismatch skips the step silently); no paladin or druid tanks; no threat table is read (taunt triggers on who a mob attacks, not on threat values); the offensive stance spells of the warrior table (Rend, Charge) fail while in Defensive Stance and back off for 60 s.

## Not in this step

* **Taunt and threat.** No taunt, no threat-based target switching, no tank stance/form management (a paladin or druid tank is not recognised).
* **Entering combat.** A bot only acts while it is in combat itself. A healer standing next to a hurt tank who has not been attacked is not in the Combat state and does not heal until a mob reaches it. This needs a group-combat entry rule in the engine and is the main follow-up.
* **Choosing between heals.** `PickHeal` (emergency: most healing per cast second; normal: cheapest per point that covers the gap) is written and tested but the spell table has one heal per class, so the glue uses that one spell. It becomes useful when classes get a fast and an efficient heal.
* Mana discipline for healers, dispels, resurrection, positioning.

## Verification

`BotGroupRoles` tests pass (30 assertions, compiled standalone); `BotCombat.cpp` and `BotGroupRoles.cpp` compile in the full cmake tree. Not run on a server. To check with the switch on: a group of a warrior, a priest and two damage dealers on a pull: `GROUP_HEAL` rows on the warrior, damage dealers' `TARGET_PICKED` on the warrior's target, deaths against a run with the switch off.
