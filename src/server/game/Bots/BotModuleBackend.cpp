/*
 * ModuleBotBackend over BotMgr. See BotModuleBackend.h.
 */

#include "BotModuleBackend.h"
#include "BotMgr.h"
#include "Log.h"
#include "World.h"

bool BotModuleBackend::Exists(uint64 botGuid) const
{
    return sBotMgr->Exists(botGuid);
}

uint64 BotModuleBackend::Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason)
{
    // Race and class are required. Player::Create decides whether the combination is valid. Team follows from the
    // race and is not used.
    if (params.Race == 0 || params.ClassId == 0)
    {
        reason = "race and class are required to spawn a bot";
        return 0;
    }

    // Level 0 asks for the race's starting level. Any other level must be one the server allows.
    uint32 const maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    if (params.Level > maxLevel)
    {
        reason = "level " + std::to_string(params.Level) + " is above the server maximum " + std::to_string(maxLevel);
        return 0;
    }
    uint8 const level = params.Level == 0 ? uint8(1) : uint8(params.Level);

    std::string error;
    uint64 const guid = sBotMgr->CreateBot(uint8(params.Race), uint8(params.ClassId), level, error);
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
