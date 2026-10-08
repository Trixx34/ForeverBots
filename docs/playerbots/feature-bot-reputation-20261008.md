# Bot reputation

Bots weigh reputation rewards when they pick a quest. Off by default (`Bot.Quest.Reputation.Enabled`).

- The quest score in `BotQuest.cpp` (`Score`) is multiplied by `BotReputation::QuestWeight` (`BotReputationPlan.cpp`, unit tested).
- Points per reward are the base value the core hands out (`Player::RewardReputation`, without rate bonuses); rewards of the opposing side are ignored, as the core does.
- Gain: up to `BonusPct` (25) percent higher score for a reward of `FullPoints` (250) or more, in proportion below that. Counts half once Revered, nothing at Exalted or at the quest's cap rank.
- Loss: up to `LossPenaltyPct` (40) percent lower score. A quest that would push a faction from above Unfriendly to Hostile or worse scores 0, so a bot does not make a city's guards hostile. A bot already there has nothing more to lose and is not penalised.
- No new log lines; the choice only shifts the existing quest selection scores.

Not done: tabard grinding, reputation vendors (buying gated items), choosing a faction to work on, rep from kills.
