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

#ifndef TRINITY_BOT_LOOT_ROLL_H
#define TRINITY_BOT_LOOT_ROLL_H

// Group loot rolls of bots (Bot.Loot.Roll.*, default off). Bot sessions have no client to answer the roll window, so a roll with a bot in
// it waits for the 60 second timeout. LootRoll::TryToStart calls the hook below right after it started the roll; the bot votes at once.
// See docs/playerbots/feature-bot-group-loot-20261008.md.

#include "Define.h"

class Player;
struct ItemTemplate;

namespace BotLootRoll
{
    // Loot.cpp, LootRoll::TryToStart after SendStartRoll, for each looter that is a bot and has not voted. `needAllowed` says whether the
    // roll offers Need. Returns 0 pass, 1 need, 2 greed (the RollVote values) or -1 when the module is off (the hook then leaves the vote
    // alone).
    TC_GAME_API int32 ChooseVote(Player* bot, ItemTemplate const* item, bool needAllowed);
}

#endif
