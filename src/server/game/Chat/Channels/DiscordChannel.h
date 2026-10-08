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

#ifndef TRINITY_DISCORD_CHANNEL_H
#define TRINITY_DISCORD_CHANNEL_H

#include "Define.h"
#include <string>

class Player;

// The in-game "Discord" chat channel of the Discord bot (contrib/discord_bot): every player joins it at login unless they left
// it with /leave (remembered in characters.character_discord_optout until they /join it again). The bot posts Discord messages
// into it with the console command ".discord say"; what players type in it reaches the bot through chat.log.channel.<name>.
// worldserver.conf: Discord.Channel.Enable, Discord.Channel.Name.
namespace DiscordChannel
{
    TC_GAME_API bool IsEnabled();
    TC_GAME_API std::string const& GetName();
    TC_GAME_API bool IsDiscordChannel(std::string const& channelName);

    TC_GAME_API void OnLogin(Player* player);
    TC_GAME_API void OnJoined(Player* player);     // /join: auto-join again from now on
    TC_GAME_API void OnLeft(Player* player);       // /leave: no auto-join any more

    // Posts "[<channel>] [<sender>]: <text>" to everyone in the channel (both factions); returns how many players got it.
    TC_GAME_API uint32 Relay(std::string const& sender, std::string const& text);
}

#endif
