# Player-led dungeon runs (2026-10-08)

Requested by Trixx34. A real player leads a group of bots through a five-man dungeon. Switch: `Bot.AI.Dungeon.PlayerLed.Enabled` (default **off**, options in `worldserver.conf.dist`). It is independent of `Bot.AI.Dungeon.Enabled`, which lets bots lead their own runs; a group led by a player is never touched by `BotDungeonRun` (it only forms groups of bots without a group).

## How a run goes

1. The player invites bots (they accept as before) and orders `follow` in party chat. Without `follow` a bot is not part of the run: a bot ordered to `stay` stays outside.
2. The player walks through the dungeon entrance. `follow` only works on one map, so the bots would be left at the door. After `EnterDelaySec` every following bot that is alive and not fighting is teleported next to the player (same instance, through the group binding), spread on a ring so they do not share one point. Log row `DUNGEON_PL_ENTER`.
3. Inside, the existing group play of the bots takes over: follow, hold fire and taunt (roles), threat awareness, crowd control, chat orders such as `pull` and `heal`. Nothing of that is changed here.
4. A bot more than `CatchUpYards` from the player for `CatchUpSec` (stuck, lost) while nobody is fighting is moved next to the player (`DUNGEON_PL_CATCHUP`).
5. A dead bot is raised `RaiseDelaySec` after the fight (player alive, nobody fighting) at 50 percent health next to the player (`DUNGEON_PL_RAISED`). Bots that died outside while the player is inside are raised the same way. After a wipe nothing happens until the player is alive again.
6. The player is told, out of combat, which bots are not ready for the next pull: dead, away (not within 40 yards), health or mana below the `Bot.AI.Dungeon.Min*Pct` limits (healers use their own mana limit), and again when everybody is ready (`DUNGEON_PL_REST`, `DUNGEON_PL_READY`; `Notify` also sends it as a system message). The bots eat and drink through the rest strategy as before; the notice is information, the player decides when to pull.
7. When the player leaves the dungeon for a world map, bots still inside follow out (`DUNGEON_PL_LEAVE`). Battlegrounds, arenas and raids are never touched.

While a following bot is in or on its way into a dungeon, `BotDungeonRun::Busy` is true for it, so the quest AI stands back.

## Design

* `BotPlayerLedPlan.h/.cpp`: pure decisions over plain data. `Decide` (one answer per bot: none, enter, leave, catch up, raise, with a reason), `Spread`, `ListNeeds` (reuses `BotDungeon::MemberState` and `ReadyConfig`) and `NoticeDecision`. Tests: `tests/game/BotPlayerLedPlan.cpp` (`[BotPlayerLed]`).
* `BotPlayerLed.h/.cpp`: world thread glue called from `BotMgr::Update`: finds groups whose leader is a connected non-bot player, reads facts, applies the answer (teleport, resurrect, log, notice). Per-bot and per-leader timers are dropped when a bot or leader is no longer seen.
* Cooldowns: no move or raise of one bot within `ActCooldownSec`; the leader must have been on its map `EnterDelaySec` before anything happens (map load, stepping back out).

## Not covered

* Loot rules, repair and vendoring between pulls, the player's pull timing are the player's call; no automatic stay-behind of bots that rest while the player walks on.
* A bot in another instance of the same dungeon is left alone.
* Raised bots come back without a healer's spell; a real resurrection by a bot healer is not modelled.
* Nothing here has run on a sim server. First things to check with the switch on: a group of 4 bots on `follow` entering Deadmines (`DUNGEON_PL_ENTER` rows, bots next to the player inside), a bot lost far behind (`DUNGEON_PL_CATCHUP`), a death in a fight (`DUNGEON_PL_RAISED` after the delay, not during combat), and the resting notice between pulls.
