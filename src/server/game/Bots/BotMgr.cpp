/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BotMgr.h"
#include "AccountMgr.h"
#include "BotLogDatabase.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "UpdateFields.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <sstream>

namespace
{
// Classes bots are made from (Classic): Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid.
constexpr std::array<uint8, 9> BotClasses = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST, CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID };

// The owner's table of race/class combinations (docs/playerbots/race-class-combos.md), 52 combinations with Skyborne counted once
// per class. Race ids: 95 High Order Skyborne (Alliance), 96 Windshaper Skyborne (Horde). Mage: Alliance Skyborne only,
// Shaman: Horde Skyborne only. This table decides what bots are created; playercreateinfo must agree (a refused combination
// shows up as a creation failure, it is not worked around here).
struct BotComboRow
{
    uint8 ClassId;
    std::vector<uint8> Races;
};

std::vector<BotComboRow> const BotCombos =
{
    { CLASS_DRUID,   { RACE_NIGHTELF, RACE_TAUREN, 95, 96 } },
    { CLASS_HUNTER,  { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_ORC, RACE_TAUREN, RACE_TROLL, 95, 96 } },
    { CLASS_MAGE,    { RACE_HUMAN, RACE_GNOME, RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TROLL, 95 } },
    { CLASS_PALADIN, { RACE_HUMAN, RACE_DWARF, RACE_UNDEAD_PLAYER } },
    { CLASS_PRIEST,  { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_UNDEAD_PLAYER, RACE_TROLL } },
    { CLASS_ROGUE,   { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TROLL, 95, 96 } },
    { CLASS_SHAMAN,  { RACE_DWARF, RACE_ORC, RACE_TAUREN, RACE_TROLL, 96 } },
    { CLASS_WARLOCK, { RACE_HUMAN, RACE_GNOME, RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TROLL } },
    { CLASS_WARRIOR, { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_TROLL, 95, 96 } },
};

bool IsHordeRace(uint8 raceId)
{
    return raceId == 96 || (raceId != 95 && Player::TeamForRace(raceId) == HORDE);
}

constexpr uint32 BOT_RELOGIN_DELAY_MS = 2000;      // lets the previous logout save reach the database before the next load
constexpr uint32 BOT_LOGIN_TIMEOUT_MS = 60000;
constexpr char const* BOT_ACCOUNT_PREFIX = "BOT";
constexpr char const* BOT_ACCOUNT_EMAIL = "bot@forever.invalid";

char const* const NameStart[] = { "bal", "bel", "dor", "fen", "gal", "hal", "jor", "kar", "lor", "mar", "nor", "ral", "sal", "tor", "val", "zan", "ur", "vor" };
char const* const NameMiddle[] = { "a", "e", "i", "o", "an", "or", "el", "in" };
char const* const NameEnd[] = { "an", "ar", "en", "il", "on", "or", "us", "ia", "eth", "dan", "wyn", "mir", "gar" };

template<typename T, size_t N>
T const& RandomOf(T const (&values)[N])
{
    return values[urand(0, N - 1)];
}

// "Bot" + random syllables: letters only (a valid player name), clearly a bot
std::string GenerateBotName()
{
    std::string name = "Bot" + std::string(RandomOf(NameStart)) + (urand(0, 1) ? RandomOf(NameMiddle) : "") + RandomOf(NameEnd);
    if (name.size() > 12)
        name.resize(12);
    normalizePlayerName(name); // first letter upper case, rest lower case, like a name typed in character creation
    return name;
}

