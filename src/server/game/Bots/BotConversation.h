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

#ifndef TRINITY_BOT_CONVERSATION_H
#define TRINITY_BOT_CONVERSATION_H

// Bot conversations (docs/playerbots/feature-bot-conversations-20261008.md): bots answer whispers and party chat with a line of their
// own personality instead of staying silent. Local templates and keywords (BotConversationText), no external service, no database
// access. Chat commands keep working: a message that is command-shaped (BotChat) is never answered as conversation.
//
//  Whisper to a bot: it answers (an unknown topic gets the "did not catch that" line of its personality).
//  Party chat: a bot whose name is in the message answers; otherwise, with chance Bot.Conversation.Party.ChatterPct, one bot of the
//              party answers a greeting, farewell, thanks, joke, compliment or "ready".
//  Replies are queued and sent after a short typing delay. Off by default (Bot.Conversation.Enabled).

#include "Define.h"
#include <string_view>

class Player;

namespace BotConversation
{
// ChatHandler, whisper case, after the command hooks: `sender` whispered `receiver` (any player or bot; only bot receivers matter).
TC_GAME_API void OnWhisper(Player* sender, Player* receiver, std::string_view text);

// ChatHandler, party case, after the command hook: `sender` said `text` in party chat.
TC_GAME_API void OnPartyChat(Player* sender, std::string_view text);

// World thread, BotMgr::Update: sends the replies whose typing delay is over.
TC_GAME_API void Update(uint32 diff);
}

#endif
