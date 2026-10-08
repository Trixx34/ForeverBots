# Sim run analysis, run 3 (2026-10-07)

Status: analysis and proposals only. No rule is approved and nothing was applied. The proposed rules are also written as versioned entries in `docs/playerbots/rules/proposed-v1-20261007.md`.

## 1. Question and sample

Question: what did the bot steps A-C (hunter at range, wand/melee fallback, Judgement backoff, mana awareness, aggro avoidance) change in the 250-bot, level 1-10 sim run, and what should be done next?

- Source: sim bot log (`bot_event`, `bot`), SELECT only.
- R3 (current): 250 bots (guids 1-250), 13:04:48 to 21:18 (about 8.2 h at the time of the last query). Run boundaries come from event ids, since the run table does not track bot runs and guids are reused.
- R2 (baseline): guids 383-632, 07:59:42 to 11:31:59 (3.55 h).
- Like-for-like window: R3 events before 16:37:48 (first 3.55 h) against all of R2.
- R1 and R5-fix results are referenced from `analysis-2026-10-07.md` and `analysis-2026-10-07-run2.md`, not recomputed here.
- Minimum for a conclusion: 15 comparable attempts (fights). Below that: "not enough data".
- Per-mob rates are deaths by killer divided by fights that targeted that mob. The killer can differ from the targeted mob, so these are approximations.
- Bots in a death loop produce non-independent fights; distinct-bot counts are given for that reason.

## 2. Current run stats (R3)

Totals up to the 21:00 cut-off used in earlier queries: 5,005 deaths (rows `death`). Alive versus dead is not tracked as a state in the log: every dead bot respawns, so it is not computed.

- Race 7: 24 of 250 bots are race 7 (gnome: priest 6, warlock 8, mage 5, warrior 2, rogue 3). All 24 stayed at level 1, with 0 fights, 0 deaths and 0 turn-ins. Their stuck events: NO_GRIND_TARGET 72, NO_PATH 48, QUEST_QUARANTINED 41, QUEST_HUB_NONE 24, QUEST_QUARANTINED_GLOBAL 11. The 25 level-1 bots are these 24 plus 1 other; the 25 zero-turn-in bots are the same 24 plus 1. So the effective cohort is 226 bots. Cause not investigated.
- Highest level per bot (all 250): L1 25, L2 5, L4 17, L5 15, L6 47, L7 60, L8 27, L9 38, L10 14, L11 2. Among the 226 non-gnome bots about 54 reached L9 or more.
- Output per hour (hour of day: turn-ins, level-ups, deaths): 13h 811/526/456; 14h 321/247/500; 15h 183/172/486; 16h 107/116/769; 17h 104/86/810; 18h 49/95/695; 19h 35/54/663; 20h 30/45/502. Turn-ins and level-ups decay quickly while deaths peak at 16-18h. Time-to-level was not computed per level; the level-ups per hour above are the available proxy.
- Deaths by zone (deaths, distinct bots): Elwynn 12: 1,944 (30); Skyborne start 16593: 1,259 (38); Tirisfal 85: 797 (32); Durotar 14: 565 (44); Teldrassil 141: 238 (17); Mulgore 215: 120 (14); Dun Morogh 1: 101 (29).
- Concentration: 37 bots (about 15%) have at least 40 deaths each and account for 3,209 of the 5,005 deaths (64%). The top bot has 280 deaths.
- Stuck and blocked events by reason (events, distinct bots): NO_PROGRESS 2,350 (223); NO_PATH 1,508 (248); QUEST_PREREQ 1,453 (226); AGGRO_BLOCKED 1,276 (86); UNREACHABLE 1,165 (203); MISSING_ITEM_SOURCE 1,085 (222); QUEST_LEVEL 1,012 (178); QUEST_QUARANTINED 1,008 (246); PATH_PARTIAL_FAR 876 (191); OBJECTIVE_UNSUPPORTED 835 (220); QUEST_HUB_NONE 679 (250); NO_GRIND_TARGET 467 (191).
- Most quarantined quest ids globally: 404, 92515, 92553, 179, 183, 93319, 367, 47 (names are not logged).
- Deaths by mob: see section 6.

