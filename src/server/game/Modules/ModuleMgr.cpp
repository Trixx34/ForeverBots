/*
 * Module System — core-side module host. See ModuleMgr.h and doc/module-system/04-phase2-module-host-design.md.
 */

#include "ModuleMgr.h"
#include <map>
#include <functional>
#include <chrono>
#include "Config.h"
#include "Log.h"
#include "ModuleActions.h"
#include "ModuleBotActions.h"
#include "ModuleBotSnapshot.h"
#include "ModuleConfig.h"
#include "ModuleTier2.h"
#include "BotMgr.h"
#include "ModuleManifestInfo.h"

#include <optional>

#include <boost/process/v1.hpp>

#include <algorithm>
#include <iterator>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bp = boost::process::v1;
namespace fs = std::filesystem;

using Clock = std::chrono::steady_clock;

namespace
{

// Tier 1 hooks a module may request. Anything else is refused at handshake. Keep in step with the
// SDK's events.proto and doc 03 "Initial hook subset (v1)".
std::vector<std::string> const SupportedTier1Hooks = {
    "PlayerScript.OnLogin",
    "PlayerScript.OnLogout",
    "AccountScript.OnAccountLogin",
    "WorldScript.OnStartup",
    "WorldScript.OnShutdown",
    "CommandScript",
    "PlayerScript.OnLootItem",
    "PlayerScript.OnChat",
};

// How long a module gets to exit after Shutdown before it is terminated.
constexpr std::chrono::milliseconds ShutdownGrace{2000};

// A module's executable is <Modules.Dir>/<name>/<name>, with the platform's executable suffix.
#ifdef _WIN32
constexpr char const* ModuleExecutableSuffix = ".exe";
#else
constexpr char const* ModuleExecutableSuffix = "";
#endif

// Module actions waiting for the world thread, and how many one world tick runs.
constexpr size_t MaxPendingActions = 256;
constexpr size_t MaxActionsPerTick = 32;

// Restart policy (doc 04 §5). The window is fixed here; the budgets and base backoff are config keys.
constexpr std::chrono::minutes RestartWindow{10};
constexpr std::chrono::milliseconds MaxRestartBackoff{60000};
constexpr std::chrono::milliseconds SupervisorPollInterval{50};

// Exponential backoff: base * 2^(n-1) for the n-th failure, capped at MaxRestartBackoff.
std::chrono::milliseconds BackoffFor(uint64 baseMs, uint32 failureNumber)
{
    uint32 const shift = std::min<uint32>(failureNumber > 0 ? failureNumber - 1 : 0, 16);
    auto const backoff = std::chrono::milliseconds(baseMs << shift);
    return std::min(backoff, MaxRestartBackoff);
}

} // namespace

ModuleMgr::~ModuleMgr()
{
    Unload();
}

ModuleMgr* ModuleMgr::instance()
{
    static ModuleMgr instance;
    return &instance;
}

void ModuleMgr::LoadModules()
{
    _unloading = false; // a previous Unload() must not suppress events for modules loaded now

    // Loaded before the Enabled check so `module approve` always works on the full store.
    LoadApprovalStore();

    if (!sConfigMgr->GetBoolDefault("Modules.Enabled", false))
    {
        TC_LOG_INFO("modules", "Modules.Enabled = 0, module system is disabled.");
        return;
    }

    _config.HandshakeTimeoutMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.HandshakeTimeoutMs", 5000));
    _config.HeartbeatTimeoutMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.HeartbeatTimeoutMs", 15000));
    _config.MaxHandshakeAttempts = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.MaxHandshakeAttempts", 3));
    _config.MaxRestartAttempts = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.MaxRestartAttempts", 5));
    _config.RestartBackoffMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.RestartBackoffMs", 1000));
    _config.CommandRequestTimeoutMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.CommandRequestTimeoutMs", 200));
    _config.AutoApprove = sConfigMgr->GetBoolDefault("Modules.AutoApproveLocal", false);
    _config.Tier2MaxBots = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.Tier2.MaxBots", 1500));
    _config.Tier2MaxNearbyUnits = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.Tier2.MaxNearbyUnits", 16));
    _config.Tier2MaxActions = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.Tier2.MaxActions", 3000));
    _config.Tier2MaxTickInterval = static_cast<uint32>(sConfigMgr->GetIntDefault("Modules.Tier2.MaxTickInterval", 100));
    _config.Tier2NearbyRefreshTicks = static_cast<uint32>(std::clamp<int32>(sConfigMgr->GetIntDefault("Modules.Tier2.NearbyRefreshTicks", 0), 0, 16));

    std::string const root = sConfigMgr->GetStringDefault("Modules.Dir", "modules_runtime");

    std::error_code ec;
    if (!fs::is_directory(root, ec))
    {
        TC_LOG_INFO("modules", "No module directory at '{}', nothing to load.", root);
        return;
    }

    std::vector<std::string> names;
    for (fs::directory_entry const& entry : fs::directory_iterator(root, ec))
        if (entry.is_directory(ec))
            names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());

    for (std::string const& name : names)
    {
        if (!IsValidModuleName(name))
        {
            TC_LOG_WARN("modules", "'{}' is not a valid module directory name (letters, digits, '_' and '-' only), skipping.", name);
            continue;
        }
        ProcessModuleDirectory(root, name);
    }

    StartSupervisor();
}

