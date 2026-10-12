/*
 * Bot lifecycle. See BotMgr.h.
 */

#include "BotMgr.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ModuleMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RealmList.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <thread>

namespace
{

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

// Random syllables form a valid player name without exposing the internal account prefix.
std::string GenerateBotName()
{
    std::string name = std::string(RandomOf(NameStart)) + (urand(0, 1) ? RandomOf(NameMiddle) : "") + RandomOf(NameEnd);
    if (name.size() > 12)
        name.resize(12);
    normalizePlayerName(name);
    return name;
}

bool IsHordeRace(uint8 raceId)
{
    return Player::TeamForRace(raceId) == HORDE;
}

// A session with no socket, owned by BotMgr. Same constructor arguments as the stock session the world creates for a login.
WorldSession* MakeBotSession(uint32 accountId, std::string name)
{
    WorldSession* session = new WorldSession(accountId, std::move(name), 0, std::string(), nullptr, SEC_PLAYER, uint8(0), time_t(0),
        std::string(), Minutes(0), 0u, ClientBuild::VariantId{}, LOCALE_enUS, 0u, false);
    session->SetBot();
    return session;
}

} // namespace

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

void BotMgr::LoadRegistry()
{
    if (_registryLoaded)
        return;
    _registryLoaded = true;

    _loginMaxPerTick = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Login.MaxPerTick", 5)));
    _loginTimeoutMs = uint32(std::max<int32>(1000, sConfigMgr->GetIntDefault("Bot.Login.TimeoutMs", 60000)));
    _reloginDelayMs = uint32(std::max<int32>(0, sConfigMgr->GetIntDefault("Bot.Relogin.DelayMs", 2000)));

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

        QueryResult result = CharacterDatabase.PQuery("SELECT guid, account, name, race, class, gender, level FROM characters WHERE account IN ({})", list);
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

uint64 BotMgr::CreateBot(uint8 raceId, uint8 classId, uint8 level, std::string& error)
{
    std::string username;
    uint32 accountId = 0;
    if (!_freeAccounts.empty())
    {
        // an account whose earlier creation never got a character
        std::tie(accountId, username) = _freeAccounts.back();
        _freeAccounts.pop_back();
    }
    else
    {
        AccountOpResult accountResult = AccountOpResult::AOR_NAME_ALREADY_EXIST;
        for (uint32 attempt = 0; attempt < 5 && accountResult == AccountOpResult::AOR_NAME_ALREADY_EXIST; ++attempt)
        {
            username = Trinity::StringFormat("{}{:04}", BOT_ACCOUNT_PREFIX, _nextAccountNumber++);
            std::array<uint8, 8> raw;
            Trinity::Crypto::GetRandomBytes(raw); // a password nobody knows: the account cannot be logged into with a client
            accountResult = sAccountMgr->CreateAccount(username, ByteArrayToHexStr(raw), BOT_ACCOUNT_EMAIL);
        }

        if (accountResult != AccountOpResult::AOR_OK)
        {
            error = Trinity::StringFormat("account {} could not be created (result {})", username, uint32(accountResult));
            return 0;
        }

        accountId = sAccountMgr->GetId(username);
        LoginDatabase.DirectPExecute("UPDATE account SET locked = 1 WHERE id = {}", accountId);
    }

    auto fail = [&](std::string message)
    {
        _freeAccounts.emplace_back(accountId, username);
        error = std::move(message);
        return uint64(0);
    };

    std::unique_ptr<WorldSession> session(MakeBotSession(accountId, username));

    WorldPackets::Character::CharacterCreateInfo createInfo;
    createInfo.Race = raceId;
    createInfo.Class = classId;
    createInfo.Sex = uint8(urand(0, 1));

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

    // Level up before the first save, so the character is stored at the requested level with its stats and spells.
    if (level > 1)
        player->GiveLevel(level);

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
    LoginDatabase.CommitTransaction(loginTransaction);

    // Character inserts are async-only; the login waits for FinishCreate so the load never reads a missing row.
    uint64 const guid = player->GetGUID().GetCounter();
    _creating.emplace_back(guid, CharacterDatabase.AsyncCommitTransaction(characterTransaction));
    _creating.back().second.AfterComplete([this, guid](bool success) { FinishCreate(guid, success); });

    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), accountId, player->GetName(), player->GetNativeGender(), player->GetRace(),
        player->GetClass(), player->GetLevel(), false);
    sScriptMgr->OnPlayerCreate(player.get());

    BotInfo bot;
    bot.Guid = guid;
    bot.AccountId = accountId;
    bot.AccountName = username;
    bot.Name = player->GetName();
    bot.Race = raceId;
    bot.Class = classId;
    bot.Gender = createInfo.Sex;
    bot.Level = player->GetLevel();
    bot.Horde = IsHordeRace(raceId);
    bot.State = BOT_CREATING;
    _bots[guid] = std::move(bot);

    player.reset();
    session.reset();

    TC_LOG_INFO("server.worldserver", "BotMgr: created bot {} (guid {}, race {}, class {}, level {}).", username, guid, raceId, classId, level);
    return guid;
}

