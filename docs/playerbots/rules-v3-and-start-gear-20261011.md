# Rules R-V3-01..06, extra logging and starter gear (2026-10-11)

Implements `rules/proposed-v3-20261011.md`. All settings live in `botserver.conf.dist` and can be reloaded with `.reload config`, except the
starter gear candidate cache, which is keyed by the settings it depends on. Rules that change behavior follow `Bot.Rules.ArmPct`;
logging-only parts do not.

## Rules

| Rule | What it does | Settings (default) |
|---|---|---|
| R-V3-01 | A bot that starts grinding logs why: `QUEST_GRIND` details carry `why` (`no_quest`, `level`, `prereq`, `path`, `hub`, `quarantine`, `blocked_list`, `other`) and `rejected` (counts per reason from the quest scoring pass). `STATE_LEDGER` carries `grind_entries` (count per reason). | `Bot.Rules.V3Log.Enabled` (1) |
| R-V3-02 | Quests 794, 5441, 1656, 3905, 47, 400, 2160 are in the shipped `BlockedQuests`. The nav data fixes for 794, 5441, 400, 2160 and 1656 were not done (see below). | `Bot.Rules.QuestFilter.BlockedQuests` ("794,5441,1656,3905,47,400,2160") |
| R-V3-03 | `NeverBlock` overrides the blocklist, so the dangerous but completable quests (404, 112, 789, 792, 380, 488, 92544) are never filtered. | `Bot.Rules.QuestFilter.NeverBlock` |
| R-V3-04 | Levels 5 to 6: creature entry 251245 (Prideclaw) is not a grind target and not a free-target candidate, unless quest 92515 or 92553 is unfinished in the log. Trip pathing is not changed. `NO_GRIND_TARGET` details show `camp_skips`. | `Bot.Rules.CampAvoid.*` |
| R-V3-05 | Quests 92515, 92553, 92516, 92551 are not offered below level 7. | `Bot.Rules.QuestFilter.LowLevelBlocked`, `LowLevelUntil` (7) |
| R-V3-06 | Kills by a bot's pet or owned unit count for the bot, and kill XP without a counted kill adds a credit. `COMBAT_SUMMARY` gets `kill_src` (`self`, `pet`, `xp`), so `no_kill` rows can be told from missed credit. | `Bot.Rules.KillCredit.Enabled` (1) |

## Extra log rows

- `STATE_LEDGER`: `moving_s` (seconds spent moving per activity, next to the existing totals) and `grind_entries`.
- `QUEST_GRIND` (decision): `why`, `rejected`.
- `COMBAT_SUMMARY`: `kill_src`.
- `quest_group_skip` (decision, one row per bot/quest pair whenever the reason changes): `details.why` is one of `max_parties`, the
  not-free reasons (`offline`, `dead`, `in_combat`, `already_grouped`, `level_below_party_min`, `busy_other_task`), `quest_not_in_log`,
  `no_candidates`, `no_partner_with_quest`, `already_seeking_other_quest`.
- `START_GEAR` and `START_GEAR_NONE` (event type `gear`): details `cause` (`LOGIN` or `LEVEL`), `empty_before`, `candidates`, `granted`,
  and `items` (slot, entry, name, required level, quality).

## Starter gear (`Bot.StartGear.*`)

Runs on the bot's first tick after login (so it also covers bots the simulation creates) and again every `LevelStep` levels. It looks at
the empty equipment slots only: head, neck, shoulders, back, chest, waist, legs, feet, wrists, hands, two rings, main hand, off hand, ranged.
A filled slot is never touched, so running it twice grants nothing the second time and a bot with gear loses nothing. Shirt, tabard and
trinkets are left alone.

Candidates come from the loaded item templates, filtered per class and level: armor or weapon, quality at most `MaxQuality`, required level
at most bot level + `LevelOffset`, no required skill, spell or reputation, no start quest, no duration, no scaling curve, bind on equip or
none (unless `AllowBindOnPickup`), class allowed, test and placeholder names dropped. Each candidate is scored with `BotGear::Score` for
the class role; every pick is checked with a `CanEquipNewItem` dry run for the target slot, so proficiencies, race, level and unique rules are the
core's own (`Player::CanUseItem(ItemTemplate)` alone does not test proficiencies and must not be used as the gate). Armor prefers the best
type the bot can wear at its level (cloth, leather, mail, plate, found by dry-running the first candidate of each type) and falls back to a
lower type for a slot with no piece of the best type. Cloaks, neck and rings are not tied to an armor type. The off hand is filled even when
the main hand keeps its starting weapon or the main hand pick fails. The main hand takes the better
of the best two-hander and the best one-hander plus off hand (shield, holdable, or a second one-hander when the class can dual wield).

Settings: `Enabled` (1), `MaxQuality` (2), `LevelOffset` (0), `MinLevel` (1), `LevelStep` (5), `AllowBindOnPickup` (0), `MaxItemLevel` (0, no cap), `Armor` (1),
`Weapons` (1). Bows, crossbows and guns are skipped while the item data has no ammunition items (the current data has none).

Items with an on-use spell (parachute cloak, rocket boots and similar gimmick gear) are never candidates, so only plain gear is granted.

Limits: a bot that already wears a level 1 starter weapon keeps it (slots are not replaced). Item data comes from the loaded item templates at runtime, so no item ids are hard coded. The data has no ammunition items, so hunters get no ranged weapon.

## Not done

- Navigation fixes for quests 400 and 2160 and the root cause for 794, 5441 and 1656 (needs a running server and path data).
- R-V3-04 does not change trip pathing; only grind target choice and the free-target check.

## Fix: starter gear granted almost nothing (2026-10-12)

Sim run: 446 of 500 logins logged `START_GEAR_NONE` and 54 (shamans) got only a shield. Causes: the usability gate was `CanUseItem(ItemTemplate)`, which skips armor and weapon proficiency, so the 'best armor type' was always the top type present in the data (never wearable at level 1) and every armor candidate of the wrong type was then rejected at equip time; cloaks were tagged with the cloth type and so locked out the same way; the best weapon and shield were the top scored ones regardless of proficiency, and the off hand was only tried after a successful main hand grant. Now all gates are slot-aware `CanEquipNewItem` dry runs (see above).
