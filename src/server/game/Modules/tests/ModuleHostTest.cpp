/*
 * End-to-end test of the core-side module host (ModuleMgr + ModuleSession) against real module
 * processes: the forever-module-sdk example `minimal_module`, which handshakes and heartbeats, and
 * /bin/sleep, which exits without connecting.
 *
 * Not part of the worldserver build. Build against a bundled-fmt checkout:
 *   g++ -std=c++20 -DFMT_HEADER_ONLY -Isrc/common -Isrc/common/Cryptography -Isrc/common/Debugging \
 *       -Isrc/server/game/Modules/tests/stubs -Isrc/server/game/Modules -Idep/fmt/include \
 *       src/server/game/Modules/{ModuleMgr,ModuleSession,ModuleWire,ModuleManifestInfo,ModuleApproval}.cpp \
 *       src/server/game/Modules/tests/ModuleHostTest.cpp src/server/game/Modules/tests/stubs/ErrorsStub.cpp \
 *       -lboost_filesystem -lboost_system -lcrypto -lpthread -o module_host_test
 *   ./module_host_test <path-to-minimal_module> /bin/sleep
 */

#include "ModuleMgr.h"
#include "Config.h"

#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                   \
    } while (0)

std::string g_minimalModule;
std::string g_sleepBinary;
fs::path g_work;

// Restart budget and backoff are kept small so the timing-dependent scenarios finish quickly.
void Configure(bool autoApprove, int handshakeMs, int heartbeatMs, fs::path const& root,
               int maxHandshakeAttempts = 3, int maxRestartAttempts = 2, int backoffMs = 200)
{
    auto& cfg = StubConfigMgr::Get().Values;
    cfg["Modules.Enabled"] = "1";
    cfg["Modules.Dir"] = root.string();
    cfg["Modules.AutoApproveLocal"] = autoApprove ? "1" : "0";
    cfg["Modules.HandshakeTimeoutMs"] = std::to_string(handshakeMs);
    cfg["Modules.HeartbeatTimeoutMs"] = std::to_string(heartbeatMs);
    cfg["Modules.MaxHandshakeAttempts"] = std::to_string(maxHandshakeAttempts);
    cfg["Modules.MaxRestartAttempts"] = std::to_string(maxRestartAttempts);
    cfg["Modules.RestartBackoffMs"] = std::to_string(backoffMs);
}

// Creates <root>/<name>/module.json and copies `binary` to <root>/<name>/<name>.
void InstallModule(fs::path const& root, std::string const& name, std::string const& protocolVersion,
                   std::string const& binary)
{
    fs::create_directories(root / name);
    std::ofstream manifest(root / name / "module.json");
    manifest << "{\n"
                "  \"name\": \"" << name << "\",\n"
                "  \"version\": \"0.1.0\",\n"
                "  \"protocol_version\": \"" << protocolVersion << "\",\n"
                "  \"sdk_package_version\": \"0.1.0\",\n"
                "  \"tier1\": { \"hooks\": [\"WorldScript.OnStartup\"] },\n"
                "  \"tier2\": { \"participates\": false }\n"
                "}\n";
    manifest.close();
    fs::copy_file(binary, root / name / name, fs::copy_options::overwrite_existing);
}

bool WaitFor(std::function<bool()> const& pred, std::chrono::milliseconds timeout)
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}

ModuleSnapshot Find(std::string const& name)
{
    for (ModuleSnapshot const& s : sModuleMgr->GetSnapshots())
        if (s.Name == name)
            return s;
    return ModuleSnapshot{};
}

bool RunningWithNewProcess(std::string const& name, uint64 previousPid)
{
    ModuleSnapshot s = Find(name);
    return s.Status == ModuleStatus::Running && s.ProcessRunning && s.ProcessId != 0 && s.ProcessId != previousPid;
}

// A: approval gate. Nothing is spawned without AutoApproveLocal; a bad protocol version is refused.
void ScenarioApprovalGate()
{
    std::printf("-- approval gate\n");
    fs::path const root = g_work / "approval";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    InstallModule(root, "bad-proto", "2", g_minimalModule);
    Configure(false, 5000, 15000, root);

    sModuleMgr->LoadModules();

    ModuleSnapshot good = Find("minimal-module");
    CHECK(good.Status == ModuleStatus::PendingApproval);
    CHECK(!good.ProcessRunning);
    CHECK(!fs::exists(root / "minimal-module.sock"));

    ModuleSnapshot bad = Find("bad-proto");
    CHECK(bad.Status == ModuleStatus::PermanentFailure);
    CHECK(bad.StatusReason.find("protocol_version mismatch") != std::string::npos);
    CHECK(!bad.ProcessRunning);

    sModuleMgr->Unload();
}

