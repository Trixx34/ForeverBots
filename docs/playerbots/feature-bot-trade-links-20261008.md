# Trading with bots by whisper (2026-10-08)

Requested by **Trixx34**. Off by default (`Bot.Trade.Link.Enabled = 0`).

## What it does

1. The player opens a trade with a bot of their group. The window opens as for any bot trade.
2. The bot whispers the items it can give, as item links with their stack size (best quality and largest stacks first, at most
   `Bot.Trade.Link.MaxListed`).
3. The player whispers the bot the link(s) of the items they want. Every linked item goes into the bot's side of the window
   (the largest tradable stack of it, one slot each, at most six). Links already in the window are ignored; the bot whispers back
   when it has no such item or the window is full. Whispering again adds more.
4. The player confirms; the bot accepts when the player does (`BotSocial::OnTradePlayerAccepted`, which now also accepts a
   bot-only offer). The existing trade timeout (`Bot.Trade.TimeoutSeconds`) cancels windows left open.

Rules: alt bots trade only with their owner account (unchanged). With this switch on, world bots of the player's own group may
trade too; they refuse everyone else as before. Equipped items, soulbound items, quest items, lootable containers and items bound
to someone else are never listed or given. A world bot also accepts a gift in such a window, like an alt bot does.

## Where it hooks

| Where | What |
|---|---|
| `BotSocial::OnTradeInitiated` | allows group world bots when enabled; after the window opened, `BotTradeLink::OnTradeOpened` whispers the list |
| `ChatHandler.cpp` CHAT_MSG_WHISPER | `BotTradeLink::OnWhisper`: links whispered to a bot in a trade with the sender go into the window through the bot's own `HandleSetTradeItemOpcode` |
| `BotSocial::OnTradePlayerAccepted` | an empty player offer is fine when the bot put items in |

Pure logic (tested): `ParseItemLinks`, `SortForListing`, `PackLines`, `PlanOffer` in `BotTradeLink.h`.
Tests: `tests/game/BotTradeLink.cpp`.

## Config

`Bot.Trade.Link.Enabled` (0), `Bot.Trade.Link.MaxListed` (15). Requires `Bot.Enabled` and `Bot.Trade.Enabled`.

## Not verified

Built and unit tested in a container without game data; the live flow (window, whispers, accept) was not run on a sim server.