// Random but valid look: one choice per customization option the race/gender has (choices without requirements only).
void FillCustomizations(WorldSession* session, WorldPackets::Character::CharacterCreateInfo& info)
{
    std::vector<ChrCustomizationOptionEntry const*> const* options = sDB2Manager.GetCustomiztionOptions(info.Race, info.Sex);
    if (!options)
        return;

    std::vector<ChrCustomizationOptionEntry const*> sorted(*options);
    std::ranges::sort(sorted, std::ranges::less(), &ChrCustomizationOptionEntry::ID);

    for (ChrCustomizationOptionEntry const* option : sorted)
    {
        if (option->ChrCustomizationReqID)
            continue;

        std::vector<ChrCustomizationChoiceEntry const*> const* choices = sDB2Manager.GetCustomiztionChoices(option->ID);
        if (!choices)
            continue;

        std::vector<ChrCustomizationChoiceEntry const*> usable;
        for (ChrCustomizationChoiceEntry const* choice : *choices)
            if (!choice->ChrCustomizationReqID)
                usable.push_back(choice);

        if (usable.empty())
            continue;

        UF::ChrCustomizationChoice& entry = info.Customizations.emplace_back();
        entry.ChrCustomizationOptionID = option->ID;
        entry.ChrCustomizationChoiceID = usable[urand(0, usable.size() - 1)]->ID;
    }

    if (!session->ValidateAppearance(Races(info.Race), Classes(info.Class), Gender(info.Sex), MakeChrCustomizationChoiceRange(info.Customizations)))
        info.Customizations.resize(0); // the default look is always accepted
}

WorldSession* MakeBotSession(uint32 accountId, std::string name)
{
    WorldSession* session = new WorldSession(accountId, std::move(name), 0, std::string(), nullptr, SEC_PLAYER, uint8(0), time_t(0),
        std::string(), Minutes(0), 0u, ClientBuild::VariantId{}, LOCALE_enUS, 0u, false);
    session->SetBot();
    return session;
}
}

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

void BotMgr::Update(uint32 diff)
{
    ++_ticks;
    _uptimeMs += diff;

    ProcessLogins();

    if (!IsLogDatabaseAvailable())
        return;

    if ((_logSinceFlushMs += diff) >= _logFlushIntervalMs)
        FlushLog();
}

std::string BotMgr::GetStatus() const
{
    std::ostringstream out;
    out << "BotMgr alive: " << _ticks << " update ticks, " << (_uptimeMs / 1000) << "s uptime, "
        << _onlineCount << " bots online, " << _bots.size() << " known, bot log: ";
    if (IsLogDatabaseAvailable())
        out << "on, " << GetBufferedLogEvents() << " events buffered";
    else
        out << "off (BotLogDatabaseInfo not set)";
    return out.str();
}

void BotMgr::SetLogDatabaseAvailable(bool available)
{
    if (available)
    {
        int32 const minSeverity = sConfigMgr->GetIntDefault("BotLog.MinSeverity", BOTLOG_INFO);
        _logMinSeverity = uint8(std::clamp<int32>(minSeverity, BOTLOG_TRACE, BOTLOG_ERROR));
        _logFlushIntervalMs = uint32(std::max<int32>(100, sConfigMgr->GetIntDefault("BotLog.FlushIntervalMs", 1000)));
        _logMaxBatch = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("BotLog.MaxBatch", 500)));
        TC_LOG_INFO("server.worldserver", "Bot log enabled (min severity {}, flush every {} ms, batches of up to {} events)",
            _logMinSeverity, _logFlushIntervalMs, _logMaxBatch);
    }

    _logAvailable.store(available, std::memory_order_relaxed);
}

void BotMgr::LogEvent(BotEvent&& event)
{
    if (!IsLogDatabaseAvailable() || event.Severity < _logMinSeverity)
        return;

    if (event.Timestamp == 0.0)
        event.Timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    bool flushNow;
    {
        std::lock_guard<std::mutex> lock(_logMutex);
        _logBuffer.push_back(std::move(event));
        flushNow = _logBuffer.size() >= _logMaxBatch;
    }

    // Map update threads must not block on the database: only flag a flush, the world thread performs it.
    if (flushNow)
        _logSinceFlushMs = _logFlushIntervalMs;
}

void BotMgr::LogBotRegistration(uint64 guid, std::string const& name, uint8 classId, uint8 raceId, bool horde)
{
    if (!IsLogDatabaseAvailable())
        return;

    BotLogDatabasePreparedStatement* stmt = BotLogDatabase.GetPreparedStatement(BOTLOG_REP_BOT);
    stmt->setUInt64(0, guid);
    stmt->setString(1, name);
    stmt->setUInt8(2, classId);
    stmt->setUInt8(3, raceId);
    stmt->setString(4, std::string_view(horde ? "horde" : "alliance"));
    BotLogDatabase.Execute(stmt);
}

