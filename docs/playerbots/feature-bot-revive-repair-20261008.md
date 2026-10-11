# Bot revives and repairs

Both parts are off by default. Settings are in `botserver.conf.dist` (`Bot.AI.Revive.*`, `Bot.AI.Repair.*`).

## Resurrecting group members (`Bot.AI.Revive.Enabled`)
- Strategy `revive` (NonCombat) for priest, paladin and shaman bots: Resurrection, Redemption, Ancestral Spirit (highest known rank). Druids are left out, Rebirth needs a reagent.
- Every rezzer in a group computes the same pick (`PickResurrector`: nearest able, ties by guid), so exactly one bot goes. It walks to the lowest-guid dead member (bot or player), casts out of combat, retries every `RetrySec`, gives up after `MaxAttempts`.
- Strategy `revive_accept` (Dead engine): a dead bot takes a resurrection offer `AcceptDelaySec` after it arrives.
- `release_spirit` is held for up to `WaitBeforeReleaseSec` while a rezzer is able to come; after that the existing corpse run / spirit healer code takes over unchanged.
- Logged as `REVIVE_APPROACH`, `REVIVE_CAST`, `REVIVE_CAST_FAILED`, `REVIVE_ACCEPTED`.

## Paid repair and potion restock (`Bot.AI.Repair.Enabled`)
- Called by the dungeon run when the group leaves Gather for Travel, when a member is below `Bot.AI.Dungeon.MinDurabilityPct` (the "repair" ready state used to stall with nothing fixing it), and when the run ends (replaces `Bot.AI.Dungeon.FreeRepair`).
- Repair costs the bot gold (`Paid`); a bot that cannot pay stays broken unless `FreeWhenBroke`. One visit spends at most `MaxSpendPct` of the bot's gold, repair first, then potions.
- Supplies: the best healing and mana potion rank the bot may use, up to `PotionsPerKind` each (mana only for mana users). The consumables strategy drinks them.
- There is no walk to a vendor, the visit is abstract.
- Logged as `REPAIR_PAID`, `REPAIR_FREE`, `REPAIR_TOO_POOR`, `SUPPLY_BOUGHT`.

## Code
`BotRevivePlan` / `BotRepairPlan` are pure and unit tested (`tests/game/BotRevivePlan.cpp`, `BotRepairPlan.cpp`); `BotRevive` / `BotRepair` are the game glue.

## Not verified
Compiled, and the plan tests pass. Nothing has run on a sim server: whether a cast at a released ghost is accepted, and the offer acceptance for bots, need a sim run.
