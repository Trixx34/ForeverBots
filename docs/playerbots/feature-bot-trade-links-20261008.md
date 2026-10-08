# Trading with bots by linking items in party chat (2026-10-08)

Requested by **Trixx34**. Off by default (`Bot.Trade.Link.Enabled = 0`).

## What it does

A player says one or more item links in **party chat**. For every linked item, the bot of the group that carries the most of it
(tradable stacks only) proposes a trade to the player and puts the item in the window. The player opens the proposal and presses
accept; the bot accepts when the player does (`BotSocial::OnTradePlayerAccepted`, which now also accepts a bot-only offer).

- Several links in one message: every item is handled. Items held by the same bot share one window (six traded slots per window,
  the rest goes into a further window of that bot). Windows open one after the other: the next one starts after the previous
  trade completed. Cancelling or timing out a window drops the rest of the request.
- One slot per linked item: the largest tradable stack of it.
- Only bots of the speaker's own group respond, alt bots only for their owner account (same rule as every alt bot trade), the bot
  must be alive, out of combat, not trading, and within `Bot.Trade.Link.MaxYards` (core limit: trade distance 11.11).
- Skipped silently by design: equipped items, soulbound and quest items (`Item::CanBeTraded`, quest class / starts a quest, lootable
  containers, items bound to someone else). The speaker gets a system message for a link nobody can give.

## Where it hooks

| Where | What |
|---|---|
| `ChatHandler.cpp` CHAT_MSG_PARTY | `BotTradeLink::OnPartyChat` queues trades (planning only, nothing opens inside the chat handler) |
| `BotSocial::Update` | `BotTradeLink::Update` opens the front trade of each request, times out open ones |
| `TradeHandler.cpp` HandleBeginTradeOpcode | `OnTradeBegun`: the player opened a bot's proposal, the bot sets its items through its own `HandleSetTradeItemOpcode` |
| `BotSocial::OnTradeExecuting` | marks the trade as completed so the next window may open |

Pure logic (tested): `ParseItemLinks`, `PlanTrades` in `BotTradeLink.h`. Tests: `tests/game/BotTradeLink.cpp`.

## Config

`Bot.Trade.Link.Enabled` (0), `.MaxItems` (12 links per message), `.MaxTrades` (3 windows per message), `.MaxYards` (10),
`.TimeoutSeconds` (60). Requires `Bot.Enabled`. See `worldserver.conf.dist`.

## Not verified

Built and unit tested in a container without game data; the live flow (proposal, window, accept) was not run on a sim server.
