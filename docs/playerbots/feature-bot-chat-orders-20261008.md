# Bot chat orders: stop, aggressive, passive, pull, heal, mount, dismount, summon, revive (2026-10-08)

Requested by Trixx34. Extends the chat commands of step A2 (`BotChat.cpp`, see the header of `BotChat.h` and the `Bot.Chat.*` block of `botserver.conf.dist`). Switch: `Bot.Chat.Orders.Enabled` (default **off**). With it off the new words are ordinary chat and nothing changes.

`follow` and `release` already existed and are unchanged; the other verbs of the request are new. Syntax and authorization are the existing ones: `[selector] verb`, from the group leader, in party chat, raid chat or a whisper to one bot. None of the new verbs takes arguments (`pull now` is refused with `BAD_ARGS`). The reply codes below show in `verbose on` and in the `chat_command` log rows.

## Verbs

| Verb | Who acts | What happens | Refusal codes |
|---|---|---|---|
| `stop` | any bot | Drops the follow target, the goto goal and the `goto` strategy. Out of combat the bot also stops moving and attacking. A bot already in a fight keeps fighting (use `passive` for that). | `DEAD` |
| `aggressive` | any bot | Clears `passive`. This is the default. | none |
| `passive` | any bot | The Combat engine only fights mobs that are attacking this very bot. Healing and self-preservation (flee, eat) are unchanged. Stays until `aggressive` or the bot logs out. | none |
| `pull` | tank classes (`Bot.Chat.Role.Tank`, default warrior) | Attacks the leader's selected target with the same opening as the quest pull (attack, engage, set in combat); the Combat engine takes over on the next tick. | `NOT_TANK`, `DEAD`, `NO_TARGET`, `NOT_HOSTILE`, `DIFFERENT_MAP`, `IN_COMBAT`, `OUT_OF_RANGE` (`Bot.Chat.Orders.PullRangeYards`, 60) |
| `heal` | healer classes (`Bot.Chat.Role.Healer`, default priest) | Casts the bot's heal from the combat spell table at the leader's selected target. | `NOT_HEALER`, `DEAD`, `NO_TARGET`, `TARGET_DEAD`, `NOT_FRIENDLY`, `DIFFERENT_MAP`, `OUT_OF_RANGE`, `NO_HEAL_SPELL`, `NO_LOS`, `NO_POWER`, `COOLDOWN`, `BUSY`, `CAST_FAILED` |
| `mount` | any bot | Casts the fastest learned ground mount spell (a spell with the mounted aura; flying mounts are skipped). Stops the follow or goto leg first, and the follow action does not start a new leg while a spell is being cast (otherwise the movement would interrupt the cast). | `DEAD`, `ALREADY_MOUNTED`, `IN_COMBAT`, `NO_MOUNT`, `CANT_MOUNT` (indoors, no skill) |
| `dismount` | any bot | Removes the mounted aura. | `DEAD`, `NOT_MOUNTED` |
| `summon` | living bots | Teleports the bot next to the leader (same spiral spread as `goto here`), on the same map or another one. The bot's goal is dropped, follow stays. | `DEAD`, `ISSUER_DEAD`, `ISSUER_IN_COMBAT`, `IN_COMBAT`, `INSTANCE` (across maps when a dungeon, raid or battleground is involved), `ALREADY_HERE` (under 5 yards) |
| `revive` | dead bots (corpse or ghost) | Resurrects the bot at `Bot.Chat.Orders.ReviveHealthPct` (35) health, with resurrection sickness when `Bot.Chat.Orders.ReviveSickness` is on, then moves it next to the leader like `summon`. It is the free alternative to `release` plus the corpse run. | `NOT_DEAD`, `ISSUER_DEAD`, `ISSUER_IN_COMBAT`, `INSTANCE`, `HARDCORE` (`Classic.Hardcore` realms) |

The selector decides who is addressed, the role gate decides who may act: `all pull` makes the warriors pull and answers `NOT_TANK` for the rest, `tank pull` and `warrior pull` address only the tanks. `healer heal` likewise.

## Design notes

* The pure decisions are in `BotChat.h/.cpp` and unit tested in `tests/game/BotChatParse.cpp` (`[BotChat]`): `ParseVerb`, `ValidateOrderArgs`, `RoleGate`, `SpreadOffset` (also used by `goto here`, same numbers as before), `SummonCheck` (summon and revive rules) and `PickMount`.
* The world-state glue stays in `BotChat::Exec`. Commands run on the world thread outside map updates, like the existing verbs; the only state the map thread reads is the new `BotAI::IsPassive()` atomic.
* `heal` goes through `BotCombatHealUnit` (BotCombat.cpp): it resolves the spell table in a throwaway context, so it works out of combat and does not touch the fight state.
* **Tank pull and the dungeon run.** PR #20 (dungeon runs, stacked on #18) adds a tank puller for the dungeon plan. This PR is based on `forever`, which has none of it, so `pull` has its own small implementation in `BotChat::Exec`. Once #18/#20 are in, `case Verb::Pull` is the single place to route into their puller. `heal` and the group heal triage of PR #17 are independent: triage picks who to heal on its own while in combat, `heal` is an explicit order that works any time.
* **Item links.** Parsing item links from party chat is the trading thread's work and is not touched here.

## Limits

* `passive` only affects the Combat engine's target choice. A bot that has the quest strategy still pulls for its quest; party bots on `follow` do not.
* `mount` uses learned spells only, not mount items in the bags. A bot below the riding level has none (`NO_MOUNT`).
* `revive` does not use resurrection spells and does not make healers raise people (the dungeon run's raise after fights, PR #20, covers that). It is deliberately a convenience order and is refused on hardcore realms.
* Common words (`pull`, `stop`, `heal`) typed by a group leader whose group has bots are commands once the switch is on, by design. Non-leaders are ignored and logged as before.

## Verification

`BotChat` tests: see the PR description for the run. Not run on a server or in a sim: the pull opening, the heal cast, the mount cast and the teleport/resurrect sequence of `summon` and `revive` need a live server. First things to check with the switch on: `chat_command` rows with outcome per verb, and for `revive` that the bot is alive and standing next to the leader, `bot_recover` state reset.