void ModuleMgr::ProcessModuleDirectory(std::string const& root, std::string const& name)
{
    fs::path const dir = fs::path(root) / name;

    std::string json;
    {
        std::ifstream in(dir / "module.json");
        if (!in)
        {
            TC_LOG_WARN("modules", "'{}' has no module.json, skipping.", name);
            return;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        json = buffer.str();
    }

    ModuleManifestInfo info;
    std::string error;
    if (!ParseModuleManifest(json, info, error))
    {
        TC_LOG_ERROR("modules", "'{}' not loaded: {}", name, error);
        return;
    }
    if (info.Name != name)
    {
        TC_LOG_WARN("modules", "'{}': module.json name '{}' does not match the directory name, skipping.", name, info.Name);
        return;
    }

    ModuleId id = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        id = _nextModuleId++;
        ModuleDescriptor& d = _modules[id];
        d.Id = id;
        d.Name = name;
        d.Version = info.Version;
        d.ProtocolVersion = info.ProtocolVersion;
        d.Capabilities = CapabilitiesOf(info);
        d.Executable = (dir / (name + ModuleExecutableSuffix)).string();
        d.Endpoint = (fs::path(root) / (name + ".sock")).string();
    }

    std::string reason;
    if (!CheckProtocolVersion(info.ProtocolVersion, reason))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        MarkPermanent(_modules.at(id), reason);
        TC_LOG_ERROR("modules", "'{}' not loaded: {}", name, reason);
        return;
    }

    // Approval gate (doc 06): the module runs only if the capabilities it declares are the ones an
    // operator approved. Deny by default.
    CapabilityDeclaration const capabilities = CapabilitiesOf(info);
    std::string const hash = CapabilityHash(capabilities);

    bool approved = false;
    std::string pendingReason;
    std::vector<std::string> changes;
    if (_config.AutoApprove)
    {
        TC_LOG_WARN("modules", "'{}' runs under Modules.AutoApproveLocal = 1: its capabilities are not checked. Development only.", name);
        approved = true;
    }
    else if (!_approvalLoadError.empty())
    {
        pendingReason = "approval store unreadable: " + _approvalLoadError;
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ApprovalRecord const* record = _approvals.Find(name);
        if (!record)
            pendingReason = "never approved; run 'module approve " + name + "' to approve version " + info.Version;
        else if (record->CapabilityHash != hash)
        {
            changes = DescribeCapabilityChanges(record->Capabilities, capabilities);
            pendingReason = "capabilities changed since version " + record->ApprovedVersion + ": ";
            for (size_t i = 0; i < changes.size(); ++i)
                pendingReason += (i ? "; " : "") + changes[i];
            pendingReason += "; run 'module approve " + name + "' to approve";
        }
        else
            approved = true;
    }

    if (!approved)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::PendingApproval, pendingReason);
        TC_LOG_INFO("modules", "'{}' {} is pending approval and was not started.", name, info.Version);
        for (std::string const& change : changes)
            TC_LOG_INFO("modules", "'{}' capability change: {}", name, change);
        return;
    }

    std::error_code ec;
    std::string executable;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        executable = _modules.at(id).Executable;
    }
    if (!fs::is_regular_file(executable, ec))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        MarkPermanent(_modules.at(id), "executable not found: " + executable);
        TC_LOG_ERROR("modules", "'{}' not loaded: executable not found at '{}'.", name, executable);
        return;
    }

    SpawnModule(id);
}

// Keys the operator wrote as Module.<name>.<Key> (in a .conf file under the config directory, see worldserver.conf.dist).
// The prefix is stripped, so the module sees its own key names. See doc/module-system/03, Config delivery.
static std::vector<ModuleWire::ConfigEntry> LoadModuleConfig(std::string const& moduleName)
{
    return ModuleConfigFor(moduleName, sConfigMgr->GetKeysByString("Module." + moduleName + "."),
                           [](std::string const& key) { return sConfigMgr->GetStringDefault(key, "", true); });
}

void ModuleMgr::ResendConfig()
{
    std::vector<std::pair<std::string, std::shared_ptr<ModuleSession>>> accepted;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& entry : _modules)
            if (entry.second.Session && entry.second.Session->IsAccepted())
                accepted.emplace_back(entry.second.Name, entry.second.Session);
    }

    for (auto const& [name, session] : accepted)
    {
        if (!session->SendConfig(LoadModuleConfig(name)))
            TC_LOG_WARN("modules", "'{}': could not queue the reloaded config; it keeps the previous one.", name);
    }
}

