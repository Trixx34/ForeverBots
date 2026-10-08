# Bot group roles: healing the group and assisting the tank (2026-10-08)

Branch `feature/bot-group-roles`, started from `forever`. First slice of the tank and healer combat roles in `class-role-design.md` ("Tank threat/taunt, healer triage, assist/focus rules"). Switch: `Bot.AI.Roles.Enabled` (default **off**); with it off nothing changes.

## What it does

* **Healer triage.** A bot with a heal spell (priest, paladin, shaman, druid: the one heal row of the spell table) that is in a group heals the member who needs it most: an emergency (under `EmergencyPct`, default 35) first, then the tank (under `TankHealBelowPct`, 90), then the lowest health percent under `AllyHealBelowPct` (80). Dead members, members out of heal range and members out of line of sight are skipped; with nobody reachable and the bot itself healthy it casts nothing. The heal trigger also fires for group members, and the group heal repeats every 1.5 s instead of 3 s. Log row `GROUP_HEAL` (self heals stay `SELF_HEAL`).
* **Assist.** Every bot that is not the tank picks, among the mobs already on it, the tank's current target, so threat builds on one mob. A mob that attacks another non-tank member is attacked instead when it is nearly dead (under 25%) or when the tank has no target among them. Mobs the bot would flee from are left out.
* The tank is the warrior of the group (the first living warrior on the map). No warrior means no tank: triage then treats everybody equally and nobody assists.
* The decisions are `BotGroupRoles.h/.cpp` (pure, tests `tests/game/BotGroupRoles.cpp`, tag `[BotGroupRoles]`); the glue is in `BotCombat.cpp` (`GroupAllies`, `NeedHealTrigger`, `HealAction`, `PickTarget`).

## Not in this step

* **Taunt and threat.** No taunt, no threat-based target switching, no tank stance/form management (a paladin or druid tank is not recognised).
* **Entering combat.** A bot only acts while it is in combat itself. A healer standing next to a hurt tank who has not been attacked is not in the Combat state and does not heal until a mob reaches it. This needs a group-combat entry rule in the engine and is the main follow-up.
* **Choosing between heals.** `PickHeal` (emergency: most healing per cast second; normal: cheapest per point that covers the gap) is written and tested but the spell table has one heal per class, so the glue uses that one spell. It becomes useful when classes get a fast and an efficient heal.
* Mana discipline for healers, dispels, resurrection, positioning.

## Verification

`BotGroupRoles` tests pass (30 assertions, compiled standalone); `BotCombat.cpp` and `BotGroupRoles.cpp` compile in the full cmake tree. Not run on a server. To check with the switch on: a group of a warrior, a priest and two damage dealers on a pull: `GROUP_HEAL` rows on the warrior, damage dealers' `TARGET_PICKED` on the warrior's target, deaths against a run with the switch off.
