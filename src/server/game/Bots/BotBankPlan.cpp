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

// Decision rules of the bot bank and mail strategy, see BotBankPlan.h.

#include "BotBankPlan.h"
#include <algorithm>

namespace BotBankPlan
{
bool Depositable(ItemFacts const& f)
{
    if (f.Wanted)
        return false;
    switch (f.G)
    {
        case Group::TradeGood:
        case Group::Gem:
        case Group::Recipe:
        case Group::Reagent:
            return true;
        default:
            return false;
    }
}

Visit BankVisit(Config const& cfg, uint32 freeSlots, uint32 depositableStacks, uint32 bankFreeSlots, uint32 wantedInBank)
{
    if (wantedInBank && freeSlots > 0)
        return Visit::Withdraw;
    if (freeSlots <= cfg.FreeSlotsBelow && depositableStacks && bankFreeSlots)
        return Visit::Deposit;
    return Visit::None;
}

uint32 StacksToDeposit(Config const& cfg, uint32 freeSlots, uint32 depositableStacks, uint32 bankFreeSlots)
{
    uint32 const wantFree = cfg.FreeSlotsTarget > freeSlots ? cfg.FreeSlotsTarget - freeSlots : 0;
    uint32 n = std::max<uint32>(1, wantFree);
    n = std::min(n, cfg.MaxStacksPerVisit);
    n = std::min(n, depositableStacks);
    return std::min(n, bankFreeSlots);
}

uint64 GoldToMail(Config const& cfg, uint64 money)
{
    uint64 const keep = cfg.GoldReserve + MAIL_POSTAGE;
    if (money <= keep || money - keep < cfg.MailMinGold)
        return 0;
    uint64 gold = money - keep;
    if (cfg.MailMaxGold && gold > cfg.MailMaxGold)
        gold = cfg.MailMaxGold;
    return gold;
}
}
