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

#ifndef TRINITY_BOT_LFG_H
#define TRINITY_BOT_LFG_H

// Glue for the looking-for-group decisions in BotLfgPlan.h (Bot.LFG.*, default off): a player asks for bots by whisper (to any bot) or in
// party chat (as the leader) and free bots join the group until it is full. World thread only (chat handlers and BotMgr::Update).
// See docs/playerbots/feature-bot-lfg-20261008.md.

#include "Define.h"
#include <string_view>

class BotAI;
class Player;

namespace BotLfg
{
    // Called by BotMgr::Update every world tick.
    TC_GAME_API void Update(uint32 diff);

    // ChatHandler.cpp, CHAT_MSG_WHISPER: `player` whispered `text` to `receiver`. Handled when that is a bot and the text is an lfg request.
    TC_GAME_API void OnWhisper(Player* player, Player* receiver, std::string_view text);

    // ChatHandler.cpp, CHAT_MSG_PARTY: `player` said `text` to their party. Handled when the player leads the group and the text is an lfg request.
    TC_GAME_API void OnPartyChat(Player* player, std::string_view text);

    // True while the bot was brought into a player's group by a search and is held there: the quest AI stands back. Thread-safe (map threads call it).
    TC_GAME_API bool Busy(BotAI* ai);
}

#endif