bool ModuleMgr::SpawnModule(ModuleId id)
{
    ModuleSession::Settings settings;
    std::string executable;
    std::string endpoint;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ModuleDescriptor const& d = _modules.at(id);
        executable = d.Executable;
        endpoint = d.Endpoint;
        name = d.Name;
    }
    settings.Endpoint = endpoint;
    settings.HandshakeTimeout = std::chrono::milliseconds(_config.HandshakeTimeoutMs);
    settings.HeartbeatTimeout = std::chrono::milliseconds(_config.HeartbeatTimeoutMs);
    settings.ExpectedProtocolVersion = kModuleProtocolVersion;
    settings.SupportedHooks = SupportedTier1Hooks;
    settings.NegotiateTier2 = [this, id](ModuleWire::HandshakeRequest const& request, ModuleWire::HandshakeResponse& response,
                                         std::string& reason)
    {
        return NegotiateTier2(id, request, response, reason);
    };
    settings.Config = LoadModuleConfig(name);

    auto session = std::make_shared<ModuleSession>(settings, id, [this, id](ModuleSession::Event event, std::string const& detail)
    {
        OnSessionEvent(id, event, detail);
    });
    session->SetActionHandler([this, id, weak = std::weak_ptr<ModuleSession>(session)](ModuleWire::ActionRequestMsg const& request)
    {
        EnqueueAction(id, weak, request);
    });
    session->SetControlHandlers(
        [this, id, weak = std::weak_ptr<ModuleSession>(session)](uint64 messageId, ModuleWire::ControlRequestMsg const& request)
        {
            EnqueueControl(PendingControl{ id, weak, messageId, false, request });
        },
        [this, id, weak = std::weak_ptr<ModuleSession>(session)](ModuleWire::ReleaseControlMsg const& release)
        {
            ModuleWire::ControlRequestMsg request;
            request.BotGuid = release.BotGuid;
            EnqueueControl(PendingControl{ id, weak, 0, true, request });
        });

    std::string error;
    if (!session->Listen(error))
    {
        // Binding the endpoint is a local configuration problem; a restart will not fix it.
        std::lock_guard<std::mutex> lock(_mutex);
        MarkPermanent(_modules.at(id), "cannot listen for the module: " + error);
        TC_LOG_ERROR("modules", "'{}': cannot listen for its connection: {}", name, error);
        return false;
    }

    // Status must be Starting before the session thread runs, so a fast handshake cannot be overwritten.
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::Starting, "waiting for the module to connect");
    }
    session->Start();

    std::shared_ptr<bp::child> process;
    try
    {
        process = std::make_shared<bp::child>(executable, endpoint);
    }
    catch (std::exception const& e)
    {
        session->Stop();
        std::lock_guard<std::mutex> lock(_mutex);
        ModuleDescriptor& d = _modules.at(id);
        d.LastFailure = std::string("failed to start process: ") + e.what();
        ScheduleRestart(d, /*handshakeFailure=*/true);
        TC_LOG_ERROR("modules", "'{}': failed to start '{}': {}", name, executable, e.what());
        return false;
    }

    // Store the process under the lock, so a session event that fires now sees it and can stop it.
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ModuleDescriptor& d = _modules.at(id);
        d.Session = std::move(session);
        d.Process = process;
        TC_LOG_INFO("modules", "started '{}' (pid {}), waiting for its handshake.", d.Name, process->id());
    }
    return true;
}

bool ModuleMgr::NegotiateTier2(ModuleId id, ModuleWire::HandshakeRequest const& request,
                               ModuleWire::HandshakeResponse& response, std::string& reason)
{
    // Core may grant less than the module asked for; the module must size its view from the response.
    ModuleTier2Region::Bounds bounds;
    bounds.MaxBots = std::min(request.RequestedMaxBots, _config.Tier2MaxBots);
    bounds.MaxNearbyUnits = std::min(request.RequestedMaxNearbyUnits, _config.Tier2MaxNearbyUnits);
    bounds.MaxActions = std::min(request.RequestedMaxActions, _config.Tier2MaxActions);
    uint32 const requestedInterval = request.RequestedTickInterval == 0 ? 1 : request.RequestedTickInterval;
    bounds.TickInterval = std::min(requestedInterval, _config.Tier2MaxTickInterval);

    std::string const name = "trinity_module_" + std::to_string(id) + "_tier2";

    {
        // Release the previous region first: its destructor removes the name, which must not hit the new one.
        std::lock_guard<std::mutex> lock(_mutex);
        _modules.at(id).Tier2.reset();
    }

    std::string error;
    std::shared_ptr<ModuleTier2Region> region = ModuleTier2Region::Create(name, bounds, error);
    if (!region)
    {
        reason = "Tier 2 region: " + error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _modules.at(id).Tier2 = region;
    }

    response.Tier2RegionName = name;
    response.NegotiatedMaxBots = bounds.MaxBots;
    response.NegotiatedMaxNearbyUnits = bounds.MaxNearbyUnits;
    response.NegotiatedMaxActions = bounds.MaxActions;
    response.NegotiatedTickInterval = bounds.TickInterval;
    TC_LOG_INFO("modules", "module {}: Tier 2 region '{}' (bots {}, nearby {}, actions {}, tick interval {}).",
                id, name, bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxActions, bounds.TickInterval);
    return true;
}

void ModuleMgr::OnSessionEvent(ModuleId id, ModuleSession::Event event, std::string const& detail)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _modules.find(id);
    if (it == _modules.end())
        return;
    ModuleDescriptor& d = it->second;

    if (_unloading)
    {
        // The core is shutting down and asked the module to exit; a disconnect now is expected.
        return;
    }

    // A module that loses its connection stops controlling its bots. The bots are not revoked to the module,
    // since it cannot receive the notify, so core simply takes them back.
    if (event != ModuleSession::Event::Accepted)
    {
        std::vector<uint64> released = _owners.DropModule(id);
        if (!released.empty())
            TC_LOG_INFO("modules", "'{}': released {} bot(s) on session end ({}).", d.Name, released.size(), detail);
    }

    switch (event)
    {
        case ModuleSession::Event::Accepted:
            d.HandshakeAttempts = 0;
            SetStatus(d, ModuleStatus::Running, "");
            TC_LOG_INFO("modules", "'{}' handshake accepted (module id {}).", d.Name, id);
            break;

        case ModuleSession::Event::Rejected:
            // Refused by core (wrong protocol, unsupported hook, Tier 2 requested). Config or build
            // problem: a restart cannot fix it.
            StopProcess(d);
            MarkPermanent(d, detail);
            TC_LOG_ERROR("modules", "'{}' handshake rejected: {}", d.Name, detail);
            break;

        case ModuleSession::Event::HandshakeTimeout:
        case ModuleSession::Event::Disconnected:
            if (d.Status == ModuleStatus::Starting)
            {
                // Failed before the handshake completed: counts against MaxHandshakeAttempts.
                d.LastFailure = detail;
                StopProcess(d);
                ++d.HandshakeAttempts;
                TC_LOG_ERROR("modules", "'{}' failed before handshake (attempt {} of {}): {}",
                             d.Name, d.HandshakeAttempts, _config.MaxHandshakeAttempts, detail);
                ScheduleRestart(d, /*handshakeFailure=*/true);
            }
            else
            {
                // Crash or silence after a successful handshake: uses the rolling restart budget.
                d.LastFailure = detail;
                TC_LOG_ERROR("modules", "'{}' failed: {}", d.Name, detail);
                StopProcess(d);
                ScheduleRestart(d, /*handshakeFailure=*/false);
            }
            break;

        case ModuleSession::Event::HeartbeatTimeout:
            d.LastFailure = detail;
            TC_LOG_ERROR("modules", "'{}' is unresponsive: {}", d.Name, detail);
            StopProcess(d);
            ScheduleRestart(d, /*handshakeFailure=*/false);
            break;
    }
}

