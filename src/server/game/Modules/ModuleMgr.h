/*
 * Module System — core-side module host.
 * See doc/module-system/04-phase2-module-host-design.md for the design. Implemented: discovery,
 * spawn (boost::process), the Tier 1 handshake and heartbeat, and the restart policy (§5).
 * Not yet implemented: resource limits. Bot control, Tier 2 snapshots with bot data, and Tier 2 bot actions are
 * wired; the bot backend (ModuleBotBackend) is installed by the bot implementation.
 */

#ifndef TRINITYCORE_MODULEMGR_H
#define TRINITYCORE_MODULEMGR_H

#include "Define.h"
#include "ModuleApproval.h"
#include "ModuleBotBackend.h"
#include "ModuleBotOwnership.h"
#include "ModuleSession.h"
#include "ModuleTier2Layout.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace boost::process::v1
{
class child;
}

using ModuleId = uint32;

class ModuleTier2Region;
struct ModuleActionSet;

// Wire protocol this core speaks (Tier 1 schema + Tier 2 layout). Must equal kProtocolVersion in
// forever-module-sdk's include/forever_module/protocol_version.h. Exact match only, no compat
// shims in v1 (doc/module-system/02-phase0-decisions.md §3, doc/module-system/11-sdk-scope.md).
inline constexpr char const* kModuleProtocolVersion = "4";

enum class ModuleStatus : uint8
{
    PendingApproval,    // doc/module-system/06-phase4-approval-audit-design.md §2
    Starting,           // spawned, waiting for the handshake
    Running,            // handshake accepted, heartbeats alive
    TransientFailure,   // failed; a restart is scheduled (RestartAt) — doc 04 §5
    PermanentFailure    // will not be restarted; needs operator action — doc 04 §5
};

char const* ModuleStatusName(ModuleStatus status);

struct ModuleDescriptor
{
    ModuleId Id = 0;
    std::string Name;
    std::string Version;
    std::string ProtocolVersion;     // doc 11 — exact-match gate, not the SDK package version
    CapabilityDeclaration Capabilities; // what the manifest asks for; compared against the approval store
    std::string Executable;          // <Modules.Dir>/<name>/<name>
    std::string Endpoint;            // <Modules.Dir>/<name>.sock

    ModuleStatus Status = ModuleStatus::PendingApproval;
    std::string StatusReason;        // why the module is in its current status
    std::string LastFailure;         // most recent failure reason, kept across restarts

    uint32 HandshakeAttempts = 0;    // consecutive handshake failures; reset when a handshake is accepted
    uint32 RestartAttempts = 0;      // lifetime count of restarts scheduled, for `module info`
    uint64 DroppedEvents = 0;        // notifications not delivered because the module's queue was full
    std::vector<std::chrono::steady_clock::time_point> RestartTimes; // post-handshake restarts in the rolling window
    std::chrono::steady_clock::time_point RestartAt{};             // when a scheduled restart is due; epoch if none

    std::shared_ptr<ModuleSession> Session;             // Tier 1 socket and session thread; shared so routing can use it without _mutex
    std::shared_ptr<boost::process::v1::child> Process;    // the module executable
    std::shared_ptr<ModuleTier2Region> Tier2;              // shared-memory region while the module is connected; see ModuleTier2.h
};

// Read-only view of a module, for logs and console commands.
struct ModuleSnapshot
{
    std::string Name;
    std::string Version;
    std::string ApprovedVersion;     // empty if never approved
    CapabilityDeclaration Capabilities;
    ModuleStatus Status = ModuleStatus::PendingApproval;
    std::string StatusReason;
    std::string LastFailure;
    uint32 RestartAttempts = 0;
    uint64 DroppedEvents = 0;
    uint64 HeartbeatCount = 0;
    uint64 ProcessId = 0;
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
        // handshake. Starts the supervisor thread that performs restarts. No-op until Modules.Enabled = 1.
        //
        // Approval: a module is spawned only when approved_modules.json records its current capabilities (doc 06),
        // or when Modules.AutoApproveLocal = 1 (local development, not for production). Otherwise it stays PendingApproval.
        void LoadModules();

