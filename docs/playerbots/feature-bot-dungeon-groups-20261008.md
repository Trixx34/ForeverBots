# Bot dungeon groups (2026-10-08)

Branch `feature/bot-dungeon-groups`, started from `forever`. First step of the "Deadmines with 5 boosted bots" target in `next-plan.md`: the decisions a bot group needs to run a five-man dungeon. Config switch `Bot.AI.Dungeon.Enabled` (default **off**, options documented in `worldserver.conf.dist`), decision logic as pure functions over plain data (`BotDungeonPlan.h/.cpp`, no `Player`, no `Map`), settings reader (`BotDungeon.h/.cpp`), Catch2 tests in `tests/game/BotDungeonPlan.cpp` (tag `[BotDungeon]`).

## What is in this step

* **Roles.** `RolesOfClass` lists what each class can fill (warrior tank/dps, priest and shaman heal/dps, paladin and druid all three, the rest dps). `PreferredRole` is warrior tank, priest healer, everyone else dps. Spec and talents are not read.
* **Composition.** `Compose()` picks the group (default 1 tank, 1 healer, 3 dps) from candidate bots. The leader is always in; among the roles it can fill it takes the one that leaves the fewest places open (a paladin leader tanks only when nobody else can). Pure classes win scarce roles over hybrids, then the nearer candidate, then the lower guid. Level window anchored on the leader (`MaxLevelSpread`) and on the dungeon's min/max level; unavailable, busy or far candidates are skipped. Missing places are counted so the caller can wait.
* **Readiness.** `CheckReady()` answers Go, Rest (health/mana below the limits, healers need more mana), Repair (durability), WaitForMembers (dead or away) or InCombat.
* **Pull choice.** `ChoosePull()` takes the nearest pack not done and not too strong (`MaxLevelOverGroup`), skips oversized packs and patrols, clears trash before a boss, and takes a boss only with a full group. Result Pull, Rest, SkipAll or Finished, with a reason string for the log.
* **Run state machine.** `AdvanceRun()`: Gather, Travel, Clear, Loot, Recover (after a wipe), Done or Aborted, with timeouts per phase, a `MaxWipes` limit and a whole-run timeout that does not apply when a player leads.

## Not in this step

The glue is not written: filling `Candidate`, `MemberState`, `Pack` and `RunFacts` from the game state, forming the group, walking to the entrance, the combat roles (tank threat, healer triage, which need the role split in `class-role-design.md`) and the log rows. The logic is written so the glue only fills structs and carries out the answer, as with `BotPetLogic`. `Bot.AI.Dungeon.Enabled` has no effect yet beyond being readable through `BotDungeon::Cfg()`.

## Tests and verification

`bin/tests "[BotDungeon]"`: roles, composition (pure vs hybrid, leader choice, missing roles, filters, level window, stability), readiness, pull choice, run phases. The plan file and its tests were compiled and run standalone (95 assertions passed). The full `game` library and `tests` target were not built in this session.

## Glue (branch claude/project-thread-8mh0k5, compiled, unit tests pass, never run on a server)

`BotDungeonRun.{h,cpp}`, called from `BotMgr::Update` (world thread). With `Bot.AI.Dungeon.Enabled` on it runs one group at a time:

1. **Start** (every `Bot.AI.Dungeon.StartCooldownSec`): the entrance is found from the area trigger table for `Bot.AI.Dungeon.Map`; eligible bots (alive, no group, not in a dungeon, not an alt, level `MinLevel`..`MaxLevel`) become `Candidate`s, a random one on the entrance's continent leads, `Compose()` picks the group. Complete: a real `Group` is created, members follow the leader (`Motion().SetFollow`). `DUNGEON_GROUP`.
2. **Gather / Travel**: `AdvanceRun()` drives the phases from facts read off the players. The leader walks to the entrance (goal tag `dungeon`); once all members are within 40 yd the group is teleported to the entrance's target (`DUNGEON_ENTER`). The quest AI stands back for group members (`BotDungeonRun::Busy`).
3. **Clear**: packs are built from the creature spawn data of the dungeon map (hostile, non-civilian, spawns within 14 yd merge, `INSTANCE_BIND` = boss, waypoint movement = patrol). `CheckReady` + `ChoosePull` pick the next pack, the leader walks to it and the other bots follow and fight with their normal combat AI. A pack is done when nothing alive of its entries is within 35 yd of the leader (or after `PackTimeoutSec`: `DUNGEON_PACK_SKIPPED`). Events: DUNGEON_PULL, DUNGEON_PACK_DONE, DUNGEON_PHASE.
4. **End**: Done or Aborted disbands the group, clears follow and teleports survivors still inside back to the entrance (`DUNGEON_DONE`, `DUNGEON_ABORTED`).

Known gaps: no tank or healer behavior (`Role` is only used to compose the group), mob levels are not known from spawn data (level filter off), durability is not read (repair never triggers), loot is whatever the combat AI takes, a wipe waits for the corpse runs and usually times out into Aborted, one run at a time, and the world thread reads creatures and players that map threads update.
