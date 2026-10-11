# Loop guards (R-V2-01 to R-V2-03) and the new log rows

Source: `sim-run-800-analysis-20261011.md` and `rules/proposed-v2-20261011.md`. Nothing here has run on a sim server yet.

## What changed

| Rule | Change | Settings |
|---|---|---|
| R-V2-01 weapon | A profession tool (fishing pole by item subclass, plus the entries in the tool list) is never worn as gear (`BestGearSlot`). A tool left in the main hand, or an empty main hand, is swapped for the best usable weapon in the bags every `CheckSec`, right after a fishing stint, and when the approach loop fires. Rows: `WEAPON_RESTORED`, `WEAPON_RESTORE_NONE`. | `Bot.AI.Weapon.Restore`, `.CheckSec`, `.ToolEntries` (default `7005,2901`, unverified item entries) |
| R-V2-02 approach | The 6th `APPROACH_TIMEOUT` of a bot inside 10 min logs `APPROACH_ESCALATED` with the cause (`weapon`, `ammo`, `unknown`), fixes the cause where it can (wield the weapon, request an ammo trip when `Bot.AI.Ammo.Enabled` is on) and keeps the last target out of the pick for 10 min. | `Bot.AI.Combat.ApproachEscalate.*` |
| R-V2-03 trips | Per skill trip guard: profession trips wait `ProfCooldownSec` between trips, give up after `ProfMaxAttempts` (6 h), stay within `ProfMaxYd` (500, was 3000) and the bot zone. Workshop trips wait `WorkshopCooldownSec` and give up after `WorkshopMaxAttempts`. A trained spell or a craft after the trip resets the skill. Row on give up: `TRIP_GIVEN_UP`. | `Bot.Quest.TripGuard.*` |

Not done: the "every mob on the path is at most bot level + 2" check of R-V2-03. It needs the path mobs from the navmesh; the same-zone and distance limits stand in for it.

## New log rows (for the next sim run)

- `TRIP_ARRIVED` / `TRIP_FAILED`: every service trip (trainer, vendor, workshop, ...) ends in one of them, with kind, reason, elapsed seconds and skill. Fixes the "no arrival or failure event" gap of the analysis.
- `VENDOR_RESULT`: written at every vendor visit: junk stacks found, sold, refused (first refusal code), free slots before and after, bag item count, items of white quality or better, items that start a quest.
- Death (`DIED`) and `COMBAT_START` rows carry `task` (`quest`, `grind`, `trip`, `idle`) and `quest_id` in the details; the `quest_id` column is set too. Fixes "death events have quest_id NULL".

`COMBAT_SUMMARY.outcome` and the per-state time ledger of the analysis are not touched.
