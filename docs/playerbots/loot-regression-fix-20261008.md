# Chest loot regression mitigations (2026-10-08)

Source: `quest-fix-r5-20261008.md`. After the chest loot task went live (R5) deaths rose 459 to 960, stuck events 657 to 888 and path failures 250 to 399. The R5 note found no direct link between the loot task and the deaths (3 deaths within 90 s of a loot event, none with a loot decision in their history), so this change is a set of cheap guards, not a proven fix.

## What `origin/forever` already had

The later loot work (`Bot.AI.Loot.Improved.*`, default **off**) has deadly areas, a per-bot spawn blacklist, path-cost spawn choice and loot exits that skip the quarantine. Because it is off by default, a run without that switch still uses the R5 behavior. The destination veto hook (`BotSetDestinationVeto`) has no module installed, so there is no danger check for chests in either mode. Deaths were only noted for the deadly areas when the Improved switch was on.

## What this change adds (`Bot.AI.Loot.Safety.*`, default on, independent of Improved)

1. **Danger check.** While a chest is claimed and the bot is within 80 yd of it, once per 1.5 s the bot scans live hostile mobs in the grid. A mob 4 or more effective levels above the bot (`Bot.Quest.MaxMobLevelDiff` + 2 by default; elites count `Bot.AI.Combat.EliteLevelBonus` more) within 30 yd of the chest or 18 yd of the bot makes the chest unsafe: the bot gives that spawn up for 300 s (row `QUEST_LOOT_DANGER`) and takes another one. After 3 such spawns in one task the quest is parked for 10 min (`LOOT_DANGER`, no quarantine). Pure part: `BotLoot::ChestDangerous`.
2. **Shared spawn quarantine.** A chest spawn that fails bots three times in 30 min for reasons that do not depend on the bot (no approach progress, cannot interact, no path or unreachable drop on the way) is skipped by every bot for 20 min, doubling on repeats up to 8x (row `QUEST_LOOT_SPAWN_QUARANTINED`). A successful loot clears the history. This caps the retry storm of many bots walking to the same unreachable spawn. Pure part: `BotLoot::SpawnQuarantine`.
3. **Deadly areas in the default pick too.** Bot deaths are always noted, and the nearest-spawn pick (R5 behavior) skips chests within `Bot.AI.Loot.Improved.DeadlyRadius` of a death in the last `DeadlySec` (defaults 40 yd, 600 s). When that or the quarantine removes every spawn the quest is backed off 300 s (`LOOT_NO_USABLE_SPAWN`, with `quarantined` and `deadly` counts in the details).

Not changed: the 12 minute and 8 attempt caps, the R5 quest quarantine for loot drops when Improved is off, claim and cooling logic, path-cost choice (still the Improved switch).

Note: an earlier version of this change had its own danger check (`QUEST_LOOT_DANGER`, `LOOT_DANGER`, `Bot.AI.Loot.Safety.DangerGap`). It was dropped when merging with `forever`, which already re-checks the camp around the spawn while approaching (`GoDangerAt`). Item 1 above and the danger rows in the queries file no longer apply.

## Needs a sim run

Nothing here has been run on a server. The pure functions have unit tests (`[BotLoot]`); `BotQuest.cpp` was only read carefully, not compiled here. Open questions: whether the danger threshold is too strict for level 3 bots at level 8 to 10 chests (it could cut the 936 R5 loot successes; tune `Bot.AI.Loot.Safety.DangerGap`), whether the grid scan finds mobs far enough ahead, and whether the extra deaths come from the loot walks at all. The queries in `loot-regression-queries-20261008.sql` answer this on `bot_event_all`: deaths within 90 s of a loot go and per chest area, stuck and path failures on loot quests, retries per spawn, and loot/reward counts to check that progress did not fall. Compare against R5 on the same 60 minute window.
