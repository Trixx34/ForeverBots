# Bot bank and mail (2026-10-08)

Requested by **Trixx34**. Config `Bot.Bank.*` in `worldserver.conf.dist`, default off. Nothing has run on a sim server yet.

## What bots do
* **Deposit.** A bot at level 10 or more with at most `Bot.Bank.FreeSlotsBelow` (4) free bag slots walks to the nearest banker (`UNIT_NPC_FLAG_BANKER`, same map, within 2500 yd) and puts trade goods, gems, recipes and reagent-class items in the bank, enough stacks to reach `FreeSlotsTarget` (10), at most `MaxStacksPerVisit` (12) per visit.
* **Never deposited:** quest items and quest starters, gear, consumables, bags and ammo, poor-quality junk (the vendor trip sells it), and any item that is a reagent of a recipe the bot knows for a crafting profession, because the bot crafts from its bags.
* **Withdraw.** On the same trip the bot takes up to 3 gear items back out of the bank that the quest code's `KeepItem` calls an upgrade it can use. A bank holding such an item makes a trip due on its own.
* **Mail.** With `Bot.Bank.Mail.Enabled` and a `Bot.Bank.Mail.Recipient`, a bot whose gold exceeds `ReserveGold` (50) plus postage by at least `MinGold` (20) walks to a mailbox and mails the surplus (capped by `MaxGold`) to that character. No recipient means no mail. Collecting mail stays with `Bot.AH.*`.
* One cooldown (`VisitCooldownSec`, 600) covers bank and mail visits. A bank trip comes before a mail trip.

## Code
* `BotBankPlan.h/.cpp`: pure decisions (`Depositable`, `BankVisit`, `StacksToDeposit`, `GoldToMail`), tested in `tests/game/BotBankPlan.cpp`.
* `BotBank.h/.cpp`: bag and bank scans, and the world-thread tasks `PostBank` and `PostMailGold` (the bank calls mirror `HandleAutoBankItemOpcode` and `HandleAutoStoreBankItemOpcode`; mail mirrors `HandleSendMail`).
* `BotQuest.cpp`: service types 8 (banker) and 9 (mailbox) next to the auction ones: `BankDue`, `RunBank`, a banker index, a `BankNextMs` timer.

## Log codes
`BANK_TRIP`, `BANK_DEPOSIT`, `BANK_WITHDREW`, `BANK_VISIT`, `BANK_NONE` (no banker in reach), `BANK_REFUSED`, `BANK_UNREACHABLE`, `MAIL_TRIP`, `MAIL_SENT`, `MAIL_NO_RECIPIENT`, `MAIL_REFUSED`.

## Things to check in the sim
* Characters have bank slots in this fork (bank bags 63..72); with none, `GetFreeInventorySlotCount(Bank)` is 0 and no deposit trips start.
* A banker is present in the test world (`BANK_NONE` otherwise).
* Crafting bots keep their reagents in the bags (no `BANK_DEPOSIT` of a reagent they craft with).
* Mail arrives at the recipient with the expected gold and the bot's money drops by gold plus 30 copper.
