# Bots: fishing, cooking and food for players

Bots already fish (`FISH_*`, `BotQuest.cpp` RunFish) and cook for skill-ups (`PROF_CRAFT`). This adds the part players notice: bots turn
what they catch into food and buff food, keep a stock of it, and tell a player what they can hand over.

## Cooking for a stock (`Bot.AI.Cooking.Enabled`, default off)

`CraftForSkill` in `BotQuest.cpp` now also builds a list of food recipes (`BotCooking::Describe`: the item the spell creates, its kind, items
per cast, casts the reagents in the bags allow). `BotCook::PickStock` picks the recipe:

1. buff food ("Well Fed") while below `Bot.AI.Cooking.StockBuff`, then plain food below `StockPlain`; when the needed kind cannot be made the
   other unmet kind is tried;
2. inside a kind, recipes that still raise the skill first, then the better food (higher grey threshold), then the lower spell id;
3. nothing happens when both stocks are met or fewer than `MinFreeSlots` bag slots are free.

Grey recipes and a capped skill no longer stop a cooking bot while the stock is short. Raw fish the bots catch are ordinary reagents, so
fishing feeds the stock. A recipe that needs a cooking fire sends the bot to the nearest one (existing `WorkshopTrip`). After a stock cast
the next cast is 4 s later, not 15 s. Logged as `FOOD_COOK` (economy category). Without the setting, behavior is unchanged.

Food kind comes from the on-use spell of the item (`BotCooking::Classify`): eating auras (health only) are plain food, drinking auras are
drink, any other aura or trigger makes it buff food.

## Asking for food (`Bot.Chat.Food.Enabled`, default off)

`food` is a new chat verb (`BotChat`), with selectors like the others (`healers food`) or whispered to one bot. Unlike the other verbs every
group member may use it, not only the leader. Each bot that holds food the player can use (level requirement, tradable, not bound to
someone else) whispers it as item links, buff food first, then plain food, then drink, and says to open a trade. The hand-over itself is the
existing trade by links (`Bot.Trade.Link.Enabled`); this feature does not touch it, so it does not overlap with other hand-off work.
Bots without food stay silent (`NO_FOOD` in the command log).

## Files

| File | What |
|---|---|
| `BotCookPlan.*` | pure: stock targets, recipe pick, offer order (unit tested) |
| `BotCooking.*` | game side: config, bag scan, food classification, `OfferFood` |
| `BotQuest.cpp` | stock cooking inside `CraftForSkill` |
| `BotChat.*` | `food` verb |
| `botserver.conf.dist` | `Bot.AI.Cooking.*`, `Bot.Chat.Food.*` |

Tests: `tests/game/BotCookPlan.cpp`, plus the verb in `BotChatParse.cpp` and `FOOD_` in `BotLogCategory.cpp`.

## Not verified

Compiled in a container (the changed files and the new tests build); nothing ran on a sim server. Open points to check there: that the
buff-food classification matches the item data (some "Well Fed" foods may use a different aura layout), and that bots reach cooking fires.

## Not done

Bots do not eat buff food themselves before a pull, do not start a trade on their own, and do not fish for specific reagents on request.
