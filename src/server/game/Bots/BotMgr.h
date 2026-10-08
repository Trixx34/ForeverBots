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

#ifndef TRINITY_BOT_MGR_H
#define TRINITY_BOT_MGR_H

#include "Define.h"
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "BotLogCategory.h"
#include "DatabaseEnvFwd.h"
#include "Transaction.h"
#include <unordered_map>

class Player;
class WorldSession;

// Severity levels for BotEvent::Severity (stored as bot_event.severity).
enum BotLogSeverity : uint8
{
    BOTLOG_TRACE = 0,
    BOTLOG_INFO  = 1,
    BOTLOG_WARN  = 2,
    BOTLOG_ERROR = 3
};

// One row of the bot activity log (table bot_event in the forever_botlog database).
// Fixed fields are for filtering/grouping; Details is free-form JSON (alternatives considered,
// killer and damage log, quest step...). Invalid JSON is stored as a JSON string, never dropped.
struct BotEvent
{
    uint64 BotGuid = 0;
    std::string Type;       // decision, state_change, death, quest_blocked, quest_done, stuck, path_fail, combat, error ...
    uint8 Severity = BOTLOG_INFO;
    std::string Reason;     // short machine-readable code for grouping, e.g. NO_PATH, QUEST_PREREQ
    std::string Summary;    // human-readable one-liner
    std::optional<uint8> Level;
    std::optional<uint16> MapId;
    std::optional<uint16> ZoneId;
    std::optional<float> X, Y, Z;
    std::optional<uint32> QuestId;
    std::optional<uint32> TargetEntry;
    std::string Details;    // JSON text, may be empty
    double Timestamp = 0.0; // unix seconds; filled in by LogEvent when 0
    uint64 SessionSeq = 0;  // login session id of the bot (bot_event.session_seq); filled in by LogEvent when 0
};

// One position sample (table bot_pos), used by the sim console map. Flags: bit 0 moving, bit 1 in combat, bit 2 dead.
struct BotPosSample
{
    uint64 BotGuid = 0;
    uint16 MapId = 0;
    uint16 ZoneId = 0;
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    uint8 Flags = 0;
    double Timestamp = 0.0;
};

// Lifecycle of one bot character inside BotMgr.
enum BotRunState : uint8
{
    BOT_OFFLINE    = 0,
    BOT_QUEUED     = 1, // waiting for a login slot
    BOT_LOGGING_IN = 2, // session created, character load in flight
    BOT_ONLINE     = 3,
    BOT_CREATING   = 4  // character just created, its rows are being committed
};

// One bot character (a normal character on a reserved BOTnnnn game account, one account per bot).
struct BotInfo
{
    uint64 Guid = 0;
    uint32 AccountId = 0;
    std::string AccountName;              // BOTnnnn
    std::string Name;
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Gender = 0;
    uint8 Level = 1;
    bool Horde = false;

    BotRunState State = BOT_OFFLINE;
    WorldSession* Session = nullptr;      // owned by BotMgr, never in World's session list
    bool DespawnRequested = false;        // despawn arrived while creating or logging in
    std::optional<uint8> PendingLevel;    // applied right after login
    uint32 NotBeforeMs = 0;               // BotMgr uptime before which no login may start (previous save must land first)
    uint32 LoginStartedMs = 0;
    uint64 SessionSeq = 0;                // id of the current login session, new at every BOT_LOGIN (0 = never logged in)
    bool JustCreated = false;
    bool Alt = false;                     // a player's alt (real account) logged in as a bot by BotAlts (step A3); never touched by spawn/despawn all

