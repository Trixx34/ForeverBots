/*
 * ModuleBotBackend over BotMgr: how a module's bot control requests reach the bot lifecycle.
 */

#ifndef TRINITY_BOT_MODULE_BACKEND_H
#define TRINITY_BOT_MODULE_BACKEND_H

#include "ModuleBotBackend.h"

class BotModuleBackend final : public ModuleBotBackend
{
    public:
        bool Exists(uint64 botGuid) const override;
        uint64 Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason) override;
        void Activate(uint64 botGuid) override;
};

#endif // TRINITY_BOT_MODULE_BACKEND_H
