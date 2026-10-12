/*
 * Module System — core-side module host.
 * SKELETON: see docs/module-system/04-phase2-module-host-design.md for the full design and
 * docs/module-system/12-initial-core-changes-scope.md for what's implemented in this pass.
 *
 * No real boost::process spawning, boost::asio socket server, or boost::interprocess shared
 * memory yet -- method bodies in ModuleMgr.cpp are stubs. This establishes the class shape so it
 * can be iterated on alongside the forever-module-sdk client, which is at the same skeleton depth.
 */

#ifndef TRINITYCORE_MODULEMGR_H
#define TRINITYCORE_MODULEMGR_H

#include "Define.h"
#include <cstdint>
#include <string>
#include <unordered_map>

using ModuleId = uint32;

enum class ModuleStatus : uint8
{
    PendingApproval,    // docs/module-system/06-phase4-approval-audit-design.md §2
    Running,
    TransientFailure,   // crash/heartbeat timeout, restart policy applies — doc 04 §5
    PermanentFailure    // version mismatch / repeated handshake timeout / max restarts exceeded — doc 04 §5
};

struct ModuleDescriptor
{
    ModuleId Id = 0;
    std::string Name;
    std::string Version;
    std::string ProtocolVersion;     // doc 11 — exact-match gate, not the SDK package version
    ModuleStatus Status = ModuleStatus::PendingApproval;
    uint32 RestartAttempts = 0;

    // TODO: declared tier1 hooks, tier2 participation + negotiated bounds (doc 04 §3),
    // process handle, Tier 1 socket handle, Tier 2 shared-memory handle.
};

/*
 * Singleton core-side module host. See doc 04 for the full responsibility list:
 *   - discovery (scan Modules.Dir for manifest + built executable)
 *   - process spawning + handshake sequencing
 *   - restart policy (transient vs. permanent failure)
 *   - Tier 1 event routing (EventNotify/EventRequest) to subscribed modules
 *   - Tier 2 per-tick snapshot/action exchange (OnWorldTick)
 *   - bot ownership map (ControlRequest/ReleaseControl/ControlRevoked, doc 04 §6)
 */
class TC_GAME_API ModuleMgr
{
    private:
        ModuleMgr() = default;
        ~ModuleMgr() = default;

    public:
        ModuleMgr(ModuleMgr const&) = delete;
        ModuleMgr(ModuleMgr&&) = delete;
        ModuleMgr& operator=(ModuleMgr const&) = delete;
        ModuleMgr& operator=(ModuleMgr&&) = delete;

        static ModuleMgr* instance();

        // Scans Modules.Dir for module.json + built executable pairs, applies the capability
        // approval gate (doc 06), and spawns approved modules. No-op (logs nothing found) until
        // Modules.Enabled = 1 and at least one module exists in Modules.Dir.
        void LoadModules();

        // Called once per server tick from World::Update, the same way sBotMgr->Update(diff) is
        // called today at World.cpp:2303 (confirmed call-site precedent, doc 12 §2). Iterates
        // Tier-2-participating modules per their declared cadence. No-op in this skeleton pass.
        void OnWorldTick(uint64 tick);

        // Clean shutdown: sends Shutdown to every connected module. No-op in this skeleton pass.
        void Unload();

        // TODO (doc 04 §6): RequestControl/ReleaseControl/bot ownership map — needs a real Tier 1
        // connection to a module before there's anything meaningful to implement here.

    private:
        std::unordered_map<ModuleId, ModuleDescriptor> _modules;
        ModuleId _nextModuleId = 1;
};

#define sModuleMgr ModuleMgr::instance()

#endif // TRINITYCORE_MODULEMGR_H
