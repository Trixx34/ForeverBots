// Tier 2 region: the core creates it, a module-side reader maps it by name and reads what was published.
// Built by hand like the other module tests (see ModuleSessionTest.cpp for the command line, with
// ModuleTier2.cpp added and -lrt on older glibc).

#include "ModuleTier2.h"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

    // Actions: the module writes into the buffer the core is not reading, then publishes the tick and the index,
    // the way the SDK's Tier2Session::PublishActions does. The core reads only what is new.
    bi::shared_memory_object shmRw(bi::open_only, name.c_str(), bi::read_write);
    bi::mapped_region rw(shmRw, bi::read_write);
    auto* rwBase = static_cast<std::byte*>(rw.get_address());
    auto* rwHeader = static_cast<forever_module::Tier2RegionHeader*>(rw.get_address());

    auto publishActions = [&](std::uint64_t actionTick, std::vector<forever_module::BotAction> const& actions,
                              std::uint32_t claimedCount)
    {
        std::uint8_t const target = rwHeader->published_action_buffer.load() ^ 1;
        auto* queue = reinterpret_cast<forever_module::ActionQueueHeader*>(
            rwBase + forever_module::Tier2ActionBufferOffset(4, 2, 8, target));
        queue->tick_number = actionTick;
        queue->action_count = claimedCount;
        if (!actions.empty())
            std::memcpy(static_cast<void*>(queue + 1), actions.data(), actions.size() * sizeof(forever_module::BotAction));
        rwHeader->published_action_tick.store(actionTick);
        rwHeader->published_action_buffer.store(target);
    };

    forever_module::BotAction move;
    move.bot_guid = 42;
    move.type = forever_module::ActionType::kMoveTo;
    move.dest_x = 1.0f;
    forever_module::BotAction stop;
    stop.bot_guid = 42;
    stop.type = forever_module::ActionType::kStop;

    ModuleActionSet actions;
    CHECK(!region->ReadNewActions(actions)); // nothing published yet

    publishActions(7, {move, stop}, 2);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Tick == 7);
    CHECK(actions.Actions.size() == 2);
    if (actions.Actions.size() == 2)
    {
        CHECK(actions.Actions[0].bot_guid == 42 && actions.Actions[0].type == forever_module::ActionType::kMoveTo);
        CHECK(actions.Actions[0].dest_x == 1.0f);
        CHECK(actions.Actions[1].type == forever_module::ActionType::kStop);
    }
    CHECK(!region->ReadNewActions(actions)); // the same set is not returned twice

    // An empty set is still new (it tells the core the module now wants nothing).
    publishActions(8, {}, 0);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Tick == 8);
    CHECK(actions.Actions.empty());

    // A claimed count above MaxActions is clamped to what the region holds (8 here).
    std::vector<forever_module::BotAction> full(8, stop);
    publishActions(9, full, 9);
    CHECK(region->ReadNewActions(actions));
    CHECK(actions.Actions.size() == 8);

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
