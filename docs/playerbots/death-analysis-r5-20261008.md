# Death, stuck and path-failure analysis: sim runs R4 and R5

Question: why did deaths (459 to 960), stuck events (657 to 888) and path failures (250 to 399) rise between R4 and R5, and is it the new quest work from `BotQuest.cpp` (item sources and the game-object loot task), more active bots, or something else?

Status: findings and proposals only. Nothing here is applied; every rule in the last section is a proposal for owner approval. All queries were read-only on the sim log database (`bot_event`, `bot`) and the sim world database (names and quest levels).

## Sample and method

- Source: sim log `bot_event`, 700 bots per run, same ramp (300 bots, +5 every 30 s up to 700). Bot guids 1..700 are reused by each run, so every join is bounded by run and time.
- Window: the id windows quoted in the R5 note (R4 2025158..2385303, R5 6461507..6903033) are not equal in time. R4's window ends 53 minutes after its first event, R5's after 60. All numbers below therefore use equal 60 minute windows from each run's first event (R4 22:16:50 to 23:16:50, R5 09:14:21 to 10:14:21). Counts differ slightly from the note: R4 has 550 deaths, 760 stuck, 273 path failures in 60 minutes (note: 459, 657, 250 in 53); R5 has 942 deaths, 884 stuck, 391 path failures (note: 960, 888, 399, which run a few minutes longer). The 53 minute like-for-like comparison is also given, because it only strengthens the finding.
- "New quest" means a quest accepted in R5 and never accepted anywhere in the earlier log (139 distinct quests accepted before R5; 836 accepts of new quests in R5). "New quest active" means the bot had accepted such a quest and not yet turned it in at the time of the event (upper bound: quests never turned in count until the window ends).
- Task at death: the last `QUEST_WALK_START` decision of the bot in the 5 minutes before the death (its `task` is `kill`, `loot`, `go_giver`, `go_ender`), and the set of quests in the bot's log at that moment.
- Per-class and per-quest figures are small (tens of events per cell); treat anything under about 15 events as an indication only.

## Result in one paragraph

The rise is real, is not explained by more bots being active, and is not the loot task in isolation. It is concentrated in bots that carry one of the newly accepted quests, and within those, in level 3 characters walking long legs through areas with mobs two to four levels above them (undead, human and night elf starts). Bots without a new quest active had almost the same deaths as in R4 (578 versus 551) and fewer stuck events and path failures. The loot task itself is directly involved in only a minority of cases (about 7 percent of deaths, 4 percent of stuck events).

## 1. Exposure: more active bots? (hypothesis c)

| Measure (60 min windows) | R4 | R5 | Change |
|---|---|---|---|
| Bot-minutes since login | 32,714 | 33,541 | +2.5 % |
| Events | 412,711 | 436,415 | +5.7 % |
| Quest walk starts | 50,740 | 54,811 | +8 % |
| Combat starts (state changes) | 20,139 | 19,394 | -4 % |
| Quest accepts | 2,329 | 3,292 | +41 % |
| Quest rewards | 1,610 | 2,322 | +44 % |
| Deaths | 551 | 942 | +71 % |
| Deaths per 1000 bot-minutes | 16.8 | 28.1 | +67 % |
| Deaths per 100 combat starts | 2.7 | 4.9 | +82 % |
| Stuck per 1000 bot-minutes | 23.2 | 26.4 | +14 % |
| Path failures per 1000 bot-minutes | 8.3 | 11.7 | +41 % |
| Deaths per quest accept | 0.237 | 0.286 | +21 % |

Same comparison inside the first 53 minutes only: deaths 457 versus 790 (16.4 versus 27.6 per 1000 bot-minutes), stuck 657 versus 756, path failures 250 versus 325.

Conclusion: (c) is rejected. Bot-minutes grew 2.5 percent and combat starts fell 4 percent, while deaths grew 71 percent. Deaths per combat start nearly doubled, so bots die more per fight; they do not fight more. Normalising by quest accepts removes most of the increase in activity but still leaves +21 percent; the remainder is the specific quests below.

## 2. Where the deaths rose

Bots only reached level 5 in both runs; no death at level 6 or higher.

