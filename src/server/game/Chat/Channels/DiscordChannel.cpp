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

#include "DiscordChannel.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Util.h"

// the channel as it must stay: no join/leave announcements, no owner (nobody can kick, ban or set a password)
Channel* DiscordChannel::GetOrCreate(ChannelMgr* mgr)
{
    Channel* channel = mgr->GetCustomChannel(GetName());
    if (!channel)
        channel = mgr->CreateCustomChannel(GetName());
    if (channel)
    {
        channel->SetAnnounce(false);
        channel->SetOwnership(false);
    }
    return channel;
}

namespace
{
    // WoW chat escapes (|c colours, |H links, |T textures) would let Discord users fake item links or textures: no '|' at all
    std::string Clean(std::string text, size_t maxLength)
    {
        std::string out;
        for (char c : text)
        {
            if (c == '|' || c == '\r' || c == '\n' || c == '\t')
                c = c == '|' ? '/' : ' ';
            out += c;
        }
        if (utf8length(out) > maxLength)
            utf8truncate(out, maxLength);
        return out;
    }
}

bool DiscordChannel::IsEnabled()
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Discord.Channel.Enable", false);
    return enabled;
}

std::string const& DiscordChannel::GetName()
{
    static std::string const name = sConfigMgr->GetStringDefault("Discord.Channel.Name", "Discord");
    return name;
}

bool DiscordChannel::IsDiscordChannel(std::string const& channelName)
{
    return IsEnabled() && StringEqualI(channelName, GetName());
}

void DiscordChannel::OnLoadingScreenDone(Player* player)
{
    if (!IsEnabled())
        return;

    // after the client's own channel joins (General, Trade, ... about 1.6 s after its loading screen, official sniff), so the
    // channel numbers stay as the player knows them
    player->m_Events.AddEventAtOffset([player]()
    {
        if (!player->IsInWorld())
            return;

        if (CharacterDatabase.PQuery("SELECT 1 FROM character_discord_optout WHERE guid = {}", player->GetGUID().GetCounter()))
            return;

        if (ChannelMgr* mgr = ChannelMgr::ForTeam(player->GetTeam()))
            if (Channel* channel = GetOrCreate(mgr))
                if (!channel->HasMember(player->GetGUID()))
                    channel->JoinChannel(player);
    }, 2s);
}

void DiscordChannel::OnJoined(Player* player)
{
    CharacterDatabase.PExecute("DELETE FROM character_discord_optout WHERE guid = {}", player->GetGUID().GetCounter());
}

void DiscordChannel::OnLeft(Player* player)
{
    CharacterDatabase.PExecute("REPLACE INTO character_discord_optout (guid) VALUES ({})", player->GetGUID().GetCounter());
}

uint32 DiscordChannel::Relay(std::string const& sender, std::string const& text)
{
    if (!IsEnabled())
        return 0;

    std::string const cleanSender = Clean(sender, 24);
    std::string const cleanText = Clean(text, 255);
    if (cleanText.empty())
        return 0;

    // one channel per faction, or one shared channel with cross-faction channels (ForTeam returns the same manager)
    uint32 count = 0;
    ChannelMgr* done = nullptr;
    for (Team team : { ALLIANCE, HORDE })
    {
        ChannelMgr* mgr = ChannelMgr::ForTeam(team);
        if (!mgr || mgr == done)
            continue;
        done = mgr;
        if (Channel* channel = GetOrCreate(mgr))
        {
            channel->SayAs(cleanSender, cleanText);
            count += channel->GetNumPlayers();
        }
    }
    return count;
}
