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

#ifndef TRINITY_BOT_AUCTION_PLAN_H
#define TRINITY_BOT_AUCTION_PLAN_H

// Price and cap rules of the bot auction house (docs/playerbots/design-auction-house-20261008.md). Pure functions, unit tested without
// a database. BotAuction.cpp applies them on the world thread.

#include "Define.h"

namespace BotAuctionPlan
{
    struct Limits
    {
        uint32 UndercutPct = 5;
        uint32 MaxPerBot = 5;
        uint32 MaxTotal = 2000;
        uint32 DepositMaxPct = 10;   // the deposit may cost at most this share of the bot's money
    };

    // Classic 1.60 auction durations in minutes (2, 8 and 24 hours).
    constexpr uint32 RUN_TIME_MINUTES = 480;

    // Start price of a listing: 1.5 x the vendor sell price (or the recent median when that is higher), minus the undercut, never
    // below the vendor sell price. Copper.
    TC_GAME_API uint64 ListingPrice(uint32 vendorSell, uint64 recentMedian, uint32 undercutPct);

    // Seeded goods (stock for the economy): twice the vendor sell price, but not above 110 percent of the vendor buy price (bots
    // must not overpay) and never below the sell price. Copper. A vendor buy price of 0 means unknown: cap at 3 x sell.
    TC_GAME_API uint64 SeedPrice(uint32 vendorSell, uint32 vendorBuy);

    enum class Verdict : uint8 { Ok, PerBotLimit, TotalLimit, NoMoney, DepositTooHigh };

    // Whether one more listing is allowed, given how many the bot and the house already have and the deposit due.
    TC_GAME_API Verdict CanList(Limits const& lim, uint32 botListings, uint32 totalListings, uint64 botMoney, uint64 deposit);

    TC_GAME_API char const* VerdictCode(Verdict v);
}

#endif