    // last known place (refreshed from the Player while online and when the bot logs out)
    uint16 MapId = 0;
    uint16 ZoneId = 0;
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

struct BotSpawnResult
{
    uint32 Reused = 0;
    uint32 Created = 0;
    uint32 Failed = 0;
    std::string Error;                    // set when nothing could be done (bad arguments, no valid race...)
    std::vector<std::string> Names;       // bots now queued or online because of this call
};

// The player-bot subsystem (see docs/playerbots/implementation-plan.md): bot characters on reserved game accounts,
// socket-less sessions owned here, trimmed login/logout, plus the buffered bot activity logger.
// All bot state is touched from the world thread only (console/GM commands, Update); only LogEvent is thread-safe.
class TC_GAME_API BotMgr
{
public:
    static BotMgr* instance();

    BotMgr(BotMgr const&) = delete;
    BotMgr(BotMgr&&) = delete;
    BotMgr& operator=(BotMgr const&) = delete;
    BotMgr& operator=(BotMgr&&) = delete;

    // Called once per world update tick (see World::Update). Counts ticks and flushes the log buffer.
    void Update(uint32 diff);

    // Thread-safe. Queues a task for the world thread (run at the start of the next Update). Map-thread bot code uses it for work that
    // touches global state or session handlers that the core runs on the world thread (auction house, mail).
    void PostWorldTask(std::function<void()> task);

    // Status line: ticks, bots known/online, log state.
    std::string GetStatus() const;

    // Bots currently online (in world).
    uint32 GetBotCount() const { return _onlineCount; }

    // --- Bot characters and sessions (world thread only) ----------------------------------------------
    // Brings up to `count` bots online: reuses existing offline bot characters matching the filters first, creates
    // new ones (account BOTnnnn + character) for the rest. classId 0 = any class, faction -1 = any, 0 = alliance,
    // 1 = horde. Logins are spread over several ticks (Bot.Login.MaxPerTick).
    BotSpawnResult SpawnBots(uint32 count, uint8 classId, int8 faction, std::optional<uint8> level);

    // Bots of the spawn/despawn pool that are queued, logging in, being created or online (alts are not part of the pool).
    uint32 PoolBotCount() const;

    // Logs out up to `count` online pool bots that nobody is playing with: not in combat, not in a group with a real player. Returns
    // how many were logged out. Used by the dynamic population (BotPopulation.cpp).
    uint32 TrimPoolBots(uint32 count);

    // Logs bots out and saves them. name empty = all. Returns how many were (or will be) logged out.
    uint32 DespawnBots(std::string const& name);

    // Immediately saves and logs out every bot (worldserver shutdown).
    void LogoutAll(char const* reason = "LOGOUT_SHUTDOWN");

    // --- Alts (step A3, policy in BotAlts.cpp; world thread only) ----------------------------------------
    // Queues a character of a real account for login as a bot (same login path as other bots). Fails when it is already a bot.
    bool StartAlt(uint64 guid, uint32 accountId, std::string const& accountName, std::string const& name, uint8 race, uint8 classId, uint8 gender, uint8 level, std::string& error);
    // Logs one alt out (saves) or drops it from the queue. Returns false when it is not an active alt.
    bool StopAlt(uint64 guid);
    // True while the character is queued, logging in or online as an alt bot (also blocks the normal player login).
    bool IsActiveAlt(uint64 guid) const;
    uint32 CountActiveAlts(uint32 accountId) const;
    // Names of the active alts of an account.
    std::vector<std::string> GetActiveAltNames(uint32 accountId) const;

    // Online bot players (world thread only; names match case-insensitively, empty or "all" = every online bot).
    std::vector<Player*> GetOnlineBotPlayers(std::string const& name = std::string());

    // Snapshot for `.bot list` (positions of online bots are read from the Player).
    std::vector<BotInfo> ListBots();

    // Counts per faction, class and race over all known bot characters, one text line each (`.bot stats`).
    std::vector<std::string> GetStats();

    static uint8 ParseClass(std::string const& text);          // 0 when unknown
    static int8 ParseFaction(std::string const& text);         // -1 any/unknown, 0 alliance, 1 horde
    static char const* ClassName(uint8 classId);
    static char const* StateName(BotRunState state);

