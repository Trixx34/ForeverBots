# Bot pets, natural movement, improved loot (2026-10-08)

Branch `feature/bot-pets-movement-loot`, started from `forever`. Three independent parts. Each has its own config option (all default **off**, documented in `worldserver.conf.dist`), decision logic in pure functions over plain data structs (no `Player`, no `Map`), Catch2 tests in `tests/game`, and glue code that reads the game state and carries the decisions out.

| Part | Config switch | Pure logic | Glue | Tests (tag) |
|---|---|---|---|---|
| 1 Hunter pets | `Bot.AI.Pet.Enabled` | `BotPetLogic.h/.cpp` | `BotPet.h/.cpp`, `BotAI.cpp`, `BotMgr.cpp`, classifier hook | `BotPetLogic.cpp` `[BotPet]` |
| 2 Natural movement | `Bot.AI.Move.Natural.Enabled` | `BotMovePlan.h/.cpp` | `BotBehavior.h/.cpp` (`BotMotion`), `BotAI.cpp` | `BotMovePlan.cpp` `[BotMove]` |
| 3 Improved loot | `Bot.AI.Loot.Improved.Enabled` | `BotLootPlan.h/.cpp` | `BotQuest.cpp`, `BotAI.cpp` (death hook) | `BotLootPlan.cpp` `[BotLoot]` |

The review document `code-review-botquest-r5.md` is not in the repository. M1, M3 and M5 were worked from the descriptions in the task. Things the task lists as already done (danger scan, 20 s stall timer, `LOOT_EXHAUSTED`, store check) were not found under those names in this checkout, so nothing was redone and nothing relies on them.

## Part 1: hunter pets (`Bot.AI.Pet.*`)

Strategy `pet`, added to hunter bots (NonCombat and Combat engines) when `Bot.AI.Pet.Enabled` is on. Two triggers (`pet_due` every second, `tame_due` every 0.5 s) feed two actions: `pet_tick` (upkeep) and `pet_tame` (taming run).

* **Summon, revive, dismiss.** `Decide()` picks Call Pet, Revive Pet, Mend Pet, or a dismiss when the pet is more than 150 yd away. `RetrySec` spaces the attempts. On logout the pet is removed with the same save mode the core uses (`BotPet::OnLogout`, called from `BotAI::OnLogout`); on a map change (`BotAI::Tick`, instance or battleground) the pet and a running taming run are dropped (`BotPet::OnMapChange`).
* **Feeding.** Uses the pet's happiness power and the food the pet likes (`Pet::HaveInDiet`, the upstream happiness code). Feeds below `FeedBelowPct`, only when the pet is within 10 yd and the feed cooldown has passed. Nothing is fed when the happiness is not known.
* **Assist, follow, stay.** The pet attacks the bot's target, follows when the bot walks, stays while the bot holds position.
* **Abilities.** `AutocastPolicy` turns a short list of pet abilities on (Claw, Bite, Growl, Lightning Breath, Scorpid Poison, Screech, Thunderstomp, Charge) and off (Cower, Dash, Dive, Prowl and similar); every other spell is left as it is.
* **Taming.** `TamingQuests()` lists the six Taming the Beast quests with their rod spells and beast names. The quest classifier asks `QuestWorldLookup::EventQuestSupported()` before it returns `NEEDS_EVENT`; `BotPet::SupportsEventQuest` answers yes for those quests, so the normal quest flow accepts and turns them in. `pet_tame` walks to the nearest matching beast (spawn index built once by `BotPet::EnsureIndex()`), casts the rod spell, holds the 20 s channel and ends with a log row. `BotPet::Busy()` makes `quest_think` yield while a taming run is active. A hunter with no pet at all can also tame a first pet with Tame Beast (`TameFirstPet`).
* **Quest credit and claims.** The pet's tap goes to its owner, the existing `IsPet()` filters stay, and the claim system is keyed by the bot's guid, so a pet kill or a pet near a chest changes neither.

Limits: only taming quests that have a normal starter row run through the quest flow; a quest started by an item or a script is completed only if it is already in the log. Beast names are matched by name at runtime.

## Part 2: natural movement (`Bot.AI.Move.Natural.*`)

Everything below only runs when `Bot.AI.Move.Natural.Enabled` is on; with it off `BotMotion` behaves as before.

