# Bot consumables (suggestion 10)

Bots now use potions from their bags in a fight. Off by default (`Bot.AI.Consumables.Enabled`).

- Strategy `consumables` in the Combat engine; the action `consumable_use` runs just below the class self-heal (`combat_heal`), so a class
  heal is tried first and the potion covers the case where it is on cooldown, unaffordable or absent.
- A potion is a consumable whose on-use spell has a heal or a mana energize effect and no aura (food and drink are auras and belong to the
  rest strategy). The bot must meet its required level and `CanUseItem`.
- Choice (`BotConsumablePlan.cpp`, unit tested): health below `HealthBelowPct` (35) first, mana below `ManaBelowPct` (20) for mana users; the
  smallest potion that covers the missing points, else the biggest. A potion on cooldown is not offered.
- Logged as `POTION_HEALTH` / `POTION_MANA` with item, restore, cast result and the percentages.

Not done: buying potions (the vendor trip of the quest layer could add them later), bandages, scrolls and elixirs.
