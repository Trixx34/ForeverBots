# Bot dungeon runs: what a group needs to finish a dungeon alone

Builds on the run glue of `BotDungeonRun` (group formation, entrance, teleport in, pack by pack), the roles and group combat of #17 / #18. Off unless `Bot.AI.Dungeon.Enabled = 1`.

## Added
- **Dungeon table** (`BotDungeonData`): the 14 Classic five-man dungeons with map id, level range and final boss. `Bot.AI.Dungeon.Map = 0` picks the dungeon that fits the middle level of the online bots (raise `Bot.AI.Dungeon.MaxLevel`); the run ends when the final boss pack is done, not only when every pack is.
- **Pack clustering** (`ClusterPacks`, pure and tested): spawns within `PackLinkYards` (3D, chained) form a pack, big clusters without a boss are cut up (`PackMaxMobs`), numbering does not depend on spawn order. Replaces the greedy 2D grouping.
- **Tank pulls**: the group's tank (not the leader) walks into the pack, everybody else follows the tank, so the hold-fire / taunt logic of #18 decides who opens.
- **Raise after a fight**: dead members come back next to the tank `ResurrectDelaySec` after the fight; a wipe brings everybody back at the entrance and the pull is retried (counts against `MaxWipes`).
- **Durability**: the lowest worn item is read into the ready check (`MinDurabilityPct`) and gear is repaired when the run ends (`FreeRepair`).

## Not covered yet
- Mob levels: creature templates here scale with content tuning, so a mob's level is taken from the dungeon's range (middle, +1 elite, +2 boss).
- No threat table (taunt and hold-fire only), no crowd control, no group loot rules, no player-led runs.
- Warrior Defensive Stance / druid Bear Form belong on the spell thread's stance mechanism.
- Nothing here has run on a sim server.
