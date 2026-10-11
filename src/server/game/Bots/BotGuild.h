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

#ifndef TRINITY_BOT_GUILD_H
#define TRINITY_BOT_GUILD_H

// Bot guilds (Bot.Guild.*): the world-thread glue for BotGuildPlan.h. The core calls the hooks below; every one returns at once when
// Bot.Guild.Enabled is off. No hook changes or consumes what the player did.
//
//  Invites:  a guild invite to a bot is answered on the spot (BotGuildPlan::InviteCheck): accepted through the normal accept handler or declined.
//  Chat:     bots answer in guild chat after a short delay: welcome a member who logs in, congratulate level-ups, answer a hello, a goodbye,
//            thanks, "how are you" and their own name, and say something unprompted now and then while a player is online.
//  Whisper:  "invite" whispered to a bot of a guild makes it invite the player (if its rank may invite).
//  Auto:     bots without a guild found guilds and join guilds led by world bots (Bot.Guild.Auto.*).

#include "Define.h"
#include "BotGuildPlan.h"
#include <string_view>

class Guild;
class Player;

namespace BotGuild
{
// Bot.Guild.*, read once.
TC_GAME_API BotGuildPlan::Config const& Cfg();

// Guild::HandleInviteMember, after the invitation was sent to `invitee`. Accepts or declines on behalf of a bot invitee.
TC_GAME_API void OnGuildInvite(Guild* guild, Player* inviter, Player* invitee);

// ChatHandler, CHAT_MSG_GUILD, after the message was broadcast.
TC_GAME_API void OnGuildMessage(Player* speaker, Guild* guild, std::string_view text);

// CharacterHandler, after guild->SendLoginInfo: `player` (a human) came online.
TC_GAME_API void OnMemberLogin(Player* player);

// Player::GiveLevel, after the level changed.
TC_GAME_API void OnLevelUp(Player* player);

// ChatHandler, CHAT_MSG_WHISPER, after the whisper was delivered.
TC_GAME_API void OnWhisper(Player* sender, Player* receiver, std::string_view text);

// World thread, BotMgr::Update.
TC_GAME_API void Update(uint32 diff);
}

#endif
