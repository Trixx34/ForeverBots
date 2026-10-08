# Group loot rolls for bots (gap: group loot)

Bots have no client, so the roll window of a group loot item never gets an answer from them: a roll with a bot in it runs into the 60 s
timeout and the bot counts as passing. With `Bot.Loot.Roll.Enabled = 1` the bot votes the moment the roll starts.

- Hook: `LootRoll::TryToStart` (Loot.cpp) calls `BotLootRoll::ChooseVote` for each looter right after the roll was sent. The function returns
  -1 (no vote) for real players and while the feature is off, so the core behaves as before.
- Choice (`BotLootRollPlan.cpp`, unit tested): full bags pass; items for an active quest and usable upgrades (the class-role gear score of
  `BotGear.h`, same rule as quest rewards) get Need when the roll offers it, else Greed; everything else gets Greed
  (`Bot.Loot.Roll.GreedOnOther`, default on, the bot sells it) or Pass.
- The vote goes through the normal `LootRoll::PlayerVote`, so the roll message, the 1-100 number and the winner announcement are the core's.

Not done: Disenchant votes, a vote that depends on the group (another bot that needs the item more), master loot and round robin (the core
handles those without a window).
