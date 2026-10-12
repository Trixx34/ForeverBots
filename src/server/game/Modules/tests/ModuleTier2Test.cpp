// Tier 2 region: the core creates it, a module-side reader maps it by name and reads what was published.
// Built by hand like the other module tests (see ModuleSessionTest.cpp for the command line, with
// ModuleTier2.cpp added and -lrt on older glibc).

#include "ModuleTier2.h"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <cstdio>
#include <string>

namespace bi = boost::interprocess;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

} // namespace

int main()
{
    std::string const name = "trinity_module_test_" + std::to_string(::getpid()) + "_tier2";

    std::string error;
    ModuleTier2Region::Bounds bounds;
    bounds.MaxBots = 4;
    bounds.MaxNearbyUnits = 2;
    bounds.MaxActions = 8;
    bounds.TickInterval = 3;
    auto region = ModuleTier2Region::Create(name, bounds, error);
    CHECK(region != nullptr);
    if (!region)
    {
        std::printf("create failed: %s\n", error.c_str());
        return 1;
    }

    // Cadence: every third tick.
    CHECK(region->SnapshotDue(3));
    CHECK(region->SnapshotDue(6));
    CHECK(!region->SnapshotDue(4));

    // A module attaches by name and sees the layout version and bounds the core granted.
    bi::shared_memory_object shm(bi::open_only, name.c_str(), bi::read_only);
    bi::mapped_region map(shm, bi::read_only);
    auto const* header = static_cast<forever_module::Tier2RegionHeader const*>(map.get_address());
    CHECK(header->layout_version == forever_module::kTier2LayoutVersion);
    CHECK(header->max_bots == 4);
    CHECK(header->max_nearby_units == 2);
    CHECK(header->max_actions == 8);
    CHECK(header->tick_interval == 3);
    CHECK(header->published_snapshot_tick.load() == 0);

    // Publish two snapshots: the module must see the second one, and the buffer index must flip.
    region->PublishSnapshot(3);
    CHECK(header->published_snapshot_tick.load() == 3);
    uint8_t const firstBuffer = header->published_snapshot_buffer.load();
    region->PublishSnapshot(6);
    CHECK(header->published_snapshot_tick.load() == 6);
    CHECK(header->published_snapshot_buffer.load() != firstBuffer);
    CHECK(region->PublishedSnapshotTick() == 6);

    // The published buffer holds the header written for tick 6.
    auto const* base = static_cast<std::byte const*>(map.get_address());
    auto const* snapshot = reinterpret_cast<forever_module::WorldTickSnapshotHeader const*>(
        base + forever_module::Tier2SnapshotBufferOffset(4, 2, header->published_snapshot_buffer.load()));
    CHECK(snapshot->tick_number == 6);

    // Removing the region drops the name, so a later module attach fails.
    region.reset();
    bool gone = false;
    try
    {
        bi::shared_memory_object again(bi::open_only, name.c_str(), bi::read_only);
    }
    catch (bi::interprocess_exception const&)
    {
        gone = true;
    }
    CHECK(gone);

    if (g_failures == 0)
    {
        std::printf("PASS  Tier 2 region\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