// B: happy path. Handshake, heartbeats keep it alive, then core shutdown stops the process.
void ScenarioHappyPath()
{
    std::printf("-- handshake, heartbeats, shutdown\n");
    fs::path const root = g_work / "happy";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(true, 5000, 8000, root); // SDK heartbeats every 5 s; timeout is larger

    sModuleMgr->LoadModules();

    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::Running; }, 5000ms));
    ModuleSnapshot running = Find("minimal-module");
    CHECK(running.ProcessRunning);

    // Long enough for at least one heartbeat from the module (sent every 5 s).
    std::this_thread::sleep_for(7000ms);
    running = Find("minimal-module");
    CHECK(running.Status == ModuleStatus::Running);
    CHECK(running.HeartbeatCount >= 1);
    CHECK(running.ProcessRunning);
    CHECK(running.RestartAttempts == 0);

    sModuleMgr->Unload();
    CHECK(!fs::exists(root / "minimal-module.sock"));
}

// C: handshake failures are retried up to MaxHandshakeAttempts, then the module is permanently failed.
void ScenarioHandshakeRetryBudget()
{
    std::printf("-- handshake timeout: retried, then permanent\n");
    fs::path const root = g_work / "handshake";
    InstallModule(root, "sleeper", "1", g_sleepBinary);
    Configure(true, 500, 15000, root, /*maxHandshakeAttempts=*/3);

    sModuleMgr->LoadModules();

    CHECK(WaitFor([] { return Find("sleeper").Status == ModuleStatus::PermanentFailure; }, 10000ms));
    ModuleSnapshot s = Find("sleeper");
    CHECK(s.StatusReason.find("handshake attempt") != std::string::npos);
    CHECK(s.LastFailure.find("did not connect") != std::string::npos);
    CHECK(s.RestartAttempts == 2); // 3 attempts = 2 retries
    CHECK(!s.ProcessRunning);

    sModuleMgr->Unload();
}

// D: silence after a handshake is a post-handshake failure. Each restart times out again, so the
// rolling restart budget (MaxRestartAttempts = 2) runs out and the module is permanently failed.
void ScenarioHeartbeatCrashLoop()
{
    std::printf("-- heartbeat timeout: restarts exhausted -> permanent\n");
    fs::path const root = g_work / "heartbeat";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(true, 5000, 1000, root); // module heartbeats every 5 s, so every attempt goes silent

    sModuleMgr->LoadModules();

    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::PermanentFailure; }, 15000ms));
    ModuleSnapshot s = Find("minimal-module");
    CHECK(s.StatusReason.find("crash loop") != std::string::npos);
    CHECK(s.LastFailure.find("heartbeat") != std::string::npos);
    CHECK(s.RestartAttempts == 2);
    CHECK(!s.ProcessRunning);

    sModuleMgr->Unload();
}

// E: a module that dies after its handshake is restarted and comes back.
void ScenarioCrashRecovers()
{
    std::printf("-- crash after handshake: restarted and running again\n");
    fs::path const root = g_work / "recover";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(true, 5000, 8000, root);

    sModuleMgr->LoadModules();
    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::Running; }, 5000ms));
    uint64 const firstPid = Find("minimal-module").ProcessId;
    CHECK(firstPid != 0);

    ::kill(static_cast<pid_t>(firstPid), SIGKILL);

    CHECK(WaitFor([firstPid] { return RunningWithNewProcess("minimal-module", firstPid); }, 10000ms));
    ModuleSnapshot s = Find("minimal-module");
    CHECK(s.RestartAttempts == 1);
    CHECK(!s.LastFailure.empty());
    CHECK(s.ProcessId != firstPid);

    sModuleMgr->Unload();
}

// F: repeated crashes exhaust the rolling restart budget and the module is permanently failed.
void ScenarioCrashLoop()
{
    std::printf("-- repeated crashes: permanent after the budget is spent\n");
    fs::path const root = g_work / "crashloop";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(true, 5000, 8000, root, 3, /*maxRestartAttempts=*/2);

    sModuleMgr->LoadModules();

    // Kill each incarnation as soon as it is running: MaxRestartAttempts + 1 times in total.
    uint64 previous = 0;
    for (int kill = 0; kill < 3; ++kill)
    {
        CHECK(WaitFor([previous] { return RunningWithNewProcess("minimal-module", previous); }, 10000ms));
        uint64 const pid = Find("minimal-module").ProcessId;
        if (pid == 0)
            break;
        previous = pid;
        ::kill(static_cast<pid_t>(pid), SIGKILL);
    }

    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::PermanentFailure; }, 10000ms));
    ModuleSnapshot s = Find("minimal-module");
    CHECK(s.StatusReason.find("crash loop") != std::string::npos);
    CHECK(s.RestartAttempts == 2);
    CHECK(!s.ProcessRunning);

    sModuleMgr->Unload();
}

