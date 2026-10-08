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

#include "tc_catch2.h"

#include "BotBankPlan.h"

using namespace BotBankPlan;

TEST_CASE("BotBankPlan depositable groups", "[BotBankPlan]")
{
    CHECK(Depositable({ Group::TradeGood, false, false }));
    CHECK(Depositable({ Group::Gem, false, false }));
    CHECK(Depositable({ Group::Recipe, false, true }));
    CHECK(Depositable({ Group::Reagent, false, false }));
    CHECK_FALSE(Depositable({ Group::Consumable, false, false }));   // potions and food are for the next fight
    CHECK_FALSE(Depositable({ Group::Container, false, false }));
    CHECK_FALSE(Depositable({ Group::Gear, false, false }));         // vendor and auction house deal with gear
    CHECK_FALSE(Depositable({ Group::Quest, false, false }));
    CHECK_FALSE(Depositable({ Group::Junk, false, false }));
    CHECK_FALSE(Depositable({ Group::Other, false, false }));
}

TEST_CASE("BotBankPlan wanted items stay in the bags", "[BotBankPlan]")
{
    CHECK_FALSE(Depositable({ Group::TradeGood, true, false }));     // a crafting input the bot is using right now
    CHECK_FALSE(Depositable({ Group::Recipe, true, false }));
}

TEST_CASE("BotBankPlan bank visit", "[BotBankPlan]")
{
    Config cfg;                                                       // deposit at 4 free slots or fewer
    CHECK(BankVisit(cfg, 2, 5, 20, 0) == Visit::Deposit);
    CHECK(BankVisit(cfg, 4, 1, 1, 0) == Visit::Deposit);              // the threshold itself counts
    CHECK(BankVisit(cfg, 5, 5, 20, 0) == Visit::None);                // enough room in the bags
    CHECK(BankVisit(cfg, 0, 0, 20, 0) == Visit::None);                // nothing a bank may take
    CHECK(BankVisit(cfg, 0, 5, 0, 0) == Visit::None);                 // the bank is full
    CHECK(BankVisit(cfg, 10, 0, 20, 1) == Visit::Withdraw);           // an upgrade waits in the bank
    CHECK(BankVisit(cfg, 0, 5, 20, 1) == Visit::Deposit);             // no free slot to withdraw into: make room first
    CHECK(BankVisit(cfg, 2, 5, 20, 3) == Visit::Withdraw);            // withdraw first, it needs one slot only
}

TEST_CASE("BotBankPlan stacks to deposit", "[BotBankPlan]")
{
    Config cfg;                                                       // target 10 free, 12 stacks per visit
    CHECK(StacksToDeposit(cfg, 2, 30, 100) == 8);                     // 10 - 2
    CHECK(StacksToDeposit(cfg, 0, 30, 100) == 10);
    CHECK(StacksToDeposit(cfg, 9, 30, 100) == 1);
    CHECK(StacksToDeposit(cfg, 12, 30, 100) == 1);                    // already past the target: still move one
    cfg.FreeSlotsTarget = 40;
    CHECK(StacksToDeposit(cfg, 0, 100, 100) == 12);                   // capped by the visit limit
    CHECK(StacksToDeposit(cfg, 0, 3, 100) == 3);                      // capped by what the bot carries
    CHECK(StacksToDeposit(cfg, 0, 100, 2) == 2);                      // capped by the room in the bank
    CHECK(StacksToDeposit(cfg, 0, 0, 100) == 0);
    CHECK(StacksToDeposit(cfg, 0, 5, 0) == 0);
}

TEST_CASE("BotBankPlan gold to mail", "[BotBankPlan]")
{
    Config cfg;                                                       // reserve 50 gold, minimum surplus 20 gold
    CHECK(GoldToMail(cfg, 0) == 0);
    CHECK(GoldToMail(cfg, 500000) == 0);                              // only the reserve
    CHECK(GoldToMail(cfg, 699999) == 0);                              // surplus below the minimum
    CHECK(GoldToMail(cfg, 700030) == 200000);                         // reserve + postage + minimum
    CHECK(GoldToMail(cfg, 1000030) == 500000);                        // everything above reserve and postage
    cfg.MailMaxGold = 100000;
    CHECK(GoldToMail(cfg, 1000030) == 100000);                        // capped
    cfg.GoldReserve = 0;
    cfg.MailMinGold = 0;
    CHECK(GoldToMail(cfg, 30) == 0);                                  // cannot pay the postage
    CHECK(GoldToMail(cfg, 31) == 1);
}
