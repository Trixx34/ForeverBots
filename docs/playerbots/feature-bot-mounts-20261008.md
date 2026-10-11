# Bot mounts (Bot.AI.Mount.*)

Requested by **Trixx34**. Default off. Settings: the "BOT MOUNTS" section of `src/server/worldserver/botserver.conf.dist`.

## What players see

- A bot that follows a player mounts a few seconds after the player does and keeps up at the player's pace. The mount is the slowest
  ground mount the bot knows that is at least as fast as the leader's (the leader's run speed rate is read), else its fastest one.
- A bot that fell more than `CatchUpYards` behind a leader on foot for `CatchUpSec` rides to catch up and gets off once it is within
  `DismountYards` of the leader and the leader has been on foot for `DismountDelaySec`.
- Every ride ends when the bot or its leader enters a fight.
- The existing chat orders `mount` and `dismount` (Bot.Chat.Orders.Enabled) use the same mount choice. A ride ordered in chat is not
  ended by the leader dismounting; it ends on `dismount` or a fight.
- Not tried in dungeons, battlegrounds, indoors, in water or in a shapeshift form that forbids mounts. A refused mount is retried after
  `RetrySec`. Flying mounts are never used (the walking planner has no flight).

## Code

- `BotMountPlan.h/.cpp`: pure decision (`Decide`), mount choice (`PickMatching`), speed rate to percent. Unit tested in
  `tests/game/BotMountPlan.cpp`.
- `BotMount.h/.cpp`: strategy "mount" (NonCombat and Combat engines, trigger every 500 ms), per-bot timers, `MountUp` shared with the
  chat order, config.
- `BotChat.cpp`: the `mount` order calls `BotMount::MountUp` and tells `NoteOrder`.
- `BotAI.cpp`: adds the strategy to the default list when enabled. `BotLogCategory.h`: `MOUNT_` logs are movement.

Log tags: `MOUNT_UP` (reason leader_mounted or catch_up), `MOUNT_DOWN` (combat, leader_on_foot, no_leader), `MOUNT_REFUSED`.

## Not verified

Written without a sim server: the glue was not run. Things to look at first on the sim: the cast of a mount while the follow leg
stops, the leader's run speed rate on a mount (a mount aura is read as percent), and that the combat engine really runs the strategy's
dismount at the start of a fight.