void ModuleMgr::SetStatus(ModuleDescriptor& descriptor, ModuleStatus status, std::string reason)
{
    descriptor.Status = status;
    descriptor.StatusReason = std::move(reason);
    if (status != ModuleStatus::TransientFailure)
        descriptor.RestartAt = {};
}

void ModuleMgr::MarkPermanent(ModuleDescriptor& descriptor, std::string reason)
{
    SetStatus(descriptor, ModuleStatus::PermanentFailure, std::move(reason));
}

void ModuleMgr::ScheduleRestart(ModuleDescriptor& descriptor, bool handshakeFailure)
{
    auto const now = Clock::now();

    std::chrono::milliseconds backoff{0};
    if (handshakeFailure)
    {
        if (descriptor.HandshakeAttempts >= _config.MaxHandshakeAttempts)
        {
            MarkPermanent(descriptor, "failed " + std::to_string(descriptor.HandshakeAttempts) +
                          " handshake attempt(s), giving up; last failure: " + descriptor.LastFailure);
            TC_LOG_ERROR("modules", "'{}' permanently failed: {}", descriptor.Name, descriptor.StatusReason);
            return;
        }
        backoff = BackoffFor(_config.RestartBackoffMs, descriptor.HandshakeAttempts);
    }
    else
    {
        // Drop restarts that have aged out of the window, then check the budget.
        auto& times = descriptor.RestartTimes;
        times.erase(std::remove_if(times.begin(), times.end(), [&](Clock::time_point t) { return now - t > RestartWindow; }),
                    times.end());
        if (times.size() >= _config.MaxRestartAttempts)
        {
            MarkPermanent(descriptor, "crash loop: restarted " + std::to_string(times.size()) + " time(s) within " +
                          std::to_string(RestartWindow.count()) + " minutes, giving up; last failure: " + descriptor.LastFailure);
            TC_LOG_ERROR("modules", "'{}' permanently failed: {}", descriptor.Name, descriptor.StatusReason);
            return;
        }
        times.push_back(now);
        backoff = BackoffFor(_config.RestartBackoffMs, static_cast<uint32>(times.size()));
    }

    ++descriptor.RestartAttempts;
    SetStatus(descriptor, ModuleStatus::TransientFailure,
              "restarting in " + std::to_string(backoff.count()) + " ms after: " + descriptor.LastFailure);
    descriptor.RestartAt = now + backoff;
    TC_LOG_INFO("modules", "'{}' restart {} scheduled in {} ms.", descriptor.Name, descriptor.RestartAttempts, backoff.count());
}

void ModuleMgr::StopProcess(ModuleDescriptor& descriptor)
{
    // Every failure path ends here, so the region goes with the connection that used it.
    descriptor.Tier2.reset();

    if (!descriptor.Process)
        return;
    if (descriptor.Process->running())
        descriptor.Process->terminate();
    descriptor.Process->wait(); // reaps the child; returns at once if it has already exited
}

void ModuleMgr::StartSupervisor()
{
    _supervisorStop = false;
    if (!_supervisor.joinable())
        _supervisor = std::thread([this] { SupervisorLoop(); });
}

void ModuleMgr::StopSupervisor()
{
    _supervisorStop = true;
    if (_supervisor.joinable())
        _supervisor.join();
}

void ModuleMgr::SupervisorLoop()
{
    while (!_supervisorStop)
    {
        RestartDueModules();
        std::this_thread::sleep_for(SupervisorPollInterval);
    }
}

void ModuleMgr::RestartDueModules()
{
    auto const now = Clock::now();
    std::vector<ModuleId> due;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [id, d] : _modules)
        {
            if (d.Status == ModuleStatus::TransientFailure && d.RestartAt != Clock::time_point{} && now >= d.RestartAt)
            {
                d.RestartAt = {}; // claimed; a failure during the attempt schedules a new one
                due.push_back(id);
            }
        }
    }

    for (ModuleId id : due)
    {
        std::lock_guard<std::mutex> lifecycle(_lifecycleMutex);
        if (_unloading || _supervisorStop)
            return;

        // The previous session has already reported its failure and its thread is exiting; join it
        // and drop the process handle before starting the next attempt.
        std::shared_ptr<ModuleSession> oldSession;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ModuleDescriptor& d = _modules.at(id);
            oldSession = std::move(d.Session);
            d.Process.reset();
        }
        if (oldSession)
            oldSession->Stop();

        TC_LOG_INFO("modules", "restarting module {}.", id);
        SpawnModule(id);
    }
}

