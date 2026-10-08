# Player bots - progress notes

Per-phase validation notes (what was verified, with numbers). Plan: [feature-plan.md](feature-plan.md).

## Phase 2 - Engine core (validated on the sim, 2026-10-06)

Design: [engine-design.md](engine-design.md). Verified on the sim worldserver (forever_sim_* databases, bot log forever_sim_log),
RelWithDebInfo build, 10 to 180 bots, never on the real realm.

**Engine and logging**
- 10 bots spawned, `bot strategy all +test_idle,+test_combat,+test_dead`: 30 STRATEGY_ADDED rows, then one `decision` row per bot
  every 10 s (TEST_IDLE_NOTE / TEST_IDLE alternate; the lower-relevance alternative shows `NOT_USEFUL` or `NOT_REACHED` in
  `details.alternatives`; the band, relevance and effective relevance are in the details).
- `test_quiet` (multiplier) on one bot: both idle actions `MULTIPLIED_TO_ZERO`; with `bot trace <bot> on` the bot wrote one
  `trace/TRACE_EVAL` row per evaluation (`considered[]`), other bots wrote none.
- Dead engine: `bot kill Botbelan` -> `state_change DIED (engine noncombat -> dead)` with the last 8 decisions in
  `details.recent_decisions`, then one `TEST_DEAD` decision from the dead engine; `revive Botbelan` -> `REVIVED (dead -> noncombat)`.
  Combat engine exercised with `bot ai force <bot> combat` (FORCED, TEST_COMBAT decisions, FORCED_END on `auto`); no real combat yet.
- Despawn/restart: `server exit` with bots online wrote LOGOUT_SHUTDOWN rows, characters.online = 0 afterwards, bots log in again
  with fresh AI (AI_START) and no duplicated or missing quest events; saved quest state persisted across the restart.
- Bug found and fixed during the test: a world stall made all bots tick in lockstep forever (accumulator reset to 0). Ticks now
  fire when a per-bot time bucket changes (fixed random phase), so bots spread again after a stall. After the fix 1800 decisions
  over 100 s landed in 524 distinct 50 ms slots.

**Quest log** `bot quest add|complete|reward|abandon|fail` on throwaway sim bots (quests 783, 7, 358, 9, 183, 788):
QUEST_ACCEPTED, QUEST_PROGRESS (first change and the completing change only), QUEST_COMPLETE, QUEST_REWARDED (xp, money, reputation
ids, choice item with name, accept_to_reward_s), QUEST_ABANDONED and QUEST_FAILED (WARN) all appeared in `bot_event` with `quest_id`,
level/map/zone/position, `details.source = test_command`, `cause`, `alive`, `engine`. Not exercised: a real (non-test) failure cause
(timeout, died, escort_failed, event_failed paths are wired but unrun), the `source = "bot"` rows (only produced by non-test flows),
a choice reward with fixed items, group quests.

**Tick cost (180 idle bots, 500 ms tick, sim worldserver process CPU, one core = 100%)**
| state | CPU |
|---|---|
| 0 bots, idle | 9.4 % |
| 180 bots, AI on (no strategies) | 36.4 / 38.2 / 36.5 % (three 60 s windows) |
| 180 bots, AI off (`bot ai off`) | 37.9 / 37.3 % |
| 180 bots, AI on + test strategies on all (idle 10 s, about 14 decision rows/s) | 39.6 % |
- External sampling cannot separate AI on from off (difference is inside the +-1 % noise). Internal timers: **0.96 us average per
  tick** (max 72 us in steady state, up to 1.5 ms when a login burst coincides), 360 ticks/s for 180 bots = **about 0.35 ms of CPU
  per second in total, 0.03 % of a core, about 2 us per bot per second**. With the test strategies the average is 3.2 us per tick
  (79 ms over about 75 s, of which logging 15 ms): about 0.1 % of a core.
- The cost of the bots themselves (login, map, updates) is about (37 - 9.4) % = 27.6 % of a core = 1.5 ms per bot per second, in line
  with the earlier baseline (1.3 ms); the AI adds about 0.1 % of that. Nothing dominates: an empty tick is one state check.
- Decision: `Bot.AI.Enabled` defaults to on (cost is negligible with no strategies). `Bot.AI.TestStrategy` defaults to off.

