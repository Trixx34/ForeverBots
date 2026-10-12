/*
 * Module config as the core sends it (doc/module-system/03, Config delivery).
 *
 * A module's settings are the keys Module.<name>.<Key> in the configuration files. The core sends each one with
 * the prefix removed, in ModuleConfig right after the handshake. This file has no game dependencies, so it is
 * unit-tested directly (tests/ModuleConfigTest.cpp); ModuleMgr supplies the keys and values from ConfigMgr.
 */

#ifndef TRINITYCORE_MODULE_CONFIG_H
#define TRINITYCORE_MODULE_CONFIG_H

#include "ModuleWire.h"

#include <functional>
#include <string>
#include <vector>

// Entries for one module. `keys` may hold keys of other modules and of the core; only Module.<moduleName>.<Key> entries
// with a non-empty <Key> are returned, with the prefix removed. `valueOf` returns the value of a key from `keys`.
std::vector<ModuleWire::ConfigEntry> ModuleConfigFor(std::string const& moduleName, std::vector<std::string> const& keys,
                                                     std::function<std::string(std::string const&)> const& valueOf);

#endif // TRINITYCORE_MODULE_CONFIG_H