void WriteManifest(fs::path const& root, std::string const& name, std::string const& version, std::string const& hooks)
{
    std::ofstream manifest(root / name / "module.json");
    manifest << "{\"name\":\"" << name << "\",\"version\":\"" << version << "\",\"protocol_version\":\"1\","
             << "\"sdk_package_version\":\"0.1.0\",\"tier1\":{\"hooks\":[" << hooks << "]},\"tier2\":{\"participates\":false}}";
}

// G: approval flow. Unapproved modules are not spawned; `module approve` records them; a capability
// change blocks the module again; a version-only change does not. An unreadable store blocks everything
// and approve refuses to overwrite it.
void ScenarioApprovalFlow()
{
    std::printf("-- approval store: approve, capability change, unreadable store\n");
    fs::path const root = g_work / "approval-flow";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(false, 5000, 8000, root);
    StubConfigMgr::Get().Filename = (root / "worldserver.conf").string();
    fs::path const store = root / "approved_modules.json";

    // 1. Never approved: pending, not spawned, no store file created by loading.
    sModuleMgr->LoadModules();
    ModuleSnapshot s = Find("minimal-module");
    CHECK(s.Status == ModuleStatus::PendingApproval);
    CHECK(s.StatusReason.find("never approved") != std::string::npos);
    CHECK(!s.ProcessRunning);
    CHECK(!fs::exists(store));
    sModuleMgr->Unload();

    // 2. Approve it. Unknown modules and invalid names are refused.
    std::string message;
    CHECK(sModuleMgr->ApproveModule("minimal-module", message));
    CHECK(fs::exists(store));
    CHECK(!sModuleMgr->ApproveModule("no-such-module", message));
    CHECK(!sModuleMgr->ApproveModule("../escape", message));

    // 3. The next load runs it.
    sModuleMgr->LoadModules();
    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::Running; }, 5000ms));
    CHECK(Find("minimal-module").ApprovedVersion == "0.1.0");
    sModuleMgr->Unload();

    // 4. Version-only change: same capabilities, so still approved.
    WriteManifest(root, "minimal-module", "0.2.0", "\"WorldScript.OnStartup\"");
    sModuleMgr->LoadModules();
    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::Running; }, 5000ms));
    sModuleMgr->Unload();

    // 5. Capability change: blocked until approved again, with the change described.
    WriteManifest(root, "minimal-module", "0.2.0", "\"WorldScript.OnStartup\",\"CommandScript\"");
    sModuleMgr->LoadModules();
    s = Find("minimal-module");
    CHECK(s.Status == ModuleStatus::PendingApproval);
    CHECK(s.StatusReason.find("capabilities changed") != std::string::npos);
    CHECK(s.StatusReason.find("adds Tier 1 hook CommandScript") != std::string::npos);
    CHECK(!s.ProcessRunning);
    sModuleMgr->Unload();

    CHECK(sModuleMgr->ApproveModule("minimal-module", message));
    sModuleMgr->LoadModules();
    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::Running; }, 5000ms));
    sModuleMgr->Unload();

    // 6. Unreadable store: nothing spawns, and approve refuses rather than overwriting the file.
    std::string const garbage = "{ this is not json";
    { std::ofstream(store) << garbage; }
    sModuleMgr->LoadModules();
    s = Find("minimal-module");
    CHECK(s.Status == ModuleStatus::PendingApproval);
    CHECK(s.StatusReason.find("approval store unreadable") != std::string::npos);
    CHECK(!s.ProcessRunning);
    CHECK(!sModuleMgr->ApproveModule("minimal-module", message));
    CHECK(message.find("unreadable") != std::string::npos);
    sModuleMgr->Unload();

    std::ifstream check(store);
    std::string contents((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    CHECK(contents == garbage);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <minimal_module> <sleep>\n", argv[0]);
        return 2;
    }
    g_minimalModule = fs::absolute(argv[1]).string();
    g_sleepBinary = fs::absolute(argv[2]).string();
    g_work = fs::temp_directory_path() / ("module-host-test-" + std::to_string(::getpid()));
    fs::remove_all(g_work);
    fs::create_directories(g_work);

    ScenarioApprovalGate();
    ScenarioHappyPath();
    ScenarioHandshakeRetryBudget();
    ScenarioHeartbeatCrashLoop();
    ScenarioCrashRecovers();
    ScenarioCrashLoop();
    ScenarioApprovalFlow();

    fs::remove_all(g_work);
    if (g_failures == 0)
    {
        std::printf("PASS  module host: approval flow, handshake, heartbeats, shutdown, handshake retries,\n"
                    "      crash restart, restart budget (heartbeat and crash loops)\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