### By bot level (deaths per 1000 bot-minutes spent at that level)

| Level | Bot-min R4 / R5 | Deaths R4 / R5 | Rate R4 / R5 | Combat start rows R4 / R5 (two row types, so about twice the number of fights) |
|---|---|---|---|---|
| 1 | 13,981 / 14,339 | 279 / 291 | 20.0 / 20.3 | 10,191 / 10,170 |
| 2 | 9,078 / 8,739 | 90 / 98 | 9.9 / 11.2 | 12,686 / 12,493 |
| 3 | 6,860 / 7,668 | 107 / 454 | 15.6 / 59.2 | 11,111 / 11,224 |
| 4 | 2,508 / 2,469 | 59 / 83 | 23.5 / 33.6 | 6,085 / 4,659 |
| 5 | 292 / 329 | 16 / 16 | 54.9 / 48.6 | 612 / 462 |

Levels 1 and 2 are unchanged. Of the +391 deaths, +347 are level 3 (rate x3.8 at the same number of level 3 combat rows). This single level band is the story.

### By start zone and level (deaths per 1000 bot-minutes, bots grouped by their login zone)

| Start zone (id) | Level 3 deaths R4 / R5 | Level 3 rate R4 / R5 |
|---|---|---|
| Tirisfal (85) | 2 / 151 | 2.3 / 122.7 |
| Elwynn (12) | 36 / 114 | 25.1 / 98.9 |
| Teldrassil (141) | 13 / 57 | 14.6 / 60.8 |
| Durotar (14) | 16 / 57 | 16.2 / 47.8 |
| Dun Morogh (1) | 14 / 30 | 16.9 / 38.0 |
| Echo Isles area (16593) | 24 / 42 | 26.5 / 32.5 |
| Exile's Reach style start (215) | 2 / 3 | 2.1 / 2.8 |

Total deaths by death zone: Durotar 271 to 379 (note, 53 min R4), Tirisfal 29 to 172, Elwynn 42 to 161, Teldrassil 27 to 95, Dun Morogh 18 to 50, zone 16593 71 to 100.

Separate, unchanged baseline: Durotar level 1 bots die at about 70 per 1000 bot-minutes in both runs (269 and 276 deaths), mostly to the Clattering Scorpid (173 and 175 deaths across levels). This is the largest single killer in both runs but did not change, so it is not part of the increase.

### Top killers (R4 / R5 deaths, 60 min windows)

| Killer (entry), zone | Mob level | R4 | R5 | Note |
|---|---|---|---|---|
| Clattering Scorpid (3125), Durotar | 5-6 | 150 | 179 | baseline, mostly level 1 bots |
| Ravaged Corpse (1526), Tirisfal | 6-7 | 7 | 82 | new, level 3 bots, 80 of 82 with a new quest active |
| Rotting Dead (1525), Tirisfal | 5-6 | 10 | 60 | new, same pattern (58 of 59) |
| Kolkar Drudge (3119), Durotar | 5-6 | 20 | 41 | |
| Defias Cutpurse (94), Elwynn | 5-6 | 8 | 51 | 20 with a new quest active |
| Makrura Clacker (3103), Durotar | 6-7 | 26 | 37 | |
| Defias Bandit (116), Elwynn | 8-9 | 2 | 36 | mostly without a new quest active |
| Defias Thug (38), Elwynn | 4 | 18 | 33 | |
| Armored Scorpid (3126), Durotar | 7-8 | 4 | 30 | 14 with a new quest active |
| Nightsaber (2042), Teldrassil | 5-6 | 1 | 27 | 22 with a new quest active |
| Frostmane Troll Whelp (706), Dun Morogh | 4 | 8 | 28 | |
| Webwood Lurker (1998), Teldrassil | 5-6 | 0 | 22 | 13 with a new quest active |

