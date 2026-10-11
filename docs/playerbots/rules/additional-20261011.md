# Additional rule suggestions (not in R-01..R-09 or R-V2-01..08)

Basis: `docs/playerbots/sim-run-800-analysis-20261011.md` (800 bots, 22.4 h) read against `rules/proposed-v1-20261007.md`, `rules/proposed-v2-20261011.md` and the code on `forever`. All numbers are from that analysis. N-01 to N-09 are implemented (see `../progress-rules-20261011.md`); N-10 is the run protocol (`../sim-run-protocol-20261011.md`). Nothing has run on a sim server yet. Size: S = a few hours, M = a day, L = several days. Top picks are marked **TOP**.

## N-01 XP-based stall detector (second watchdog tier)  **TOP**
- Evidence: bots earning XP each hour fall from 750 to 215; 587 of 800 earned nothing in the last hour; about 75% of bot time is not combat, rest, death or walking. The watchdog (`feature-bot-watchdog-20261008.md`) counts "moved 10 yd" as progress, so a bot that walks trips in circles (PROF_TRIP, WORKSHOP_TRIP, quest walks) never stalls.
- Rule: a second clock that only resets on XP, a quest reward, or a loot of a quest item. After 20 min without it: clear task and blacklists, log `STALL_XP` with the last 10 decisions. After 40 min: relog; after 90 min: send the bot to the nearest hub for its level. Capped per hour like the existing steps.
- Effect: catches every loop, including ones not found yet; the analysis found at least five.
- Size: M. Risk: low (reuses watchdog steps). Rollback: flag.

