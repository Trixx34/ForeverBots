/*
 * Tier 2 shared-memory region for one module (doc/module-system/03, Tier 2 section).
 *
 * The core creates the region when a module's handshake is accepted and removes it when the module's
 * session ends. Layout is ModuleTier2Layout.h. Publishing never blocks: the core writes into the unpublished
 * buffer and then flips the published index, so the module always reads a complete snapshot.
 *
 * Deviation from the design doc: a plain shared_memory_object is used instead of managed_shared_memory.
 * The region holds fixed-size arrays only, so the allocator in managed_shared_memory would add nothing.
 */

#ifndef TRINITYCORE_MODULE_TIER2_H
#define TRINITYCORE_MODULE_TIER2_H

#include "Define.h"
#include "ModuleTier2Layout.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace boost::interprocess
{
class shared_memory_object;
class mapped_region;
}

// One action set the module published: the snapshot tick it answers, and its actions (at most MaxActions).
struct ModuleActionSet
{
    uint64 Tick = 0;
    std::vector<forever_module::BotAction> Actions;
};

class ModuleTier2Region
{
    public:
        struct Bounds
        {
            uint32 MaxBots = 0;
            uint32 MaxNearbyUnits = 0;
            uint32 MaxActions = 0;
            uint32 TickInterval = 1;
        };

        // Creates the region named `name`, replacing any leftover one with the same name.
        static std::shared_ptr<ModuleTier2Region> Create(std::string const& name, Bounds bounds, std::string& error);

        ~ModuleTier2Region();
        ModuleTier2Region(ModuleTier2Region const&) = delete;
        ModuleTier2Region& operator=(ModuleTier2Region const&) = delete;

        std::string const& Name() const { return _name; }
        Bounds const& GetBounds() const { return _bounds; }

        // True when a snapshot is due at this server tick, given the module's tick_interval.
        bool SnapshotDue(uint64 tick) const;

        // Publishes a snapshot for `tick`. Currently writes the header only (no bots yet).
        void PublishSnapshot(uint64 tick);

        // Last tick published, and the action tick the module last published (for logging/staleness).
        uint64 PublishedSnapshotTick() const;
        uint64 PublishedActionTick() const;

        // Copies the action set the module published since the last call. Returns false if nothing new was published.
        // The module publishes into the buffer the core is not reading, so a copy is retried if the index flips during it.
        // A set is new when its buffer index or tick differs from the last one returned. Two publishes between two
        // calls leave only the second visible, so the module should publish at most once per world tick.
        bool ReadNewActions(ModuleActionSet& out);

    private:
        ModuleTier2Region(std::string name, Bounds bounds);

        std::string _name;
        Bounds _bounds;
        std::unique_ptr<boost::interprocess::shared_memory_object> _shm;
        std::unique_ptr<boost::interprocess::mapped_region> _mapping;
        forever_module::Tier2RegionHeader* _header = nullptr;
        std::size_t _snapshotOffset = 0; // first snapshot buffer, from the start of the region
        std::size_t _snapshotBytes = 0;  // one snapshot buffer
        std::size_t _actionOffset = 0;   // first action buffer
        std::size_t _actionBytes = 0;    // one action buffer
        uint8 _seenActionBuffer = 0;     // index of the action set last returned by ReadNewActions
        uint64 _seenActionTick = 0;      // tick of that set
};

#endif // TRINITYCORE_MODULE_TIER2_H
