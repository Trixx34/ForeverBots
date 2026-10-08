# Bot travel and leveling progression (2026-10-08)

Branch `claude/project-thread-6fjleh`, started from `forever`. Config switch `Bot.AI.Travel.Enabled` (default **off**, documented in `botserver.conf.dist`). Decisions are pure functions over plain data (`BotTravelPlan.h/.cpp`, no `Player`, no `Map`, no DBC), Catch2 tests in `tests/game/BotTravelPlan.cpp` (tag `[BotTravel]`, 18 cases), glue in `BotTravel.h/.cpp`.

## What was missing

The quest layer already picks a quest hub on the bot's **current map** (`QUEST_HUB_TRAVEL`, capped at `Bot.Quest.HubMaxDist`, default 2500 yd) and falls back to grinding when none fits. Nothing else existed: no choice of the next zone by level and faction, no flight masters, no hearthstone or inn binding, no transports. A bot whose zone ran dry ground mobs in place or walked to the nearest hub regardless of level fit.

## Planner (`BotTravelPlan`)

* **Zone table.** 37 vanilla leveling zones, levels 1 to 60, each with a level band and the faction(s) that quest there (area ids from AreaTable). A test checks that every level 1 to 60 has a zone for both factions.
* **Connection graph.** Walking links between neighbouring zones, plus boats, zeppelins, the Deeprun Tram and (unused in vanilla) portals, each with a rough travel time and the factions that may use it. Flight paths are not in the static table: the glue adds one edge per pair of zones whose flight masters the bot knows.
* **`PlanRoute`.** Cheapest route in seconds (Dijkstra), filtered by the allowed modes, the bot's faction and a step limit. Tests: every zone is reachable from the faction's starting zone, Teldrassil can only be left by boat, a Horde zeppelin is closed to the Alliance, a known flight beats walking.
* **`PickZone`.** Stays while the level fits the current zone's band. Leaves when the level is above the band (`Outgrown`), more than two levels below it (`TooLow`), or when the quest layer has nothing left to do and the bot is past the middle of the band (`Exhausted`). The destination is the reachable zone with the best level fit, minus travel time, plus a bonus for the same continent, minus a penalty for recently visited zones, plus a per-bot hash jitter so a hundred bots in Elwynn do not all pick the same zone. Reproducible in tests.
* **`ShouldHearth`.** True when casting the hearthstone and travelling from the bind is faster than travelling from here by at least 90 s.
* **`ShouldRebind`.** True when binding at this inn shortens the way back to the next leveling zone by at least 180 s, or when the bot has no bind.

## Glue (`BotTravel`)

Strategy `travel` (NonCombat engine, added to non-alt bots when the switch is on), trigger `travel_due` every 2 s, action `travel_tick`. The action returns false unless a trip holds the bot, so other actions still run. While a trip is active `BotTravel::Busy()` makes `quest_think` yield, exactly like the taming run.

