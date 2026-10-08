# Death and level-gap avoidance (2026-10-08, branch claude/bots-item-2-death-avoidance)

Status: code written, NOT compiled with the full server and NOT sim-verified. Only the pure helpers (`BotAvoid.h`) were checked with a stand-alone g++ run; the Catch2 test `tests/game/BotAvoid.cpp` was not run.

Evidence base: analysis-2026-10-07-run2.md sections 4 to 6 (avoid list, proposals 1, 2 and 6, R-07 in rules/proposed-v1-20261007.md).

## What changed
1. **Avoid list** (`Bot.AI.Avoid.Entries`, default `116,822,250927,79`; `Bot.AI.Avoid.MaxLevelGap`, default 1, -1 = off).
   - Quest kill and grind target choice (`CandidateMob`) skips a listed creature that is more than MaxLevelGap levels above the bot (plain level gap, no elite bonus). Counted in `TooStrongSeen`.
   - Grind target choice also skips targets with a listed creature within 22 yd (`HighGapMobNear`).
   - Combat target choice (`PickTarget`) adds 100 yd to the score of a listed creature, so it is fought last. It is not marked too strong, so it never starts a flee by itself.
   - Side effect to watch: a quest whose only kill target is a listed creature (for example a Defias Bandit kill quest) is skipped until the bot is within one level, then dropped by `Bot.Quest.StallSec`. Compare quest completion before and after.
2. **First strike** (`Bot.AI.Pull.FirstStrikeMargin` default 2, `Bot.AI.Pull.RangedMaxYd` default 28). Non-melee classes open a pull from the mob's aggro radius plus the margin (capped), never closer than the old 24 yd. Melee classes are unchanged: they still walk into the aggro radius, a ranged or charge opener for them is not done. Expect little change with the defaults (aggro radius is usually below 22 yd); raise the margin to test.
3. **Flee mode per bot** (`Bot.AI.Flee.AbPct`, `Bot.AI.Flee.AbMode`). `Bot.AI.Flee.Mode` already existed globally; now a share of bots (guid counter % 100 < AbPct) can run AbMode while the rest run Flee.Mode. Default AbPct 0 = everyone on Flee.Mode 0, so nothing changes by default. The per-bot mode is in `flee_mode` of every combat row and COMBAT_SUMMARY.

## Flee A/B protocol (R-07)
1. Config: `Bot.AI.Flee.Mode = 0`, `Bot.AI.Flee.AbMode = 1`, `Bot.AI.Flee.AbPct = 50`. Keep `Bot.AI.Avoid.*` and `Bot.AI.Pull.*` identical for both arms (they are global).
2. Run about 60 bots (about 30 per arm), race 7 excluded, same zones, same elapsed time (at least 3 h, as in run2). Check the class mix per arm from the bot list; the arm is fixed by guid, so rebalance by AbPct or by bot selection if the mix is skewed.
3. Compare by `flee_mode` (combat rows, fights) and by bot (deaths): deaths per 100 fights, deaths per bot-hour, kills and level-ups per bot-hour, quest completions. Use per-bot values (the death distribution is skewed: top 25 bots held 49% of deaths) and report class-stratified numbers.
4. Rollback: `Bot.AI.Flee.AbPct = 0`.
Mode 1 also caps quest and grind targets at +1 gap and disables the low-HP flee (fight to the end).

## Still needs sim verification
- Build and the new unit test.
- Deaths per 100 fights from entries 116, 822, 250927, 79 before and after; quest stalls caused by skipped quest targets.
- Whether first strike changes who hits first (`first_hit_s_before_death`, started_by in death rows) for ranged classes.
- The flee A/B itself.