void ModuleMgr::DispatchItemLooted(std::string_view hook, ModuleWire::ItemLootedEvent const& event)
{
    std::vector<std::pair<ModuleId, std::shared_ptr<ModuleSession>>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& [id, d] : _modules)
            if (d.Session && d.Session->Subscribes(hook))
                targets.emplace_back(id, d.Session);
    }

    for (auto const& [id, session] : targets)
    {
        if (session->NotifyItemLooted(hook, event))
            continue;

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _modules.find(id);
        if (it != _modules.end() && ++it->second.DroppedEvents == 1)
            TC_LOG_WARN("modules", "'{}' is not keeping up with its events; notifications are being dropped (first: {}).",
                        it->second.Name, hook);
    }
}

bool ModuleMgr::HasSubscriber(std::string_view hook)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto const& [id, d] : _modules)
        if (d.Session && d.Session->Subscribes(hook))
            return true;
    return false;
}

void ModuleMgr::DispatchChat(std::string_view hook, ModuleWire::ChatEvent const& event)
{
    std::vector<std::pair<ModuleId, std::shared_ptr<ModuleSession>>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& [id, d] : _modules)
            if (d.Session && d.Session->Subscribes(hook))
                targets.emplace_back(id, d.Session);
    }

    for (auto const& [id, session] : targets)
    {
        if (session->NotifyChat(hook, event))
            continue;

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _modules.find(id);
        if (it != _modules.end() && ++it->second.DroppedEvents == 1)
            TC_LOG_WARN("modules", "'{}' is not keeping up with its events; notifications are being dropped (first: {}).",
                        it->second.Name, hook);
    }
}

void ModuleMgr::EnqueueAction(ModuleId module, std::weak_ptr<ModuleSession> session, ModuleWire::ActionRequestMsg request)
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        if (_pendingActions.size() < MaxPendingActions)
        {
            _pendingActions.push_back(PendingAction{ module, std::move(session), std::move(request) });
            return;
        }
    }

    // Too many actions waiting for the world thread. Refuse this one now rather than let the queue grow.
    ModuleWire::ActionReplyMsg reply;
    reply.MessageId = request.MessageId;
    reply.Reason = "core is busy, try again later";
    if (std::shared_ptr<ModuleSession> target = session.lock())
        target->SendActionReply(reply);
}

void ModuleMgr::SetBotBackend(std::shared_ptr<ModuleBotBackend> backend)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _botBackend = std::move(backend);
}

void ModuleMgr::RevokeBot(uint64 botGuid, std::string const& reason)
{
    std::shared_ptr<ModuleSession> session;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        uint32 owner = 0;
        if (!_owners.Revoke(botGuid, owner))
            return;
        auto it = _modules.find(owner);
        if (it != _modules.end())
            session = it->second.Session;
    }

    if (session)
    {
        ModuleWire::ControlRevokedMsg revoked;
        revoked.BotGuid = botGuid;
        revoked.Reason = reason;
        session->SendControlRevoked(revoked);
    }
}

void ModuleMgr::EnqueueControl(PendingControl pending)
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        // A release always fits: it only shrinks state, and a module can have at most max_bots of them queued.
        if (pending.Release || _pendingControls.size() < MaxPendingActions)
        {
            _pendingControls.push_back(std::move(pending));
            return;
        }
    }

    ModuleWire::ControlResponseMsg response;
    response.Reason = "core is busy, try again later";
    if (std::shared_ptr<ModuleSession> target = pending.Session.lock())
        target->SendControlResponse(pending.MessageId, response);
}

ModuleWire::ControlResponseMsg ModuleMgr::ExecuteControlRequest(ModuleId module, ModuleWire::ControlRequestMsg const& request)
{
    // Runs on the world thread. Checks the module's grant and the backend first; ownership is decided under _mutex.
    ModuleWire::ControlResponseMsg response;
    std::shared_ptr<ModuleBotBackend> backend;
    uint32 maxBots = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _modules.find(module);
        if (it == _modules.end())
        {
            response.Reason = "module is no longer loaded";
            return response;
        }
        if (!it->second.Tier2)
        {
            response.Reason = "Tier 2 was not granted to this module";
            return response;
        }
        maxBots = it->second.Tier2->GetBounds().MaxBots;
        backend = _botBackend;
    }

    if (!backend)
    {
        response.Reason = "bot control is not available on this core";
        return response;
    }

    if (request.BotGuid == 0)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_owners.CanTakeMore(module, maxBots))
            {
                response.Reason = "max_bots exceeded, request a larger bound at handshake";
                return response;
            }
        }

        std::string reason;
        uint64 const guid = backend->Spawn(request.SpawnParams, reason);
        if (guid == 0)
        {
            response.Reason = reason.empty() ? "bot could not be spawned" : reason;
            return response;
        }

        uint32 owner = 0;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_owners.TryClaim(guid, module, maxBots, owner) != ModuleBotOwnership::Claim::Granted)
        {
            response.Reason = "bot was spawned but its ownership could not be recorded";
            return response;
        }
        backend->Activate(guid);
        TC_LOG_INFO("modules", "module {} spawned bot {}.", module, guid);
        response.Granted = true;
        response.BotGuid = guid;
        backend->Describe(guid, response.Race, response.ClassId);
        return response;
    }

    if (!backend->Exists(request.BotGuid))
    {
        response.Reason = "character not found";
        return response;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    uint32 owner = 0;
    switch (_owners.TryClaim(request.BotGuid, module, maxBots, owner))
    {
        case ModuleBotOwnership::Claim::Granted:
            response.Granted = true;
            response.BotGuid = request.BotGuid;
            backend->Activate(request.BotGuid);
            backend->Describe(request.BotGuid, response.Race, response.ClassId);
            break;
        case ModuleBotOwnership::Claim::OwnedByOther:
        {
            auto other = _modules.find(owner);
            response.Reason = "already controlled by module " +
                              (other != _modules.end() ? other->second.Name : std::to_string(owner));
            break;
        }
        case ModuleBotOwnership::Claim::LimitReached:
            response.Reason = "max_bots exceeded, request a larger bound at handshake";
            break;
    }
    return response;
}

