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

// ".discord say <name> <text>": the Discord bot (contrib/discord_bot) posts a Discord message into the in-game Discord channel
// (DiscordChannel.h), shown as [Discord] [<name>]: <text>. Used from the console / Remote Access.

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "DiscordChannel.h"
#include "GroupFinderListings.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

class classic_discord_commandscript : public CommandScript
{
public:
    classic_discord_commandscript() : CommandScript("classic_discord_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable discordCommandTable =
        {
            { "say", HandleSayCommand, rbac::RBAC_PERM_COMMAND_ANNOUNCE, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "discord", discordCommandTable },
        };
        return commandTable;
    }

    static bool HandleSayCommand(ChatHandler* handler, std::string sender, Tail text)
    {
        if (!DiscordChannel::IsEnabled())
        {
            handler->SendSysMessage("Discord channel is off (worldserver.conf Discord.Channel.Enable = 1)");
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (text.empty())
            return false;

        uint32 players = DiscordChannel::Relay(sender, std::string(text));
        handler->PSendSysMessage("Discord message sent to %u players", players);
        return true;
    }
};

// Group Finder listings for the Discord LFG channels (GroupFinderListings.h): a member joining or leaving updates the post,
// the leader logging out removes it
class classic_discord_group_finder_groups : public GroupScript
{
public:
    classic_discord_group_finder_groups() : GroupScript("classic_discord_group_finder_groups") { }

    void OnAddMember(Group* group, ObjectGuid /*guid*/) override { GroupFinderListings::OnGroupChanged(group); }
    void OnRemoveMember(Group* group, ObjectGuid /*guid*/, RemoveMethod /*method*/, ObjectGuid /*kicker*/, char const* /*reason*/) override
    {
        GroupFinderListings::OnGroupChanged(group);
    }
};

class classic_discord_group_finder_players : public PlayerScript
{
public:
    classic_discord_group_finder_players() : PlayerScript("classic_discord_group_finder_players") { }

    void OnLogout(Player* player) override { GroupFinderListings::OnLogout(player); }
};

void AddSC_classic_discord_commands()
{
    new classic_discord_commandscript();
    new classic_discord_group_finder_groups();
    new classic_discord_group_finder_players();
}