## N-02 Zone-band fence for every walk, not only trips  **TOP**
- Evidence: 39% of deaths (9,830) come from mobs +3 or more that were never picked, i.e. aggro on a route; Duskwood alone 2,233 deaths from 24 bots and 6,186 AGGRO_BLOCKED rows, L19-27 mobs against L5-8 bots. R-V2-03 only fences profession trips.
- Rule: in `Travel()` and the quest leg planner, refuse a leg whose path enters a zone with a level band whose minimum is more than bot level + 4 (use the travel module's zone bands); take the quest or trainer elsewhere. Same check for flee goals.
- Effect: removes most >=+6 deaths (11%) and part of +3..+5 (23%); about 10-15% of all deaths. Size: M. Risk: some quests need a border crossing; blacklist those quests for the bot.

## N-03 Pre-pull readiness gate
- Evidence: warriors, hunters, rogues and warlocks die 40-51 times per bot, paladins 16; bots deal 16-23 hp in the last 10 s of a fight and take 35-41; 54% of deaths happen in combat with no movement; `CAST_SKIPPED_WEAPON` 20,409 and `NO_AMMO` 10,894 rows show fights started with a dead rotation.
- Rule: do not start or accept a pull when (a) the class weapon or ammo check fails (R-V2-01 conditions), (b) health below 80% or power below 50% for casters and out of combat, or (c) the bot is on a profession tool. Rest first (`REST_*` exists).
- Effect: fewer deaths for weapon-class bots; complements R-V2-01. Size: S-M. Risk: more resting time (cheap, resting is 135 h of 17,800).

## N-04 No-damage fight breaker
- Evidence: average damage dealt in the last 10 s before death is 16-23 hp against 35-41 taken; fights last 42-47 s before death, the bot never changes plan; `combat_flee` in melee range with a mob still on the bot is the dominant last decision (6,687 + 4,965).
- Rule: if 10 s into a fight the bot has dealt less than 5% of the mob's health, switch to a fallback (auto-attack, wand, melee) and log `FIGHT_NO_DAMAGE` with the cause (weapon, power, range, LOS); if the mob outlevels the bot, use the existing flee-to-guard mode instead of fleeing in melee range.
- Effect: shortens the losing fights that make up the deaths per bot; size S-M. Pairs with R-V2-06.

## N-05 Prevent full bags at the source  **TOP**
- Evidence: 190-200 bots per hour have full bags from 22:00 on (about 25%); vendor trips start only at `freeSlots <= 1` (`BotQuest.cpp` `full = freeSlots <= 1`); QUEST_LOOT/turn-in plans stall on BAG_FULL. R-V2-04 treats the symptom (sell before turn-in).
- Rule: plan the vendor trip at 4 free slots when a vendor is within 400 yd; stop looting grey items below 4 free slots (keep quest items, greens and better); sell grey items during any town pass. Log `BAG_PRESSURE` once per level.
- Effect: removes the full-bag state for most of those 25% of bots. Size: S. Risk: slightly less vendor gold (greys are skipped).

## N-06 Spread bots over hubs and spawns
- Evidence: top blocked quests have 107-195 bots each; 169 bots competed for one object on quest 3361 (`GO_BUSY`); 82 `QUEST_*` top-14 quests stay in the blocked list for hundreds of bots; 800 bots start in ten zones.
- Rule: a soft cap per quest objective and per grind anchor (default 8 bots within 150 yd); the bot over the cap picks another objective, hub or grind anchor. Prefer the quest with fewer bots on it when scores are close.
- Effect: less kill/loot contention, fewer `QUEST_LOOT_SPAWN_EMPTY` (67,124 rows) and `GO_BUSY`. Size: M. Risk: needs a shared counter per objective (the loot claim code is a model).

## N-07 Leave a quest-less zone at once
- Evidence: `QUEST_HUB_NONE` 2,323 rows from 794 bots, `NO_GRIND_TARGET` 1,733 from 644 bots, `QUEST_GRIND` 227k rows (332/h to 14k/h): bots at L9-12 keep grinding in start zones with 22-52 xp kills.
- Rule: after 2 `QUEST_HUB_NONE` rows on a map for the same level, or 10 min of grind below 40 xp per kill, call the travel module for the next band zone (it reads `IsGrinding` already). Log `ZONE_LEAVE` with the reason.
- Effect: fewer grind hours at flat XP; size S (threshold plus a call). Risk: travel over unsafe ground, so combine with N-02.

## N-08 Class-aware pull margin
- Evidence: death rate per pick is 4.0% at diff 0, 9.9% at +1, 21.6% at +2; hunters (51.4 deaths/bot), warlocks (46.5), rogues (44.5) and warriors (39.5) against paladins (15.9) and priests (22.7) at the same targets.
- Rule: table of the highest allowed level difference per class (for example 0 for hunter, rogue, warlock, warrior until level 10; +1 for paladin, priest, druid). Complements R-V2-07, which sets one global cap.
- Effect: fewer deaths in the four weak classes (they hold 56% of deaths with 40% of bots); size S. Needs the A/B below to size it.

## N-09 Log volume diet
- Evidence: telemetry (aura, ai_stats, cast, combat, state_change) is 55% of 7.69M rows; `ai_stats` is constant at about 47.5k per hour; loop rows are 4.7% by themselves; `TARGET_PICKED` 447k and `AGGRO_AVOID` 204k.
- Rule: `ai_stats` every 5 min instead of 1 (-16% rows); per-bot-per-hour cap on `TARGET_PICKED` and `AGGRO_AVOID` (use the existing `LOG_SUPPRESSED` mechanism); aura rows only for fights that end in a death.
- Effect: about 30-35% fewer rows, so longer runs fit; size S. Risk: coarser stats; keep the full level behind `Bot.Log.Categories`.

## N-10 Run protocol for the next sim (not a bot rule)
- Evidence: the 800-bot run mixed ramp-up, loops and deaths; every proposed rule is unmeasured; `BotAvoid::FleeModeFor` already assigns bots to A/B arms by guid.
- Rule: run 6 h at 800 bots, assign arms by guid (arm B gets R-V2-01..03 and N-01), and judge by "bots with at least one XP event per hour", XP/h per bot and deaths per bot-hour, per class. Keep the same seed and zone mix.
- Effect: turns the rules into measured results. Size: S.

## Order I would pick
1. N-01 (catches all loops, including unknown ones), 2. N-05 (cheap, 25% of bots), 3. N-02 (about 10-15% of deaths), then N-03/N-04 with R-V2-05, N-06/N-07 for progress, N-08 after the A/B. N-09 and N-10 before the next run.