void ModuleMgr::RunPendingControls()
{
    std::vector<PendingControl> batch;
    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        while (!_pendingControls.empty() && batch.size() < MaxActionsPerTick)
        {
            batch.push_back(std::move(_pendingControls.front()));
            _pendingControls.pop_front();
        }
    }

    for (PendingControl& pending : batch)
    {
        if (pending.Release)
        {
            std::shared_ptr<ModuleBotBackend> backend;
            bool owned = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                owned = _owners.Release(pending.Request.BotGuid, pending.Module);
                backend = _botBackend;
            }
            if (backend && owned)
                backend->Deactivate(pending.Request.BotGuid); // an alt logs out when its owner lets it go
            continue;
        }

        ModuleWire::ControlResponseMsg response = ExecuteControlRequest(pending.Module, pending.Request);
        if (std::shared_ptr<ModuleSession> target = pending.Session.lock())
            target->SendControlResponse(pending.MessageId, response);
    }
}

void ModuleMgr::RunPendingActions()
{
    std::vector<PendingAction> batch;
    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        while (!_pendingActions.empty() && batch.size() < MaxActionsPerTick)
        {
            batch.push_back(std::move(_pendingActions.front()));
            _pendingActions.pop_front();
        }
    }

    for (PendingAction& action : batch)
    {
        // The module's name decides which store rows an action may touch. Empty if the module has been unloaded.
        std::string moduleName;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (auto it = _modules.find(action.Module); it != _modules.end())
                moduleName = it->second.Name;
        }

        ModuleWire::ActionReplyMsg reply;
        ExecuteModuleAction(action.Request, reply, moduleName);
        if (std::shared_ptr<ModuleSession> target = action.Session.lock())
            target->SendActionReply(reply);
    }
}

void ModuleMgr::OnWorldTick(uint64 tick)
{
    // Runs on the world thread (doc 04 §4). Copy the regions out under the lock; publishing needs no lock.
    std::vector<std::pair<ModuleId, std::shared_ptr<ModuleTier2Region>>> regions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& entry : _modules)
            if (entry.second.Tier2 && entry.second.Session && entry.second.Session->IsAccepted())
                regions.emplace_back(entry.first, entry.second.Tier2);
    }

    for (auto const& [id, region] : regions)
    {
        if (region->SnapshotDue(tick))
        {
            // Only the bots this module controls. The list is copied under the lock; the bots are read without it.
            std::vector<uint64> owned;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                owned = _owners.BotsOf(id);
            }

            std::vector<ModuleSnapshotBot> bots;
            // Auto: one refresh tick per 400 bots, so a bot's nearby list is at most that many ticks old.
            uint32 const refreshEvery = _config.Tier2NearbyRefreshTicks != 0
                                            ? _config.Tier2NearbyRefreshTicks
                                            : std::max<uint32>(1, static_cast<uint32>((owned.size() + 399) / 400));
            for (uint64 guid : owned)
            {
                Player* bot = sBotMgr->GetOnlinePlayer(guid);
                if (!bot)
                    continue;

                // Staggered by guid: each bot refreshes on one tick in refreshEvery, so the cost is spread over the ticks.
                bool const refresh = refreshEvery <= 1 || (tick + guid) % refreshEvery == 0;
                ModuleSnapshotBot entry = BuildSnapshotBot(*bot, region->GetBounds().MaxNearbyUnits, refresh, _nearbyCache[guid]);
                if (auto result = _actionResults.find(guid); result != _actionResults.end())
                {
                    entry.Bot.last_action_type = result->second.Type;
                    entry.Bot.last_action_status = result->second.Status;
                    entry.Bot.last_action_spell = result->second.Spell;
                    entry.Bot.last_action_sequence = result->second.Sequence;
                }
                bots.push_back(std::move(entry));
            }
            // Forget the nearby cache of bots this module no longer controls.
            for (auto it = _nearbyCache.begin(); it != _nearbyCache.end();)
                it = std::find(owned.begin(), owned.end(), it->first) == owned.end() ? _nearbyCache.erase(it) : std::next(it);

            region->PublishSnapshot(tick, bots);
        }

        ModuleActionSet actions;
        if (region->ReadNewActions(actions))
            ApplyModuleActions(id, actions);
    }

    RunPendingControls();
    RunPendingActions();

    // Results are kept only while some module owns the bot; a bot nobody controls has nothing to report to.
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _actionResults.begin(); it != _actionResults.end();)
    {
        uint32 owner = 0;
        it = _owners.Owner(it->first, owner) ? std::next(it) : _actionResults.erase(it);
    }
}

void ModuleMgr::RecordActionResult(uint64 botGuid, forever_module::BotAction const& action, forever_module::BotActionStatus status)
{
    ActionResult& result = _actionResults[botGuid];
    result.Status = status;
    result.Type = uint8(action.type);
    result.Spell = action.type == forever_module::ActionType::kCastSpell ? action.spell_id : 0;
    ++result.Sequence;
}