size_t BotMgr::GetBufferedLogEvents() const
{
    std::lock_guard<std::mutex> lock(_logMutex);
    return _logBuffer.size();
}

void BotMgr::FlushLog(bool sync)
{
    _logSinceFlushMs = 0;

    std::vector<BotEvent> batch;
    {
        std::lock_guard<std::mutex> lock(_logMutex);
        batch.swap(_logBuffer);
    }

    if (batch.empty() || !IsLogDatabaseAvailable())
        return;

    BotLogDatabaseTransaction trans = BotLogDatabase.BeginTransaction();
    for (BotEvent const& e : batch)
    {
        BotLogDatabasePreparedStatement* stmt = BotLogDatabase.GetPreparedStatement(BOTLOG_INS_EVENT);
        uint8 i = 0;
        stmt->setDouble(i++, e.Timestamp);
        stmt->setUInt64(i++, e.BotGuid);
        stmt->setString(i++, e.Type);
        stmt->setUInt8(i++, e.Severity);

        if (e.Reason.empty()) stmt->setNull(i++); else stmt->setString(i++, e.Reason);
        if (e.Summary.empty()) stmt->setNull(i++); else stmt->setString(i++, e.Summary);

        if (e.Level) stmt->setUInt8(i++, *e.Level); else stmt->setNull(i++);
        if (e.MapId) stmt->setUInt16(i++, *e.MapId); else stmt->setNull(i++);
        if (e.ZoneId) stmt->setUInt16(i++, *e.ZoneId); else stmt->setNull(i++);
        if (e.X) stmt->setFloat(i++, *e.X); else stmt->setNull(i++);
        if (e.Y) stmt->setFloat(i++, *e.Y); else stmt->setNull(i++);
        if (e.Z) stmt->setFloat(i++, *e.Z); else stmt->setNull(i++);
        if (e.QuestId) stmt->setUInt32(i++, *e.QuestId); else stmt->setNull(i++);
        if (e.TargetEntry) stmt->setUInt32(i++, *e.TargetEntry); else stmt->setNull(i++);

        // details is bound three times (see BOTLOG_INS_EVENT)
        for (int n = 0; n < 3; ++n)
        {
            if (e.Details.empty()) stmt->setNull(i++); else stmt->setString(i++, e.Details);
        }

        trans->Append(stmt);
    }

    if (sync)
        BotLogDatabase.DirectCommitTransaction(trans);
    else
        BotLogDatabase.CommitTransaction(trans);
}

// --- Bot characters and sessions ---------------------------------------------------------------------

char const* BotMgr::ClassName(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER:  return "hunter";
        case CLASS_ROGUE:   return "rogue";
        case CLASS_PRIEST:  return "priest";
        case CLASS_SHAMAN:  return "shaman";
        case CLASS_MAGE:    return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID:   return "druid";
        default:            return "unknown";
    }
}

char const* BotMgr::StateName(BotRunState state)
{
    switch (state)
    {
        case BOT_OFFLINE:    return "offline";
        case BOT_QUEUED:     return "queued";
        case BOT_LOGGING_IN: return "logging_in";
        case BOT_ONLINE:     return "online";
        case BOT_CREATING:   return "creating";
    }
    return "?";
}

uint8 BotMgr::ParseClass(std::string const& text)
{
    for (uint8 classId : BotClasses)
        if (StringEqualI(text, ClassName(classId)))
            return classId;

    if (Optional<uint32> number = Trinity::StringTo<uint32>(text))
        if (std::ranges::find(BotClasses, *number) != BotClasses.end())
            return uint8(*number);

    return 0;
}

int8 BotMgr::ParseFaction(std::string const& text)
{
    if (StringEqualI(text, "alliance") || StringEqualI(text, "a"))
        return 0;
    if (StringEqualI(text, "horde") || StringEqualI(text, "h"))
        return 1;
    return -1;
}

