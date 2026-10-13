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
        void Deactivate(uint64 botGuid) override;
        bool Describe(uint64 botGuid, uint32& race, uint32& classId) const override;

    private:
        uint64 SpawnAlt(ModuleWire::SpawnParamsMsg const& params, std::string& reason);
};

#endif // TRINITY_BOT_MODULE_BACKEND_H