## 3. Comparison with earlier runs (like-for-like, 3.55 h)

| Item | R2 (3.55 h) | R3 first 3.55 h | Verdict |
|---|---|---|---|
| Quest turn-ins | 1,525 | 1,396 | flat to slightly worse |
| Distinct quests rewarded | 76 | 76 | same |
| Level-ups | 1,090 | 1,020 | flat to slightly worse |
| Kill XP events | 38,323 | 35,393 | flat to slightly worse |
| Deaths | 1,810 | 1,914 | slightly worse (cohort includes 24 idle gnomes) |
| NO_POWER failures per hour | baseline | about 55% lower | improved (mana awareness) |
| Judgement | fails | fails less, still zero successes | not fixed |

Over the whole 8.2 h R3 reaches 1,646 turn-ins and 97 distinct quests, but that is a longer window and is not comparable. Earlier runs R1 and R5-fix: see the two earlier analysis files; their windows and cohorts differ, so no like-for-like numbers are claimed here.

Hypothesis: steps A-C did not change progress or deaths measurably. They did remove some noise (mana, Judgement spam).

New finding: death loops. 53% of deaths in R3 happen within 5 min and 150 yd of the same bot's previous death. This was not checked against a baseline (a bot respawning at the same graveyard makes proximity partly trivial) and R2 was not measured the same way; see rule R-01.

Correction to the run2 analysis: the claim "wand 1,209 failures vs 104 successes" was a misread. Shoot casts succeed more often than they fail (successes 2,501 / 1,607 / 1,576 against failures 733 / 601 / 849 for the three Shoot-using classes in R3).

## 4. Pull behavior

- 29.9k fights in R3 were mob-first (the mob initiated); 25.5k of those (85%) start while the bot has no activity (idle or grind). The total number of fights and the bot-first share in the same window were not recomputed, so 85% is not yet comparable with a baseline. Mob-first and high-gap mob-first rates were found unchanged against R2 in the earlier check (the numbers were not retained in this file).
- Aggro avoidance (AGGRO_AVOID, AGGRO_BLOCKED) acts during quest travel only. 1,276 AGGRO_BLOCKED events over 86 bots. No death reduction is demonstrated (hypothesis: coverage is too narrow).
- Hunters pull at about 15.7 yd, so range pulling works. They then fight with Raptor Strike (12,210 casts) rather than Auto Shot (2,286), and NO_AMMO is present. Hypothesis: ammo or a ranged weapon is not provisioned (not verified in character data).
- Pull distance for other classes and deliberate versus accidental pulls were not computed (see section 8).

## 5. Per-class findings (race 7 excluded)

Deaths per 100 fights (fights = COMBAT_START rows of type `combat`):

| Class | Bots | Deaths | Fights | Deaths/100 fights |
|---|---|---|---|---|
| Warrior | 26 | 437 | 7,969 | 5.5 |
| Paladin | 27 | 859 | 10,127 | 8.5 |
| Hunter | 28 | 526 | 5,853 | 9.0 |
| Rogue | 25 | 626 | 7,232 | 8.7 |
| Priest | 22 | 242 | 8,778 | 2.8 |
| Shaman | 28 | 273 | 7,986 | 3.4 |
| Mage | 23 | 807 | 10,698 | 7.5 |
| Warlock | 19 | 763 | 7,807 | 9.8 |
| Druid | 28 | 495 | 10,426 | 4.7 |

The raw deaths-per-bot figure (paladin 31.6, mage 28.7, warlock 28.1 among all 250 bots) is diluted by the idle gnomes; the per-fight rate above is the better measure. Median level per class was not computed.

- Priest, shaman, druid and warrior have the lowest rates (2.8 to 5.5). Cause not established.
- Warlock, hunter, rogue, paladin and mage are at 7.5 to 9.8 per 100 fights.
- Paladin: Judgement (20271) still fails with CASTER_AURASTATE even though the source maps active seals to that aura state and the sim binary was built after the change. Either the fix does not take effect at runtime or the running binary differs. Unresolved.
- Casters: wand/melee fallback fired only 5 times; Shoot failures are mostly DONT_REPORT, which looks benign, so the fallback is mostly moot.
- Hunters: see section 4.
- Mana awareness: NO_POWER per hour down about 55%.