void BotMgr::FinishCreate(uint64 guid, bool success)
{
    _creating.erase(std::remove_if(_creating.begin(), _creating.end(), [guid](auto const& entry) { return entry.first == guid; }), _creating.end());

    auto it = _bots.find(guid);
    if (it == _bots.end())
        return;

    BotInfo& bot = it->second;
    if (!success)
    {
        TC_LOG_ERROR("server.worldserver", "BotMgr: saving new bot {} (guid {}) failed.", bot.Name, guid);
        _freeAccounts.emplace_back(bot.AccountId, bot.AccountName);
        _bots.erase(it);
        return;
    }

    bot.State = BOT_OFFLINE;
    if (bot.LoginRequested)
    {
        bot.LoginRequested = false;
        bot.State = BOT_QUEUED;
        _loginQueue.push_back(guid);
    }
}

bool BotMgr::RequestLogin(uint64 guid, std::string& error)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
    {
        error = "not a bot";
        return false;
    }

    BotInfo& bot = it->second;
    switch (bot.State)
    {
        case BOT_ONLINE:
        case BOT_LOGGING_IN:
        case BOT_QUEUED:
            return true;
        case BOT_CREATING:
            bot.LoginRequested = true;
            return true;
        case BOT_OFFLINE:
            bot.State = BOT_QUEUED;
            bot.LogoutRequested = false;
            _loginQueue.push_back(guid);
            return true;
    }
    return true;
}

bool BotMgr::RequestLogout(uint64 guid, char const* reason)
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return false;

    BotInfo& bot = it->second;
    switch (bot.State)
    {
        case BOT_ONLINE:
            LogoutBot(bot, reason);
            break;
        case BOT_LOGGING_IN:
            bot.LogoutRequested = true; // logs out as soon as it is in the world
            break;
        case BOT_QUEUED:
            _loginQueue.erase(std::remove(_loginQueue.begin(), _loginQueue.end(), guid), _loginQueue.end());
            bot.State = BOT_OFFLINE;
            break;
        case BOT_CREATING:
            bot.LoginRequested = false;
            break;
        case BOT_OFFLINE:
            break;
    }
    return true;
}

void BotMgr::LogoutAll(char const* reason)
{
    std::vector<uint64> guids;
    guids.reserve(_bots.size());
    for (auto const& [guid, bot] : _bots)
        guids.push_back(guid);
    for (uint64 guid : guids)
        RequestLogout(guid, reason);
}

void BotMgr::Update(uint32 diff)
{
    _uptimeMs += diff;
    PollLogins();
    ProcessLogins();
}

// Advances the logins in flight: a bot that is in the world is finished, one that timed out fails.
void BotMgr::PollLogins()
{
    for (size_t i = 0; i < _loggingIn.size();)
    {
        uint64 const guid = _loggingIn[i];
        auto it = _bots.find(guid);
        if (it == _bots.end() || !it->second.Session)
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            continue;
        }

        BotInfo& bot = it->second;
        bot.Session->ProcessBotLoginCallbacks();
        Player* player = bot.Session->GetPlayer();
        if (player && player->IsInWorld())
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            FinishLogin(bot);
            continue;
        }

        if (_uptimeMs - bot.LoginStartedMs > _loginTimeoutMs)
        {
            _loggingIn.erase(_loggingIn.begin() + i);
            FailLogin(bot, "login timed out");
            continue;
        }
        ++i;
    }
}

