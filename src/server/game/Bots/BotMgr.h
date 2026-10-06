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
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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

// Phase 0 scaffold for the player-bot subsystem (see docs/playerbots/implementation-plan.md),
// plus the bot activity logger. This does not yet create or control any bot characters.
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

    // Phase 0 smoke test: returns a status line proving the manager is alive and ticking.
    std::string GetStatus() const;

    uint32 GetBotCount() const { return 0; } // Phase 1 will back this with real bot sessions.

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

    uint32 _ticks = 0;
    uint32 _uptimeMs = 0;

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
