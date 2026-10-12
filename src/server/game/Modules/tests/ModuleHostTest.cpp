/*
 * End-to-end test of the core-side module host (ModuleMgr + ModuleSession) against a real module
 * process: the forever-module-sdk example `minimal_module`, which handshakes and heartbeats.
 *
 * Not part of the worldserver build. Build against an SDK build tree and a bundled-fmt checkout:
 *   g++ -std=c++17 -DFMT_HEADER_ONLY -Isrc/common -Isrc/server/game/Modules/tests/stubs \
 *       -Isrc/server/game/Modules -Idep/fmt/include \
 *       src/server/game/Modules/{ModuleMgr,ModuleSession,ModuleWire,ModuleManifestInfo}.cpp \
 *       src/server/game/Modules/tests/ModuleHostTest.cpp \
 *       -lboost_filesystem -lboost_system -lpthread -o module_host_test
 *   ./module_host_test <path-to-minimal_module> <path-to-sleep>
 * where <path-to-sleep> is /bin/sleep: it exits at once with an error and never handshakes, which
 * exercises the handshake timeout.
 */

#include "ModuleMgr.h"
#include "Config.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

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

void Configure(bool autoApprove, int handshakeMs, int heartbeatMs, fs::path const& root)
{
    auto& cfg = StubConfigMgr::Get().Values;
    cfg["Modules.Enabled"] = "1";
    cfg["Modules.Dir"] = root.string();
    cfg["Modules.AutoApproveLocal"] = autoApprove ? "1" : "0";
    cfg["Modules.HandshakeTimeoutMs"] = std::to_string(handshakeMs);
    cfg["Modules.HeartbeatTimeoutMs"] = std::to_string(heartbeatMs);
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

    sModuleMgr->Unload();
    CHECK(!fs::exists(root / "minimal-module.sock"));
}

// C: handshake timeout. The "module" exits without connecting.
void ScenarioHandshakeTimeout()
{
    std::printf("-- handshake timeout\n");
    fs::path const root = g_work / "handshake";
    InstallModule(root, "sleeper", "1", g_sleepBinary);
    Configure(true, 1000, 15000, root);

    sModuleMgr->LoadModules();

    CHECK(WaitFor([] { return Find("sleeper").Status == ModuleStatus::TransientFailure; }, 5000ms));
    ModuleSnapshot s = Find("sleeper");
    CHECK(s.StatusReason.find("did not connect") != std::string::npos);
    CHECK(!s.ProcessRunning);

    sModuleMgr->Unload();
}

// D: heartbeat timeout. Handshake succeeds, but the timeout is shorter than the module's interval.
void ScenarioHeartbeatTimeout()
{
    std::printf("-- heartbeat timeout\n");
    fs::path const root = g_work / "heartbeat";
    InstallModule(root, "minimal-module", "1", g_minimalModule);
    Configure(true, 5000, 1000, root); // module heartbeats every 5 s

    sModuleMgr->LoadModules();

    CHECK(WaitFor([] { return Find("minimal-module").Status == ModuleStatus::TransientFailure; }, 6000ms));
    ModuleSnapshot s = Find("minimal-module");
    CHECK(s.StatusReason.find("heartbeat") != std::string::npos);
    CHECK(!s.ProcessRunning); // killed after the timeout

    sModuleMgr->Unload();
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
    ScenarioHandshakeTimeout();
    ScenarioHeartbeatTimeout();

    fs::remove_all(g_work);
    if (g_failures == 0)
    {
        std::printf("PASS  module host: approval gate, handshake, heartbeats, timeouts, shutdown\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