void BotMgr::Shutdown(char const* reason)
{
    // The world loop has stopped by now, so Update() does not run again. Finish the logins in flight, waiting at most
    // the login timeout, so each bot is either in the world (and logged out below) or failed. Then log everything out.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_loginTimeoutMs);
    while (!_loggingIn.empty() && std::chrono::steady_clock::now() < deadline)
    {
        PollLogins();
        if (!_loggingIn.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<uint64> const stillLoggingIn = _loggingIn;
    for (uint64 guid : stillLoggingIn)
    {
        auto it = _bots.find(guid);
        if (it != _bots.end() && it->second.Session)
            FailLogin(it->second, "server shut down before the login finished");
    }
    _loggingIn.clear();

    LogoutAll(reason);
}

void BotMgr::ProcessLogins()
{
    uint32 started = 0;
    while (!_loginQueue.empty() && started < _loginMaxPerTick)
    {
        uint64 const guid = _loginQueue.front();
        auto it = _bots.find(guid);
        if (it == _bots.end() || it->second.State != BOT_QUEUED)
        {
            _loginQueue.pop_front();
            continue;
        }

        BotInfo& bot = it->second;
        if (bot.NotBeforeMs > _uptimeMs)
            break; // the head is still cooling down; keep the order

        _loginQueue.pop_front();
        StartLogin(bot);
        ++started;
    }
}

void BotMgr::StartLogin(BotInfo& bot)
{
    bot.Session = MakeBotSession(bot.AccountId, bot.AccountName);
    bot.State = BOT_LOGGING_IN;
    bot.LoginStartedMs = _uptimeMs;
    bot.Session->BeginBotLogin(ObjectGuid::Create<HighGuid::Player>(bot.Guid));
    _loggingIn.push_back(bot.Guid);
}

void BotMgr::FinishLogin(BotInfo& bot)
{
    bot.State = BOT_ONLINE;
    if (Player* player = bot.Session->GetPlayer())
    {
        bot.Level = player->GetLevel();
        TC_LOG_INFO("server.worldserver", "BotMgr: bot {} (guid {}) is in the world.", bot.Name, bot.Guid);
    }

    if (bot.LogoutRequested)
    {
        bot.LogoutRequested = false;
        LogoutBot(bot, "LOGOUT_REQUESTED");
    }
}

void BotMgr::FailLogin(BotInfo& bot, char const* reason)
{
    TC_LOG_ERROR("server.worldserver", "BotMgr: login of bot {} (guid {}) failed: {}", bot.Name, bot.Guid, reason);
    delete bot.Session;
    bot.Session = nullptr;
    bot.State = BOT_OFFLINE;
    bot.LogoutRequested = false;
    bot.NotBeforeMs = _uptimeMs + _reloginDelayMs;
}

// Saves and removes the character, drops the session, and tells the controlling module (if any) the bot is gone.
void BotMgr::LogoutBot(BotInfo& bot, char const* reason)
{
    if (!bot.Session)
    {
        bot.State = BOT_OFFLINE;
        return;
    }

    if (Player* player = bot.Session->GetPlayer())
        bot.Level = player->GetLevel();

    sModuleMgr->RevokeBot(bot.Guid, reason);

    bot.Session->LogoutPlayer(true);
    delete bot.Session;
    bot.Session = nullptr;
    bot.State = BOT_OFFLINE;
    bot.NotBeforeMs = _uptimeMs + _reloginDelayMs;

    TC_LOG_INFO("server.worldserver", "BotMgr: bot {} (guid {}) logged out ({}).", bot.Name, bot.Guid, reason);
}

bool BotMgr::Exists(uint64 guid) const
{
    return _bots.contains(guid);
}

bool BotMgr::GetBot(uint64 guid, BotInfo& out) const
{
    auto it = _bots.find(guid);
    if (it == _bots.end())
        return false;
    out = it->second;
    return true;
}

std::vector<BotInfo> BotMgr::ListBots() const
{
    std::vector<BotInfo> list;
    list.reserve(_bots.size());
    for (auto const& [guid, bot] : _bots)
        list.push_back(bot);
    std::sort(list.begin(), list.end(), [](BotInfo const& a, BotInfo const& b) { return a.Guid < b.Guid; });
    return list;
}

Player* BotMgr::GetOnlinePlayer(uint64 guid) const
{
    auto it = _bots.find(guid);
    if (it == _bots.end() || it->second.State != BOT_ONLINE || !it->second.Session)
        return nullptr;
    return it->second.Session->GetPlayer();
}

uint32 BotMgr::OnlineCount() const
{
    return uint32(std::count_if(_bots.begin(), _bots.end(), [](auto const& entry) { return entry.second.State == BOT_ONLINE; }));
}
