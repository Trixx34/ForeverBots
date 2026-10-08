/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITY_BOT_BANK_PLAN_H
#define TRINITY_BOT_BANK_PLAN_H

// Decision rules of the bot bank and mail strategy (Bot.Bank.*, BotBank.cpp). Pure functions over plain data so they are unit tested
// without a map or a Player. See docs/playerbots/feature-bot-bank-mail-20261008.md.

#include "Define.h"

namespace BotBankPlan
{
    struct Config
    {
        uint32 FreeSlotsBelow = 4;       // a bank trip is due when the bags have at most this many free slots
        uint32 FreeSlotsTarget = 10;     // a deposit tries to get the bags back to this many free slots
        uint32 MaxStacksPerVisit = 12;   // stacks moved per bank visit
        uint64 GoldReserve = 500000;     // copper a bot keeps for itself (training, repairs) before it mails any away
        uint64 MailMinGold = 200000;     // copper that must be above the reserve before a mail is worth the trip
        uint64 MailMaxGold = 0;          // copper cap of one mail, 0 = no cap
    };

    // Coarse item groups. BotBank.cpp maps the item template onto these.
    enum class Group : uint8 { Other, TradeGood, Gem, Recipe, Reagent, Consumable, Container, Gear, Quest, Junk };

    struct ItemFacts
    {
        Group G = Group::Other;
        bool Wanted = false;             // the bot still needs it: an upgrade it can equip, a quest item, a crafting input it is working on
        bool Bound = false;              // soulbound
    };

    // Whether a bag item may go to the bank. Materials, gems and recipes are the bag clutter of a bot that gathers and crafts; everything
    // else is the business of the vendor, the auction house or the quest log.
    TC_GAME_API bool Depositable(ItemFacts const& f);

    enum class Visit : uint8 { None, Deposit, Withdraw };

    // What a bank trip is for. Withdrawing a wanted item (an upgrade that sits in the bank) comes first and needs a free bag slot; a deposit
    // needs tight bags, something to put in and room in the bank.
    TC_GAME_API Visit BankVisit(Config const& cfg, uint32 freeSlots, uint32 depositableStacks, uint32 bankFreeSlots, uint32 wantedInBank);

    // Stacks to move this visit: enough to reach the free-slot target, at least one, never more than the cap, the stacks on hand or the
    // room in the bank.
    TC_GAME_API uint32 StacksToDeposit(Config const& cfg, uint32 freeSlots, uint32 depositableStacks, uint32 bankFreeSlots);

    // Classic mail postage for a mail without items.
    constexpr uint64 MAIL_POSTAGE = 30;

    // Copper to put in a mail to the configured recipient: everything above the reserve less the postage, capped, and 0 when the surplus is
    // below the minimum that makes the trip worthwhile or the bot cannot pay the postage.
    TC_GAME_API uint64 GoldToMail(Config const& cfg, uint64 money);
}

#endif
