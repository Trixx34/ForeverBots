# 16. Porting the bot AI into the module: plan

Goal: the bot behaviour now on the `forever` branch (`src/server/game/Bots/`, 154 files, about 43,700 lines) runs as a
module, not in the core. The end goal: a bot levels from 1 to 60, and a five-bot party clears a Classic dungeon unattended.

## 1. What the module can and cannot do today

| Need | Today | Gap |
|------|-------|-----|
| Read the bot's state | Tier 2 snapshot: position, health, mana, combat target, nearby units, cooldowns (spell id and ms) | No quest log, inventory, equipment, spells known, talents, money, group, instance, combat state |
| Fight | `attack`, `stop`, `move`, `follow`, `cast` (spells the bot knows) | No cast result feedback, no spell range or power cost, no auto-attack or ranged-weapon control |
| Quests | none | accept, turn in, objective progress, quest giver list |
| Economy and gear | `SellItem` (module store only for rows) | buy, repair, equip, use item, loot a corpse or object, bank |
| Learning | none | trainers (spells, talents), class and skill trainers |
| Groups | none | invite, accept invite, leave, group members and roles, follow leader |
| Dungeons | none | enter instance (teleport or area trigger), instance lock state, boss and encounter state |
| Movement | `move` to a point (core paths) | travel between zones (flight, boats, transports), map and zone data |

Nothing in the table is missing from the core engine itself: each needs a core action or a snapshot field, plus a module
policy that uses it.

## 2. Architecture

- **Core**: executes primitives and reports facts. No strategy, no quest choice, no pull choice. It keeps the existing
  ownership and revoke rules.
- **Module**: all decisions. The forever code splits into pieces that already fit this:
  - pure logic, already unit-tested on the branch: `BotRotation` (rule conditions), `BotDungeonRun` (composition, pull
    choice, run state machine), `BotProgressRules` (rule table), `RoleProfile` design (class role split).
  - game-coupled logic: the combat engine (`BotCombat`), quest layer (`BotQuest`, `BotQuestAssist`), travel
    (`BotTravel`, `BotTravelPlan`), dungeon glue (`BotDungeonRun` game functions). Each gets rewritten against snapshot
    facts and the new actions.
- **Snapshot size**: each new fact costs region bytes for every bot. The layout moves to version 3, and the core sizes the
  region from the bot count. Facts that are not needed every tick (quest log, inventory, spells known) go in a slower
  "detail" snapshot, every N ticks, instead of every tick.

## 3. Milestones

Each milestone ends with a sim run, the same way the forever work was validated, and a written result.

| # | Milestone | Module work | Core work | Done when |
|---|-----------|-------------|-----------|-----------|
| M1 | Facts and feedback | combat state from snapshot | cast result per action (spell failed, out of range, no line of sight), spell range and power cost in snapshot, auto-attack control | a bot casts and reports each refusal reason |
| M2 | Combat | port `BotCombat` roles, `BotRotation` (pure, tested), flee and rest | target and threat facts (who attacks the bot) | 1 bot kills level-appropriate mobs in a sim, no over-pull, rest works |
| M3 | Equipment and vendor | policy for sell, repair, equip best (from item facts) | buy, repair, equip, use item actions; inventory and equipment in detail snapshot | a bot repairs, sells grey, equips a better weapon |
| M4 | Trainers and talents | learn spells and talents by level | trainer list and learn-spell action; talent learn action | a bot learns its class spells to level 10 from trainers |
| M5 | Quests 1-10 | port quest selection, accept, objective tracking, turn-in (`BotQuest`, pure parts first) | quest log, quest giver and taker actions, gossip select | a bot levels from 1 to 10 by questing, blocked reasons logged |
| M6 | Travel and zones | port `BotTravel` plan logic | flight paths, boats and transports used by the core's move when asked | a bot moves between two zones on the same continent |
| M7 | Leveling 1-60 | class and zone band selection (the progress rules, pure) | none new expected | each class reaches 60 in a sim run, timeline logged |
| M8 | Five-man group | group formation, role choice (`BotDungeonRun` pure), follow leader | invite, accept, leave, group members in snapshot | 5 bots form a group and stay together |
| M9 | Dungeon | run state machine, pull and tank logic from `BotDungeonRun` | instance entry, encounter state, lockout facts | a five-bot party clears Deadmines unattended |

M1 to M4 come first because each later milestone depends on them. M5 and M7 are the bulk of the leveling work and the
largest code port (`BotQuest` alone is 6,758 lines). M9 is the stated goal.

## 4. Decisions needed before coding

1. **Port or rewrite.** Copy the pure files from the forever branch with attribution, or write fresh against the module
   API? Copying is faster for `BotRotation`, `BotDungeonRun` pure code and `BotProgressRules`. Rewriting the game-coupled
   code is needed either way.
2. **First milestone.** M1 and M2 (combat first) can be tested with bots that already exist. Quests come later. Do you
   agree with this order?
3. **Snapshot layout 3.** The change is breaking: modules built against layout 2 and protocol 2 stop working. Is that
   acceptable now, while nothing is released?
4. **Dungeon target.** Deadmines first, as the forever docs say, or another Classic dungeon?
5. **Verification.** Which sim run do we use for each milestone: the 180-bot sim from the forever docs, or a smaller
   party on your Windows server?

## 5. Risks

- The forever AI has not been verified end to end (progress rules and dungeon runs were never run on a server). Porting it
  copies untested behaviour. Each milestone needs its own sim check before the next starts.
- Snapshot size grows with the bot count. The region is fixed at handshake, so bot caps must be set with the snapshot size
  in mind.
- Blocking calls on the world thread (database, pathing) must stay out of the action path. Pathing queries are already
  done by the core's `MotionMaster`; keep it that way.
