/*
 * Executes module -> core actions (proto/actions.proto) on the world thread.
 * ModuleMgr queues each ActionRequest and calls ExecuteModuleAction from OnWorldTick, so the world
 * objects are only touched on the thread that owns them. Every action re-checks the world state it
 * depends on; a module's view of a player or item can be stale by the time the action runs.
 */

#ifndef TRINITYCORE_MODULE_ACTIONS_H
#define TRINITYCORE_MODULE_ACTIONS_H

#include "ModuleWire.h"

// Fills reply with the outcome. reply.MessageId is set from the request. Must be called on the world thread.
// moduleName is the requesting module's name; store actions only reach that module's rows. Empty means the module
// is no longer loaded, and the action is refused.
void ExecuteModuleAction(ModuleWire::ActionRequestMsg const& request, ModuleWire::ActionReplyMsg& reply,
                         std::string const& moduleName);

#endif // TRINITYCORE_MODULE_ACTIONS_H
