# Duels

Players can challenge bots to duels with the normal Duel spell, ask a bot for one by whisper, and (optionally) watch bots duel each
other in cities. Everything is off by default (`Bot.Duel.Enabled`); the settings are in `botserver.conf.dist`.

## What happens

- **Challenge from a player.** The core creates the duel flag as usual and calls `BotDuel::OnChallenged`. The bot decides at once
  (dead, already dueling, in combat, busy: group / instance / flight / battleground, just fought, hurt, level gap, then the
  `AcceptPct` roll) but answers only after a pause of `ReplyMinMs`..`ReplyMaxMs`. Meanwhile it stands still facing the challenger. It
  "presses" accept or cancel through the session handlers (`HandleDuelAccepted` / `HandleDuelCancelled`) and says a line; a refusal is
  whispered with the reason. An alt bot challenged by its owner skips the chance, cooldown and level checks.
- **Countdown and fight.** The bot holds still during the countdown. When the duel is in progress the bot goes to the combat engine
  (`DesiredState` cause `DUEL`) and `PickTarget` returns the duel opponent, so the normal class rotations fight it. With
  `YieldPct` > 0 a bot fighting a player forfeits when it is that hurt.
- **Result.** After 1 to 3 seconds the winner cheers, bows or salutes, the loser bows, salutes or cries, each with a line.
- **Whisper.** A short whisper with "duel", "spar", "1v1" or "fight me" (and no "no" / "not" / "don't") makes the bot walk up
  (tag `duel`) and cast the challenge from `Challenge.Yards`. Far away (60+ yards) or refusing, it whispers why.
- **Bot against bot.** `BotVsBot.Enabled`: a free bot in a city looks for a partner every `IntervalSec` (with `ChancePct`), only while a
  real player is within `WatchYards` (`NeedWatcher`). The partner is the closest free bot within `SearchYards` that passes the same
  mood checks. A duel that lasts `TimeoutSec` ends, the bot that notices it first forfeits.

## Log reasons (category social)

`DUEL_CHALLENGED`, `DUEL_ACCEPTED`, `DUEL_DECLINED_{DEAD,DUELING,COMBAT,BUSY,HURT,LEVEL,COOLDOWN,CHANCE}`, `DUEL_FIGHT_START`,
`DUEL_WON`, `DUEL_LOST`, `DUEL_CANCELLED`, `DUEL_YIELD`, `DUEL_TIMEOUT`, `DUEL_REQUESTED`, `DUEL_CHALLENGE_SENT`, `DUEL_TARGET`
(combat decision), state change cause `DUEL`.

## Code

- `BotDuelPlan.h/.cpp`: pure decisions (`Decide`, `FightStep`, `PickPartner`, `WantsSparring`, `IsDuelRequest`, lines, emotes). Tests in
  `tests/game/BotDuelPlan.cpp`.
- `BotDuel.h/.cpp`: hooks (`SpellEffects.cpp` EffectDuel, `Player::DuelComplete`, `ChatHandler.cpp` whisper), strategy `duel`
  (`duel_due` -> `duel_tick`), config.
- `BotAI.cpp` (state + default strategy), `BotCombat.cpp` (`PickTarget`).

## Not verified yet

Nothing has run on a sim server. Look at: whether the bot casts well against a player (rotations were written for mobs), whether
`HandleDuelAccepted` from a bot session starts the countdown on the player's client, whether duel areas in the cities allow dueling,
the walk-up for whispers, and the tone of the lines.
