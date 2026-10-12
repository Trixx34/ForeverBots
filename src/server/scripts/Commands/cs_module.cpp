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

/* ScriptData
Name: module_commandscript
%Complete: 10 (skeleton -- see docs/module-system/06-phase4-approval-audit-design.md §3)
Comment: Operator commands for the external module system: approve/list/info.
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ModuleMgr.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

class module_commandscript : public CommandScript
{
public:
    module_commandscript() : CommandScript("module_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable moduleCommandTable =
        {
            { "approve", HandleModuleApproveCommand, rbac::RBAC_PERM_COMMAND_MODULE, Console::Yes },
            { "list",    HandleModuleListCommand,    rbac::RBAC_PERM_COMMAND_MODULE, Console::Yes },
            { "info",    HandleModuleInfoCommand,    rbac::RBAC_PERM_COMMAND_MODULE, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "module", moduleCommandTable },
        };

        return commandTable;
    }

    // SKELETON: all three handlers just report "not yet implemented" -- see
    // docs/module-system/12-initial-core-changes-scope.md. ModuleMgr itself has no discovery/
    // approval-store logic yet for these to call into.

    static bool HandleModuleApproveCommand(ChatHandler* handler, std::string const& /*moduleName*/)
    {
        handler->SendSysMessage("Module system: approval flow not yet implemented (skeleton).");
        return true;
    }

    static bool HandleModuleListCommand(ChatHandler* handler)
    {
        handler->SendSysMessage("Module system: discovery not yet implemented (skeleton). No modules to list.");
        return true;
    }

    static bool HandleModuleInfoCommand(ChatHandler* handler, std::string const& /*moduleName*/)
    {
        handler->SendSysMessage("Module system: discovery not yet implemented (skeleton).");
        return true;
    }
};

void AddSC_module_commandscript()
{
    new module_commandscript();
}
