/*
 * The core's side of bot control: how a bot is looked up and created. ModuleMgr calls these on the world thread,
 * only after checking ownership and max_bots itself.
 *
 * The bot implementation provides the concrete backend when it moves to the module system. Until a backend is
 * installed, every control request is refused with "bot control is not available on this core".
 */

#ifndef TRINITYCORE_MODULE_BOT_BACKEND_H
#define TRINITYCORE_MODULE_BOT_BACKEND_H

#include "Define.h"
#include "ModuleWire.h"

#include <string>

class ModuleBotBackend
{
    public:
        virtual ~ModuleBotBackend() = default;

        // True if a bot with this guid exists on this core.
        virtual bool Exists(uint64 botGuid) const = 0;

        // Creates a bot from the spawn parameters. Returns its guid, or 0 with the reason in `reason`.
        virtual uint64 Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason) = 0;
};

#endif // TRINITYCORE_MODULE_BOT_BACKEND_H
