# Design: auction house bots (2026-10-08, design only, nothing implemented)

Extends next-plan step E5. Depends on E4 (vendor trips, `RunNpcVisit`) and a check that the Classic 1.60 auction handlers work in this fork.

## Goals
Bots list surplus they cannot use, buy upgrades they can afford, and a small seeder keeps basic goods on the house so the economy has stock. No effect on quest or combat flow when off.

## Config (all default off)
`Bot.AH.Enabled`, `Bot.AH.Seed.Enabled`, `Bot.AH.MaxListingsPerBot` (5), `Bot.AH.MaxListingsTotal` (2000), `Bot.AH.UndercutPct` (5), `Bot.AH.VisitCooldownSec` (900), `Bot.AH.Seed.Bots` (account/char names allowed to seed).

## Index and trips
At startup index auctioneers per faction (creatures with UNIT_NPC_FLAG_AUCTIONEER, same pattern as `g.Vendors`; neutral houses reachable by both). A visit is a service task like vendor: walk, `RunNpcVisit`, act, finish. Trigger: bag pressure (replaces vendoring for green-or-better unusable gear), or an upgrade want (see below), never in combat, never below a money floor.

## Price policy
* Listing: `base = max(vendorSellPrice * 1.5, recentMedian)`; list at `base * (1 - UndercutPct/100)` but never below vendor sell price. Recent median from the last N AH sales of that item (kept in memory, seeded from the auction history table if present). Buyout = 1.0 x start price; deposit paid from copper, skip if deposit > 10% of bot money. Duration 12h.
* Buying: only items the bot can equip and that beat the equipped item by the existing gear score; pay at most `min(buyout, 25% of bot money)` and keep the training reserve (`TrainReserve`).

## Seeding
A seeder keeps stock of goods bots actually want: food/drink by level band, bags, ammo, bandages, low-level armor/weapons by slot. Per item: target 3-10 listings, price band 100-130% of vendor buy price for consumables and bags, 150-250% of vendor sell for gear (never above vendor buy price + margin so bots do not overpay). Caps: per seeder bot `MaxListingsPerBot`, global `MaxListingsTotal`, and a per-item cap so one item never floods. Re-list expired items at the same price, then drop 10% per cycle down to the floor. Tune with the log data (AH_LISTED and AH_BOUGHT prices against vendor values).

## Codes
Decisions: `AH_LISTED`, `AH_BOUGHT`, `AH_SEEDED`, `AH_EXPIRED_RELIST`. Blocked: `AH_NO_MONEY`, `AH_UNREACHABLE`, `AH_NONE` (no auctioneer in range), `AH_LIMIT` (listing cap), `AH_REFUSED` (handler error).

## Open questions
Does the Classic 1.60 client path use the same `AuctionHouseMgr` calls here? Is a cross-faction neutral house present in the test world? Where to persist recent sales (memory only is fine for v1).

## Verification
Sim: 20 bots, 1 seeder; expect AH_SEEDED then AH_BOUGHT; check price distribution against the bands, no listing count above caps, no money going negative.
