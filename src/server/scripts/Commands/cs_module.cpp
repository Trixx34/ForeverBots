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
%Complete: 100
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

    // Records the module's current capabilities as approved. Takes effect at the next module load.
    static bool HandleModuleApproveCommand(ChatHandler* handler, std::string const& moduleName)
    {
        std::string message;
        sModuleMgr->ApproveModule(moduleName, message);
        handler->PSendSysMessage("{}", message);
        return true;
    }

    static bool HandleModuleListCommand(ChatHandler* handler)
    {
        std::vector<ModuleSnapshot> modules = sModuleMgr->GetSnapshots();
        if (modules.empty())
        {
            handler->SendSysMessage("No modules are loaded (see Modules.Enabled and Modules.Dir).");
            return true;
        }

        for (ModuleSnapshot const& m : modules)
            handler->PSendSysMessage("{} {} [{}] {}", m.Name, m.Version, ModuleStatusName(m.Status), m.StatusReason);
        return true;
    }

    static bool HandleModuleInfoCommand(ChatHandler* handler, std::string const& moduleName)
    {
        for (ModuleSnapshot const& m : sModuleMgr->GetSnapshots())
        {
            if (m.Name != moduleName)
                continue;

            std::string hooks;
            for (std::string const& hook : m.Capabilities.Hooks)
                hooks += (hooks.empty() ? "" : ", ") + hook;

            handler->PSendSysMessage("Module {} version {}", m.Name, m.Version);
            handler->PSendSysMessage("  status:      {} ({})", ModuleStatusName(m.Status), m.StatusReason);
            handler->PSendSysMessage("  approved:    {}", m.ApprovedVersion.empty() ? std::string("never") : m.ApprovedVersion);
            handler->PSendSysMessage("  tier 1 hooks: {}", hooks.empty() ? std::string("none") : hooks);
            handler->PSendSysMessage("  tier 2:      {}", m.Capabilities.Tier2Participates ? "requested" : "no");
            handler->PSendSysMessage("  process:     {}", m.ProcessRunning ? "pid " + std::to_string(m.ProcessId) : std::string("not running"));
            handler->PSendSysMessage("  heartbeats:  {}", m.HeartbeatCount);
            handler->PSendSysMessage("  restarts:    {}", m.RestartAttempts);
            if (!m.LastFailure.empty())
                handler->PSendSysMessage("  last failure: {}", m.LastFailure);
            return true;
        }

        handler->PSendSysMessage("No module named '{}'.", moduleName);
        return true;
    }
};

void AddSC_module_commandscript()
{
    new module_commandscript();
}
