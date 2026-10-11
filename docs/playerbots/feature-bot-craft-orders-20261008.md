# Crafted goods for players (2026-10-08)

Requested by **Trixx34**. Off by default (`Bot.Craft.Enabled = 0`).

## What it does

1. The player whispers a bot of their group `craft <item link or name> [x5]`. `craft list` lists what the bot can make right now.
2. The bot looks through the spells it knows in its crafting professions (not gathering) for one that creates the item. A link
   matches by item entry, a name exactly, then as a prefix, then as a substring.
3. It checks its own reagents. Without enough, it whispers what one cast needs as links, so the player can trade the materials
   to it (the trade-link feature, or any bot trade). Otherwise it whispers what it will make.
4. The bot casts the recipe one cast at a time, standing still and out of combat (it waits otherwise, up to a deadline).
5. It mails exactly the crafted items to the player (new stacks are created, so earlier stock of the bot is never given away) and
   whispers the result. With `Bot.Craft.Fee.VendorPercent` above 0 the mail is cash on delivery.

Limits: one order per bot at a time; at most `Bot.Craft.MaxItems` items and 12 mail attachment slots per order; only bots of the
player's own group listen. Enchants are not covered (bots have no enchanting profession in the profession plan). A recipe that needs a
forge, anvil or fire stops the order with a whisper when the bot is not next to one; the bot does not walk to one.

## Where it hooks

| Where | What |
|---|---|
| `ChatHandler.cpp` CHAT_MSG_WHISPER | `BotCraftOrder::OnWhisper` |
| `BotMgr::Update` | `BotCraftOrder::Update` runs the queue |

Pure logic (tested): `ParseRequest`, `Match`, `PlanOrder`, `MailStacks`, `Fee`, `FormatMoney`. Tests: `tests/game/BotCraftOrder.cpp`.
Log codes: `CRAFT_QUEUED`, `CRAFT_DONE`, `CRAFT_FAIL` (economy category).

## Config (botserver.conf.dist)

`Bot.Craft.Enabled` (0), `Bot.Craft.MaxItems` (20), `Bot.Craft.MaxListed` (15), `Bot.Craft.Fee.VendorPercent` (0).

## Not verified

Built and unit tested in a container without game data; the recipe scan, the casts and the mail were not run on a sim server. The
yield of a cast comes from the spell effect value (at least 1).