* **Smooth paths.** After `PathGenerator` computes a path, `ShortcutPath` removes points whose neighbours see each other (line of sight plus a ground-height check every 3 yd, so a shortcut never crosses a ledge), `RoundCorners` replaces sharp corners with a short chamfer, and the spline uses `SetSmooth()` when there are at least four points. `MaxShortcutYards` limits a straight leg.
* **Human pacing.** A per-bot speed factor drifts around 1.0 (plus or minus 5 percent). Short pauses: at the start of a real trip, on arrival, after combat (`ChangeState`), after loot, after accepting or turning in a quest. `BotMotion::Pause/IsPaused`; `quest_think` waits while a pause runs.
* **No robotic loops.** The same leg (rounded start and goal) issued a third time within 8 s counts as a failure, as does a path that cannot be computed. A failure leads to `AdviceFor()`: retry after a back-off, a side route, a longer wait, and finally `ROUTE_GIVEUP` (a normal `path_fail`, so the quest layer picks another goal). Waiting is not counted as being stuck.
* **Stuck.** Episode 1 looks around and steps back along a short trail of positions, episode 2 tries a side route, then the existing `UNREACHABLE_TARGET` failure.
* **Spread.** Each bot gets its own approach offset (inside the arrival radius, on the side it comes from), its own pause lengths and its own speed drift. All randomness is a hash of bot, target and attempt, so results are reproducible in tests and two bots never share a roll.
* **Natural idling.** Strategy `natural_idle`, lowest relevance: look around, sit when hurt or drained, wander a few yards (the new goal tag `idle`, quiet, replaceable by quest work), and never stand still longer than `IdleMaxStandSec`.
* **Metrics.** One `decision` row `MOVE_METRICS` per bot per window: window, moving and idle time, idle ratio, distance, turn rate, sharp turns, repeated legs, stuck events, path failures, longest idle.
* **Hook.** `BotSetDestinationVeto(fn)` lets another module veto destinations (used by idle wandering and the loot spawn choice). The danger gating of under-level bots was not redone; the hook is the place for it.

Destination choice by path cost is done for the chest loot spawns (part 3). Kill, talk and giver targets are still chosen by straight distance; they get the back-off and side-route handling above when a path fails, but not a path-cost choice.

## Part 3: improved loot (`Bot.AI.Loot.Improved.*`)

Applies to the chest-object loot task of the quest strategy.

* **Spawn choice by path cost.** `PickSpawn` drops spawns beyond `MaxYards` (150), spawns within `DeadlyRadius` of a bot death in the last `DeadlySec`, spawns on this bot's blacklist and spawns the destination veto rejects, then probes the nearest `PathProbes` for a path and takes the cheapest. Partial paths are rejected; pooled spawns cost a little more. When every spawn is rejected the quest is backed off (`LOOT_NO_USABLE_SPAWN`, 300 s), not dropped.
* **Per-bot blacklist.** A spawn that gives no progress (approach stalled, cannot interact, not active for this quest) is blocked for that bot for `BlacklistSec`, doubling on repeats.
* **Bags.** Before a pick: enough room goes on, tight bags loot first and then set `VendorNow`, no room sets `VendorNow` and backs the loot off (`LOOT_BAGS_FULL`), no room and no vendor on the map skips the loot (`LOOT_BAGS_FULL_NO_VENDOR`).
* **Quarantine.** A loot task that ends through `Drop` no longer counts toward `c.Repeats` or the global quest quarantine.
* **M1.** The quest index skips spawns of other phases, other difficulties and manual or system spawn groups, and marks pooled spawns. A pooled spawn that is simply not up does not use the 8-attempt budget.
* **M3.** Quests that hand over their items (`ItemDrop`, no start item, no world source) now get the missing items when they are accepted, and when such a quest is already in the log (`SupplyQuestItems`, `PlanQuestSupply`). The classifier already accepted these quests.
* **M5.** The claim check takes the shared lock at the renewal cadence instead of every tick; scratch vectors for the pick and the object entries are reused per thread.
* **Pauses.** A short pause after a chest or corpse loot and after a quest-giver interaction (needs the natural movement pacing switch).

## Tests

All new tests are in `tests/game` and use the `game` library: `[BotPet]`, `[BotMove]`, `[BotLoot]`, plus a new `[BotQuest]` case for `EventQuestSupported`. Run with `bin/tests "[BotQuest],[BotPet],[BotMove],[BotLoot]"`.

## Not verified (needs a running server)

The game server cannot be run here. The pure logic is unit-tested and the whole tree compiles; everything that touches the world is unverified:

* that pets are summoned, revived, fed, dismissed and follow as intended, including on logout and map change, and that nothing leaks;
* the taming run end to end (approach, 20 s channel, quest credit), beast names in the live data, and the spawn index;
* whether the autocast action-bar slots match the pet's real spells;
* that shortcuts and rounded corners stay on walkable ground (the ground-height check is a heuristic, not a navmesh test), that `SetSmooth()` does not clip geometry, and the cost of the line-of-sight queries;
* that the pauses, back-offs, side routes and the step back look natural and never deadlock a bot; thresholds are first guesses;
* idle sitting and wandering next to rest and quest logic;
* path-probe cost of the loot pick under many bots, and the deadly-area and blacklist behavior;
* that item-only quests are completed after supply, and that the M1 filter does not remove spawns the bots need;
* the `MOVE_METRICS` numbers against a real run.
