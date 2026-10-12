/*
 * SKELETON — see ModuleMgr.h and docs/module-system/04-phase2-module-host-design.md.
 * No real boost::process/boost::asio/boost::interprocess implementation yet.
 */

#include "ModuleMgr.h"
#include "Config.h"
#include "Log.h"

ModuleMgr* ModuleMgr::instance()
{
    static ModuleMgr instance;
    return &instance;
}

void ModuleMgr::LoadModules()
{
    if (!sConfigMgr->GetBoolDefault("Modules.Enabled", false))
    {
        TC_LOG_INFO("modules", "ModuleMgr: Modules.Enabled = 0, module system is disabled.");
        return;
    }

    // TODO (doc 04 §1): scan Modules.Dir for module.json + built executable pairs.
    // TODO (doc 06 §2): apply the capability-approval gate (approved_modules.json) before spawning.
    // TODO (doc 04 §2): boost::process spawn + handshake sequencing for each approved module.
    TC_LOG_INFO("modules", "ModuleMgr: LoadModules() skeleton — discovery/spawn not yet implemented.");
}

void ModuleMgr::OnWorldTick(uint64 /*tick*/)
{
    // TODO (doc 04 §4, doc 03 Tier 2 section): for each Tier-2-participating module, check its
    // declared cadence, write a filtered WorldTickSnapshot, read+apply its latest published
    // ActionQueue (non-blocking). No-op until a module with Tier 2 participation actually exists.
}

void ModuleMgr::Unload()
{
    // TODO (doc 04 §3): send Shutdown to every connected module, wait briefly, then terminate.
    _modules.clear();
}
