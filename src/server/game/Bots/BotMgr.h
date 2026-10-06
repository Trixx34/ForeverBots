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
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "DatabaseEnvFwd.h"
#include "Transaction.h"

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
    bool JustCreated = false;

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

    // Status line: ticks, bots known/online, log state.
    std::string GetStatus() const;

    // Bots currently online (in world).
    uint32 GetBotCount() const { return _onlineCount; }

    // --- Bot characters and sessions (world thread only) ----------------------------------------------
    // Brings up to `count` bots online: reuses existing offline bot characters matching the filters first, creates
    // new ones (account BOTnnnn + character) for the rest. classId 0 = any class, faction -1 = any, 0 = alliance,
    // 1 = horde. Logins are spread over several ticks (Bot.Login.MaxPerTick).
    BotSpawnResult SpawnBots(uint32 count, uint8 classId, int8 faction, std::optional<uint8> level);

    // Logs bots out and saves them. name empty = all. Returns how many were (or will be) logged out.
    uint32 DespawnBots(std::string const& name);

    // Immediately saves and logs out every bot (worldserver shutdown).
    void LogoutAll(char const* reason = "LOGOUT_SHUTDOWN");

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

    // Registers or refreshes a bot in the `bot` table (one row per bot, events carry only the guid).
    void LogBotRegistration(uint64 guid, std::string const& name, uint8 classId, uint8 raceId, bool horde);

    // Writes buffered events now. sync = true blocks until committed (use at shutdown).
    void FlushLog(bool sync = false);

    // Number of events buffered and waiting for the next flush (diagnostics).
    size_t GetBufferedLogEvents() const;

private:
    BotMgr() = default;

    void LoadRegistry();
    bool CreateBot(uint8 raceId, uint8 classId, BotInfo& out, std::string& error);
    void StartLogin(BotInfo& bot);
    void ProcessLogins();
    void FinishCreate(uint64 guid, bool success);
    void FinishLogin(BotInfo& bot);
    void FailLogin(BotInfo& bot, char const* reason);
    void LogoutBot(BotInfo& bot, char const* reason);
    void LogLifecycle(BotInfo const& bot, char const* type, char const* reason, char const* summary, uint8 severity, std::string details);
    static bool IsValidCombo(uint8 raceId, uint8 classId);
    static uint8 PickRace(uint8 classId, int8 faction);

    uint32 _ticks = 0;
    uint32 _uptimeMs = 0;

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
    uint32 _logFlushIntervalMs = 1000;
    uint32 _logMaxBatch = 500;
    std::atomic<uint32> _logSinceFlushMs{0}; // also bumped by map threads to request an early flush
    mutable std::mutex _logMutex;
    std::vector<BotEvent> _logBuffer;
};

#define sBotMgr BotMgr::instance()

#endif
