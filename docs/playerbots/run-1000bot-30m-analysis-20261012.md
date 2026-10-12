# Sim run analysis, 1000 bots, 2026-10-12 06:55-08:06 (log window ~71 min)

Source: sim log DB (bot_event, 1000 bots, 700 death, 1661 stuck, 452 path_fail, 15748 quest_blocked, 1006 gear rows). Read-only. Levels are the max level seen per bot (bots are L1-6 only; avg 3.04).

## Deaths (700 total, 0.71 per bot; 579 started by a mob, 122 by the bot)
| class id | bots | deaths | per bot | avg level |
|---|---|---|---|---|
| 5 | 113 | 160 | 1.42 | 3.5 |
| 8 | 108 | 135 | 1.25 | 3.4 |
| 9 | 110 | 126 | 1.15 | 3.4 |
| 7 | 107 | 89 | 0.83 | 3.5 |
| 3 | 112 | 68 | 0.61 | 3.2 |
| 11 | 115 | 38 | 0.33 | 2.7 |
| 1 | 110 | 34 | 0.31 | 2.8 |
| 2 | 113 | 30 | 0.27 | 2.3 |
| 4 | 112 | 22 | 0.20 | 2.5 |

Cloth casters (5, 8, 9) die 4-7x as often as rogues/paladins/warriors. Deaths by bot level: L1 57, L2 167, L3 391, L4 73, L5 14.
Zones: 85 (210 deaths, 42 bots), 14 (178, 62), 16593 (95, 44), 1 (81, 70), 12 (78, 28), 141 (59, 29).
Top killers (count, killer lvl vs bot lvl): Clattering Scorpid 3125 (77, +2.6), Ravaged Corpse 1526 (67, +3.2), Rotting Dead 1525 (57, +2.5), Frostmane Troll Whelp 706 (41, +2.4), Razormane Quilboar 3111 (27, +4.1), Cirrusfly Queen 251404 (26, +0.8), Scarlet Initiate 1507 (25), Decrepit Darkhound 1547 (23, +3.3), Defias Cutpurse 94 (23), Vile Familiar 3101 (20).
Pattern: killers are 2-4 levels above the bot and fights are multi-mob (avg 2 attackers, ~7 hostiles within 30 yd); sampled deaths show flee decisions executed but death anyway.

## Stuck (1661) and path_fail (452)
| reason | count |
|---|---|
| stuck NO_PROGRESS | 1555 |
| stuck UNREACHABLE_TARGET | 110 |
| path_fail AGGRO_BLOCKED | 411 |
| path_fail NO_PATH | 43 |

Hot spots (50-yd cells): zone 85 around (1650,1650) 134 events by 134 distinct bots (one event per bot, looks like a common start/exit point) and (1950,1800) 71; zone 1 (-6550,400..450) 109 events by 45 bots; zone 14 (-550,-4250) 49; zone 141 (10500,850) 47.

## quest_blocked (15748)
| reason | events | bots |
|---|---|---|
| QUEST_PREREQ | 4861 | 886 (130 quests) |
| QUEST_LEVEL | 4734 | 769 (169 quests) |
| QUEST_HUB_NONE | 1153 | 690 |
| NO_PATH | 998 | 501 |
| UNREACHABLE | 696 | 377 |
| PATH_PARTIAL_FAR | 612 | 373 |
| QUEST_FILTERED | 540 | 220 |
| TARGET_UNREACHABLE | 345 | 313 |

Repeated per-bot quest ids: 170, 4402, 805, 789 (QUEST_PREREQ, ~206 bots each), 6394 (QUEST_LEVEL, 206 bots). Many NO_PATH/PATH_PARTIAL_FAR rows have no quest id (grind fallback).

## Level progress per class (max level seen)
Avg: 5 3.5, 7 3.5, 8 3.4, 9 3.4, 3 3.2, 1 2.8, 11 2.7, 4 2.5, 2 2.3. Distribution: L1 29, L2 263, L3 402, L4 262, L5 43, L6 1. Classes that die most also level fastest (they probably fight more), so deaths are not slowing XP at this horizon.

## Starter gear
All bots got START_GEAR (1001 rows; 5 START_GEAR_NONE rows). Granted slots are 6-9 and nearly constant per class (class 5: 6-7, 8: 6-7, 9: always 6; others 7-9), so gear count is almost fully confounded with class. Only classes 5 and 8 have both groups:
| class | granted | bots | deaths/bot | avg lvl |
|---|---|---|---|---|
| 5 | 7 | 39 | 1.23 | 3.03 |
| 5 | 6 | 74 | 1.53 | 3.84 |
| 8 | 7 | 13 | 1.00 | 4.46 |
| 8 | 6 | 95 | 1.29 | 3.31 |
Overall 8+ slots vs 6-7: deaths/bot 0.69 vs 0.71, level 3.43 vs 2.96 (class mix only). Deaths show avg item level 8.4, 12 slots equipped. Conclusion: no usable evidence either way (groups far below 15 comparable bots per cell in one case, directions inconsistent). Not a finding.

## Proposed rules (draft, not approved)
1. Casters (classes 5, 8, 9) at L1-4: do not engage targets more than 1 level above, or packs of 2+ hostile within pull range; evidence 421 of 700 deaths, killers +2.5 to +4 levels. Confidence medium. Rollback: config toggle off.
2. Raise flee/rest HP threshold and disengage earlier when attackers >= 2 (flee executed repeatedly but bot still died). Confidence low-medium.
3. Blocklist/deprioritize quests 170, 4402, 805, 789, 6394 until prereq/level met (about 206 bots each re-logged); throttle repeated quest_blocked per bot+quest. Confidence high on noise reduction.
4. Investigate zone 85 cell (1650,1650) and zone 1 (-6550,400): 134 and 109 stuck events; needs a nav review, not a rule yet.
5. Avoid mob entries 3125, 1526, 1525, 706 for bots under killer level -2 (top 4 killers = 242 deaths).

## Not answerable / decisions
- No A/B or random gear assignment, so gear effect is untestable; ask for a run with a gear on/off split per class.
- Window is ~71 min and bots are L1-6; late-level behavior unknown.
- Zone 16593 is not identified here. Decide: approve rules 1-3 for the next run?
