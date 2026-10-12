/*
 * Unit tests for the capability approval store (ModuleApproval.{h,cpp}).
 *
 * Not part of the worldserver build. Build with the core's include paths, OpenSSL and Boost:
 *   g++ -std=c++20 -Isrc/common -Isrc/common/Cryptography -Isrc/common/Debugging \
 *       -Isrc/server/game/Modules -Isrc/server/game/Modules/tests/stubs \
 *       src/server/game/Modules/{ModuleApproval,ModuleManifestInfo}.cpp \
 *       src/server/game/Modules/tests/ModuleApprovalTest.cpp -lcrypto -lpthread -o module_approval_test
 */

#include "ModuleApproval.h"
#include "ModuleManifestInfo.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;

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

ModuleManifestInfo Parse(std::string const& json)
{
    ModuleManifestInfo info;
    std::string error;
    if (!ParseModuleManifest(json, info, error))
        std::printf("manifest did not parse: %s\n", error.c_str());
    return info;
}

std::string Manifest(std::string const& name, std::string const& version, std::string const& hooks,
                     std::string const& tier2 = "{ \"participates\": false }")
{
    return "{\"name\":\"" + name + "\",\"version\":\"" + version + "\",\"protocol_version\":\"1\"," +
           "\"sdk_package_version\":\"0.1.0\",\"tier1\":{\"hooks\":[" + hooks + "]},\"tier2\":" + tier2 + "}";
}

void TestCanonicalForm()
{
    // Pinned: the canonical JSON and its SHA-256 were computed independently (Python hashlib). A change
    // here changes every stored approval hash, so it must be deliberate.
    ModuleManifestInfo info = Parse(Manifest("m", "0.1.0", "\"WorldScript.OnStartup\",\"CommandScript\""));
    CapabilityDeclaration caps = CapabilitiesOf(info);
    CHECK(CanonicalCapabilityJson(caps) ==
          "{\"tier1\":{\"hooks\":[\"CommandScript\",\"WorldScript.OnStartup\"]},\"tier2\":{\"participates\":false,"
          "\"requested_max_actions\":0,\"requested_max_bots\":0,\"requested_max_nearby_units\":0,\"requested_tick_interval\":1}}");
    CHECK(CapabilityHash(caps) == "b9d2d56f1a680fe415bac13cad1e783eb2761eb9b30e8f2a2b487a4b51ac4ea5");
}

void TestHashInvariance()
{
    // Name, version and SDK version are not capabilities: a bug-fix release keeps its approval.
    auto a = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\""))));
    auto b = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "9.9.9", "\"CommandScript\""))));
    CHECK(a == b);

    // Hook order and duplicates do not change what the module asks for.
    auto c = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"WorldScript.OnStartup\",\"CommandScript\""))));
    auto d = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\",\"WorldScript.OnStartup\",\"CommandScript\""))));
    CHECK(c == d);

    // Adding a hook, or changing Tier 2 at all, does.
    auto e = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\",\"PlayerScript.OnLogin\""))));
    CHECK(a != e);
    auto f = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\"",
        "{ \"participates\": true, \"requested_max_bots\": 8 }"))));
    CHECK(a != f);
    auto g = CapabilityHash(CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\"",
        "{ \"participates\": false, \"requested_max_bots\": 16 }"))));
    CHECK(a != g);
    CHECK(f != g);
}

void TestDescribeChanges()
{
    CapabilityDeclaration before = CapabilitiesOf(Parse(Manifest("m", "0.1.0", "\"CommandScript\"")));
    CapabilityDeclaration after = CapabilitiesOf(Parse(Manifest("m", "0.2.0", "\"PlayerScript.OnLogin\"",
        "{ \"participates\": true, \"requested_max_bots\": 8 }")));

    std::vector<std::string> changes = DescribeCapabilityChanges(before, after);
    auto has = [&](std::string const& text)
    {
        for (std::string const& c : changes)
            if (c == text)
                return true;
        return false;
    };
    CHECK(has("adds Tier 1 hook PlayerScript.OnLogin"));
    CHECK(has("removes Tier 1 hook CommandScript"));
    CHECK(has("enables Tier 2 participation"));
    CHECK(has("changes requested_max_bots from 0 to 8"));
    CHECK(changes.size() == 4);

    // Identical declarations still produce a non-empty description, so the log is never blank.
    CHECK(!DescribeCapabilityChanges(before, before).empty());
}

void TestStore()
{
    fs::path const dir = fs::temp_directory_path() / ("module-approval-test-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    std::string const path = (dir / "approved_modules.json").string();
    std::string error;

    // Missing file: empty store, no error.
    {
        ApprovalStore store;
        CHECK(store.Load(path, error));
        CHECK(store.Find("m") == nullptr);
    }

    // Round trip, including hooks with dots and the tier 2 block.
    ModuleManifestInfo info = Parse(Manifest("m", "0.1.0", "\"WorldScript.OnStartup\",\"CommandScript\"",
        "{ \"participates\": true, \"requested_max_bots\": 4, \"requested_max_nearby_units\": 2, \"requested_max_actions\": 3, \"requested_tick_interval\": 5 }"));
    ApprovalRecord record;
    record.ApprovedVersion = info.Version;
    record.ApprovedAt = "2026-10-12T14:00:00Z";
    record.Capabilities = CapabilitiesOf(info);
    record.CapabilityHash = CapabilityHash(record.Capabilities);
    {
        ApprovalStore store;
        store.Set("m", record);
        store.Set("other-module", record);
        CHECK(store.Save(path, error));
    }
    {
        ApprovalStore store;
        CHECK(store.Load(path, error));
        ApprovalRecord const* loaded = store.Find("m");
        CHECK(loaded != nullptr);
        if (loaded)
        {
            CHECK(loaded->ApprovedVersion == "0.1.0");
            CHECK(loaded->ApprovedAt == "2026-10-12T14:00:00Z");
            CHECK(loaded->CapabilityHash == record.CapabilityHash);
            CHECK(loaded->Capabilities == record.Capabilities);
        }
        CHECK(store.Find("other-module") != nullptr);
    }

    // Malformed file: an error, and the store is left empty so nothing is trusted from it.
    {
        std::ofstream(path) << "{ this is not json";
        ApprovalStore store;
        CHECK(!store.Load(path, error));
        CHECK(!error.empty());
        CHECK(store.Find("m") == nullptr);
    }

    fs::remove_all(dir);
}

void TestModuleNames()
{
    CHECK(IsValidModuleName("minimal-module"));
    CHECK(IsValidModuleName("Module_2"));
    CHECK(!IsValidModuleName(""));
    CHECK(!IsValidModuleName("../escape"));
    CHECK(!IsValidModuleName("has.dot"));
    CHECK(!IsValidModuleName("has space"));
    CHECK(!IsValidModuleName(std::string(65, 'a')));
}

void TestTimestamp()
{
    std::string ts = UtcTimestampNow();
    // 2026-10-12T14:00:00Z
    CHECK(ts.size() == 20);
    CHECK(ts[4] == '-' && ts[7] == '-' && ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts[19] == 'Z');
}

} // namespace

int main()
{
    TestCanonicalForm();
    TestHashInvariance();
    TestDescribeChanges();
    TestStore();
    TestModuleNames();
    TestTimestamp();

    if (g_failures == 0)
    {
        std::printf("PASS  capability approval: canonical hash, invariance, change description, store, names\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