    // --- Bot activity log -------------------------------------------------------------------------
    // Called by worldserver after the optional BotLog database was opened (BotLogDatabaseInfo set).
    void SetLogDatabaseAvailable(bool available);
    bool IsLogDatabaseAvailable() const { return _logAvailable.load(std::memory_order_relaxed); }

    // Thread-safe (map update threads may call it). No-op when the log database is not configured
    // or the event is below BotLog.MinSeverity. Rows are written asynchronously in batches.
    void LogEvent(BotEvent&& event);

    // Thread-safe, same contract as LogEvent (buffered, written by the world thread). No-op when logging is off.
    void LogPosition(BotPosSample&& sample);

    // Bot.Log.PosIntervalSec in milliseconds (0 = position telemetry off, also 0 while the log database is not available).
    uint32 GetPosIntervalMs() const { return _posIntervalMs.load(std::memory_order_relaxed); }

    // Flushes the suppressed-repeat counters of a bot as one LOG_SUPPRESSED row (see Bot.Log.RepeatCap). Thread-safe, called by
    // LogoutBot; other code never needs it (LogEvent flushes on the next different event).
    void FlushSuppressed(uint64 botGuid);
    void EraseLogState(uint64 botGuid); // drops the per-bot repeat-cap state once the bot's last row of a session was written

    // Registers or refreshes a bot in the `bot` table (one row per bot, events carry only the guid).
    void LogBotRegistration(uint64 guid, std::string const& name, uint8 classId, uint8 raceId, bool horde);

    // Writes buffered events now. sync = true blocks until committed (use at shutdown).
    void FlushLog(bool sync = false);

    // Why logging is off (empty while on): shown by .bot status. Set by worldserver when the optional pool could not be opened.
    void SetLogOffReason(std::string reason) { _logOffReason = std::move(reason); }

    // Number of events buffered and waiting for the next flush (diagnostics).
    size_t GetBufferedLogEvents() const;

private:
    BotMgr() = default;

    void LoadRegistry();
    bool CreateBot(uint8 raceId, uint8 classId, BotInfo& out, std::string& error);
    void StartLogin(BotInfo& bot);
    void ProcessLogins();
    void ProcessBotTeleports(); // completes the teleports of bots (they have no client to acknowledge them)
    void FinishCreate(uint64 guid, bool success);
    void FinishLogin(BotInfo& bot);
    void FailLogin(BotInfo& bot, char const* reason);
    void LogoutBot(BotInfo& bot, char const* reason);
    void BeginLogSession(BotInfo& bot);                       // new session id + repeat counters reset (world thread)
    bool ApplyRepeatCap(BotEvent const& event, std::vector<BotEvent>& extra); // _logMutex held; true = drop the event
    void LogLifecycle(BotInfo const& bot, char const* type, char const* reason, char const* summary, uint8 severity, std::string details);
    static bool IsValidCombo(uint8 raceId, uint8 classId);
    static uint8 PickRace(uint8 classId, int8 faction);

    uint32 _ticks = 0;
    uint32 _uptimeMs = 0;

    // Bot.Log.ServerStatsSec: periodic server log line (BOT_TICK_STATS) with the server update diff and the BotMgr::Update cost, for A/B runs
    void LogTickStats(uint32 diff, uint64 updateUs);
    uint32 _statsIntervalMs = 60000;
    uint32 _statsSinceMs = 0;
    uint32 _statsTicks = 0;
    uint64 _statsDiffSum = 0, _statsMgrUsSum = 0;
    uint32 _statsDiffMax = 0, _statsMgrUsMax = 0;

    bool _registryLoaded = false;
    uint32 _nextAccountNumber = 1;
    uint32 _onlineCount = 0;
    uint32 _loginMaxPerTick = 5;
    std::map<uint64, BotInfo> _bots;      // by guid; node addresses stay valid
    std::vector<std::pair<uint32, std::string>> _freeAccounts; // BOTnnnn accounts without a character (left over from a failed creation)
    std::vector<std::pair<uint64, TransactionCallback>> _creating;
    std::deque<uint64> _loginQueue;
    std::vector<uint64> _loggingIn;

