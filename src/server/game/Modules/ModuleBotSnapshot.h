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

class Player;
class Unit;

// Packs a unit into a unit reference (forever_module::MakeUnitRef). Returns false for a unit that cannot be packed:
// a counter above 32 bits, an entry above 24 bits, or a type other than player or creature.
bool EncodeUnitRef(Unit const& unit, uint64& ref);

// The snapshot entry for one bot, with up to `maxNearby` units near it, closest first.
ModuleSnapshotBot BuildSnapshotBot(Player& bot, uint32 maxNearby);

#endif // TRINITYCORE_MODULE_BOT_SNAPSHOT_H