**Not done / notes**: no real behavior (movement, combat, release/revive are Phase 3/5); no killer/damage capture in the death
event; config is read once (no reload); trace rows are INFO severity by design (see engine-design.md); a build with the full
`scripts` target needs `/m:1 /p:CL_MPCount=2` after touching `WorldSession.h` (the `/m:2` run hit the compiler heap limit).

## Position telemetry (`bot_pos`, for the sim console map trails; verified on the sim 2026-10-06)

- Table `bot_pos` (forever-botlog-setup.sql, applied to `forever_sim_log`; NOT yet applied to `forever_botlog`): `ts DATETIME(3)`,
  `bot_guid`, `map_id`, `zone_id`, `x`, `y`, `z` (float), `flags` (bit 0 moving, bit 1 in combat, bit 2 dead). Indexes `(map_id, ts)` and
  `(bot_guid, ts)`. Partitioned by day, kept 2 days. `botlog_roll_partitions(keep)` now calls the new `botlog_roll_table(table, keep)` for
  `bot_event` (keep days) and `bot_pos` (2 days) and uses `DATABASE()` instead of a hard-coded schema, so the same script works for
  `forever_botlog` and `forever_sim_log`.
- Producer: `BotAI::SamplePosition` at the end of every AI tick (map thread) -> `BotMgr::LogPosition` (mutex-protected buffer, same pattern
  as `LogEvent`, capped at 100000 samples if the DB stalls) -> `BotMgr::FlushLog` on the world thread writes a separate async transaction
  (`BOTLOG_INS_POS`). Samples: first tick after login, map or zone change, then at most every `Bot.Log.PosIntervalSec` (default 5, 0 = off,
  read once at startup, forced to 0 when the bot log DB is not available) and only if the bot moved more than 2 yards since its last sample.
  Idle bots write nothing.
- Test aid: `bot nudge <name|all> <yards> [degrees]` (0 = +x, 90 = +y; default along facing) moves bots via `UpdatePosition` without a
  teleport handshake. `bot ai status` prints the sample count and time spent.
- Result (180 bots, sim): 180 login rows (180 distinct bots), then 60 s of `bot nudge all 5 90` once per second gave 2408 rows in 72 s
  (13.4 per bot, about one per 5.4 s = interval 5 s plus tick granularity); y advanced 25 yards per sample as expected; 53 s of idle after
  that added 0 rows. Tick cost: time inside `SamplePosition` about 0.5 us per tick while idle (7.3 ms over 14420 ticks), about 8 us per
  sample taken; at 360 ticks/s that is 0.2 ms of CPU per second (0.02 % of a core). Whole-tick average stayed about 2.1 us (it was 0.96 us
  in the Phase 2 run; the two extra clock reads and the position checks account for part of it, rest is noise between runs).

## Phase 3: non-combat basics (verified on the sim 2026-10-06, not committed)

- Walking with mmaps, arrival, NO_PATH, stuck (NO_PROGRESS then UNREACHABLE_TARGET), eat, drink, eat+drink together (EAT_START/DRINK_START/
  EAT_DONE/DRINK_DONE/REST_END rows), follow, stay, bot_pos moving flag: all seen in bot_event/bot_pos.
- Dead recovery: kill -> DIED -> RELEASE_SPIRIT (3-8 s) -> CORPSE_RUN_START -> CORPSE_RECLAIMED -> REVIVED; spirit healer fallback
  (SPIRIT_HEALER_PLAN -> FOUND -> SPIRIT_HEALED -> REVIVED, sickness logged); unreachable/off-navmesh corpse -> healer. The 23 bots that
  were dead before the deploy recovered. Restart while dead: two bots killed, worldserver restarted within seconds, `bot spawn 180`: both
  logged in dead with a fresh AI and recovered (one by corpse reclaim, one by spirit healer, CORPSE_UNREACHABLE plan).
- Tick cost, 180 bots (re-measured with a true idle window, no bot moving): idle 3.4 us avg, max 299 us (the Phase 2 / telemetry baseline
  was about 2.1 us; the rest/goto/follow/hold triggers evaluated every tick account for the difference). Burst of 120 simultaneous gotos:
  first 6.5 s avg 10.9 us (max 0.9 ms), over 22 s (path queries plus walking) avg 21.7 us, worst single tick 4.4 ms (a tick that
  loaded grids and ran a path query), total 223 ms in 22 s = about 1 % of a core. Tail after the burst 4.7 us. Process CPU 34-37 % of one
  core in all windows (dominated by the core: 180 sessions, maps, movement), the AI is not visible in it.
