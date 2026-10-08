# Quest coverage near start areas (sim run R4)

Scope: quests whose giver stands within 600 yd of a bot start point, level 10 or lower, 227 quests. Log window: R4 (700 bots). "Accepted" means at least one QUEST_ACCEPTED event.

| Outcome | Quests |
|---|---|
| Accepted by at least one bot | 86 |
| Never accepted | 141 |

## Why the 141 were never accepted

| Cause (dominant block code) | Quests | Meaning |
|---|---|---|
| MISSING_ITEM_SOURCE | 67 | Bug in the bot quest classifier (see below) |
| OBJECTIVE_UNSUPPORTED | 22 | Objective type the bots do not do (game-object items, area triggers, other objective types) |
| class/race-locked, never evaluated | 16 | No bot of that class/race went near the giver (includes gnome-only quests) |
| QUEST_LEVEL | 12 | Level 10 quests, bots too low: expected |
| QUEST_PREREQ | 8 | Chain quests behind an unaccepted quest, so they inherit the gaps above |
| no events at all | 8 | Level 10 hubs 560 yd+ away that no bot reached |
| NEEDS_EVENT | 4 | Completion-event quests (Taming the Beast etc.) |
| SKILL_REQUIRED | 2 | Needs a profession |
| TIMED_UNSUPPORTED | 2 | Timed quests |

## Main finding: start-item quests are misclassified

`BotQuest.cpp` (item objective check, `MISSING_ITEM_SOURCE`) looks up the objective item in the loot-source index only. Delivery and "report to" quests supply that item at accept time through `quest_template.StartItem` (or `ItemDrop1..4`), so the bot decides there is no source and blocks the quest.

Examples: "Report to Goldshire" (54), "Coldridge Valley Mail Delivery" (233), "Dolanaar Delivery" (2159), "Supplies to Tannok" (2160), "Senir's Observations" (282, 420), "Bring Back the Mug" (3365), the level-1 class quests (Simple Letter/Rune/Sigil..., 3091-3120) and the new-zone Harvest quest 92485.

Scale: 2,609 quests in the whole DB have an item objective satisfied by StartItem/ItemDrop. In the run, 67 start-area quests were blocked this way, and nearly every bot hit at least one.

Proposed fix: treat an item objective as sourced when the quest's `StartItem` or `ItemDrop1..4` equals the objective item. The quest then completes as soon as it is accepted (or by turning in to the ender, for delivery quests).

## Reachability (accepted quests that later failed)

Over the accepted near-start quests the bots logged NO_PATH 1,713, UNREACHABLE 1,297, QUEST_QUARANTINED 1,277, PATH_PARTIAL_FAR 558, TARGET_UNREACHABLE 407. Worst offenders by block count:

| Quest | Giver | Zone start | Note |
|---|---|---|---|
| 92470 Foul Matriarch | 251366 | map 2991 | NO_PATH 89, UNREACHABLE 103 |
| 789 Sting of the Scorpid | 3143 | Durotar | PATH_PARTIAL_FAR 128 |
| 818 A Solvent Spirit | 3304 | Durotar | NO_PATH 122 |
| 794 / 792 | 3145 | Durotar | NO_PATH 108 / 75 |
| 92544, 92473 | 252095, 257551 | map 2991 | NO_PATH 73 / 46 |
| 804, 790 | 3287 | Durotar | NO_PATH 96 / 41 |
| 218, 182 | 786 | Northshire/Elwynn side | NO_PATH 88 / 53 |

The givers were reached for these (they were accepted); the failures are on the objective side: mob or item spawn off the navmesh or only partially connected. That needs a per-target navmesh check, not a quest-giver check.

## Not yet done

- Every giver NPC itself was not path-checked offline against the navmesh. That is the remaining test for "NPC unreachable".
- Per-race/class availability: 16 locked quests were never evaluated because no matching bot passed by. A forced test (spawn one bot of each race/class at its start point) would settle it, and also cover the idle race-7 gnomes.
