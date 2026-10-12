/*
 * Module System — core-side module host. See ModuleMgr.h and doc/module-system/04-phase2-module-host-design.md.
 */

#include "ModuleMgr.h"
#include "Config.h"
#include "Log.h"
#include "ModuleManifestInfo.h"

#include <boost/process.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace bp = boost::process;
namespace fs = std::filesystem;

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
};

// How long a module gets to exit after Shutdown before it is terminated.
constexpr std::chrono::milliseconds ShutdownGrace{2000};

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

    if (!sConfigMgr->GetBoolDefault("Modules.Enabled", false))
    {
        TC_LOG_INFO("modules", "Modules.Enabled = 0, module system is disabled.");
        return;
    }

    std::string const root = sConfigMgr->GetStringDefault("Modules.Dir", "modules_runtime");
    auto const handshakeTimeoutMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.HandshakeTimeoutMs", 5000));
    auto const heartbeatTimeoutMs = static_cast<uint64>(sConfigMgr->GetIntDefault("Modules.HeartbeatTimeoutMs", 15000));
    bool const autoApprove = sConfigMgr->GetBoolDefault("Modules.AutoApproveLocal", false);

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
        ProcessModuleDirectory(root, name, handshakeTimeoutMs, heartbeatTimeoutMs, autoApprove);
}

void ModuleMgr::ProcessModuleDirectory(std::string const& root, std::string const& name, uint64 handshakeTimeoutMs,
                                       uint64 heartbeatTimeoutMs, bool autoApprove)
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
    }

    std::string reason;
    if (!CheckProtocolVersion(info.ProtocolVersion, reason))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::PermanentFailure, reason);
        TC_LOG_ERROR("modules", "'{}' not loaded: {}", name, reason);
        return;
    }

    // Approval gate (doc 06). The approval store is not implemented yet, so the only way to spawn a
    // module is the local-development override. Deny by default.
    if (!autoApprove)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::PendingApproval,
                  "not approved: the approval store is not implemented yet; set Modules.AutoApproveLocal = 1 for local development");
        TC_LOG_INFO("modules", "'{}' {} is pending approval and was not started.", name, info.Version);
        return;
    }

    fs::path const executable = dir / name;
    std::error_code ec;
    if (!fs::is_regular_file(executable, ec))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::PermanentFailure, "executable not found: " + executable.string());
        TC_LOG_ERROR("modules", "'{}' not loaded: executable not found at '{}'.", name, executable.string());
        return;
    }

    std::string const endpoint = (fs::path(root) / (name + ".sock")).string();
    SpawnModule(id, executable.string(), endpoint, handshakeTimeoutMs, heartbeatTimeoutMs);
}

bool ModuleMgr::SpawnModule(ModuleId id, std::string const& executable, std::string const& endpoint,
                            uint64 handshakeTimeoutMs, uint64 heartbeatTimeoutMs)
{
    ModuleSession::Settings settings;
    settings.Endpoint = endpoint;
    settings.HandshakeTimeout = std::chrono::milliseconds(handshakeTimeoutMs);
    settings.HeartbeatTimeout = std::chrono::milliseconds(heartbeatTimeoutMs);
    settings.ExpectedProtocolVersion = kModuleProtocolVersion;
    settings.SupportedHooks = SupportedTier1Hooks;
    // Module config from modules.d/<name>.conf is not wired up yet; the module gets an empty config.

    auto session = std::make_unique<ModuleSession>(settings, id, [this, id](ModuleSession::Event event, std::string const& detail)
    {
        OnSessionEvent(id, event, detail);
    });

    std::string error;
    if (!session->Listen(error))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SetStatus(_modules.at(id), ModuleStatus::PermanentFailure, error);
        TC_LOG_ERROR("modules", "module {}: cannot listen for its connection: {}", id, error);
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
        SetStatus(_modules.at(id), ModuleStatus::TransientFailure, std::string("failed to start process: ") + e.what());
        TC_LOG_ERROR("modules", "module {}: failed to start '{}': {}", id, executable, e.what());
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

    switch (event)
    {
        case ModuleSession::Event::Accepted:
            SetStatus(d, ModuleStatus::Running, "");
            TC_LOG_INFO("modules", "'{}' handshake accepted (module id {}).", d.Name, id);
            break;
        case ModuleSession::Event::Rejected:
            SetStatus(d, ModuleStatus::PermanentFailure, detail);
            TC_LOG_ERROR("modules", "'{}' handshake rejected: {}", d.Name, detail);
            StopProcess(d);
            break;
        case ModuleSession::Event::HandshakeTimeout:
            SetStatus(d, ModuleStatus::TransientFailure, detail);
            TC_LOG_ERROR("modules", "'{}' handshake failed: {} (restart policy not implemented yet).", d.Name, detail);
            StopProcess(d);
            break;
        case ModuleSession::Event::HeartbeatTimeout:
            SetStatus(d, ModuleStatus::TransientFailure, detail);
            TC_LOG_ERROR("modules", "'{}' is unresponsive: {} (restart policy not implemented yet).", d.Name, detail);
            StopProcess(d);
            break;
        case ModuleSession::Event::Disconnected:
            SetStatus(d, ModuleStatus::TransientFailure, detail);
            TC_LOG_ERROR("modules", "'{}' disconnected: {} (restart policy not implemented yet).", d.Name, detail);
            StopProcess(d);
            break;
    }
}

void ModuleMgr::SetStatus(ModuleDescriptor& descriptor, ModuleStatus status, std::string reason)
{
    descriptor.Status = status;
    descriptor.StatusReason = std::move(reason);
}

void ModuleMgr::StopProcess(ModuleDescriptor& descriptor)
{
    if (!descriptor.Process)
        return;
    if (descriptor.Process->running())
        descriptor.Process->terminate();
    descriptor.Process->wait(); // reaps the child; returns at once if it has already exited
}

void ModuleMgr::OnWorldTick(uint64 /*tick*/)
{
    // TODO (doc 04 §4, doc 03 Tier 2 section): Tier 2 is not implemented yet.
}

void ModuleMgr::Unload()
{
    _unloading = true;

    // Move sessions and processes out under the lock, then stop them without holding it: a session
    // thread may be waiting on the lock to report an event while it shuts down.
    std::vector<std::unique_ptr<ModuleSession>> sessions;
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
        auto const deadline = std::chrono::steady_clock::now() + ShutdownGrace;
        while (process->running() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (process->running())
            process->terminate();
        process->wait();
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _modules.clear();
}

std::vector<ModuleSnapshot> ModuleMgr::GetSnapshots()
{
    std::vector<ModuleSnapshot> snapshots;
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto const& [id, d] : _modules)
    {
        ModuleSnapshot s;
        s.Name = d.Name;
        s.Status = d.Status;
        s.StatusReason = d.StatusReason;
        s.HeartbeatCount = d.Session ? d.Session->HeartbeatCount() : 0;
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