void ModuleMgr::ApplyModuleActions(ModuleId module, ModuleActionSet const& actions)
{
    for (forever_module::BotAction const& action : actions.Actions)
    {
        uint32 owner = 0;
        bool owned = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            owned = _owners.Owner(action.bot_guid, owner) && owner == module;
        }
        if (!owned)
        {
            TC_LOG_DEBUG("modules", "module {} sent an action for bot {}, which it does not control; dropped.", module, action.bot_guid);
            RecordActionResult(action.bot_guid, action, forever_module::BotActionStatus::kRefused);
            continue;
        }

        Player* bot = sBotMgr->GetOnlinePlayer(action.bot_guid);
        if (!bot)
        {
            TC_LOG_DEBUG("modules", "module {} sent an action for bot {}, which is not online; dropped.", module, action.bot_guid);
            RecordActionResult(action.bot_guid, action, forever_module::BotActionStatus::kNotOnline);
            continue;
        }

        std::string reason;
        forever_module::BotActionStatus const status = ApplyBotAction(*bot, action, reason);
        RecordActionResult(action.bot_guid, action, status);
        if (status != forever_module::BotActionStatus::kApplied)
        {
            TC_LOG_DEBUG("modules", "module {}: action on bot {} refused ({}): {}.", module, action.bot_guid, int(status), reason);
            NoteRefusal(int(action.type), int(status), reason);
        }
    }
}

// Refusals are counted by action type, status and reason, and the commonest are logged once a minute, so a run shows why its actions
// fail without debug logging.
void ModuleMgr::NoteRefusal(int actionType, int status, std::string const& reason)
{
    static std::map<std::string, uint32> counts;
    static std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    ++counts["type " + std::to_string(actionType) + " status " + std::to_string(status) + ": " + reason];

    auto const now = std::chrono::steady_clock::now();
    if (now - lastReport < std::chrono::minutes(1))
        return;
    lastReport = now;

    std::vector<std::pair<uint32, std::string>> ranked;
    for (auto const& [text, count] : counts)
        ranked.emplace_back(count, text);
    std::sort(ranked.begin(), ranked.end(), std::greater<>());
    uint32 total = 0;
    for (auto const& entry : ranked)
        total += entry.first;
    TC_LOG_INFO("server.worldserver", "Modules: {} action(s) refused in the last minute; the commonest reasons:", total);
    for (size_t i = 0; i < ranked.size() && i < 12; ++i)
        TC_LOG_INFO("server.worldserver", "Modules:   {} x {}", ranked[i].first, ranked[i].second);
    counts.clear();
}

void ModuleMgr::Unload()
{
    // Stop new restarts and events from running before the modules are stopped. The lifecycle lock
    // makes this wait for a restart already in progress, so the module set is stable from here on.
    _unloading = true;
    _supervisorStop = true;

    std::lock_guard<std::mutex> lifecycle(_lifecycleMutex);

    // Move sessions and processes out under the lock, then stop them without holding it: a session
    // thread may be waiting on the lock to report an event while it shuts down.
    std::vector<std::shared_ptr<ModuleSession>> sessions;
    std::vector<std::shared_ptr<bp::child>> processes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [id, d] : _modules)
        {
            if (d.Session)
                sessions.push_back(std::move(d.Session));
            if (d.Process)
                processes.push_back(d.Process);
        }
    }

    for (auto& session : sessions)
        session->Stop(); // sends Shutdown to a connected module, then joins the session thread

    for (auto& process : processes)
    {
        auto const deadline = Clock::now() + ShutdownGrace;
        while (process->running() && Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (process->running())
            process->terminate();
        process->wait();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _modules.clear();
    }

    // Join last: processes spawned by the supervisor die with it (PR_SET_PDEATHSIG), and by now they
    // have been stopped gracefully.
    StopSupervisor();
}

std::vector<ModuleSnapshot> ModuleMgr::GetSnapshots()
{
    std::vector<ModuleSnapshot> snapshots;
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto const& [id, d] : _modules)
    {
        ModuleSnapshot s;
        s.Name = d.Name;
        s.Version = d.Version;
        if (ApprovalRecord const* record = _approvals.Find(d.Name))
            s.ApprovedVersion = record->ApprovedVersion;
        s.Capabilities = d.Capabilities;
        s.Status = d.Status;
        s.StatusReason = d.StatusReason;
        s.LastFailure = d.LastFailure;
        s.RestartAttempts = d.RestartAttempts;
        s.DroppedEvents = d.DroppedEvents;
        s.HeartbeatCount = d.Session ? d.Session->HeartbeatCount() : 0;
        s.ProcessId = d.Process ? static_cast<uint64>(d.Process->id()) : 0;
        s.ProcessRunning = d.Process && d.Process->running();
        snapshots.push_back(std::move(s));
    }
    std::sort(snapshots.begin(), snapshots.end(), [](ModuleSnapshot const& a, ModuleSnapshot const& b)
    {
        return a.Name < b.Name;
    });
    return snapshots;
}

bool ModuleMgr::CheckProtocolVersion(std::string const& declared, std::string& reason)
{
    if (declared == kModuleProtocolVersion)
        return true;

    reason = "protocol_version mismatch: core speaks \"" + std::string(kModuleProtocolVersion) +
             "\", module declared \"" + declared + "\"";
    return false;
}

void ModuleMgr::LoadApprovalStore()
{
    _approvals = ApprovalStore();
    _approvalLoadError.clear();

    std::string error;
    if (!_approvals.Load(ApprovalStorePath(), error))
    {
        _approvalLoadError = error;
        TC_LOG_ERROR("modules", "{}. Modules will not be spawned until it is fixed; `module approve` is refused.", error);
    }
    _approvalsLoaded = true;
}

std::string ModuleMgr::ApprovalStorePath() const
{
    return (fs::path(sConfigMgr->GetFilename()).parent_path() / "approved_modules.json").string();
}