- Goto burst outcome (120 bots, goal 108 yd away, random start positions): 128 GOTO_START, 56 GOTO_ARRIVED, 43 NO_PATH (path_type 8 = core
  NOPATH, off_navmesh false), 16 UNREACHABLE_TARGET. The 16 stuck bots are all dwarf/gnome race-3 bots stacked at one spawn point
  (-6094, 818.8, 429.7), 46 yd above the grounded goal z: `bot path` from there gives a zero-length path to its own spot and NOPATH to
  anything 36+ yd away, so their start poly is isolated: a test artifact of the sim spawn position, not a stuck-detection defect (the
  detector fired as designed). **Superseded**: see "Goto navigation fixes" below; the causes were position drift from `bot nudge` and goals over
  navmesh holes/ledges, not isolated spawn polys.
- Not verified: far (cross-map) teleport handshake, instance/battleground deaths, Hardcore path, killer capture in the death event, real
  consumables from bags (FreeFood placeholder), grid-load memory for many far goals, follow across maps, a long partial-path goto.

## Death post-mortem and combat events (verified on the sim 2026-10-06, not committed)

- `death` rows (reason DIED) carry: `killer` (guid, entry, name, level, rank, type creature/player/environment/self, last hit melee/spell, lvl_diff, hp_before), `damage` (last 10 s per source and per spell, hits, total, fight_s, started_by, first_hit_s_before_death), `hp_traj` ([ms before death, hp]), `auras` and `cc`, `resources` (power at death and 10 s ago, armor, gear summary), `combat` (attackers, hostiles_30yd, dealt_10s, strategies per engine, activity), `bots_30yd` and `bots_30yd_fighting`, `repeat` (per bot and per killer entry+zone), `source` (`test_command` for bot kill/hurt/aggro/envdmg, else `bot`), `fight_id`. Header: zone_id, level, target_entry = killer entry. `recent_decisions` limited to 15 s / 10 entries.
- `combat` rows: COMBAT_START (target, first=bot/mob, hp, power, hostiles_30yd, dist, activity) and COMBAT_END (outcome died/target_killed/fled/reset/other, duration_s, dealt, taken, hp_start/end/min, kills), linked by `fight_id = "<botguid>-<unix_ms>"`; END after 2 s without combat or hits (flapping coalesced).
- REVIVED rows carry `dead_s`, `attempts`, `recovery` (corpse_run or spirit_healer), `plan_reason`.
- Tracking: per-bot ring buffers (hits 32, dealt 32, vitals 12 at 1 Hz); heavy work only at death (snapshot avg 146 us). Hooks in Unit::DealDamage / Unit::Kill, env type stashed in Player::EnvironmentalDamage; no core header changed.
- Test aids: `bot aggro <name> [radius]`, `bot envdmg <name> <type 0-6> <amount>`.
- Cost, 180 bots (`bot ai status`): tick steady windows 3.28 us before, 3.17-3.42 us after; hooks outside the tick avg 4.76 us/call; death row 0.8-2.1 KB.

## Goto navigation fixes (verified on the sim 2026-10-06, not committed)

- Root cause of the NO_PATH / stuck bursts: (1) `bot nudge` moved bots in straight lines off the navmesh and they were saved there (every later goto
  NO_PATH, `start_off_navmesh`); (2) burst goals at a fixed offset landed on navmesh holes (gnome, Skyborne) or on ledges 46 yd above/below
  the walkable surface (dwarf, orc: path 0x84, partial, goal far from poly), where the bot walked to the nearest poly and could never arrive
  (arrival needs |dz| < 25). Spawn points (`playercreateinfo`) are fine.
- Code: `PATH_PARTIAL_FAR` (path_fail) now fails fast at GOTO_START when the path is partial and the goal is > 7 yd (3D) from a walkable poly
  (details: path_type, end_gap, end_gap_3d, path_length, nav_end). The goal z is replaced by the navmesh height of the path end when the path is
  complete and within 10 yd, so arrival is judged against the walkable surface. NO_PATH details now carry `start_off_navmesh` /
  `goal_off_navmesh`; GOTO_START carries `end_gap_3d`, `goal_far_from_poly`. `bot nudge` now only moves a bot when the destination is a
  complete walkable path (otherwise skipped and reported). PATH_PARTIAL_FAR is a new reason code (to be registered in the schema docs).
