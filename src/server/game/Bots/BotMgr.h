/*
 * Bot lifecycle for the core: bot characters on reserved accounts, created, logged in, and logged out here.
 * What a bot does is decided by its controlling module (see Modules/ModuleBotBackend.h and ModuleMgr::RevokeBot).
 *
 * All state is touched from the world thread only.
 */

#ifndef TRINITY_BOT_MGR_H
#define TRINITY_BOT_MGR_H

#include "Define.h"
#include "DatabaseEnvFwd.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Player;
class WorldSession;

enum BotRunState : uint8
{
    BOT_OFFLINE    = 0,
    BOT_QUEUED     = 1, // waiting for a login slot
    BOT_LOGGING_IN = 2, // session created, character load in flight
    BOT_ONLINE     = 3,
    BOT_CREATING   = 4  // character just created, its rows are being committed
};

// One bot character: a normal character on a BOTnnnn game account, one account per bot.
struct BotInfo
{
    uint64 Guid = 0;
    uint32 AccountId = 0;
    std::string AccountName;
    std::string Name;
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Gender = 0;
    uint8 Level = 1;
    bool Horde = false;

    BotRunState State = BOT_OFFLINE;
    WorldSession* Session = nullptr;   // owned by BotMgr; never in World's session list
    bool LoginRequested = false;       // login asked for while the character was being created
    bool LogoutRequested = false;      // logout asked for while the character was logging in
    uint32 NotBeforeMs = 0;            // no login before this uptime (the previous logout save must land first)
    uint32 LoginStartedMs = 0;
};

class TC_GAME_API BotMgr
{
    public:
        static BotMgr* instance();

        BotMgr(BotMgr const&) = delete;
        BotMgr(BotMgr&&) = delete;
        BotMgr& operator=(BotMgr const&) = delete;
        BotMgr& operator=(BotMgr&&) = delete;

        // Reads the existing bot accounts and their characters. Call once, after the databases are open.
        void LoadRegistry();

        // Called once per world update tick. Advances creation, login and logout work.
        void Update(uint32 diff);

        // Called once the world loop has stopped. Finishes the logins in flight (bounded by the login timeout), then logs
        // every bot out with `reason`. Update() does not run after the loop, so LogoutAll alone would leave logins hanging.
        void Shutdown(char const* reason);

        // Whether a new bot may be created now. When it may not, reason says why: the run's stop time has passed, the bot
        // limit is reached, the world update is running behind, or the character database queue is too long. The reason is
        // logged when it changes.
        bool AcceptSpawn(std::string& reason);

        // Creates a bot character with this race and class (Player::Create decides whether the combination is valid).
        // level is the character's level (1 is the race's starting level). Returns its guid, or 0 with the reason in error. The character is saved in the background; login is
        // requested with RequestLogin once it exists.
        uint64 CreateBot(uint8 race, uint8 classId, uint8 level, std::string& error);

        // Queues the bot for login. Returns false with the reason if the guid is not a bot. Already online or queued is success.
        bool RequestLogin(uint64 guid, std::string& error);

        // Saves and logs out the bot. Returns false if the guid is not a bot. A bot still logging in logs out as soon as it is online.
        bool RequestLogout(uint64 guid, char const* reason);

        // Logs out every bot (worldserver shutdown).
        void LogoutAll(char const* reason);

        bool Exists(uint64 guid) const;
        bool GetBot(uint64 guid, BotInfo& out) const;
        std::vector<BotInfo> ListBots() const;
        Player* GetOnlinePlayer(uint64 guid) const;
        uint32 OnlineCount() const;

    private:
        BotMgr() = default;

        void StartLogin(BotInfo& bot);
        void ProcessLogins(uint32 diff);
        void CheckStopTime();
        void PollLogins(); // advances the logins in flight: finishes or fails them
        void PollCreates(); // runs the save callbacks of characters that finished committing
        void FinishCreate(uint64 guid, bool success);
        void FinishLogin(BotInfo& bot);
        void SpreadBot(Player& bot); // puts a bot that is in the world onto its spread point around its race's start
        void FailLogin(BotInfo& bot, char const* reason);
        void LogoutBot(BotInfo& bot, char const* reason);

        std::unordered_map<uint64, BotInfo> _bots;
        std::vector<std::pair<uint32, std::string>> _freeAccounts; // accounts without a character, reused for the next bot
        uint32 _nextAccountNumber = 1;
        std::deque<uint64> _loginQueue;
        std::vector<uint64> _loggingIn;
        std::vector<std::pair<uint64, TransactionCallback>> _creating; // character commits in flight

        uint32 _uptimeMs = 0;
        uint32 _loginMaxPerTick = 5;
        // Spread around each race's start (Bot.Spread.*): spacing between bots, and the largest distance from the start.
        float _spreadSpacing = 3.0f;
        float _spreadMaxRadius = 100.0f;
        uint32 _loginTimeoutMs = 60000;
        uint32 _reloginDelayMs = 2000; // lets the previous logout save reach the database before the next load
        bool _registryLoaded = false;

        // Login rate: a token bucket, one login per token, refilled at Bot.Login.MaxPerSecond.
        uint32 _loginPerSecond = 10;
        double _loginTokens = 0.0;

        // Spawn limits (Bot.Limit.*) and the run's stop time (Bot.Run.StopTime).
        uint32 _limitMaxBots = 1500;
        uint32 _limitMaxWorldDiffMs = 250;
        uint32 _limitMaxDbQueue = 5000;
        uint32 _lastWorldDiffMs = 0;     // the interval of the last world update: it grows when the world falls behind
        bool _stopConfigured = false;
        uint32 _stopAtMs = 0;            // uptime at which the run stops
        bool _stopRequested = false;
        std::string _lastRefusal;        // the last reason a spawn was refused, so each new reason is logged once
};

#define sBotMgr BotMgr::instance()

#endif // TRINITY_BOT_MGR_H
