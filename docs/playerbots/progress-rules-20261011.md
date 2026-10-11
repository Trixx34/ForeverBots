# Progress rules N-01 to N-09

Source: `rules/additional-20261011.md` (from `sim-run-800-analysis-20261011.md`). Nothing here has run on a sim server yet. Every rule has its own switch in the `ADDITIONAL PROGRESS RULES` section of `botserver.conf.dist`; the decisions are pure functions in `BotProgressRules.h` (unit tests in `tests/game/BotProgressRules.cpp`), the settings and shared state are in `BotRules.h/.cpp`.

| Rule | What it does | Where | Rows |
|---|---|---|---|
| N-01 XP stall | Second watchdog clock, reset only by xp, a level, a rewarded quest or a changed quest objective counter (kill, loot, use). 20 min: clear task, blacklists, ignore lists and trip guards. 40 min: relog (`BotMgr::RequestRelog`, runs on the world thread). 90 min: walk to the nearest quest hub for the level. Capped per hour. One clock per bot guid, kept outside the AI so it survives the relog. Bots at the level cap, dead, flying, in an instance, battleground, duel or group hold the clock. | `BotWatchdog.cpp`, `BotQuest.cpp` (`ClearForStall`, `SendToHub`) | `STALL_XP` (with the last 10 quest rows), `STALL_XP_EXHAUSTED` |
| N-02 zone fence | `Travel()` of the quest layer (quest, trainer, vendor, profession legs) refuses a leg whose end or sampled point (every 150 yd, at most 12) is in a leveling zone whose lowest level is more than bot level + 4. The task is dropped and the quest blacklisted for the bot. Flee points inside such a zone are not used. A bot already standing in the zone may stay. Uses the zone bands of the travel module. | `BotRules.cpp` (`Fenced`, `LegFenced`), `BotQuest.cpp`, `BotCombat.cpp` | `ZONE_FENCE` (quest_blocked) |
| N-03 pull gate | The quest layer's mob choice refuses every mob while the main hand holds a tool or nothing, or the ranged weapon has no ammo (it wields the weapon, requests an ammo trip), and the rest module eats below 80% health and drinks below 50% mana before the next pull. A health or power block is let go after 120 s; a weapon or ammo block is not. | `BotQuest.cpp` (`PullBlocked`), `BotBehavior.cpp` | `PULL_BLOCKED` (once a minute) |
| N-04 no-damage breaker | 10 s into a fight with one target and under 5% of its health taken: log the cause (weapon, ammo, power, line of sight, range), fix it where possible, casters fall back to melee, a mob that outlevels the bot goes back to the target choice (flee run) instead of a blow-for-blow fight. Once per target. | `BotCombat.cpp` (`EngageAction`) | `FIGHT_NO_DAMAGE` |
| N-05 bag pressure | Vendor trip planned at 4 free slots when a vendor is within 400 yd (at most one per 15 min; the one-slot trip stays). Grey items are left on the corpse below 4 free slots (quest items and better always taken). | `BotQuest.cpp` (vendor planner, `TakeLoot`) | `BAG_PRESSURE` once per level |
| N-06 crowd cap | Quest, quest hub and grind area scores drop to 35% when 8 or more other bots work on the same one within 150 yd. A crowded grind area moves to the nearest free grind spawn point. Soft: a crowded choice still wins when it is the only one. Claims in a shared book keyed by objective, one claim per bot. | `BotQuest.cpp`, `BotProgressRules.h` (`CrowdBook`) | `CROWD_SPREAD` |
| N-07 zone leave | After 2 hub scans without a hub for the same map and level, or 10 min of grind kills under 40 xp each, ask the travel module for the next band zone (`BotTravel::RequestLeave`). Needs `Bot.AI.Travel.Enabled`; otherwise only the row is written. | `BotQuest.cpp`, `BotTravel.cpp`, `BotAI.cpp` (`OnXpGain`) | `ZONE_LEAVE` |
| N-08 class margin | Highest mob level above the bot for the quest layer's kill and grind targets while the bot is level 10 or lower: warrior, hunter, rogue, warlock 0; paladin, priest, druid 1; other classes unchanged. **Off by default** until an A/B run has sized it. Never raises `Bot.Quest.MaxMobLevelDiff`. | `BotQuest.cpp` (`CandidateMob`) | none |
| N-09 log diet | `AI_TICK_STATS` every 300 s (was 60), per bot and hour caps for `TARGET_PICKED` (60) and `AGGRO_AVOID` (30) counted into `LOG_SUPPRESSED`, aura rows only for fights that end in a death. | `BotAI.cpp`, `BotMgr.cpp` | none |

`Bot.Rules.ArmPct` limits N-01 to N-08 to a share of the bots (guid counter modulo 100 below the value) for the A/B run of `sim-run-protocol-20261011.md`; 0 (default) means all bots.

## Not done / limits

- N-02 checks a straight line from the bot to the goal and the goal itself, not the navmesh path (the path is only known after the query). A winding route through a high zone is not seen.
- N-03 acts on the quest layer's mob choice only. The combat engine's own target pick (mobs that attack the bot) is not gated.
- N-04's flee for outleveling mobs goes through the existing target pick and its flee settings; it does not start a second flee mechanism.
- N-06 counts bots by the position of the quest giver, hub or grind area, not by where the objective mobs stand.
- N-08 covers the quest layer only; the combat engine's `Bot.AI.Combat.Flee.LevelDiff` is unchanged.
