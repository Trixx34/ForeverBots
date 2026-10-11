# Bot conversations (2026-10-08)

Requested by **Trixx34**. Bots answer whispers and party chat in a voice of their own instead of staying silent.

## Behaviour

- **Whisper a bot**: it answers after a short typing delay. A topic it does not know gets the "did not catch that" line of its personality.
- **Party chat**: a bot whose name is in the message answers (at most two bots per message). If no bot is named and the message is a greeting, goodbye, thanks, joke, compliment or "ready", one bot of the party answers with chance `Bot.Conversation.Party.ChatterPct`. In a raid only the subgroup of the speaker is considered, matching who can read the message.
- **Personalities**: friendly, cheerful, grumpy, stoic, sarcastic, nerdy. Derived from the bot guid (stable across sessions, no database), or one personality for all bots (`Bot.Conversation.Personality`).
- **Topics**: greeting, farewell, thanks, how are you, what are you doing, where are we, who are you (class and level), help, compliment, insult, joke, sorry, ready, wait. Lines can mention the player, the bot's class, level, zone, health and what it is doing (dead, fighting, catching its breath, tagging along).
- **Commands keep working**: a message that is command-shaped (`follow`, `g1 stay`, `tank pull`, ...) is never answered as conversation, even from a player who may not give the command. Messages with item/quest links are ignored (trade links read those).
- **Pacing**: one reply in flight per bot, a per-bot cooldown, a cap of 64 queued replies, typing delay of `MinMs` + 30 ms per character + up to 500 ms jitter, capped by `MaxMs`.
- Local only: keyword matching and line templates. No LLM, no network, no database access.

## Code

| File | Role |
|---|---|
| `BotConversationText.{h,cpp}` | Pure: `Classify`, `Addresses`, `Reply`, personalities, `TypingDelayMs`. No game types, unit tested. |
| `BotConversation.{h,cpp}` | Game side: `OnWhisper`, `OnPartyChat` (queue a reply), `Update` (send due replies). |
| `ChatHandler.cpp` | One added call after the existing bot hook in the whisper case and in the party case. |
| `BotMgr.cpp` | `BotConversation::Update` in `BotMgr::Update`. |
| `BotLogCategory.h` | `CONV_` prefix and `conversation` type map to the `social` log category. |
| `tests/game/BotConversationText.cpp` | Catch2 tests. |

Each reply writes a `conversation` bot_event (reason `CONV_REPLY`, category social) with player, channel, personality and topic, unless `Bot.Conversation.LogEvents = 0`.

## Config (botserver.conf.dist, section CONVERSATIONS, off by default)

`Bot.Conversation.Enabled` (0), `.Whisper.Enabled` (1), `.Party.Enabled` (1), `.Party.ChatterPct` (35), `.Personality` (random), `.Delay.MinMs` (800), `.Delay.MaxMs` (3500), `.CooldownSec` (4), `.LogEvents` (1). Read once at startup.

## Not verified

No sim server has run this. Compiled: the new files and the two touched files in the `game` target. The text half is covered by Catch2 (run standalone, all pass). `BotMgr.cpp` does not compile without the core precompiled header in a no-PCH build (`Group` incomplete at line 1434); that predates this change.

## Ideas not done

Topics from game state (quest progress, loot, low mana), per-bot memory of the player, raid chat, remembering a player's name for the bot, locale lines.