- Sim tooling: `scripts/sim-burst.py` (`--mode valid` probes 16 headings per start and needs `type 0x01 valid 1 partial 0`, matched by
  goal coordinates; `--expect 180` refuses to run unless exactly 180 bots are online), `reset-bot-positions.py` (despawn, reset to
  playercreateinfo, spawn 180). The sim has 360 bot characters; `bot spawn 180` must only run with 0 online (it created 180 extra characters when
  run with 180 online). The sim worldserver can also be restarted by the run controller or console; check the uptime before a burst.
- Results (sim, 180 bots online, one terminal outcome per bot):
  - Fixed-offset burst, old code, same start positions: 91 arrived, 49 NO_PATH, 39 NO_PROGRESS then 39 UNREACHABLE_TARGET (stuck cycle 8 s x 3).
  - Fixed-offset burst, new code: 90 arrived, 50 NO_PATH (gnome 20 + Skyborne 30, goals on mesh holes), 39 PATH_PARTIAL_FAR (orc 18, dwarf 21)
    failed at GOTO_START instead of after about 24 s of walking; 0 stuck.
  - Valid-goal burst (goal probed as a complete path), 160 bots (the gnome start has no valid goal in 16 headings): old code 14:04 had 30 orcs
    stuck (path 0x01 but z mismatch), new code 159 GOTO_ARRIVED, 0 NO_PATH, 0 stuck; the 160th bot died on the way (spirit healer path).
- Not verified: the gnome start (-4983, 878, 274) has no complete path in any of 16 headings at 108 yd (not investigated); why some 400-500 yd
  paths return 0x0A; Skyborne goals at 108 yd (only holes tested).


## A2 chat commands (built, NOT yet verified on the sim, not committed)

- Code: `src/server/game/Bots/BotChat.h/.cpp` (parser, authorization, verbs follow/stay/goto/rest/release/status/strategy/verbose, aggregated replies,
  per-bot `chat_command` bot_event with issuer, channel, match, outcome, reason; unauthorized attempts rate limited per issuer), test path
  `bot say <issuer> <party|raid|whisper> <text>` and `bot group form|move|list|disband|disbandall` (cs_bot.cpp), three one-line hooks in
  `Handlers/ChatHandler.cpp` (party, raid, whisper), documented `Bot.Chat.*` keys in `worldserver.conf.dist`.
- Builds clean (worldserver, RelWithDebInfo). Not deployed: the sim was live with 180 bots and could not be restarted at that point, so none of the
  verify steps (g-prefix/role routing on a 10-40 bot raid, unauthorized issuer logged, verbose toggle, tick cost at 180 bots) have run yet.
- Known gaps: `rest` only toggles the strategy (no forced rest without editing BotBehavior), role selectors map classes via `Bot.Chat.Role.*`
  (hybrids are not split by spec), the unauthorized event is logged against the first bot of the group (or the whispered bot).
- Decisions: selectors combine as follows. Subgroup parts union among themselves (`g1,g3`), role/class parts union among themselves (`tank,dps`), and a subgroup plus a role/class is an INTERSECTION (`g1,tank` = tanks inside subgroup 1). Only the party/raid leader commands bots; verbose defaults to off. Selector change rebuilt but not yet verified on the sim.

