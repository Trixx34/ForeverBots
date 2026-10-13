/*
 * ModuleBotBackend over BotMgr. See BotModuleBackend.h.
 */

#include "BotModuleBackend.h"
#include "BotMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DB2Structure.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Log.h"
#include "Player.h"
#include "World.h"

bool BotModuleBackend::Exists(uint64 botGuid) const
{
    return sBotMgr->Exists(botGuid);
}

// An alt (Bot.Alt.Enabled): a character of the owner's own account logs in as a bot. The owner must be online, the character must not be,
// and each account may have Bot.Alt.MaxPerAccount alts at once. Nothing is created or changed in the character.
uint64 BotModuleBackend::SpawnAlt(ModuleWire::SpawnParamsMsg const& params, std::string& reason)
{
    if (!sConfigMgr->GetBoolDefault("Bot.Alt.Enabled", false))
    {
        reason = "alts as bots are off (Bot.Alt.Enabled = 0)";
        return 0;
    }
    Player* owner = ObjectAccessor::FindPlayerByLowGUID(params.OwnerGuid);
    if (!owner || !owner->GetSession() || owner->GetSession()->IsBot())
    {
        reason = "the owner is not online as a player";
        return 0;
    }
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(params.AltName);
    if (guid.IsEmpty())
    {
        reason = "no character named " + params.AltName;
        return 0;
    }
    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(guid);
    if (!entry || entry->AccountId != owner->GetSession()->GetAccountId())
    {
        reason = "that character is not on your account";
        return 0;
    }
    if (guid == owner->GetGUID())
    {
        reason = "you cannot make the character you play a bot";
        return 0;
    }
    if (ObjectAccessor::FindPlayerByLowGUID(guid.GetCounter()))
    {
        reason = "that character is online as a player";
        return 0;
    }
    uint32 const maxPerAccount = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Alt.MaxPerAccount", 4)));
    if (sBotMgr->ActiveAltsOf(entry->AccountId) >= maxPerAccount)
    {
        reason = "you already have " + std::to_string(maxPerAccount) + " alts as bots";
        return 0;
    }
    if (!sBotMgr->AcceptSpawn(reason))
        return 0;

    std::string error;
    if (!sBotMgr->StartAlt(guid.GetCounter(), entry->AccountId, owner->GetSession()->GetAccountName(), entry->Name, entry->Race, entry->Class,
                           entry->Sex, entry->Level, error))
    {
        reason = error;
        return 0;
    }
    return guid.GetCounter();
}

void BotModuleBackend::Deactivate(uint64 botGuid)
{
    sBotMgr->StopAlt(botGuid); // only alts: a bot a module lets go of stays in the world
}

bool BotModuleBackend::Describe(uint64 botGuid, uint32& race, uint32& classId) const
{
    BotInfo info;
    if (!sBotMgr->GetBot(botGuid, info))
        return false;
    race = info.Race;
    classId = info.Class;
    return true;
}

uint64 BotModuleBackend::Spawn(ModuleWire::SpawnParamsMsg const& params, std::string& reason)
{
    if (!params.AltName.empty())
        return SpawnAlt(params, reason);

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
    else if (params.Place)
        sBotMgr->SetStart(guid, params.MapId, params.X, params.Y, params.Z);
    return guid;
}

void BotModuleBackend::Activate(uint64 botGuid)
{
    std::string error;
    if (!sBotMgr->RequestLogin(botGuid, error))
        TC_LOG_ERROR("server.worldserver", "BotMgr: cannot activate bot {}: {}", botGuid, error);
}
