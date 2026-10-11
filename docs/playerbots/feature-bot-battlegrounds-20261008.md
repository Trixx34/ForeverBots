# Bot battlegrounds: bots queue next to players and play Warsong Gulch

Off unless `Bot.AI.Battleground.Enabled = 1`. Settings are in `botserver.conf.dist` (`Bot.AI.Battleground.*`), log tags `BG_*` (category `battleground`).

## What a player sees
1. A player queues for Warsong Gulch. After `JoinDelaySec` bots of both factions queue too, a few per `QueueIntervalSec`, until each side has the battleground's minimum (`FillTo` raises it).
2. When the invitation comes, each bot accepts after its own delay (`AcceptMinSec`..`AcceptMaxSec`).
3. Inside, each side's bots get a job by their rank: attackers (`OffensePct`), defenders (`DefensePct`), roamers (the rest). A side always has an attacker.
4. Bots leave after the match (`LeaveMinSec`..`LeaveMaxSec`), or when no player is left in the battleground (`EmptyLeaveSec`). Bots that queued stay in the queue only while a player waits (`IdleDropSec`), so bots never start a match among themselves.

## Play (Warsong Gulch)
`BotBattlegroundPlan` decides, `BotBattleground` carries it out every `StepMs`:
- Gates closed or dead: hold (ghosts register with the area spirit healer and are raised in its waves).
- Carrier: run to the own flag room and score (the capture trigger is server side, so walking in is enough); wait there while the own flag is away.
- Own flag on the ground: touch it to return it (defenders from three times `ReturnYards`).
- Enemy flag on the ground: attackers and roamers pick it up.
- Own flag taken: hunt the carrier (the bot knows where he is, like the minimap does for a player).
- A side member carries: attackers and roamers escort, defenders stay.
- Otherwise attackers take the enemy flag from its stand, defenders stand in a ring around their flag room, roamers go to the middle.
- Enemy players within `EngageYards` are attacked (`Player::Attack` puts the bot into the Combat engine). Carriers and bots about to touch a flag fight only what is almost on them.

Changes outside the new files: `BotCombat` now also takes players that fight the bot (PvP combat references) as opponents; the three ghost actions of the Dead engine stand back inside a battleground; the quest, party and dungeon modules treat a bot inside a battleground as busy.

## How the others would follow
The decision part is generic: orders, roles and queue fill do not mention Warsong Gulch. A second battleground needs a table entry (queue id, flag or node objects) and its own order set:
- Arathi Basin and Eye of the Storm: nodes instead of flags. New orders `CaptureNode` and `DefendNode`, roles split bots over the nodes by rank; the same queue and invitation code applies.
- Alterac Valley: large objectives (towers, bunkers, bosses) with a march order for the whole side; needs the group-roles work for raid sizes.
- The queue code takes the queue type from one function today (`QueueId()`); a list of enabled battlegrounds would replace it.

## Not covered / unverified
- Nothing ran on a sim server (no database or game data here). Compiled and the plan unit tested only.
- The bot session needs the RBAC permission to join normal battlegrounds; if not, the first refused join is logged as `BG_QUEUE_FAILED`.
- The flag objects are found by entry (227740/227741) in the loaded grids; the flag stand coordinates are the fallback and were written from memory of the map, check them on the sim.
- A carrier that is attacked is taken into the Combat engine and stops running; no kiting, no stealth or mounts, no use of consumables or the battleground buffs, no balance of classes between the sides.
- The shared Combat engine still flees from "too strong" opponents by level; inside a battleground the levels are close, but it is PvE logic.
- Other battlegrounds, arenas and the random battleground queue are not touched.