    std::atomic<bool> _logAvailable{false};
    uint8 _logMinSeverity = BOTLOG_INFO;
    BotLogCat::Config _logCategories;     // Bot.Log.Categories / Bot.Log.CategoryMinSeverity; written at startup only
    uint32 _logFlushIntervalMs = 1000;
    uint32 _logMaxBatch = 500;
    uint32 _logBufferMax = 200000;        // Bot.Log.BufferMax: events held while the database is slow; over it events are dropped and counted
    uint32 _posBufferMax = 100000;        // Bot.Log.PosBufferMax
    uint32 _flushRetries = 3;             // Bot.Log.FlushRetries: re-submissions of a failed batch before it is dropped
    bool _hotSplit = false;               // Bot.Log.HotSplit
    std::set<std::string> _hotTypes;      // Bot.Log.HotTypes (lower case)
    uint32 _posBaseMs = 5000, _posSlowMs = 15000, _posScaleBots = 500;
    std::string _logOffReason;
    // Dropped-row accounting (guarded by _logMutex): written as one log_dropped event when the database accepts rows again.
    uint64 _droppedEvents = 0, _droppedPos = 0, _droppedFailed = 0;
    double _dropWindowStart = 0.0;
    // Batches handed to the async pool, kept until the result is known so that a failed transaction can be re-submitted.
    struct InFlight
    {
        TransactionCallback Callback;
        std::vector<BotEvent> Events;
        std::vector<BotPosSample> Pos;
        uint32 Attempts = 1;
        InFlight(TransactionCallback&& cb) : Callback(std::move(cb)) { }
    };
    std::list<InFlight> _inFlight;        // world thread only
    std::mutex _worldTaskLock;
    std::vector<std::function<void()>> _worldTasks;
    // Reachability probe (Bot.Log.ProbeIntervalSec): a TCP connect to the bot log host on a helper thread, polled by the world thread.
    // The core DB layer aborts the process when a reconnect fails, so rows must not be submitted while the server is unreachable.
    std::string _probeHost, _probePort;
    uint32 _probeIntervalMs = 10000, _probeSinceMs = 0;
    bool _probeDown = false;              // logging was switched off by the probe (and is switched back on by it)
    std::future<bool> _probeFuture;
    void UpdateProbe(uint32 diff);
    void PollInFlight();
    void SubmitBatch(std::vector<BotEvent>&& events, std::vector<BotPosSample>&& pos, uint32 attempts, bool sync);
    void NoteDropped(uint64 events, uint64 pos, bool failed);
    bool IsHotEvent(BotEvent const& e) const;
    std::atomic<uint32> _logSinceFlushMs{0}; // also bumped by map threads to request an early flush
    mutable std::mutex _logMutex;
    std::vector<BotEvent> _logBuffer;
    std::atomic<uint32> _posIntervalMs{0};
    std::vector<BotPosSample> _posBuffer; // guarded by _logMutex

    // Per-bot log state, guarded by _logMutex: session id and the repeat cap bookkeeping.
    struct LogRepeat { std::string Type, Reason; uint32 QuestId = 0, Entry = 0; uint32 Suppressed = 0; double LastTs = 0.0; };
    struct BotLogState
    {
        uint64 Session = 0;
        std::unordered_map<std::string, uint32> Counts;            // events let through per key this session
        std::unordered_map<std::string, LogRepeat> Pending;        // suppressed since the last flush row
        uint32 SuppressedLogin = 0;                                // suppressed rows this session, reported as login_total
    };
    std::unordered_map<uint64, BotLogState> _botLogState;
    uint32 _repeatCap = 3;                                          // Bot.Log.RepeatCap, 0 = no cap
    uint64 _nextSession = 0;
};

#define sBotMgr BotMgr::instance()

#endif