        // Called once per server tick from World::Update, the same way sBotMgr->Update(diff) is
        // called today at World.cpp:2303 (confirmed call-site precedent, doc 12 §2). Iterates
        // Tier-2-participating modules per their declared cadence. No-op until Tier 2 exists.
        void OnWorldTick(uint64 tick);

        // Sends Shutdown to every connected module, gives processes a short grace period, terminates
        // any still running, then stops the supervisor.
        void Unload();

        // Handshake gate: true only when a module's declared protocol_version exactly equals
        // kModuleProtocolVersion. On mismatch, fills reason with a message naming both versions for
        // the log (doc 04: mismatch → permanent failure, never spawned, no restart attempts).
        static bool CheckProtocolVersion(std::string const& declared, std::string& reason);

        // Delivers an event to every running module subscribed to `hook`. Never blocks on a module: the
        // event is queued for the module's session, or dropped if that queue is full.
        void DispatchNotify(std::string_view hook, ModuleWire::NotifyKind kind, uint64 value);

        // Outcome of offering a chat command to the modules (doc 03 "CommandScript").
        enum class CommandRouteResult : uint8
        {
            NotRouted,  // no module claimed the command
            Succeeded,  // a module handled it; resultText is for the player
            Failed,     // a module claimed it and failed; resultText explains why
            TimedOut    // a module that subscribed to CommandScript did not reply in time
        };

        // Delivers PlayerScript.OnLootItem to subscribed modules. Same non-blocking rules as DispatchNotify.
        // Counts a refused bot action for the once-a-minute summary log (see ModuleMgr.cpp).
        static void NoteRefusal(int actionType, int status, std::string const& reason);

        void DispatchItemLooted(std::string_view hook, ModuleWire::ItemLootedEvent const& event);

        // Offers a chat command (without its leading '.') to the modules subscribed to CommandScript,
        // in module id order. The first success wins. A reply with success=false and no text means
        // "not mine" and the next module is asked. The caller must not hold locks that session events need.
        CommandRouteResult RouteCommand(std::string_view commandText, uint64 issuerGuid, std::string& resultText);

        // Installs the core's bot backend. Until one is installed, bot control requests are refused.
        void SetBotBackend(std::shared_ptr<ModuleBotBackend> backend);

        // Core takes a bot back from the module controlling it (bot deleted, logged out, GM action). The module
        // is sent ControlRevoked. Call from the world thread.
        void RevokeBot(uint64 botGuid, std::string const& reason);

        // Sends every accepted module its current config again. Called after a successful config reload, on the world
        // thread. Modules treat each delivery as the full config (doc 03, Config delivery).
        void ResendConfig();

        // Snapshot of every known module, sorted by name.
        std::vector<ModuleSnapshot> GetSnapshots();

        // Operator approval (`module approve <name>`): records the module's current capabilities in
        // approved_modules.json. Reads the manifest from Modules.Dir. Takes effect at the next load.
        // Returns false with an explanation in message on any failure.
        bool ApproveModule(std::string const& name, std::string& message);

    private:
        // Settings read from worldserver.conf in LoadModules().
        struct Config
        {
            uint64 HandshakeTimeoutMs = 5000;
            uint64 HeartbeatTimeoutMs = 15000;
            uint32 MaxHandshakeAttempts = 3;
            uint32 MaxRestartAttempts = 5;
            uint64 RestartBackoffMs = 1000;
            uint64 CommandRequestTimeoutMs = 200;
            // Upper bounds for Tier 2 grants. A module's request is capped by these (doc 03, Tier 2).
            uint32 Tier2MaxBots = 64;
            uint32 Tier2MaxNearbyUnits = 16;
            uint32 Tier2MaxActions = 256;
            uint32 Tier2MaxTickInterval = 100;
            // Each bot's nearby units are queried on one tick in this many, staggered by guid, so the world thread pays for a
            // fraction of the range queries per tick. 0 (auto) picks one tick in ceil(bots / 400): 1 up to 400 bots, 4 at 1500.
            uint32 Tier2NearbyRefreshTicks = 0;
            bool AutoApprove = false;
        };

        void ProcessModuleDirectory(std::string const& root, std::string const& name);
        // Applies the actions a module published, for the bots that module controls and that are online.
        // Called on the world thread with no lock held.
        void ApplyModuleActions(ModuleId module, ModuleActionSet const& actions);

