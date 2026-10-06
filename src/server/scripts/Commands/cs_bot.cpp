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

#include "ScriptMgr.h"
#include "BotMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

// Phase 0 hello-world for the player-bot subsystem. See docs/playerbots/implementation-plan.md.
// Uses the dedicated RBAC_PERM_COMMAND_BOT (granted to Gamemaster/Administrator by default,
// see sql/custom/auth/2026_10_05_00_auth_rbac_bot.sql).
class bot_commandscript : public CommandScript
{
public:
    bot_commandscript() : CommandScript("bot_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable botCommandTable =
        {
            { "hello",   HandleBotHelloCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "logtest", HandleBotLogTestCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "bot", botCommandTable },
        };

        return commandTable;
    }

    static bool HandleBotHelloCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("%s", sBotMgr->GetStatus().c_str());
        return true;
    }

    // Writes one test row (bot_guid 0, event_type 'test') to the bot log and flushes, to verify the pipeline end to end.
    static bool HandleBotLogTestCommand(ChatHandler* handler)
    {
        if (!sBotMgr->IsLogDatabaseAvailable())
        {
            handler->PSendSysMessage("%s", "Bot log is off: set BotLogDatabaseInfo in worldserver.conf and restart.");
            return true;
        }

        BotEvent event;
        event.Type = "test";
        event.Severity = BOTLOG_INFO;
        event.Reason = "LOGTEST";
        event.Summary = "bot log test row from .bot logtest";
        event.Details = R"({"source":"cs_bot","note":"safe to delete"})";
        sBotMgr->LogEvent(std::move(event));
        sBotMgr->FlushLog();

        handler->PSendSysMessage("%s", "Bot log test event queued and flushed (check bot_event where event_type = 'test').");
        return true;
    }
};

void AddSC_bot_commandscript()
{
    new bot_commandscript();
}
