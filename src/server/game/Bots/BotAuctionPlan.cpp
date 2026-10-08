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

#include "BotAuctionPlan.h"
#include <algorithm>

namespace BotAuctionPlan
{
uint64 ListingPrice(uint32 vendorSell, uint64 recentMedian, uint32 undercutPct)
{
    uint64 const base = std::max<uint64>(uint64(vendorSell) * 3 / 2, recentMedian);
    uint64 const cut = std::min<uint32>(undercutPct, 90);
    uint64 const price = base * (100 - cut) / 100;
    return std::max<uint64>(price, std::max<uint32>(vendorSell, 1));
}

uint64 SeedPrice(uint32 vendorSell, uint32 vendorBuy)
{
    uint64 const sell = std::max<uint32>(vendorSell, 1);
    uint64 const ceiling = vendorBuy ? uint64(vendorBuy) * 110 / 100 : sell * 3;
    return std::max<uint64>(std::min<uint64>(sell * 2, ceiling), sell);
}

Verdict CanList(Limits const& lim, uint32 botListings, uint32 totalListings, uint64 botMoney, uint64 deposit)
{
    if (botListings >= lim.MaxPerBot)
        return Verdict::PerBotLimit;
    if (totalListings >= lim.MaxTotal)
        return Verdict::TotalLimit;
    if (botMoney < deposit)
        return Verdict::NoMoney;
    if (deposit * 100 > botMoney * lim.DepositMaxPct)
        return Verdict::DepositTooHigh;
    return Verdict::Ok;
}

char const* VerdictCode(Verdict v)
{
    switch (v)
    {
        case Verdict::Ok: return "OK";
        case Verdict::PerBotLimit:
        case Verdict::TotalLimit: return "AH_LIMIT";
        case Verdict::NoMoney:
        case Verdict::DepositTooHigh: return "AH_NO_MONEY";
    }
    return "?";
}
}
