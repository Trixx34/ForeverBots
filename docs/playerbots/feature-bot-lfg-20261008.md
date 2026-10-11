# Looking for group (2026-10-08)

Branch `claude/bot-lfg-7m0rdi`, based on the `botserver.conf` branch (PR #50) so the options sit in `botserver.conf.dist`. With `Bot.LFG.Enabled = 1` a player never has to wait for a full party: they ask a bot, free bots join. Default **off**.

## Using it

* Whisper `lfg` to any bot, or say `lfg` in party chat as the leader. `lfg tank|healer|dps` names the role the player plays (default: the class's preferred role; a class that cannot take the role is refused). `lfg status` shows the search, `lfg off` cancels it and the bots leave.
* Works for a solo player (a group is created) or for the leader of a party with room (only the open places are filled, counting everybody already in it). Not in a raid group, a battleground or a raid instance. In a five-man dungeon the bots are moved next to the player and follow through `BotPlayerLed`.
* The player sees system lines: who joined and in which role, what is still missing, "your group is full", "no more bots found" on timeout (`Bot.LFG.Notify`).

## Rules (`BotLfgPlan.h/.cpp`, pure, tag `[BotLfg]`)

* **Places.** One tank and one healer unless the group already has one, damage dealers for the rest of `GroupSize`. Extra tanks or healers take a damage place.
* **Who.** Free bots of the player's team within `MaxLevelSpread` levels (and at least `MinLevel`). Tanks are picked first, then healers, then damage. Inside a role a class that prefers it comes first (warrior before paladin as tank, priest before shaman as healer), then the nearer bot, then the lower guid. A bot is used once. Without `Teleport` only bots on the player's map within `MaxDistance`.
* **Life of a search (`Evaluate`).** `full` when nothing is open, `timeout` after `TimeoutSec` (the bots found stay), bots are released when the player is offline for `ReleaseSec` (`offline`) or stops being able to lead (`not_leader`: joined another group, raid, battleground).

## Glue (`BotLfg.h/.cpp`, world thread)

* Chat hooks in `ChatHandler.cpp` (whisper to a bot, party chat); `BotLfg::Update` runs from `BotMgr::Update` every `IntervalSec`.
* A candidate bot is alive, out of combat, not flying or teleporting, ungrouped, in the open world, not an active alt, and not held by a dungeon run, trip, taming run, bot party or player-led run.
* A found bot joins the player's `Group` (created if the player has none), follows the player, is moved next to them (`Teleport`) and is marked busy (`BotLfg::Busy`: the quest AI stands back with `why = lfg`, the watchdog leaves it alone). A bot that leaves or is kicked is forgotten; `lfg off`, release and timeouts remove only bots this search brought.
* Events: `LFG_QUEUED`, `LFG_JOINED`, `LFG_COMPLETE`, `LFG_TIMEOUT`, `LFG_RELEASED`, `LFG_GONE` (log category dungeon).

## Limits

* No sim run yet. `BotLfg.cpp`, `ChatHandler.cpp`, `BotQuest.cpp`, `BotWatchdog.cpp` and `BotMgr.cpp` compile in the container; the plan tests pass (standalone build, 120 assertions).
* Bots found are not asked to agree and have no say in the role: a paladin may be called as tank or healer, whichever place is open.
* The search does not top the group up again after it ended; whisper `lfg` again.
* Bots do not walk to the dungeon entrance together: the player leads, as in a player-led run.

## Tests

`bin/tests "[BotLfg]"`: request parsing, open places, filling (roles, one use per bot, determinism), eligibility (level window, minimum level, team, classes, teleport/distance), preference order, life of a search, texts.