## 6. Mobs that kill

Fights and deaths for entries with at least 15 fights (deaths by killer / fights targeted; distinct bots):

| Entry | R3 | R2 |
|---|---|---|
| Shadowgale Ursera 250927 (map 2991) | 445 / 453 = 98%; 12 bots | 93 / 94 = 99%; 8 bots |
| Young Forest Bear 822 | 412 / 647 = 64%; 22-24 bots | 99 / 195 = 51%; 22-28 bots |
| Defias Bandit 116 | 255 / 448 = 57%; 23-26 bots | 210 / 327 = 64%; 27-29 bots |
| Prowler 118 | 507 / 1,066 = 48%; 16 bots | 43 / 128 = 34%; 4-7 bots |
| Narg 79 | 0 fights, 3 deaths | 10 / 16 = 63%; 8-13 bots (16 fights, just above the minimum) |

The rates for 250927, 822 and 116 repeat in R2 and R3 across many bots, so they are not only loop artifacts. For 250927 all fights are on map 2991 (Skyborne); scope that rule by map. Murloc Forager (46) and Shadowgale Shriekling (256092) were seen as weaker candidates and were not re-measured here. Whether these mobs are kill-quest objectives was not checked.

## 7. Proposed rules

Full entries (condition, action, evidence, expected effect, risk, confidence, rollback, version) are in `docs/playerbots/rules/proposed-v1-20261007.md`. Summary, all PROPOSED and none approved:

- R-01 Death-loop breaker (high evidence of the pattern; 37 bots hold 64% of deaths).
- R-02 Avoid list: 250927 (map 2991), 822, 116; Prowler 118 pending; Narg 79 low data.
- R-03 Extend aggro avoidance to idle and grind states (hypothesis, no baseline yet).
- R-04 Hunter ammo and Auto Shot.
- R-05 Judgement: verification steps first, drop from rotation if unresolved.
- R-06 Wand failures: do not count DONT_REPORT toward fallback.
- R-07 Flee A/B at mode 1.
- R-08 Extra logging.
- R-09 Race-7 gnome bots: investigate or drop from the cohort.

## 8. What the data could not answer

- Alive versus dead snapshot and time-to-level per level.
- Pull distance for classes other than hunter; deliberate versus accidental pulls; total fights and bot-first share for a baseline.
- Whether aggro avoidance would reduce deaths in idle and grind states.
- Real killer per fight (no cheap join of fight start to death).
- Why priests, shamans, druids and warriors die less often.
- Whether hunters have ammo, and whether the avoid-list mobs are quest targets.
- Quest names for the quarantined ids.
- Distribution of consecutive-death chain lengths and a proximity baseline for the loop detection.
- Why Judgement still fails; whether the running binary equals the one built from the tree.
- Why race 7 never fights.
- Whether flee mode matters (no comparable arm).

## 9. Open decisions

1. Approve or reject R-01 to R-06, and set N for the death-loop breaker (provisional 3, to be tuned).
2. Narg (79): hold until more fights (16 total).
3. Judgement: fix versus drop now.
4. Approve the A/B plan for flee mode 1 and its size.
5. Approve the extra logging in R-08.
6. Fix or drop race-7 gnome bots.

## Addendum (2026-10-08): baseline predates two combat changes

The death and flee numbers for the shield classes (warrior, paladin, shaman) in this analysis were measured before merge `3f3bbd53c5`, which
changed block handling: the critical-block clamp (`Unit.cpp:1494`, `Blocked = min(Blocked, Damage)`, which removes a wraparound that could
one-shot shield bots) and block applying to weapon-based spells (`Unit.cpp:1266`, `GetClassicShieldBlockValue()`). Both reduce damage taken by
shield wearers, so their death rates and flee counts here may be too high, and the comparison with the other classes is not like-for-like. The
shield-class sim needs a re-run on the current build before these figures are used as a baseline or for the flee mode A/B (R-07). Source: the
merge review, finding 3 (`review-upstream-merge-20261008.md`).
