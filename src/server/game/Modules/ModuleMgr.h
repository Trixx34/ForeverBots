/*
 * Module System — core-side module host.
 * See doc/module-system/04-phase2-module-host-design.md for the design. This file implements
 * discovery, spawn (boost::process), the Tier 1 handshake and heartbeat. Not yet implemented:
 * restart policy, approval store (doc 06), resource limits, Tier 1 event routing, Tier 2, bot control.
 */

#ifndef TRINITYCORE_MODULEMGR_H
#define TRINITYCORE_MODULEMGR_H

#include "Define.h"
#include "ModuleSession.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace boost::process
{
class child;
}

using ModuleId = uint32;

// Wire protocol this core speaks (Tier 1 schema + Tier 2 layout). Must equal kProtocolVersion in
// forever-module-sdk's include/forever_module/protocol_version.h. Exact match only, no compat
// shims in v1 (doc/module-system/02-phase0-decisions.md §3, doc/module-system/11-sdk-scope.md).
inline constexpr char const* kModuleProtocolVersion = "1";

enum class ModuleStatus : uint8
{
    PendingApproval,    // doc/module-system/06-phase4-approval-audit-design.md §2
    Starting,           // spawned, waiting for the handshake
    Running,            // handshake accepted, heartbeats alive
    TransientFailure,   // crash/heartbeat timeout/handshake timeout; restart policy applies (not yet implemented)
    PermanentFailure    // version mismatch / rejected handshake / bad manifest or executable — doc 04 §5
};

struct ModuleDescriptor
{
    ModuleId Id = 0;
    std::string Name;
    std::string Version;
    std::string ProtocolVersion;     // doc 11 — exact-match gate, not the SDK package version
    ModuleStatus Status = ModuleStatus::PendingApproval;
    std::string StatusReason;        // why the module is in its current status, for logs and `module info`
    uint32 RestartAttempts = 0;

    std::unique_ptr<ModuleSession> Session;             // owns the Tier 1 socket and session thread
    std::shared_ptr<boost::process::child> Process;     // the module executable
};

// Read-only view of a module, for logs and console commands.
struct ModuleSnapshot
{
    std::string Name;
    ModuleStatus Status = ModuleStatus::PendingApproval;
    std::string StatusReason;
    uint64 HeartbeatCount = 0;
    bool ProcessRunning = false;
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
        ~ModuleMgr();

    public:
        ModuleMgr(ModuleMgr const&) = delete;
        ModuleMgr(ModuleMgr&&) = delete;
        ModuleMgr& operator=(ModuleMgr const&) = delete;
        ModuleMgr& operator=(ModuleMgr&&) = delete;

        static ModuleMgr* instance();

        // Scans Modules.Dir for <name>/module.json + <name>/<name> pairs, and for each one: checks
        // the protocol version, requires approval (see below), then spawns the module and runs its
        // handshake. No-op until Modules.Enabled = 1.
        //
        // Approval: the approval store (doc 06) is not implemented yet, so modules are only spawned
        // when Modules.AutoApproveLocal = 1 (local development). Otherwise they stay PendingApproval.
        void LoadModules();

        // Called once per server tick from World::Update, the same way sBotMgr->Update(diff) is
        // called today at World.cpp:2303 (confirmed call-site precedent, doc 12 §2). Iterates
        // Tier-2-participating modules per their declared cadence. No-op until Tier 2 exists.
        void OnWorldTick(uint64 tick);

        // Stops every session (sends Shutdown), gives processes a short grace period, then
        // terminates any that are still running.
        void Unload();

        // Handshake gate: true only when a module's declared protocol_version exactly equals
        // kModuleProtocolVersion. On mismatch, fills reason with a message naming both versions for
        // the log (doc 04: mismatch → permanent failure, never spawned, no restart attempts).
        static bool CheckProtocolVersion(std::string const& declared, std::string& reason);

        // Snapshot of every known module, sorted by name.
        std::vector<ModuleSnapshot> GetSnapshots();

        // TODO (doc 04 §6): RequestControl/ReleaseControl/bot ownership map — needs Tier 1 event
        // routing before there's anything meaningful to implement here.

    private:
        void ProcessModuleDirectory(std::string const& root, std::string const& name, uint64 handshakeTimeoutMs,
                                    uint64 heartbeatTimeoutMs, bool autoApprove);
        bool SpawnModule(ModuleId id, std::string const& executable, std::string const& endpoint,
                         uint64 handshakeTimeoutMs, uint64 heartbeatTimeoutMs);
        void OnSessionEvent(ModuleId id, ModuleSession::Event event, std::string const& detail);
        // Callers must hold _mutex.
        void SetStatus(ModuleDescriptor& descriptor, ModuleStatus status, std::string reason);
        static void StopProcess(ModuleDescriptor& descriptor);

        std::mutex _mutex; // guards _modules and descriptor fields; never held while stopping sessions
        std::unordered_map<ModuleId, ModuleDescriptor> _modules;
        ModuleId _nextModuleId = 1;
        std::atomic<bool> _unloading{false}; // set by Unload() so expected disconnects are not reported as failures
};

#define sModuleMgr ModuleMgr::instance()

#endif // TRINITYCORE_MODULEMGR_H
