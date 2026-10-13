/*
 * Module store (doc/module-system/15-module-store.md): key/value rows private to one module, kept in the world database
 * table module_store. The core calls these on a module's behalf, with the module name taken from the session, so a
 * module can only reach its own rows. Modules never send SQL.
 *
 * These run on the world thread and wait for the database. They are small (a few rows per module), so the wait is short.
 */

#ifndef TRINITYCORE_MODULE_STORE_H
#define TRINITYCORE_MODULE_STORE_H

#include "Define.h"
#include "ModuleStoreRules.h"

#include <string>
#include <vector>

namespace ModuleStore
{
// Each function returns an empty string on success, or the reason it failed (for the module's log).
std::string Get(std::string const& module, std::string const& key, bool& found, std::string& value);
std::string Set(std::string const& module, std::string const& key, std::string const& value);
std::string Delete(std::string const& module, std::string const& key, bool& existed);
std::string List(std::string const& module, std::string const& prefix, std::vector<std::string>& keys);
} // namespace ModuleStore

#endif // TRINITYCORE_MODULE_STORE_H