        // What the last action applied to each bot gave (world thread only). Copied into the bot's snapshot entry.
        struct ActionResult
        {
            forever_module::BotActionStatus Status = forever_module::BotActionStatus::kNone;
            uint8 Type = 0;   // ActionType
            uint32 Spell = 0;
            uint32 Sequence = 0;
        };
        std::unordered_map<uint64, ActionResult> _actionResults;
        // The last nearby units of each controlled bot (world thread only), reused between refreshes.
        std::unordered_map<uint64, std::vector<forever_module::NearbyUnit>> _nearbyCache;
        void RecordActionResult(uint64 botGuid, forever_module::BotAction const& action, forever_module::BotActionStatus status);
        void LoadApprovalStore(); // called with _mutex held, or before any module threads exist
        std::string ApprovalStorePath() const;
        // Starts one attempt: listen, spawn the process, start the session. Callers must not hold _mutex.
        bool SpawnModule(ModuleId id);
        void OnSessionEvent(ModuleId id, ModuleSession::Event event, std::string const& detail);
        // Handshake hook for Tier 2: creates the module's region and fills the response's grant.
        bool NegotiateTier2(ModuleId id, ModuleWire::HandshakeRequest const& request,
                            ModuleWire::HandshakeResponse& response, std::string& reason);

        // Bot control requests and releases from a module. Queued on the session thread, run on the world thread.
        struct PendingControl
        {
            ModuleId Module = 0;
            std::weak_ptr<ModuleSession> Session;
            uint64 MessageId = 0;
            bool Release = false;
            ModuleWire::ControlRequestMsg Request; // for a release, Request.BotGuid is the bot
        };
        void EnqueueControl(PendingControl pending);
        void RunPendingControls();
        ModuleWire::ControlResponseMsg ExecuteControlRequest(ModuleId module, ModuleWire::ControlRequestMsg const& request);

        // Module -> core actions. EnqueueAction runs on the session thread and only queues; RunPendingActions
        // runs on the world thread from OnWorldTick and sends the replies.
        struct PendingAction
        {
            ModuleId Module = 0;
            std::weak_ptr<ModuleSession> Session;
            ModuleWire::ActionRequestMsg Request;
        };
        void EnqueueAction(ModuleId module, std::weak_ptr<ModuleSession> session, ModuleWire::ActionRequestMsg request);
        void RunPendingActions();

        // Failure bookkeeping. Callers must hold _mutex.
        void SetStatus(ModuleDescriptor& descriptor, ModuleStatus status, std::string reason);
        void ScheduleRestart(ModuleDescriptor& descriptor, bool handshakeFailure);
        void MarkPermanent(ModuleDescriptor& descriptor, std::string reason);
        static void StopProcess(ModuleDescriptor& descriptor);

        // Restarts due transient failures. Runs on its own thread so that restarts (and the module
        // processes they spawn) live on a thread that only exits during Unload().
        void StartSupervisor();
        void StopSupervisor();
        void SupervisorLoop();
        void RestartDueModules();

        Config _config;
        ApprovalStore _approvals;
        bool _approvalsLoaded = false;
        std::string _approvalLoadError; // non-empty if approved_modules.json exists but cannot be read

        std::mutex _mutex; // guards _modules and descriptor fields; never held while stopping sessions
        std::mutex _actionMutex; // guards _pendingActions; held only to push or pop
        std::deque<PendingAction> _pendingActions;
        std::deque<PendingControl> _pendingControls;

        std::shared_ptr<ModuleBotBackend> _botBackend; // guarded by _mutex
        ModuleBotOwnership _owners;                    // guarded by _mutex; changed on the world thread only

        std::mutex _lifecycleMutex; // serializes restarts with Unload(); held across a restart attempt
        std::unordered_map<ModuleId, ModuleDescriptor> _modules;
        ModuleId _nextModuleId = 1;

        std::atomic<bool> _unloading{false};       // set by Unload() so expected disconnects are not reported as failures
        std::atomic<bool> _supervisorStop{false};
        std::thread _supervisor;
};

#define sModuleMgr ModuleMgr::instance()

#endif // TRINITYCORE_MODULEMGR_H