// A race/class pair bots may be created with: only the owner's table.
bool BotMgr::IsValidCombo(uint8 raceId, uint8 classId)
{
    for (BotComboRow const& row : BotCombos)
        if (row.ClassId == classId)
            return std::ranges::find(row.Races, raceId) != row.Races.end();

    return false;
}

// Random race for the class, uniform over the table's races for that faction (any faction: over all of them), so every
// allowed combination gets exercised.
uint8 BotMgr::PickRace(uint8 classId, int8 faction)
{
    std::vector<uint8> pool;
    for (BotComboRow const& row : BotCombos)
        if (row.ClassId == classId)
            for (uint8 raceId : row.Races)
                if (faction < 0 || IsHordeRace(raceId) == (faction == 1))
                    pool.push_back(raceId);

    return pool.empty() ? 0 : pool[urand(0, pool.size() - 1)];
}

// Finds the bot accounts (BOTnnnn) and their characters. One-time synchronous load on the world thread.
void BotMgr::LoadRegistry()
{
    if (_registryLoaded)
        return;

    _registryLoaded = true;
    _loginMaxPerTick = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Login.MaxPerTick", 5)));

    std::vector<uint32> accounts;
    std::map<uint32, std::string> accountNames;
    if (QueryResult result = LoginDatabase.PQuery("SELECT id, username FROM account WHERE username LIKE '{}%'", BOT_ACCOUNT_PREFIX))
    {
        size_t const prefixLength = strlen(BOT_ACCOUNT_PREFIX);
        do
        {
            Field* fields = result->Fetch();
            std::string username = fields[1].GetString();
            Optional<uint32> number = username.size() > prefixLength ? Trinity::StringTo<uint32>(username.substr(prefixLength)) : std::nullopt;
            if (!number)
                continue;

            accounts.push_back(fields[0].GetUInt32());
            accountNames.emplace(fields[0].GetUInt32(), username);
            _nextAccountNumber = std::max(_nextAccountNumber, *number + 1);
        } while (result->NextRow());
    }

    for (size_t begin = 0; begin < accounts.size(); begin += 400)
    {
        std::string list;
        for (size_t i = begin; i < std::min(accounts.size(), begin + 400); ++i)
            list += (list.empty() ? "" : ",") + std::to_string(accounts[i]);

        QueryResult result = CharacterDatabase.PQuery("SELECT guid, account, name, race, class, gender, level, map, zone, position_x, position_y, position_z FROM characters WHERE account IN ({})", list);
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            BotInfo bot;
            bot.Guid = fields[0].GetUInt64();
            bot.AccountId = fields[1].GetUInt32();
            bot.AccountName = accountNames[bot.AccountId];
            bot.Name = fields[2].GetString();
            bot.Race = fields[3].GetUInt8();
            bot.Class = fields[4].GetUInt8();
            bot.Gender = fields[5].GetUInt8();
            bot.Level = fields[6].GetUInt8();
            bot.Horde = IsHordeRace(bot.Race);
            bot.MapId = fields[7].GetUInt16();
            bot.ZoneId = fields[8].GetUInt16();
            bot.X = fields[9].GetFloat();
            bot.Y = fields[10].GetFloat();
            bot.Z = fields[11].GetFloat();
            _bots.emplace(bot.Guid, std::move(bot));
        } while (result->NextRow());
    }

    std::set<uint32> usedAccounts;
    for (auto const& [guid, bot] : _bots)
        usedAccounts.insert(bot.AccountId);
    for (auto const& [accountId, username] : accountNames)
        if (!usedAccounts.contains(accountId))
            _freeAccounts.emplace_back(accountId, username);

    TC_LOG_INFO("server.worldserver", "BotMgr: {} bot accounts, {} bot characters found", accounts.size(), _bots.size());
}

