/*
 * ModuleBotBackend over BotMgr. See BotModuleBackend.h.
 */

#include "BotModuleBackend.h"
#include "BotMgr.h"
#include "Log.h"

bool BotModuleBackend::Exists(uint64 botGuid) const
{
    return sBotMgr->Exists(botGuid);
}

uint64 BotModuleBackend::Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason)
{
    // Race and class are required. Player::Create decides whether the combination is valid. Team follows from
    // the race, and level is the character's starting level, so those two fields are not used here.
    if (params.Race == 0 || params.ClassId == 0)
    {
        reason = "race and class are required to spawn a bot";
        return 0;
    }

    std::string error;
    uint64 const guid = sBotMgr->CreateBot(uint8(params.Race), uint8(params.ClassId), error);
    if (!guid)
        reason = error;
    return guid;
}

void BotModuleBackend::Activate(uint64 botGuid)
{
    std::string error;
    if (!sBotMgr->RequestLogin(botGuid, error))
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot activate bot {}: {}", botGuid, error);
}
