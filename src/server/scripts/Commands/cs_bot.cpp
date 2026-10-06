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
#include "StringFormat.h"

using namespace Trinity::ChatCommands;

// Console/GM commands of the player-bot subsystem (spawn, despawn, list; Phase 1). See docs/playerbots/implementation-plan.md.
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
            { "spawn",   HandleBotSpawnCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "despawn", HandleBotDespawnCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "list",    HandleBotListCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "stats",   HandleBotStatsCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
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

    // bot spawn <count> [class] [faction] [level]   class: warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid|any,
    // faction: alliance|horde|any. Reuses existing bot characters first, creates the rest. Logins are spread over a few ticks.
    static bool HandleBotSpawnCommand(ChatHandler* handler, uint32 count, Optional<std::string> classArg, Optional<std::string> factionArg, Optional<uint8> level)
    {
        uint8 classId = 0;
        if (classArg && !StringEqualI(*classArg, "any"))
        {
            classId = BotMgr::ParseClass(*classArg);
            if (!classId)
            {
                handler->PSendSysMessage("Unknown class '%s' (warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid or any).", classArg->c_str());
                return false;
            }
        }

        int8 faction = -1;
        if (factionArg && !StringEqualI(*factionArg, "any"))
        {
            faction = BotMgr::ParseFaction(*factionArg);
            if (faction < 0)
            {
                handler->PSendSysMessage("Unknown faction '%s' (alliance, horde or any).", factionArg->c_str());
                return false;
            }
        }

        if (count < 1 || count > 1000)
        {
            handler->PSendSysMessage("%s", "Count must be between 1 and 1000.");
            return false;
        }

        BotSpawnResult result = sBotMgr->SpawnBots(count, classId, faction, level);
        handler->PSendSysMessage("Bot spawn: %u requested, %u reused, %u created, %u failed%s%s", count, result.Reused, result.Created, result.Failed,
            result.Error.empty() ? "" : " - ", result.Error.c_str());
        for (std::string const& name : result.Names)
            handler->PSendSysMessage("  queued %s", name.c_str());

        return result.Error.empty() || !result.Names.empty();
    }

    // bot despawn [all|<name>]: saves and logs the bot(s) out.
    static bool HandleBotDespawnCommand(ChatHandler* handler, Optional<std::string> name)
    {
        uint32 count = sBotMgr->DespawnBots(name ? *name : std::string());
        handler->PSendSysMessage("Bot despawn: %u bots logged out or queued for logout.", count);
        return count > 0 || !name || StringEqualI(*name, "all");
    }

    // bot stats: counts per faction, class and race over all known bots (to check a batch is balanced).
    static bool HandleBotStatsCommand(ChatHandler* handler)
    {
        for (std::string const& line : sBotMgr->GetStats())
            handler->PSendSysMessage("%s", line.c_str());
        return true;
    }

    // bot list: one line per known bot character (online bots show their live position, offline ones the saved one).
    static bool HandleBotListCommand(ChatHandler* handler)
    {
        std::vector<BotInfo> bots = sBotMgr->ListBots();
        uint32 online = 0;
        for (BotInfo const& bot : bots)
        {
            if (bot.State == BOT_ONLINE)
                ++online;

            handler->PSendSysMessage("%llu %s %s race %u %s L%u %s map %u zone %u (%.1f, %.1f, %.1f)", (unsigned long long)bot.Guid, bot.Name.c_str(),
                BotMgr::ClassName(bot.Class), uint32(bot.Race), bot.Horde ? "horde" : "alliance", uint32(bot.Level), BotMgr::StateName(bot.State),
                uint32(bot.MapId), uint32(bot.ZoneId), bot.X, bot.Y, bot.Z);
        }

        handler->PSendSysMessage("Bots: %u known, %u online.", uint32(bots.size()), online);
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