// Creates a reserved game account (BOTnnnn, random password nobody knows, locked, no battle.net link so no client can log in
// with it) and one character on it through the same path as the character creation handler (Player::Create + SaveToDB).
bool BotMgr::CreateBot(uint8 raceId, uint8 classId, BotInfo& out, std::string& error)
{
    std::string username;
    uint32 accountId = 0;
    if (!_freeAccounts.empty())
    {
        // an account from an earlier creation that never got its character
        std::tie(accountId, username) = _freeAccounts.back();
        _freeAccounts.pop_back();
    }
    else
    {
        AccountOpResult accountResult = AccountOpResult::AOR_NAME_ALREADY_EXIST;
        for (uint32 attempt = 0; attempt < 5 && accountResult == AccountOpResult::AOR_NAME_ALREADY_EXIST; ++attempt)
        {
            username = Trinity::StringFormat("{}{:04}", BOT_ACCOUNT_PREFIX, _nextAccountNumber++);
            std::string password = ByteArrayToHexStr(Trinity::Crypto::GetRandomBytes<8>());
            accountResult = sAccountMgr->CreateAccount(username, password, BOT_ACCOUNT_EMAIL);
        }

        if (accountResult != AccountOpResult::AOR_OK)
        {
            error = Trinity::StringFormat("account {} could not be created (result {})", username, uint32(accountResult));
            return false;
        }

        accountId = sAccountMgr->GetId(username);
        LoginDatabase.DirectPExecute("UPDATE account SET locked = 1, expansion = 0 WHERE id = {}", accountId);
    }

    auto fail = [&](std::string message)
    {
        _freeAccounts.emplace_back(accountId, username); // keep the account for the next bot
        error = std::move(message);
        return false;
    };

    std::unique_ptr<WorldSession> session(MakeBotSession(accountId, std::string(username)));

    WorldPackets::Character::CharacterCreateInfo createInfo;
    createInfo.Race = raceId;
    createInfo.Class = classId;
    createInfo.Sex = uint8(urand(0, 1));
    createInfo.CharCount = 0;
    FillCustomizations(session.get(), createInfo);

    for (uint32 attempt = 0; attempt < 30; ++attempt)
    {
        std::string name = GenerateBotName();
        if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS || sObjectMgr->IsReservedName(name) || sCharacterCache->GetCharacterCacheByName(name))
            continue;

        createInfo.Name = std::move(name);
        break;
    }

    if (createInfo.Name.empty())
        return fail("no free valid bot name found");

    auto deleter = [](Player* player)
    {
        player->CleanupsBeforeDelete();
        delete player;
    };
    std::unique_ptr<Player, decltype(deleter)> player(new Player(session.get()), deleter);
    player->GetMotionMaster()->Initialize();
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &createInfo))
        return fail(Trinity::StringFormat("Player::Create refused race {} class {}", raceId, classId));

    player->setCinematic(1);
    player->SetAtLoginFlag(AT_LOGIN_FIRST);

    CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
    LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();
    player->SaveToDB(loginTransaction, characterTransaction, true);

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
    stmt->setUInt32(0, 1);
    stmt->setUInt32(1, accountId);
    stmt->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
    loginTransaction->Append(stmt);

    // The character insert statements are async-only, so commit asynchronously; the bot is queued for login when the
    // commit completes (FinishCreate), so the login query never reads a row that is not there yet.
    LoginDatabase.CommitTransaction(loginTransaction);
    uint64 const newGuid = player->GetGUID().GetCounter();
    _creating.emplace_back(newGuid, CharacterDatabase.AsyncCommitTransaction(characterTransaction));
    _creating.back().second.AfterComplete([this, newGuid](bool success) { FinishCreate(newGuid, success); });

    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), accountId, player->GetName(), player->GetNativeGender(), player->GetRace(), player->GetClass(), player->GetLevel(), false);
    sScriptMgr->OnPlayerCreate(player.get());

    out = BotInfo();
    out.Guid = player->GetGUID().GetCounter();
    out.AccountId = accountId;
    out.AccountName = username;
    out.Name = player->GetName();
    out.Race = raceId;
    out.Class = classId;
    out.Gender = createInfo.Sex;
    out.Level = player->GetLevel();
    out.Horde = IsHordeRace(raceId);
    out.JustCreated = true;
    out.State = BOT_CREATING;
    out.MapId = uint16(player->GetMapId());
    out.ZoneId = uint16(player->GetZoneId());
    out.X = player->GetPositionX();
    out.Y = player->GetPositionY();
    out.Z = player->GetPositionZ();

    player.reset();
    session.reset();

    LogBotRegistration(out.Guid, out.Name, out.Class, out.Race, out.Horde);
    LogLifecycle(out, "create", "BOT_CREATED", "bot character created", BOTLOG_INFO, Trinity::StringFormat(
        R"({{"account":"{}","account_id":{},"gender":{},"start_map":{}}})", username, accountId, out.Gender, out.MapId));
    return true;
}

