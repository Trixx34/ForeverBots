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
#include "BotLogDatabase.h"
#include "Config.h"
#include "Log.h"
#include <algorithm>
#include <chrono>
#include <sstream>

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

void BotMgr::Update(uint32 diff)
{
    ++_ticks;
    _uptimeMs += diff;

    if (!IsLogDatabaseAvailable())
        return;

    if ((_logSinceFlushMs += diff) >= _logFlushIntervalMs)
        FlushLog();
}

std::string BotMgr::GetStatus() const
{
    std::ostringstream out;
    out << "BotMgr alive: " << _ticks << " update ticks, " << (_uptimeMs / 1000) << "s uptime, "
        << GetBotCount() << " bots tracked (Phase 0 - no bots implemented yet), bot log: ";
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