Mob level difference at death (killer level minus bot level): R5 level 3 deaths are mostly +2 to +4. Deaths started by the mob (not by the bot's pull) rose from 447 to 839, while bot-started deaths stayed at 102; none of the increase comes from the bot choosing to pull.

## 3. Attribution to the new quest work (hypotheses a and b)

### Active quest at death

| Group | R4 | R5 |
|---|---|---|
| Deaths with no new quest active | 551 | 578 (+5 %) |
| Deaths with a new quest active | 0 | 364 |

Bot-minutes with at least one new quest active in R5: about 5,865 (17 percent of all bot-minutes, upper bound). Rates per 1000 bot-minutes in R5: 62 with a new quest active, 21 without (R4: 17). So the increase of 391 deaths is 364 deaths in bots carrying new quests plus 27 elsewhere.

Same split for the other symptoms (events in R4 / R5 without a new quest active / R5 with one):

| Event | R4 | R5 without | R5 with new quest |
|---|---|---|---|
| Stuck | 760 | 585 | 299 |
| Path failure | 273 | 251 | 140 |

Without new quests R5 would have had fewer stuck events and path failures than R4; the whole rise is in bots carrying new quests. This is an association: the bots with new quests also spend less time on old quests, which is why the "without" rows fall.

### Which new quests (R5 deaths while the quest was active, active-minutes, deaths per 1000 active minutes; overlapping, a bot can carry several)

| Quest | Title | Quest level | Deaths | Active min | Per 1000 min |
|---|---|---|---|---|---|
| 8 | A Rogue's Deal | 5 | 154 | 562 | 274 |
| 3902 | Scavenging Deathknell | 3 | 76 | 946 | 80 |
| 3521 | Iverron's Antidote | 4 | 63 | 866 | 73 |
| 1598 | The Stolen Tome | 4 | 41 | 548 | 75 |
| 4402 | Galgar's Cactus Apple Surprise | 3 | 26 | 497 | 52 |
| 93552 | Harvesting Windstones | 4 | 23 | 481 | 48 |
| 5441 | Lazy Peons | 4 | 20 | 432 | 46 |
| 3361 | A Refugee's Quandary | 3 | 18 | 797 | 23 |
| 3904 | Milly's Harvest | 4 | 15 | 296 | 51 |
| 2160 | Supplies to Tannok | 5 | 14 | 519 | 27 |

Baseline without a new quest is about 21 per 1000 bot-minutes. Quest 8 stands out by a factor of about 13; the Tirisfal loot quest 3902 and several others run at three to four times the baseline. Quest 3361 and 2160 are near baseline.

Deaths cluster in repeat chains: 190 of the 364 deaths with a new quest active are second or later deaths in a death-loop chain (against 248 of 551 in R4 overall for chains of any kind), so a bot that dies on the route often returns by the same route and dies again.

### Direct involvement of the walking leg (last quest walk within 5 minutes before the death)

Of the 942 R5 deaths, 147 had a new-quest walk as the last walk: 67 `loot`, 66 `go_ender` (turn-in trips), 9 `kill`, 4 `go_giver`. A further 217 had a new quest active without a recent new-quest walk (flee and corpse-run phases after the first death). 398 had no quest walk within 5 minutes. Details:

- Quest 8 `go_ender` (36 deaths directly): the turn-in leg is long (median 194 yd, up to 571 yd) from the start hub (around x 1900, y 1550) to a destination near x 2213, y 633, through the corridor where the two undead mobs patrol. Killers: Rotting Dead 17, Ravaged Corpse 11, Decrepit Darkhound 5, Cursed Darkhound 3. 60 of the 66 `go_ender` deaths are level 3 bots, mob level difference +2 to +4. Priests (20) and warlocks (16) are the largest classes in these deaths.
- Loot task: 67 deaths in loot walks (7 percent of all R5 deaths). 24 of them belong to quest 3902: the bot picks the nearest spawn of the chest object, and when the bot is already away from the hub the nearest spawn is 200 to 500 yd away in the same Tirisfal corridor; 24 bot deaths on this task are at x 2300 to 2450, y 500 to 650 against targets at x 2190 to 2300, y 700 to 900. 267 of the 1553 loot walks (17 percent) are longer than 150 yd. Only 11 of the 67 loot-walk deaths happened within 60 yd of the loot target, so the target itself being near hostile mobs is the smaller part; the walk to it is the larger part. Loot-walk deaths: killers Ravaged Corpse 14, Defias Thug 10, Rotting Dead 9, Frostmane Troll Whelp 5, Nightsaber 5; mostly level 3 (38) and 4 (17).
- `kill` task deaths for new quests: 9 only. The new kill quests do not drive the rise.

### Verdict per hypothesis

- (a) New quests sending bots into dangerous areas: supported, and the main cause. 364 of the 391 extra deaths, 119 of 124 extra stuck events (new quest in the event's own quest id: stuck 119 of 884) and all of the extra path failures are in bots carrying a new quest. The mechanism is long walking legs (turn-in trips and loot target walks) for level 3 bots through areas with mobs of level +2 to +4, not combat on the quest objective.
- (b) The loot task itself: contributes but is a minority. About 67 deaths in loot walks (7 percent of all R5 deaths, 18 percent of the new-quest direct walk deaths), 36 stuck events (`NO_PROGRESS` on quests 93552, 3521, 3902 and others) and 24 path failures (`AGGRO_BLOCKED` on quest 3361 12 times). Contested single spawns do not appear as a cause of deaths (11 deaths near the target). The loot task's distance rule (nearest spawn from the current position) is the part that matters.
- (c) More bots active: rejected, see section 1.
- (d) Something else: no evidence of a change outside the quest code. Deaths without a new quest are +5 percent in count and the killer mix for them is stable (Durotar scorpids, Elwynn Defias, Teldrassil spiders). A world data update was committed between the two runs (spawn density cap, sniff import); I could not tell from the log whether the sim database received it, but the stable non-new-quest deaths argue against any visible effect. Not fully excluded.

## 4. Stuck events and path failures

Stuck events (60 min): R4 760, R5 884 (+124). Reason `NO_PROGRESS` 720 to 784, `UNREACHABLE_TARGET` 40 to 100 (+60, mostly without a quest id: 21 to 71, a spawn or target selection problem outside quests; not examined further). By quest class: old quests 420 to 354, no quest 340 to 411, new quests 0 to 119. By zone (R4 to R5): Durotar 263 to 299, Tirisfal 168 to 178, Teldrassil 159 to 177 (40 new), Elwynn 96 to 118, Dun Morogh 32 to 56, zone 16593 18 to 43. New-quest stuck events by quest and task: 3120 `go_ender` 16, 2161 `go_giver` 16, 93552 `loot` 12, 3521 `loot` 12, 3905 `go_ender` 11, 5441 `kill` 10, 3117 `go_ender` 6, 3902 `loot` 6. The `go_giver` and `go_ender` ones are navigation (turn-in or start trips that make no progress), the `loot` ones are the loot task (36 in total). Stuck per quest walk start did not change materially (R4 15 per 1000 walks, R5 16).

Path failures (60 min): R4 273, R5 391 (+118). Reasons `AGGRO_BLOCKED` 197 to 268, `NO_PATH` 42 to 79, `PATH_PARTIAL_FAR` 34 to 44. New-quest path failures 55: quest 3361 `loot` `AGGRO_BLOCKED` 12, quest 8 `go_ender` `NO_PATH` 7 and `AGGRO_BLOCKED` 6, quest 2161 `go_ender` `NO_PATH` 4. By bot level the rise is level 3 (43 to 235), the same band as the deaths; level 1 fell (171 to 100). By zone the rise is Elwynn (20 to 67), Teldrassil (1 to 26), Dun Morogh (3 to 16). `AGGRO_BLOCKED` is the bot refusing a route because of hostile mobs near it; it rises with the same level 3 walking legs.

The R5 note mentions `GO_BUSY` 153 and `QUEST_LOOT_SPAWN_EMPTY` 957 events; those are decision events, not stuck, and are not counted here.

## 5. What the data could not answer

- Whether the Tirisfal corridor mobs (Rotting Dead and Ravaged Corpse) were as dense in R4: the log has deaths, not mob positions; the only R4 data points are 17 deaths by these two mobs.
- A true counterfactual: a run with the new quests but a leg-length or level gate is needed to prove the mechanism (R6 A/B).
- Per quest-leg exposure (how many legs per quest cross the corridor): only the number of walk starts is logged, not the route.
- Whether the sim world data matches R4's. Please confirm which world SQL the sim used for each run.
- Class effects: priests and warlocks make up 206 of the 364 new-quest-active deaths, but the bot class mix is balanced (75 to 83 per class), so this is a hypothesis (low armor and health on long legs), not tested.

## 6. Proposed rules (for owner approval; nothing applied)

Each rule can be switched off through a config key and is evaluated in the next sim run (R6) against this document.

1. R6-1 Leg-length and level gate for quest legs. Condition: a `go_ender`, `go_giver` or `loot` leg longer than 150 yd, bot level below quest level, and the bot has died on any quest leg in the last 15 minutes or the area has a hostile mob at bot level + 2 or more near the route. Action: do not start the leg; take another quest or grind at the hub until level (quest level minus 1) is reached. Evidence: 133 of 147 new-quest direct walk deaths are on `loot` or `go_ender` legs (67 and 66); 60 of 66 `go_ender` deaths are level 3 bots; quest 8 (quest level 5) 274 deaths per 1000 active minutes, baseline 21. Confidence: medium. Rollback: config key to disable; no data change.
2. R6-2 Loot spawn choice by cost. Condition: loot task selects a spawn. Action: choose the cheapest reachable spawn by path length, not the nearest by straight line; skip spawns above 150 yd unless the bot is level quest-level or higher and has full health; add a penalty for spawns whose area had two or more bot deaths in the last hour. Evidence: 267 of 1553 loot walks above 150 yd, 24 deaths on quest 3902 loot walks with targets 200 to 500 yd away. Confidence: medium. Rollback: config key.
3. R6-3 Quest quarantine by death rate. Condition: a quest has at least 3 deaths per 10 accepts within 30 minutes (at least 10 accepts). Action: quarantine for bots below quest level plus 2 for 30 minutes, using the existing quest quarantine path (reason code to be added by the owner of the quest code). Evidence: quest 8 154 deaths in 71 accepts; quests 3902, 3521, 1598 at 73 to 80 per 1000 minutes. Confidence: medium; this is a safety net, it does not fix the cause. Rollback: config key.
4. R6-4 Death-loop breaker on repeat trips. Condition: second death within 10 minutes on the same quest or in the same 150 yd area. Action: abandon the current leg and mark the quest for the bot as deferred for 15 minutes; after reviving do not repeat the same route. Evidence: 190 of 364 new-quest-active deaths are second or later deaths in a chain. Confidence: medium. Rollback: config key.
5. R6-5 Loot task no-progress back-off. Condition: `NO_PROGRESS` on a loot leg. Action: after one `NO_PROGRESS`, blacklist the spawn per bot for 10 minutes (like `GO_BUSY`) and pick the next. Evidence: loot stuck events 36 (93552: 12, 3521: 12, 3902: 6). Confidence: low to medium (sample is small). Rollback: config key.
6. R6-6 Navigation review for the stuck quests (not a rule: a work item). Quests 3120, 2161, 3905, 3117 (turn-in or start trips) and 8 turn-in (`NO_PATH` 7) have `NO_PROGRESS` or `NO_PATH` clusters: check the destination NPC position against the navmesh. Evidence: 16, 16, 11, 6 stuck events and 7 `NO_PATH`. Confidence: medium.

Not part of this change but visible: the Durotar level 1 scorpid deaths (about 175 deaths per hour at about 70 per 1000 bot-minutes) are the biggest single killer in both runs; a separate rule (avoid pulling level +4 and higher at level 1) would cut more deaths than any of the above, but it is independent of R5.

## 7. Decisions needed from the owner

1. Which of R6-1 to R6-5 to implement for the next run; the suggested order is R6-1 and R6-2 first (they target 133 of the 147 direct-walk deaths), R6-4 next.
2. Whether to run R6 as an A/B on the same ramp with the gates on versus off, to turn this association into a measured effect (needs at least two runs per arm to see run-to-run variance; I have only one run per version).
3. Confirm that the sim world data was identical for R4 and R5.
4. Whether the quest 8 turn-in trip should be kept at level 3 (quest level 5) or the quest delayed until the bot is level 4.