## Quest pipeline (BotQuest.*), verified on the sim 2026-10-06 (partially), not committed
- New: `src/server/game/Bots/BotQuest.{h,cpp}`; hunks in BotEngine.h, BotStrategies.cpp, BotMgr.cpp (EnsureIndex in StartLogin), worldserver.conf.dist (Bot.Quest.*).
- Enable with "quest" in Bot.AI.Default.NonCombat (sim worldserver.conf has it; the .dist default is unchanged).
- Static index at startup (starter grid, spawns, kill credit, quest-item drop sources from one world-thread query); per-bot task state in `quest_ctx`.
- Events: `decision` (QUEST_PICK, QUEST_PICK_FAR, QUEST_WORK, QUEST_TURNIN_PLAN, QUEST_TURNED_IN, QUEST_PULL trace), `quest_blocked` codes: NEEDS_GROUP, TIMED_UNSUPPORTED, SKILL_REQUIRED, REPUTATION_REQUIRED, NEEDS_EVENT, OBJECTIVE_UNSUPPORTED, NO_TARGET_SPAWN, MISSING_ITEM_SOURCE, NO_ENDER_ROW, NO_ENDER_SPAWN, QUEST_LEVEL, QUEST_PREREQ, NO_QUEST_AVAILABLE, NO_PATH, PATH_PARTIAL_FAR, UNREACHABLE, TARGET_UNREACHABLE, ELITE_TOO_STRONG, TARGET_LEVEL_TOO_HIGH, ITEM_NOT_DROPPING, GIVER_NOT_INTERACTABLE, QUEST_LOG_FULL, BAG_FULL, DATA_ERROR.
- Auto-equip of quest rewards (armour/weapons only, clear upgrades usable by the bot), logged as decision QUEST_REWARD_EQUIPPED.
- Fixes found by the 1-5 run: (1) MoveChase does not move a Player, the approach now uses BotMotion goals; (2) PathGenerator returns NOPATH|SHORTCUT on routes longer than about 300 yd (74 point buffer), Travel now hops via validated intermediate waypoints (FindHop), NO_PATH dropped from about 150 to about 20 per run; (3) bots with a left-over auto-repeat spell (Auto Shot/Shoot) counted as casting and froze the quest tick, now skipAutorepeat; (4) melee pull range. `bot state` shows task diagnostics (calls/why).
- Known gaps: game-object objectives and area triggers, trainer visits (extension point: walk-to-NPC visit in RunNpcVisit), no grind fallback and no quest-hub travel (bots idle when nothing is takeable locally, next-plan E3), no spell training (E1).

## Basic combat (BotCombat.*), verified on the sim 2026-10-06, not committed
- Strategy `combat` (Combat engine, default `Bot.AI.Default.Combat = "combat"`): triggers `combat_engaged`, `combat_need_heal`, `combat_fleeing`; actions `combat_flee` (Emergency, BotMotion SetGoal+Step), `combat_heal` (SELF_HEAL below `HealBelowPct`), `combat_engage` (pick nearest opponent that is not too strong, Attack + MoveChase, ranged classes stop at `CasterRangePct` of spell range), `combat_cast` (per class table of 27 vanilla rank-1 spell ids, resolved against what the bot knows incl. rank chain; `bot spells <name>` shows it). Only level-gated spells are known: trainer spells (Serpent Sting, Arcane Shot, Battle Shout, Rend, Frostbolt, Fire Blast, Earth Shock, Immolate, Corruption, Moonfire, SWP, Judgement ...) are NOT known at level 1 and bots never train, so L1 rotations are 1-2 spells.
- Config (`Bot.AI.Combat.*`, documented in worldserver.conf.dist): Flee.Enabled, Flee.LevelDiff (4), EliteLevelBonus (3), Flee.MaxSec, Flee.Yards, ApproachTimeoutSec, HealBelowPct, CasterRangePct.
- Events: TARGET_PICKED, FLEE_LEVEL_DIFF / FLEE_ENDED / FLEE_GAVE_UP / FLEE_BLOCKED, FIGHT_TOO_STRONG, APPROACH_TIMEOUT, RANGED_TO_MELEE, CAST_FAILED (deduped, with result name), CAST_NO_POWER, SELF_HEAL, NO_TARGET (diagnostic), COMBAT_SUMMARY (casts per spell, targets, heals) next to COMBAT_START/END.
- ROOT CAUSE found on the way (fixed in CharacterHandler.cpp, HandleBotPlayerLogin, one line): `Player::CanNeverSee` hides every object from a player lacking PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME (normally set by the client time sync). Bots never got it, so a bot saw NOTHING: IsValidAttackTarget and spell target checks failed against every creature. Now set at bot login. Anything else that depends on bot visibility was affected before.
- Evidence (sim, n = 4 bots per class = 36 bots, one run each, teleported next to Elwynn Young Wolf spawns, `bot aggro 30`, 4-5 min, same protocol both runs; "kills" = mobs killed per the combat_end rows): combat strategy removed (`bot strategy all -combat`): 44 fights, 0 kills, 0 damage dealt, 43 deaths. Combat on: 45 fights, 41 mobs killed (18 fights ended target_killed), 26 deaths; every class dealt damage (mobs killed per class: warrior 0, paladin 1, hunter 3, rogue 7, priest 6, shaman 6, mage 6, warlock 6, druid 6; deaths: warrior 6, paladin 6, hunter 6, rogue 0, others 1-2). Why warriors/paladins/hunters die more was not investigated (hypothesis: empty rage/mana at start, no ranged pull). A second run (all 180 bots `bot aggro 30` plus the same 36 teleported onto wolves): 120 fights, 30 ended target_killed, 21 deaths, 10 FLEE_LEVEL_DIFF each followed by FLEE_ENDED, but all 12 fights that had a FLEE_LEVEL_DIFF still ended `died` (Northshire Guard L55 one-shots, wolves outrun): the `bot_fled` outcome (set by BotAI.cpp when the bot is still moving to a goal at fight end) was NOT observed. Casts seen: Heroic Strike, Wrath, Healing Touch, Seal of Righteousness, Auto Shot, Raptor Strike, Sinister Strike, Eviscerate, Smite, Lightning Bolt, Fireball, Shadow Bolt.
- Tick cost, 180 bots in combat: avg 14.2 us (baseline idle/no-combat 10.4-11.9 us); combat hooks 8 us avg per call outside the tick. Whole-server cost of the visibility fix (bots now get visibility updates) is NOT measured; `server info` showed update time diff 2 ms at 180 bots idle on this build, with no A/B against the old build.
- Fight then `bot goto` on the same bot: GOTO_START/GOTO_ARRIVED fine (2 of 2), eat/drink after the fight works (EAT/DRINK_START/DONE/REST_END). Corpse run after a death works.
- Not verified: Judgement (not known at L1), hunter ammo/Auto Shot failures at scale, rogue EQUIPPED_ITEM_CLASS (seen once for Sinister Strike), higher levels, groups, pets, PvP (CanFight uses IsValidAttackTarget, so neutral NPCs that are not at war are skipped with NO_TARGET).

