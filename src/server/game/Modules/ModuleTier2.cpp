/*
 * Tier 2 shared-memory region. See ModuleTier2.h.
 */

#include "ModuleTier2.h"
#include "Log.h"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <algorithm>
#include <cstring>
#include <new>

using namespace forever_module;

ModuleTier2Region::ModuleTier2Region(std::string name, Bounds bounds)
    : _name(std::move(name)), _bounds(bounds)
{
}

ModuleTier2Region::~ModuleTier2Region()
{
    // Unmap before removing the name so the region can't be reopened by a stale module handle.
    _mapping.reset();
    _shm.reset();
    if (!_name.empty())
        boost::interprocess::shared_memory_object::remove(_name.c_str());
}

std::shared_ptr<ModuleTier2Region> ModuleTier2Region::Create(std::string const& name, Bounds bounds, std::string& error)
{
    using namespace boost::interprocess;

    std::size_t const total = Tier2RegionBytes(bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxActions);

    std::shared_ptr<ModuleTier2Region> region(new ModuleTier2Region(name, bounds));
    region->_snapshotOffset = Tier2HeaderBytes();
    region->_snapshotBytes = Tier2SnapshotBufferBytes(bounds.MaxBots, bounds.MaxNearbyUnits);
    region->_actionOffset = Tier2ActionBufferOffset(bounds.MaxBots, bounds.MaxNearbyUnits, bounds.MaxActions, 0);
    region->_actionBytes = Tier2ActionBufferBytes(bounds.MaxActions);

    try
    {
        // A region left behind by a crashed core would make create_only fail.
        shared_memory_object::remove(name.c_str());
        region->_shm = std::make_unique<shared_memory_object>(create_only, name.c_str(), read_write);
        region->_shm->truncate(static_cast<offset_t>(total));
        region->_mapping = std::make_unique<mapped_region>(*region->_shm, read_write);
    }
    catch (std::exception const& e)
    {
        error = "cannot create shared memory '" + name + "': " + e.what();
        return nullptr;
    }

    void* base = region->_mapping->get_address();
    region->_header = new (base) Tier2RegionHeader();
    region->_header->layout_version = kTier2LayoutVersion;
    region->_header->max_bots = bounds.MaxBots;
    region->_header->max_nearby_units = bounds.MaxNearbyUnits;
    region->_header->max_actions = bounds.MaxActions;
    region->_header->tick_interval = bounds.TickInterval;
    return region;
}

bool ModuleTier2Region::SnapshotDue(uint64 tick) const
{
    uint32 const interval = _bounds.TickInterval == 0 ? 1 : _bounds.TickInterval;
    return tick % interval == 0;
}

void ModuleTier2Region::PublishSnapshot(uint64 tick)
{
    // Write into the buffer the module is not reading, then publish it: tick first, then the index.
    uint8 const next = static_cast<uint8>(1 - _header->published_snapshot_buffer.load(std::memory_order_relaxed));
    auto* base = static_cast<std::byte*>(_mapping->get_address());
    auto* snapshot = new (base + _snapshotOffset + next * _snapshotBytes) WorldTickSnapshotHeader();
    snapshot->tick_number = tick;
    snapshot->bot_count = 0;

    _header->published_snapshot_tick.store(tick, std::memory_order_release);
    _header->published_snapshot_buffer.store(next, std::memory_order_release);
}

uint64 ModuleTier2Region::PublishedSnapshotTick() const
{
    return _header->published_snapshot_tick.load(std::memory_order_acquire);
}

uint64 ModuleTier2Region::PublishedActionTick() const
{
    return _header->published_action_tick.load(std::memory_order_acquire);
}

bool ModuleTier2Region::ReadNewActions(ModuleActionSet& out)
{
    auto const* base = static_cast<std::byte const*>(_mapping->get_address());

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint8 const buffer = _header->published_action_buffer.load(std::memory_order_acquire);
        // The header tick is the cheap check for "nothing new", so an idle module costs one load per tick.
        if (buffer == _seenActionBuffer && _header->published_action_tick.load(std::memory_order_acquire) == _seenActionTick)
            return false;

        auto const* queue = reinterpret_cast<ActionQueueHeader const*>(base + _actionOffset + buffer * _actionBytes);
        uint32 const count = std::min(queue->action_count, _bounds.MaxActions);
        ModuleActionSet copy;
        copy.Tick = queue->tick_number;
        copy.Actions.resize(count);
        if (count > 0)
            std::memcpy(copy.Actions.data(), reinterpret_cast<std::byte const*>(queue + 1), count * sizeof(forever_module::BotAction));

        if (_header->published_action_buffer.load(std::memory_order_acquire) != buffer)
            continue; // the module published again while we copied; read the new buffer

        _seenActionBuffer = buffer;
        _seenActionTick = copy.Tick;
        out = std::move(copy);
        return true;
    }
    return false; // the index kept flipping; the next tick tries again
}
