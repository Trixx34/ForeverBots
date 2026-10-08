# Town idling

Bots that have nothing to do used to stand on the spot or take small steps (`natural_idle`, `Bot.AI.Move.Natural.Idle`). In a
capital that looks wrong: a crowd of bots frozen in a ring. Town idling builds on that idle step. When the bot is inside a city or an
inn (the rest flags `REST_FLAG_IN_CITY` / `REST_FLAG_IN_TAVERN`), the idle step hands over to the town behavior; outside towns the
old idling is unchanged.

## What a bot does

- **Gathering places.** Scanned from the creatures and mailboxes within `SearchYards` and cached for a minute: innkeepers, bankers,
  auctioneers, flight masters, mailboxes, vendors. NPCs within 8 yards of each other count as one place.
- **Walking.** A bot with no place yet heads for one; after its linger time it picks the next, never the one it is leaving. Inns and
  banks draw the most bots, vendors the least. The bot stops 2.5 to 6 yards from the place, at a random bearing, so bots spread out
  instead of stacking on the NPC. Destinations go through the usual danger veto.
- **Lingering.** Each bot has its own linger time between `LingerMinSec` and `LingerMaxSec`. While it waits it looks around, emotes
  (talk, wave, laugh, yes, no, rarely dance) or sits (more often when hurt or drained). A threat or combat cancels everything and the
  aggro behavior takes over.
- **No places.** If the scan finds none, the bot makes short wanders like the plain idling.

## Config

`Bot.AI.TownIdle.Enabled` (default 0), `SearchYards`, `LingerMinSec`, `LingerMaxSec`, `EmotePct`, `SitPct`; see
`worldserver.conf.dist`. Enabling it adds the `natural_idle` strategy even when `Bot.AI.Move.Natural.Enabled` is off.

## Code

- `BotTownIdlePlan.h/.cpp`: pure decision logic (`PlanStep`, `PickSpot`, `LingerMs`). Unit tests in `tests/game/BotTownIdlePlan.cpp`.
- `BotBehavior.cpp`: `BotMotion::TownIdleStep` reads the facts, scans the places, executes the plan; `BotMotion::IdleStep` dispatches.

## Not verified yet

Nothing has run on a sim server. Things to look at: `TOWN_IDLE_GO` rows (trace level), whether the rest flags are set for bots that
log in inside a city, whether the stand-off points land on walkable ground near the auction house and the bank, and whether the
crowds look natural.