## Navigation: E3 hubs, E1 trainers, E4 vendors, Skyborne data gap, quarantine (sim-verified, 60 bots, 20 min)
- E3: startup index (starter quests, hubs, grind spawns). Events QUEST_NO_LOCAL, QUEST_HUB_TRAVEL, QUEST_HUB_NONE, QUEST_GRIND.
- E1: class trainers cached at startup (Index.Trainers, key is class id; profession trainers would use 0x100|skill). Events TRAIN_TRIP, TRAINED, TRAIN_NO_MONEY, TRAIN_NO_TRAINER, TRAIN_UNREACHABLE. Trainer.h: GetSpell/CanTeachSpell/GetSpellState made public, GetSpells() added.
- E4: vendors cached at startup. Sells greys then unusable armor/weapons (never quest items, bags), repairs, buys bigger bags after spell reserve. Events VENDOR_TRIP, SOLD_ITEMS, REPAIRED, BAG_BOUGHT, BAG_NO_MONEY, BAG_BUY_FAILED, VENDOR_NONE, VENDOR_UNREACHABLE. BAG_BOUGHT not yet seen in a run.
- Skyborne: quest 92460 'Coming of Age' reward item 264908 has no item template; TurnIn now logs REWARD_ITEM_MISSING and rewards anyway. Data gap still needs a data extraction/database fix.
- Quarantine: per-bot escalation (quest blacklisted 6 h after 3 reach failures, QUEST_QUARANTINED) and global (10 drops across bots, 1 h, QUEST_QUARANTINED_GLOBAL).

## Gear scoring and profession trainers (branch claude/project-thread-8mh0k5, not compiled or sim-verified)
- BotGear (pure, unit tested in tests/game/BotGear.cpp): class role (melee str / melee agi / ranged / caster) weights over stats x item level, armor and weapon DPS; upgrade rule = +10 percent plus a floor. Used by `ChooseReward` (a real upgrade for the class beats vendor value) and `EquipIfUpgrade`, which also runs as a bag sweep every 30 s from `ServiceDue` (decision `GEAR_EQUIPPED`; quest rewards keep `QUEST_REWARD_EQUIPPED`). One-hand weapons only go to the main hand (no dual wield yet); two-handers are scored against main + off hand.
- BotProfession (pure, tests/game/BotProfession.cpp): plan per bot = first aid, cooking and two of mining/herbalism/skinning (guid % 3). Profession trainers are indexed at startup under key `0x100 | skill` (spell -> skill line via SkillLineAbility). `ProfessionTrip` in `ServiceDue` (level 5+, every 60 s) walks to the nearest trainer for a missing planned skill, then for new ranks; events PROF_TRIP, PROF_NO_TRAINER, PROF_NO_MONEY, learning is logged as TRAINED by the existing trainer visit.
- Not done yet: using the professions (skinning corpses, mining/herbalism nodes, bandage and cooking crafts), gear purchases at vendors, fishing, crafting, auction house.