BotSpawnResult BotMgr::SpawnBots(uint32 count, uint8 classId, int8 faction, std::optional<uint8> level)
{
    BotSpawnResult result;
    LoadRegistry();

    if (!sWorld->getBoolConfig(CONFIG_BOT_ENABLED))
    {
        result.Error = "bots are disabled (Bot.Enabled = 0)";
        return result;
    }

    if (count == 0)
    {
        result.Error = "count must be at least 1";
        return result;
    }

    if (classId && !PickRace(classId, faction))
    {
        result.Error = Trinity::StringFormat("no race can be a {} {}", faction == 0 ? "alliance" : faction == 1 ? "horde" : "bot of either faction", ClassName(classId));
        return result;
    }

    std::vector<BotInfo*> targets;
    for (auto& [guid, bot] : _bots)
    {
        if (targets.size() >= count)
            break;

        if (bot.State != BOT_OFFLINE || (classId && bot.Class != classId) || (faction >= 0 && bot.Horde != (faction == 1)))
            continue;

        targets.push_back(&bot);
        ++result.Reused;
    }

    while (targets.size() < count)
    {
        uint8 const botClass = classId ? classId : BotClasses[urand(0, BotClasses.size() - 1)];
        uint8 const raceId = PickRace(botClass, faction);
        if (!raceId)
        {
            ++result.Failed;
            result.Error = Trinity::StringFormat("no race can be a {} {}", faction == 0 ? "alliance" : "horde", ClassName(botClass));
            break;
        }

        BotInfo created;
        std::string error;
        if (!CreateBot(raceId, botClass, created, error))
        {
            ++result.Failed;
            result.Error = std::move(error);
            break;
        }

        BotInfo& bot = _bots.emplace(created.Guid, std::move(created)).first->second;
        targets.push_back(&bot);
        ++result.Created;
    }

    for (BotInfo* bot : targets)
    {
        bot->PendingLevel = level;
        bot->DespawnRequested = false;
        if (bot->State != BOT_CREATING) // new characters are queued once their rows are committed
        {
            bot->State = BOT_QUEUED;
            _loginQueue.push_back(bot->Guid);
        }
        result.Names.push_back(bot->Name);
    }

    return result;
}

uint32 BotMgr::DespawnBots(std::string const& name)
{
    bool const all = name.empty() || StringEqualI(name, "all");
    uint32 count = 0;
    for (auto& [guid, bot] : _bots)
    {
        if (!all && !StringEqualI(name, bot.Name))
            continue;

        switch (bot.State)
        {
            case BOT_ONLINE:
                LogoutBot(bot, "LOGOUT_COMMAND");
                ++count;
                break;
            case BOT_LOGGING_IN:
            case BOT_CREATING:
                bot.DespawnRequested = true;
                ++count;
                break;
            case BOT_QUEUED:
                bot.State = BOT_OFFLINE; // dropped from the queue when it comes up
                ++count;
                break;
            case BOT_OFFLINE:
                break;
        }
    }

    return count;
}

void BotMgr::LogoutAll(char const* reason)
{
    uint32 count = 0;
    for (auto& [guid, bot] : _bots)
    {
        if (bot.State == BOT_ONLINE)
        {
            LogoutBot(bot, reason);
            ++count;
        }
        else if (bot.State == BOT_LOGGING_IN)
        {
            // character load still in flight: nothing to save yet, drop the session
            delete bot.Session;
            bot.Session = nullptr;
            bot.State = BOT_OFFLINE;
        }
        else
            bot.State = BOT_OFFLINE;
    }

    _loginQueue.clear();
    _loggingIn.clear();
    _creating.clear(); // the commits still reach the database, the bots are picked up by the next spawn
    if (count)
    {
        TC_LOG_INFO("server.worldserver", "BotMgr: {} bots saved and logged out ({})", count, reason);
        FlushLog(true); // the logout events must not be lost with the buffer
    }
}

