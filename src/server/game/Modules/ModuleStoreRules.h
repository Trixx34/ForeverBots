/*
 * Limits and key rules for the module store (doc/module-system/15-module-store.md). No database dependencies, so the
 * rules are unit-tested directly (tests/ModuleStoreRulesTest.cpp). ModuleStore applies them before it touches the database.
 */

#ifndef TRINITYCORE_MODULE_STORE_RULES_H
#define TRINITYCORE_MODULE_STORE_RULES_H

#include <cstddef>
#include <string>

namespace ModuleStore
{
inline constexpr std::size_t MaxKeyBytes = 128;
inline constexpr std::size_t MaxValueBytes = 1024;
inline constexpr std::size_t MaxRowsPerModule = 1000;
inline constexpr std::size_t MaxListed = 1000;

// Keys are printable ASCII without spaces, 1 to MaxKeyBytes long. Returns false, with the reason, if one is not.
bool ValidKey(std::string const& key, std::string& reason);

// The LIKE pattern that matches keys starting with `prefix`: %, _ and \ in the prefix are escaped.
std::string PrefixPattern(std::string const& prefix);
} // namespace ModuleStore

#endif // TRINITYCORE_MODULE_STORE_RULES_H
