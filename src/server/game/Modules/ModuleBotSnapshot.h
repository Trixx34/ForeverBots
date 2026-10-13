/*
 * Fills the per-bot part of a Tier 2 snapshot from game state (doc/module-system/03, Tier 2 section).
 *
 * Runs on the world thread, once per snapshot tick, for the bots a module controls and that are online. The
 * snapshot carries what a module needs to decide the next action: position, health, mana, combat target and
 * nearby units. Spell cooldowns are not filled yet and read as zero.
 */

#ifndef TRINITYCORE_MODULE_BOT_SNAPSHOT_H
#define TRINITYCORE_MODULE_BOT_SNAPSHOT_H

#include "Define.h"
#include "ModuleTier2.h"

#include <vector>

class Player;
class Unit;

// Packs a unit into a unit reference (forever_module::MakeUnitRef). Returns false for a unit that cannot be packed:
// a counter above 32 bits, an entry above 24 bits, or a type other than player or creature.
bool EncodeUnitRef(Unit const& unit, uint64& ref);

// Players the nearby list keeps before creatures. A bot fights creatures; a few players keep following possible. Without
// this cap, a crowd of bots fills every slot with other bots and no creature is ever seen (the 1500-bot run).
constexpr uint32 kNearbyPlayerSlots = 4;

// The snapshot entry for one bot, with up to `maxNearby` units near it. Creatures come first, closest first, and at most
// kNearbyPlayerSlots players are listed ahead of them. When refreshNearby is false the nearby units are the last ones
// stored in cachedNearby, so the range query runs for only some of the bots on each tick.
ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby, bool refreshNearby, std::vector<NearbyUnit>& cachedNearby);

#endif // TRINITYCORE_MODULE_BOT_SNAPSHOT_H