bool ModuleMgr::ApproveModule(std::string const& name, std::string& message)
{
    if (!IsValidModuleName(name))
    {
        message = "invalid module name '" + name + "'";
        return false;
    }

    std::string const root = sConfigMgr->GetStringDefault("Modules.Dir", "modules_runtime");
    fs::path const dir = fs::path(root) / name;

    std::string json;
    {
        std::ifstream in(dir / "module.json");
        if (!in)
        {
            message = "no module.json for '" + name + "' in " + root;
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        json = buffer.str();
    }

    ModuleManifestInfo info;
    std::string error;
    if (!ParseModuleManifest(json, info, error))
    {
        message = error;
        return false;
    }
    if (info.Name != name)
    {
        message = "module.json name '" + info.Name + "' does not match directory '" + name + "'";
        return false;
    }
    std::string reason;
    if (!CheckProtocolVersion(info.ProtocolVersion, reason))
    {
        message = "cannot approve: " + reason;
        return false;
    }

    CapabilityDeclaration const capabilities = CapabilitiesOf(info);
    ApprovalRecord record;
    record.ApprovedVersion = info.Version;
    record.ApprovedAt = UtcTimestampNow();
    record.CapabilityHash = CapabilityHash(capabilities);
    record.Capabilities = capabilities;

    std::lock_guard<std::mutex> lock(_mutex);
    if (!_approvalsLoaded)
        LoadApprovalStore();
    if (!_approvalLoadError.empty())
    {
        // Saving now would overwrite a store we could not read, losing every other approval.
        message = "approval store is unreadable (" + _approvalLoadError + "); fix or move it before approving";
        return false;
    }

    // Save a copy first, so a failed write leaves the in-memory store unchanged.
    ApprovalStore next = _approvals;
    next.Set(name, record);
    std::string saveError;
    if (!next.Save(ApprovalStorePath(), saveError))
    {
        message = "approval not saved: " + saveError;
        return false;
    }
    _approvals = std::move(next);

    message = "approved '" + name + "' version " + info.Version + " (capability hash " +
              record.CapabilityHash.substr(0, 12) + "). Takes effect at the next module load.";
    TC_LOG_INFO("modules", "'{}' {} approved by operator; capability hash {}.", name, info.Version, record.CapabilityHash);
    return true;
}

char const* ModuleStatusName(ModuleStatus status)
{
    switch (status)
    {
        case ModuleStatus::PendingApproval:  return "pending approval";
        case ModuleStatus::Starting:         return "starting";
        case ModuleStatus::Running:          return "running";
        case ModuleStatus::TransientFailure: return "restart scheduled";
        case ModuleStatus::PermanentFailure: return "failed";
    }
    return "unknown";
}

void ModuleMgr::DispatchNotify(std::string_view hook, ModuleWire::NotifyKind kind, uint64 value)
{
    // Collect targets under the lock and deliver outside it. Delivery only queues the frame, so a slow
    // module cannot hold up the world thread here.
    std::vector<std::pair<ModuleId, std::shared_ptr<ModuleSession>>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& [id, d] : _modules)
            if (d.Session && d.Session->Subscribes(hook))
                targets.emplace_back(id, d.Session);
    }

    for (auto const& [id, session] : targets)
    {
        if (session->NotifyEvent(hook, kind, value))
            continue;

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _modules.find(id);
        if (it != _modules.end() && ++it->second.DroppedEvents == 1)
            TC_LOG_WARN("modules", "'{}' is not keeping up with its events; notifications are being dropped (first: {}).",
                        it->second.Name, hook);
    }
}

ModuleMgr::CommandRouteResult ModuleMgr::RouteCommand(std::string_view commandText, uint64 issuerGuid, std::string& resultText)
{
    struct Target
    {
        ModuleId Id;
        std::string Name;
        std::shared_ptr<ModuleSession> Session;
    };

    std::vector<Target> targets;
    std::chrono::milliseconds timeout{0};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& [id, d] : _modules)
            if (d.Session && d.Session->Subscribes("CommandScript"))
                targets.push_back({id, d.Name, d.Session});
        timeout = std::chrono::milliseconds(_config.CommandRequestTimeoutMs);
    }
    if (targets.empty())
        return CommandRouteResult::NotRouted;

    std::sort(targets.begin(), targets.end(), [](Target const& a, Target const& b) { return a.Id < b.Id; });

    // Waiting happens without _mutex held: session events for these modules need it to be reported.
    bool timedOut = false;
    std::optional<std::string> failureText;
    for (Target const& target : targets)
    {
        std::optional<ModuleWire::EventReplyMsg> reply = target.Session->RequestCommand("CommandScript", commandText, issuerGuid, timeout);
        if (!reply)
        {
            timedOut = true;
            TC_LOG_WARN("modules", "'{}' did not reply to command '{}' within {} ms.", target.Name, std::string(commandText), timeout.count());
            continue;
        }
        if (!reply->HasCommand)
            continue;
        if (reply->Success)
        {
            resultText = reply->ResultText;
            return CommandRouteResult::Succeeded;
        }
        if (!failureText && !reply->ResultText.empty())
            failureText = reply->ResultText;
    }

    if (failureText)
    {
        resultText = *failureText;
        return CommandRouteResult::Failed;
    }
    if (timedOut)
        return CommandRouteResult::TimedOut;
    return CommandRouteResult::NotRouted;
}

std::string ModuleMgr::RunningModulesLine()
{
    if (!sConfigMgr->GetBoolDefault("Modules.LoginMessage", true))
        return {};

    std::string names;
    for (ModuleSnapshot const& module : GetSnapshots())
    {
        if (module.Status != ModuleStatus::Running)
            continue;
        if (!names.empty())
            names += ", ";
        names += module.Name;
        if (!module.Version.empty())
            names += " " + module.Version;
    }
    return names.empty() ? std::string() : "This server is running modules: " + names;
}