std::vector<BotInfo> BotMgr::ListBots()
{
    LoadRegistry();

    std::vector<BotInfo> list;
    list.reserve(_bots.size());
    for (auto& [guid, bot] : _bots)
    {
        if (bot.State == BOT_ONLINE && bot.Session)
        {
            if (Player* player = bot.Session->GetPlayer())
            {
                bot.Level = player->GetLevel();
                bot.MapId = uint16(player->GetMapId());
                bot.ZoneId = uint16(player->GetZoneId());
                bot.X = player->GetPositionX();
                bot.Y = player->GetPositionY();
                bot.Z = player->GetPositionZ();
            }
        }

        list.push_back(bot);
    }

    return list;
}

void BotMgr::StartLogin(BotInfo& bot)
{
    bot.Session = MakeBotSession(bot.AccountId, std::string(bot.AccountName));
    bot.State = BOT_LOGGING_IN;
    bot.LoginStartedMs = _uptimeMs;
    bot.Session->BeginBotLogin(ObjectGuid::Create<HighGuid::Player>(bot.Guid));
    _loggingIn.push_back(bot.Guid);
}

// Starts queued logins (a few per tick) and completes the ones whose character load has finished.
void BotMgr::ProcessLogins()
{
    for (size_t i = 0; i < _creating.size();)
    {
        // InvokeIfReady runs FinishCreate once the commit is done
        if (_creating[i].second.InvokeIfReady())
            _creating.erase(_creating.begin() + i);
        else
            ++i;
    }

    if (_loginQueue.empty() && _loggingIn.empty())
        return;

    uint32 started = 0;
    for (size_t n = _loginQueue.size(); n > 0 && started < _loginMaxPerTick; --n)
    {
        uint64 const guid = _loginQueue.front();
        _loginQueue.pop_front();

        auto itr = _bots.find(guid);
        if (itr == _bots.end() || itr->second.State != BOT_QUEUED)
            continue;

        BotInfo& bot = itr->second;
        if (bot.NotBeforeMs > _uptimeMs)
        {
            _loginQueue.push_back(guid);
            continue;
        }

        StartLogin(bot);
        ++started;
    }

    for (size_t i = 0; i < _loggingIn.size();)
    {
        auto itr = _bots.find(_loggingIn[i]);
        if (itr == _bots.end() || itr->second.State != BOT_LOGGING_IN || !itr->second.Session)
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            continue;
        }

        BotInfo& bot = itr->second;
        bot.Session->ProcessBotLoginCallbacks();

        if (bot.Session->PlayerLoading())
        {
            if (_uptimeMs - bot.LoginStartedMs > BOT_LOGIN_TIMEOUT_MS)
            {
                _loggingIn.erase(_loggingIn.begin() + i);
                FailLogin(bot, "LOGIN_TIMEOUT");
            }
            else
                ++i;
            continue;
        }

        _loggingIn.erase(_loggingIn.begin() + i);
        if (bot.Session->GetPlayer() && bot.Session->GetPlayer()->IsInWorld())
            FinishLogin(bot);
        else
            FailLogin(bot, "LOGIN_FAILED");
    }
}

void BotMgr::FinishCreate(uint64 guid, bool success)
{
    auto itr = _bots.find(guid);
    if (itr == _bots.end() || itr->second.State != BOT_CREATING)
        return;

    BotInfo& bot = itr->second;
    if (!success)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: saving the new bot {} (guid {}) failed", bot.Name, bot.Guid);
        LogLifecycle(bot, "error", "CREATE_FAILED", "bot character could not be saved", BOTLOG_ERROR, Trinity::StringFormat(R"({{"account_id":{}}})", bot.AccountId));
        sCharacterCache->DeleteCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(bot.Guid), bot.Name);
        _freeAccounts.emplace_back(bot.AccountId, bot.AccountName);
        _bots.erase(itr);
        return;
    }

    if (bot.DespawnRequested)
    {
        bot.DespawnRequested = false;
        bot.State = BOT_OFFLINE;
        return;
    }

    bot.State = BOT_QUEUED;
    _loginQueue.push_back(guid);
}