* **Check** (every `CheckSec`, jittered): zone of the bot, `BotQuest::IsGrinding()` for two checks in a row as the "nothing left here" signal, then `PickZone`. Skipped in a group, an instance, a battleground, in combat or in flight. A bot in a city or a zone outside the table only moves when it is grinding.
* **Walking a leg.** `BotQuest::FindHubInZone()` (new) returns the nearest quest hub of a zone whose quest levels fit the bot; the bot walks there with the normal movement layer (goal tag `travel`). A leg counts as done when the bot's zone changes to the planned one. A path that never works (six goal re-issues) ends the trip and the zone is remembered as failed.
* **Flying a leg.** Nearest flight master within 120 yd, else walk to the known node of the zone. At the flight master: learn the node (`SendLearnNewTaxiNode`), route with the core's `TaxiPathGraph::GetCompleteNodeRoute`, `ActivateTaxiPathTo`. If the core refuses (usually money) taxis are switched off for that bot for 15 minutes and the next plan walks.
* **Flight master discovery.** Every 60 s a bot looks for an unknown flight master of its faction within `DiscoverRadius` (500 yd), walks there once and learns it. Each node is tried once per bot per session. This is how the flight network fills up for bots that start with only their starter node.
* **Boats and zeppelins** (`Bot.AI.Travel.Transport`). Dock data comes from the core, not from a table: every `MAP_OBJ_TRANSPORT` game object template gives its pause waypoints (stops with a delay) with map, position and time inside the cycle. For a planned boat or zeppelin leg the bot looks for a transport that stops in its zone and later stops on the destination continent (`NextStop`, cyclic, with the ride time). A walkable place next to each stop is the nearest flight master node (`NearestAnchor`, within 250 yd). The bot walks to the dock node, waits until the real transport object stands within 60 yd of its stop (gives up after 7 minutes, or 90 s if no such object is spawned at all), is held for the ride time of the template, then is teleported to the node at the destination stop. The ride is therefore **simulated**: the bot does not stand on the deck, it disappears at the dock and appears at the other one. A failed ride bans that zone pair for 20 minutes so the next plan goes another way. Rows: `TRAVEL_RIDE_PLAN`, `TRAVEL_RIDE_START`, `TRAVEL_RIDE_END`.
* **Hearthstone.** At the start of a trip, if `ShouldHearth` says so, the bot casts the hearthstone (spell 8690, item 6948), waits for the load and plans again from the bind. At most once per 10 minutes.
* **Inn binding.** A bot that stands within 20 yd of an innkeeper while it checks, and `ShouldRebind` is true, binds (`SendBindPoint`). It never walks to an inn on purpose.
* **Log rows** (type `decision`): `TRAVEL_START` (from, to, why, route steps), `TRAVEL_FLIGHT`, `TRAVEL_HEARTH`, `TRAVEL_REBIND`, `TRAVEL_DISCOVER`, `TRAVEL_NO_CANDIDATE`, `TRAVEL_DONE`, `TRAVEL_ABORT` (reason in the summary).

## Config

Defaults as listed in `botserver.conf.dist`: `Bot.AI.Travel.Enabled` (0), `.Zones` (1), `.Taxi` (1), `.Transport` (1), `.Hearth` (1), `.CheckSec` (10), `.TripMinutes` (25), `.MaxRouteSteps` (8), `.RecentZones` (4), `.LeaveMarginLevels` (0), `.DiscoverRadius` (500).

## Limits and not done

* **The Deeprun Tram is not used** (own instance map, `Bot.AI.Travel.Transport` leaves the tram mode out). The Elwynn and Dun Morogh sides of the Eastern Kingdoms stay connected on foot through Searing Gorge and Burning Steppes.
* **Ride is simulated** (see above). Boarding a moving deck would need passenger movement on a transport, which is not done for bots. Dock nodes are flight master positions: a dock without a flight master within 250 yd is skipped.
* **Not run on a sim server.** The container has no database or game data. The planner is covered by 18 test cases (3280 assertions); the glue compiles with the full `game` target and the whole test suite passes, but no bot has walked a trip yet. First things to check in a run: `TRAVEL_START` rows with a sensible `route_sec`, that `QUEST_HUB_TRAVEL` resumes in the new zone, `TRAVEL_FLIGHT` followed by `TRAVEL_DONE`, and for transports `TRAVEL_RIDE_PLAN` followed by `TRAVEL_RIDE_START` (not `transport_not_spawned`).
* The zone table uses the area ids of the vanilla client data. If an id is wrong the zone is simply never found and the bot stays put; the tests cannot catch a wrong id.
* Bands are the common quest ranges, not measured from the quest index. Tuning from a run: `LeaveMarginLevels`.
* Flight edges are built per decision from pairs of known nodes (cheap for the few dozen nodes a bot knows). The flight time is estimated from the straight line.
