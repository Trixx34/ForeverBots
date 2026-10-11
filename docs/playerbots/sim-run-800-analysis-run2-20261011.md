# Sim run 2, 800 bots, 4 h: analysis (2026-10-11)

Source: the `bot_event` table of a simulation log database (read-only), the whole run. Window 2026-10-11 17:46 to 21:41 (3.9 h). 800 bots (9 classes, about 88 each; 10 races), ramp 200 to 800 in steps of 20 every 300 s, all 800 online from 20:21. 1,340,199 rows, 2,353 bot-hours (from each bot's login to 21:41). Build under test: the old worldserver (no loop guards, no R-V2-04..08, no group-after-fails) with the per-state time ledger (`ai_stats` / `STATE_LEDGER`), `TRIP_END`, `COMBAT_SUMMARY` fields, group-after-deaths (threshold 3 deaths) and the auction house enabled. Death rows now carry the active task and quest id in `details`. Run 1 is `sim-run-800-analysis-20261011.md` (22 h).

Hour 21 contains only 41 minutes. Zone names are ids as logged (1 Dun Morogh, 12 Elwynn, 14 Durotar, 85 Tirisfal, 141 Teldrassil, 215 Mulgore, 10 Duskwood; 16593 is the start zone of races 95 and 96).

## Summary (top findings)

1. Levelling is limited by grinding, not by dying. 58% of all ledger time is the `grind` activity (no quest being worked), and bot-hours with a high grind share earn about one fifth of the XP of quest-kill hours (0.7k versus 3.8k XP per bot-hour). Dying is 0.9% of time (17 of 1,880 ledger hours, about 10 s per death).
2. The grind share depends on the start area: races starting in Dun Morogh and Durotar spend 74 to 76% of their time grinding, races starting elsewhere 41 to 53%. Those two zones also have the most navigation-blocked quests (`NO_PATH`, `UNREACHABLE`, `PATH_PARTIAL_FAR`). Grind also grows with time online (37% in the first hour, 66 to 68% from the third hour).
3. Deaths: 5,886 (2.50 per bot-hour, run 1: 1.43 averaged over 22 h, about 2.3 at its peak). 57% happen while the task is `grind`, 25% on a quest kill task, 7% on trips. Zone 16593 is 18.7% of deaths (run 1: 26%) and its death rate per hour is close to the other zones, so it is no longer an outlier. One mob, Prideclaw (251245), causes 36% of the deaths in that zone (398 deaths, 90 bots), mostly mob-started aggro while grinding or travelling.
4. Group-after-deaths works mechanically (seek and grouped rows, 9 parties formed) but touched few cases: 21 (bot, quest) pairs sought a group and 14 got grouped, against 163 pairs with 3 or more deaths on the same quest. The sample is below the minimum for an effect claim.
5. `no_kill` outcomes with `kills > 0`: none (0 of 1,057), but the fix is incomplete: 86% of `no_kill` summaries coincide with a kill-XP event in the fight window (control windows 13 to 15%), so `kills` is still 0 for fights that did kill something.

## 1. Levelling speed and where the time goes

Level versus join time. Mean level at the same time after login, by join cohort (join time since the first bot):

| cohort (joined) | bots | level at +1 h | level at +2 h | level at +3 h | level at end |
|---|---|---|---|---|---|
| 0 to 20 min | 280 | 3.80 | 4.64 | 5.20 | 5.56 |
| 20 to 80 min | 240 | 3.84 | 4.68 | 5.27 (n=140) | 5.26 |
| 80 to 140 min | 240 | 3.79 | 4.59 (n=140) | not enough | 4.60 |
| 140 to 200 min | 40 | 3.88 | not enough | not enough | 4.20 |

Bots follow the same curve regardless of when they joined (3.8 after 1 h, 4.6 after 2 h, 5.2 after 3 h): later joiners are not slower per hour of play, they only have fewer hours. Final distribution (800 bots, mean 5.11): L1 4, L2 4, L3 88, L4 146, L5 254, L6 192, L7 89, L8 22, L9 1. The curve is concave because XP per hour is flat while XP per level rises: bots in the first cohort (all online at least 3.7 h) spend on average 0.15 h on L1, 0.25 h on L2, 0.45 h on L3, 0.78 h on L4, about 1.0 h on L5 and L6 (right-censored from L5 up: 58 of 220 bots still on L5). XP per bot-hour averages 2.4k (5.68M XP in 2,353 bot-hours; kill 4.14M, quest 1.19M, explore 0.35M). Level-ups per hour: 1,018 (18h), 975 (19h), 816 (20h), 297 in the 41 minutes of the 21st hour.

Time ledger (1,880 `STATE_LEDGER` rows, one per bot per hour boundary, 800 bots, 1,880 bot-hours = 80% of all bot-hours; the first row of each bot comes about one hour after login). The ledger gives seconds per state (dead, combat, noncombat) and per activity (task). Shares of total ledger time:

| bucket | share | hours | what it is |
|---|---|---|---|
| grind, not in combat | 50.6% | 952 | no quest being worked; searching, walking, resting between fights |
| kill task, not in combat | 18.8% | 354 | quest kill objective: walking to and waiting for targets |
| combat (all tasks) | 15.3% | 288 | of which grind 145 h, kill task 115 h |
| quest walking (`go_giver`, `go_ender`) | 7.1% | 133 | travel to quest givers and enders |
| loot | 3.1% | 58 | |
| trips (profession 1.9%+0.3% combat, trainer 1.0%, vendor 1.0%, gather and workshop <0.1%) | 4.5% | about 84 | profession trips are the largest |
| dead | 0.9% | 17 | spirit healer / corpse run, about 10 s per death |
| idle (`none`) | 0.2% | 3.5 | |

The ledger does not split a task into moving and standing still, so "grind, not in combat" mixes searching and idle time. There is no stuck, quest_blocked or vendor/AH state in the ledger. Stuck and quest_blocked are events only (5,582 stuck and 30,195 quest_blocked rows); their time cost is inside the `grind` and quest task seconds. No auction-house events exist in the log at all (no event type or reason mentions it), so AH time cannot be measured; selling is inside the vendor trips.

Which bucket costs the most levels? Per ledger row (bot-hour, level >= 3, n = 1,845) the XP gained in that hour was regressed on the time shares (observational, not an experiment):

- XP per hour by grind share quintile (n = 376 each): 0 to 0.33 -> 3,836; 0.33 to 0.52 -> 3,291; 0.52 to 0.69 -> 3,061; 0.69 to 0.87 -> 2,077; 0.87 to 1.00 -> 735. Same pattern restricted to levels 4 and 5 (4,011 down to about 1,000). Combat share falls from 21% to 9% across the quintiles.
- Linear fit: XP/h = 4,882 - 3,834 x grind share (R2 0.42); with dead, walk and trip shares added: 4,208 - 3,142 x grind - 14,424 x dead + 3,651 x walk + 2,534 x trip (R2 0.47).
- Grind: mean share 0.59 -> about 1.8k to 2.2k XP per bot-hour lost relative to a no-grind hour, i.e. 3.4M to 4.2M XP over the ledger hours, about 60 to 75% on top of the XP actually earned. In levels at the 2.1k to 3.6k XP per level of L4 to L6 that is roughly 1.5 levels per bot. This is an upper bound: some grinding is unavoidable when no quest fits the level.
- Dead: 17 h x 12.6k to 14.4k XP per share-hour = about 0.22M XP, about 0.1 level per bot (3 to 5% of earned XP).
- Trips and quest walking have a positive coefficient (they belong to quest progress hours); they are not a cost bucket in this data.

So grind is by far the most expensive bucket; dying and corpse runs are the cheapest. This is a correlation across bot-hours (hours with quests and kills versus hours without); the direct cause of the XP gap is combat share: grind-heavy hours (>0.87) have 20 combat starts per hour against 53 in low-grind hours, and 18.5 `FLEE_LEVEL_DIFF` decisions against 7.6 (flee decisions per combat start 0.92 versus 0.14; each flight counts up to three attempts). Hypothesis, not tested: grinding bots flee from most fights.

What drives the grind share (descriptive):

| group | bot-hours | grind share | XP per bot-hour |
|---|---|---|---|
| races 3, 7 (Dun Morogh), 2, 8 (Durotar) | 132 to 255 each | 74 to 76% | not computed |
| race 6 | 116 | 63% | |
| races 4, 1 | 157, 248 | 53, 51% | |
| races 5, 95, 96 | 284, 150, 148 | 41% | |
| by zone at ledger time: Durotar 75% (390 rows), Dun Morogh 76% (380), Mulgore 63%, Teldrassil 53%, Elwynn 51%, Tirisfal 41%, 16593 41% | | | |
| by class: hunter 66%, shaman 66%, warlock 64%, priest 63%, rogue 61%, mage 58%, warrior 56%, paladin 48%, druid 48% | | | XP/h: hunter 1,852, rogue 2,242, warrior 2,392 (weapon classes lowest), paladin 3,489 |
| by time online: first hour 37 to 53%, 2nd 53 to 58%, 3rd hour 66 to 69%, 4th 67 to 68% | | | |

Quests rewarded per bot by race: human 11.8, races 95 and 96 about 9.5, night elf 6.8, undead 6.8, dwarf 5.9, gnome 5.9, troll 4.7, orc 4.6. Races with few completed quests are the ones that grind most. Quest-blocked per zone (rows): Durotar 5,295 (1,474 navigation reasons, 421 `QUEST_HUB_NONE`), Dun Morogh 4,712 (1,357 navigation, 430 hub-none, 306 `NO_GRIND_TARGET`), 16593 5,403, Elwynn 4,950, Tirisfal 4,730, Teldrassil 2,819, Mulgore 1,166. Class: hunters average level 4.46 and 4,984 XP per bot (class lowest), paladin 5.86 and 9,638 XP (highest); average level and deaths per bot by class are in section 6.

## 2. Zone 16593

- 1,102 deaths (18.7% of 5,886; run 1: 6,545 = 26%), 126 bots, bot level at death mostly L5 (416) and L6 (278). Death rate per ledger hour in that zone about 3.7 against 3.1 overall (Elwynn 5.3, Teldrassil 4.5, Tirisfal 4.1, Durotar 3.5, Dun Morogh 0.7; hours from ledger rows by zone, approximate).
- By task at death: kill task 419, grind 312, no task 102, profession trip 100, vendor trip 68, quest walking 85. Quest id known for 506 of the 1,102 deaths (46%); the other 596 have no quest (grind, trips).
- Killers: Prideclaw 251245 398 deaths / 90 bots (36%; level 5 to 6, normal rank, bot level 5 to 6, 357 mob-started, on average 2.3 attackers, bot deals 26 and takes 54 in the last 10 s; tasks: grind 100, profession trip 60, vendor trip 44, none 28, quest kills 92553 27 and 92515 25 plus 14 walking to the giver of 92515); Hippogryph Youth 251291 91 / 25; Ornery Galestrider 251707 66 / 9; Shadowgale Ursera 250927 61 / 10; Al'Aketh Brute 251145 56 / 37; Cirrusfly Queen 251404 49 / 32; Malduko Cloudcrush 256935 45 / 28; Roiling Winds 251143 42 / 35; Al'Aketh Convert 251160 40 / 33.
- Quests (deaths / bots dying / accepted / rewarded): 92544 Al'Aketh Thugs 100 / 57 / 100 / 23; 92465 66 / 47 / 131 / 129; 92463 59 / 35 / 102 / 98; 92515 The Problem With Prideclaws 46 / 28 / 16 / 0; 92553 Restocking the Larders 45 / 26 / 35 / 0; 92516 35 / 14 / 10 / 0; 92470 Foul Matriarch 29 / 23 / 126 / 15; 92551 29 / 7 / 5 / 0.
- Would a blocklist fix it? Four quests with no rewards (92515, 92553, 92516, 92551) account for 155 deaths, 14% of the zone's deaths, from 62 bots total on 66 acceptances (below 15 attempts for 92516 and 92551). Prideclaw is mostly killing bots that are not on its quest (grinding, travelling, vendor trips: 232 of 398), so a quest blocklist would remove at most about 15% of the zone's deaths. A rule on avoiding Prideclaw packs while grinding or travelling is the larger lever (see R-V3-04), but that is a hypothesis; there is no with/without comparison.
- 92544 (100 deaths, 23 of 100 acceptances rewarded) and 92470 (126 acceptances, 15 rewarded, 29 deaths, 277 quest_blocked rows) are poor but not hopeless quests; 92465 and 92463 are fine (98% and 96% rewarded).

## 3. Group-after-deaths (first live check)

- Rows `quest_group_after_deaths`: 33 (19 `seek`, 14 `grouped`), plus `PARTY_FORMED` 9 and `PARTY_DISBANDED` 10 (reasons: too_small 5, lifetime 3, leader_gone 2; party age 70 to 1,080 s, 2 to 3 members).
- Eligible cases: 163 (bot, quest) pairs with 3 or more deaths on the same quest (quest-tagged deaths only, of 1,040 pairs with at least one). Seek rows cover 21 distinct pairs (13%), 14 pairs got grouped; of those 14, 10 have no preceding seek row in the log (the log suppresses repeats, or the group was joined without a logged seek). Only 4 seek pairs have a later grouped row.
- Effect on repeat deaths. After grouping, the 14 grouped pairs had 11 further deaths on that quest (5 pairs; 9 pairs none) and 2 pairs rewarded the quest later (sample 14, below the 15 minimum). Control: pairs with 3 or more deaths with no seek or group (n = 139) had on average 1.73 deaths after their third (240 total) and 31 (22%) rewarded afterwards; seek-only pairs (n = 13) 1.46 and 8 rewarded; grouped pairs counted from their third death (n = 11) 3.27 and 2 rewarded. The grouped pairs are selected as the worst repeaters (5 to 10 deaths when grouped), so the groups cannot be compared to the control. Conclusion: not enough data to say the rule reduces repeat deaths; no sign that it makes them worse once grouped.
- Quests with seek/grouped rows: 112 (6 rows), 93951 (6), 404 (5), 3902 (4), 92544 (3), 47 (3), 488 (3), 60 (2), others one each.

## 4. `no_kill` outcome

`COMBAT_SUMMARY` is logged for 8,767 of 91,763 fights (9.5%). Outcomes: all_targets_dead 6,673 (6,507 plus 166 with `targets_dead` > 0 covered in the sum), no_kill 1,057, bot_died 574, fled 444, partial_kills 20. `no_kill` with `kills > 0`: 0 of 1,057 (the field is consistent). But a kill-XP event exists inside the fight window (start minus 3 s to end plus 3 s) for 912 of the 1,057 `no_kill` rows (86%), against 100% for all_targets_dead and 13 to 15% for the same rows shifted by +-600 s. So the outcome label is still wrong for most `no_kill` fights, because `kills` does not count the kill. By role: hunters 194 of 816 (24%), casters 724 of 4,980 (15%), melee 139 of 2,972 (5%); 59 hunter rows have no cast at all. Probable cause (untested): kills by damage over time, pets or an auto-attack kill not credited. Do not use `no_kill` for kill rates yet.

## 5. quest_blocked

30,195 rows (15,095 severity 2) from 798 bots; 8.3k to 8.6k per hour at full population (run 1: 56,885 in 22.4 h, 2.5k per hour on average). Reasons (rows / bots):

| reason | rows | bots | note |
|---|---|---|---|
| QUEST_LEVEL | 6,004 | 695 | 285 quests; one-off per bot and quest (informational) |
| QUEST_PREREQ | 5,754 | 796 | 163 quests; same |
| NO_PATH | 2,593 | 685 | 151 quests |
| UNREACHABLE | 2,214 | 670 | 138 quests |
| QUEST_HUB_NONE | 2,098 | 798 | no quest id |
| PATH_PARTIAL_FAR | 1,482 | 632 | 103 quests |
| TRAIN_NO_MONEY | 1,219 | 667 | no quest id |
| NO_GRIND_TARGET | 1,160 | 543 | no quest id |
| OBJECTIVE_UNSUPPORTED | 1,024 | 431 | 21 quests, already skipped |
| MISSING_ITEM_SOURCE | 921 | 277 | 46 quests |
| QUEST_QUARANTINED / _GLOBAL | 856 / 271 | 426 / 213 | |
| PROF_NO_MONEY, PROF_NO_TRAINER, TRAIN_UNREACHABLE | 625, 603, 523 | | |
| GO_BUSY, GO_RESPAWN_WAIT | 424, 235 | 367, 225 | contention on quest objects |

Most volume is one-off (level, prerequisite, hub) rows. The recurring ones that cost bot time are the navigation reasons and quest-object contention (3361 `GO_BUSY` / `GO_RESPAWN_WAIT` 256 of 348 hard-blocked rows, 145 acceptances, 42 rewards).

Candidates for the `BlockedQuests` list (not changed). Table of quests with at least 15 acceptances (bots accepting before 20:40, so most had time to finish) and a low reward rate; deaths are quest-tagged deaths:

| quest | zone | accepted by 20:40 | rewarded | deaths | hard-blocked rows | main reason |
|---|---|---|---|---|---|---|
| 794 | Durotar | 37 | 1 (3%) | 27 | 121 | NO_PATH / UNREACHABLE |
| 47 | Elwynn | 36 | 2 (6%) | 56 | 113 | PATH_PARTIAL_FAR |
| 5441 | Durotar | 19 | 0 | 10 | 102 | TARGET_UNREACHABLE |
| 1656 | Mulgore | 39 | 1 (3%) | 2 | 100 | NO_PATH |
| 3905 | Elwynn | 37 | 4 (11%) | 0 | 99 | GIVER_NOT_INTERACTABLE |
| 92553 | 16593 | 21 | 0 | 45 | 0 | kill target dies |
| 92470 | 16593 | 99 | 14 (14%) | 29 | 277 | UNREACHABLE |
| 60 | Elwynn | 54 | 9 (17%) | 70 | 152 | UNREACHABLE / PATH_PARTIAL_FAR |

Below 15 acceptances but zero rewards: 92515 (5 by 20:40, 46 deaths), 92516 (7, 35 deaths), 92551 (3, 29 deaths), 475 (8, 22 deaths). Not blocklist material despite high volume: 182 (150 of 160 rewarded), 218 (53 of 139 rewarded, but 140 `NO_PATH` rows, one per bot: a navigation problem), 789 (37 of 134 rewarded, 322 rows, `PATH_PARTIAL_FAR` / `NO_PATH`), 792 (53 of 108), 404 (24 of 63) and 112 (16 of 48) are dangerous but completable (184 and 161 deaths; the group-after-deaths rule applies). Quests 400 and 2160 (Dun Morogh) are blocked at the path stage (128 and 107 rows).

Other error streams: `path_fail` 4,352 (AGGRO_BLOCKED 3,035, NO_PATH 776, PATH_PARTIAL_FAR 598; Durotar 1,642, 16593 1,090, Teldrassil 554), `stuck` 5,582 (NO_PROGRESS 5,051; Dun Morogh 1,342, Durotar 1,249, Tirisfal 859, Elwynn 788, 16593 692).

## 6. Comparison with run 1

| metric | run 1 (22 h) | run 2 (3.9 h) |
|---|---|---|
| rows per hour at 800 bots | 569k (22:00 peak), 239k late | 412k (20h), 259k in 41 min of 21h |
| XP events per hour at 800 bots | 27.4k (22:00), 5.1k late | 27.2k (20h), 22.1k per hour-equivalent in 21h |
| level-ups per hour | 984 (22:00) | 816 (20h) |
| deaths per hour at 800 bots | 1.8k (23:00), 0.6 to 0.7k late | 1.9k (20h), 2.3k per hour-equivalent in 21h |
| deaths per bot-hour (whole run) | 1.43 | 2.50 |
| deaths in zone 16593 | 6,545 (26%) | 1,102 (18.7%) |
| bots with XP in the hour | 750 (22:00), 215 (17:00 next day) | 675 (19h), 716 (20h), 634 in the last 41 min |
| `APPROACH_TIMEOUT` per hour | 8 (20:00) rising to 10.4k | 103 (18h), 263, 751, 1,164 in 41 min of 21h |
| `PROF_TRIP` / `WORKSHOP_TRIP` rows per hour | up to 3k / 11k | 120 and 34 (19h), 227 and 146 (20h), 206 and 220 (41 min of 21h) |
| `path_fail` per hour | 25,535 total | 9, 344, 863, 1,768, 1,425 per hour |
| quest_blocked rows | 56,885 (2.5k per hour avg) | 30,195 (7.7k per hour avg, 8.3k to 8.6k at full population) |
| final level | median about 7 after 22 h | mean 5.11 after 3.9 h |

Reading: during the first four hours at the same population the two runs match on XP (about 27k events per hour), level-ups and deaths per hour; deaths per bot-hour rise again in hour 21. No improvement from the changes in the test build is visible, as expected (its loop guards are not in the build). The loops of run 1 start now: the approach timeout, profession trip and workshop trip streams grow by roughly a factor 3 to 5 per hour, and active XP bots drop from 716 to 634 in the last 41 minutes. Run 1's level curve over time was not recorded, so levels cannot be compared at equal time online; decision-row volumes differ strongly between runs (run 1 3.0M decision rows in 22 h with 447k `TARGET_PICKED`; run 2 118k in 3.9 h with 9k), so decision counts are not comparable between the runs (the logger seems to sample or cap them differently). Death and XP events are comparable.

Per class (run 2; ids 1 warrior, 2 paladin, 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid): deaths per bot warrior 6.8, paladin 7.1, hunter 7.3, rogue 10.9, priest 6.3, shaman 5.1, mage 7.8, warlock 7.3, druid 7.4 (n = 83 to 93 bots each); average max level 4.93, 5.86, 4.46, 4.79, 5.24, 5.29, 5.48, 5.13, 5.41; XP per bot 6.5k, 9.6k, 5.0k, 6.1k, 7.5k, 7.0k, 8.3k, 7.3k, 7.9k. Hunters and warriors/rogues, the classes hit by the weapon and ammo loops in run 1, are again at the bottom for XP.

Deaths by task (5,886): grind 3,354 (57%), kill 1,443 (25%), profession trip 313 (5.3%, only 2.4% of time), quest walking 397, none 257, vendor trip 81, trainer trip 23. Mob-started 5,076 (86%), bot-started 807. Death chains of length 3 or more: 1,225 (21%). Profession trips: 5,675 `PROF_TRIP` ends, 87% dropped (NO_PATH 3,058, PATH_PARTIAL_FAR 1,469, UNREACHABLE 397; typical 11 s to 31 s), 749 reached, 511 bots. Vendor trips 2,149 reached, 716 dropped.

## 7. What the data could not answer

- Time moving versus standing still within a task (the ledger has only state x task): whether "grind, not in combat" is walking, searching, resting or idle. Request: split the ledger into moving, resting, waiting in the same activity table.
- Why a bot is in the grind task at a given time (no quest taken: level, prerequisites, path, hub, quarantine). A reason code on the activity change (`grind` entered because of X) would allow splitting the 58% by cause.
- Auction-house activity: no events at all, so the AH setting cannot be evaluated.
- `no_kill` mislabel cause (pets, damage over time, melee auto-attacks), and whether a kill is credited when the bot did not land the last hit. Needs the kill credit source in `COMBAT_SUMMARY`.
- Group-after-deaths: a reason when a bot with 3 or more deaths does not seek (163 eligible pairs, 21 seeks), and the death counter per quest; 10 of 14 grouped pairs have no seek row. No effect size is possible below 15 comparable cases.
- `COMBAT_SUMMARY` covers 9.5% of fights, `TARGET_PICKED` is sampled; kill outcomes per class are therefore computed on a sample.
- Ledger covers 80% of bot-hours (hour-aligned rows, the first about one hour after login), and the first hour of each bot is never in it.
- Whether grinding is the cause of the XP gap or a symptom of weak bots (observational regression, R2 0.42 to 0.47).

## 8. Follow-ups

1. The proposed rules are in `docs/playerbots/rules/proposed-v3-20261011.md` (R-V3-01 to R-V3-06).
2. Whether the `BlockedQuests` list gets the candidates of section 5 (kept empty so far; this document changes nothing).
3. Logging additions of section 7 before the next run (reason on grind entry, moving/standing split, kill credit source, seek reason).
4. Whether the next run is analysed in 1 h buckets by grind share and active XP bots (the metrics that tracked the decline here).
