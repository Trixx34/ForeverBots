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

#ifndef TRINITY_BOT_AUCTION_H
#define TRINITY_BOT_AUCTION_H

// Bot auction house, v1: bots list surplus non-stackable gear at an auctioneer and collect sold-item gold and expired items from a
// mailbox. Config Bot.AH.* (default off). Auction and mail handlers touch global state, so the actual calls run on the world thread
// (BotMgr::PostWorldTask); the map-thread AI only decides and posts. See BotAuctionPlan.h for the price and cap rules.

#include "Define.h"
#include "ObjectGuid.h"
#include <functional>

class Item;
class Player;

namespace BotAuction
{
    TC_GAME_API bool Enabled();
    TC_GAME_API uint32 VisitCooldownMs();
    TC_GAME_API uint32 MaxPostsPerVisit();

    // First unequipped item of the bot that is worth listing (`keep` says which items the bot still wants), or null.
    TC_GAME_API Item* PickListing(Player* bot, std::function<bool(Item*)> const& keep);

    // Whether the bot has mail with gold or items to collect (auction results, returned items).
    TC_GAME_API bool HasMail(Player* bot);

    // World-thread tasks. Both re-check everything (the bot may have moved, logged out or lost the item) and log AH_* events.
    TC_GAME_API void PostSell(ObjectGuid bot, ObjectGuid auctioneer, ObjectGuid item);
    TC_GAME_API void PostMail(ObjectGuid bot, ObjectGuid mailbox);
}

#endif