void BotMgr::FinishLogin(BotInfo& bot)
{
    Player* player = bot.Session->GetPlayer();
    bot.State = BOT_ONLINE;
    ++_onlineCount;

    if (bot.PendingLevel && player->GetLevel() != *bot.PendingLevel)
    {
        uint8 const level = std::clamp<uint8>(*bot.PendingLevel, 1, uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)));
        player->GiveLevel(level);
        player->InitTalentForLevel();
        player->SetXP(0);
    }
    bot.PendingLevel.reset();

    bot.Level = player->GetLevel();
    bot.MapId = uint16(player->GetMapId());
    bot.ZoneId = uint16(player->GetZoneId());
    bot.X = player->GetPositionX();
    bot.Y = player->GetPositionY();
    bot.Z = player->GetPositionZ();

    LogBotRegistration(bot.Guid, bot.Name, bot.Class, bot.Race, bot.Horde);
    LogLifecycle(bot, "state_change", "BOT_LOGIN", "bot logged in", BOTLOG_INFO, Trinity::StringFormat(
        R"({{"login_ms":{},"new_character":{},"account_id":{}}})", _uptimeMs - bot.LoginStartedMs, bot.JustCreated ? "true" : "false", bot.AccountId));
    bot.JustCreated = false;

    if (bot.DespawnRequested)
    {
        bot.DespawnRequested = false;
        LogoutBot(bot, "LOGOUT_COMMAND");
    }
}

void BotMgr::FailLogin(BotInfo& bot, char const* reason)
{
    TC_LOG_ERROR("server.worldserver", "BotMgr: login of bot {} (guid {}) failed: {}", bot.Name, bot.Guid, reason);
    LogLifecycle(bot, "error", reason, "bot login failed", BOTLOG_ERROR, Trinity::StringFormat(R"({{"account_id":{}}})", bot.AccountId));

    delete bot.Session;
    bot.Session = nullptr;
    bot.State = BOT_OFFLINE;
    bot.DespawnRequested = false;
    bot.PendingLevel.reset();
    bot.NotBeforeMs = _uptimeMs + BOT_RELOGIN_DELAY_MS;
}

// Saves and removes the character, then drops the session. Same sequence the world uses for a logging out player.
void BotMgr::LogoutBot(BotInfo& bot, char const* reason)
{
    if (!bot.Session)
        return;

    if (Player* player = bot.Session->GetPlayer())
    {
        bot.Level = player->GetLevel();
        bot.MapId = uint16(player->GetMapId());
        bot.ZoneId = uint16(player->GetZoneId());
        bot.X = player->GetPositionX();
        bot.Y = player->GetPositionY();
        bot.Z = player->GetPositionZ();
    }

    LogLifecycle(bot, "state_change", reason, "bot logged out", BOTLOG_INFO, Trinity::StringFormat(
        R"({{"online_ms":{}}})", _uptimeMs - bot.LoginStartedMs));

    bot.Session->LogoutPlayer(true);
    delete bot.Session;
    bot.Session = nullptr;

    if (bot.State == BOT_ONLINE)
        --_onlineCount;

    bot.State = BOT_OFFLINE;
    bot.NotBeforeMs = _uptimeMs + BOT_RELOGIN_DELAY_MS;
}

void BotMgr::LogLifecycle(BotInfo const& bot, char const* type, char const* reason, char const* summary, uint8 severity, std::string details)
{
    BotEvent event;
    event.BotGuid = bot.Guid;
    event.Type = type;
    event.Severity = severity;
    event.Reason = reason;
    event.Summary = summary;
    event.Level = bot.Level;
    event.MapId = bot.MapId;
    event.ZoneId = bot.ZoneId;
    event.X = bot.X;
    event.Y = bot.Y;
    event.Z = bot.Z;
    event.Details = std::move(details);
    LogEvent(std::move(event));
}
