/*
 * ModuleBotBackend over BotMgr. See BotModuleBackend.h.
 */

#include "BotModuleBackend.h"
#include "BotMgr.h"
#include "DB2Structure.h"
#include "Log.h"
#include "Player.h"
#include "World.h"

bool BotModuleBackend::Exists(uint64 botGuid) const
{
    return sBotMgr->Exists(botGuid);
}

uint64 BotModuleBackend::Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason)
{
    // Race and class are required. Player::Create decides whether the combination is valid.
    if (params.Race == 0 || params.ClassId == 0)
    {
        reason = "race and class are required to spawn a bot";
        return 0;
    }

    // Team: 0 takes the race's faction, 1 is Alliance, 2 is Horde. A team that disagrees with the race is refused,
    // so the module never gets a bot in a faction it did not ask for.
    if (params.Team > 2)
    {
        reason = "team " + std::to_string(params.Team) + " is not 0 (from race), 1 (Alliance) or 2 (Horde)";
        return 0;
    }
    if (params.Team != 0)
    {
        Team const raceTeam = Player::TeamForRace(uint8(params.Race));
        Team const wanted = params.Team == 1 ? ALLIANCE : HORDE;
        if (raceTeam != wanted)
        {
            reason = "race " + std::to_string(params.Race) + " is in the " + (raceTeam == ALLIANCE ? "Alliance" : "Horde") +
                     " faction, not team " + std::to_string(params.Team);
            return 0;
        }
    }

    // Level 0 asks for the race's starting level. Any other level must be one the server allows.
    uint32 const maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    if (params.Level > maxLevel)
    {
        reason = "level " + std::to_string(params.Level) + " is above the server maximum " + std::to_string(maxLevel);
        return 0;
    }
    uint8 const level = params.Level == 0 ? uint8(1) : uint8(params.Level);

    // The run's limits (bot count, stop time, world update interval, database queue) are checked here, so every spawn
    // path honours them, not only the module's ramp.
    if (!sBotMgr->AcceptSpawn(reason))
        return 0;

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
