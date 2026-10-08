# Ranged ammo upkeep and premade talents (2026-10-08)

Both features are opt-in and default off. Not built, not run: no compiler, Boost or MySQL here, so the code has never been through a compiler, and nothing was seen on the sim.

## Ranged ammo (`Bot.AI.Ammo.*`, in `BotQuest.cpp`)

Before: combat only reacted to NO_AMMO / NEED_AMMO cast failures (hunter falls back to melee). Nothing checked or bought ammo.

After, with `Bot.AI.Ammo.Enabled = 1` (needs a restart, the vendor index is built at startup):
* The quest strategy's vendor layer reads the equipped ranged weapon (bow/crossbow -> arrows, gun -> bullets; wands and thrown are ignored) and counts matching ammo in the bags.
* Below `Bot.AI.Ammo.LowCount` (60) it logs `AMMO_LOW` (decision) and plans a vendor trip to the nearest friendly vendor that sells that ammo type (new `g.VendorAmmo` index next to `VendorBags`). Cooldown `Bot.AI.Ammo.CooldownSec` (300).
* At the vendor (`DoVendor` -> `BuyAmmo`) it buys the best usable offer in whole stacks up to `Bot.AI.Ammo.BuyTarget` (400). No training-money reserve is kept (a hunter without ammo is crippled).
* Codes: `AMMO_LOW`, `AMMO_BOUGHT` (decisions); `AMMO_NO_MONEY`, `AMMO_NO_VENDOR`, `AMMO_BUY_FAILED` (blocked rows).
* Not done: ammo already in the equipped quiver/ammo slot (only inventory is counted), buying ammo for non-quest strategies, picking a better ammo by weapon DPS.

Unverified: that `BuyItemFromVendorSlot` accepts the count used for ammo stacks, that `ForEachItem(Inventory)` sees ammo in every bag, that the starting hunter gear in this fork puts ammo in a counted location, and the whole trip flow.

## Premade talents (`Bot.Talents.Premade.*`, in `BotAI.cpp`)

How talents work in this fork: the engine is the retail-style tiered system (`Player::LearnTalent(talentId)`, one pick per tier, `MaxTalentTiers` from level, a primary specialization is required, level < 15 resets the points in `InitTalentForLevel`). The bot code never read or set talents before (`GiveLevel` + `InitTalentForLevel` only).

After, with `Bot.Talents.Premade.Enabled = 1`: on level-up (`BotAI::OnLevelUp` -> `ApplyPremadeTalents`) the bot learns the ids in `Bot.Talents.Premade.Class.<classId>` (Talent.db2 ids; first id of a tier wins; a tier that already holds a pick is left alone so no rest-area swap is attempted). `Bot.Talents.Premade.Spec.<classId>` optionally sets the primary specialization when none is set. Logs `TALENTS_APPLIED` (learned/failed counts, ids). The table is empty by default, so enabling the switch alone does nothing.

Not done: the actual id tables per class (they must be read from this client's Talent.db2, which is not in the repo checkout), applying at login for bots created above level 10 (only level-up calls it), respec.

Unverified: everything; in particular whether `LearnTalent` works for a bot session without a client (it checks combat, spec and tier only), and the level threshold (10 is assumed).
