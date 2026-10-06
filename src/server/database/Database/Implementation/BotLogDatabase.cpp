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

#include "BotLogDatabase.h"
#include "MySQLPreparedStatement.h"
#include "QueryResult.h"
#include <memory>

std::atomic<bool> BotLogHasSessionSeq{false};
std::atomic<bool> BotLogHasHotTable{false};

void BotLogDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_BOTLOGDATABASE_STATEMENTS);

    PrepareStatement(BOTLOG_REP_BOT,
        "REPLACE INTO bot (guid, name, class_id, race_id, faction) VALUES (?, ?, ?, ?, ?)",
        CONNECTION_BOTH);

    // Schema probe: bot_event.session_seq exists only after the ALTER in forever-botlog-setup.sql was applied. Without it the
    // old INSERT is used, so a database that was not upgraded keeps logging (without session ids).
    bool hasSession = false;
    {
        std::unique_ptr<ResultSet> r(Query("SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'bot_event' AND COLUMN_NAME = 'session_seq'"));
        hasSession = r && r->GetRowCount() > 0;
    }
    BotLogHasSessionSeq.store(hasSession, std::memory_order_relaxed);

    bool hasHot = false;
    {
        std::unique_ptr<ResultSet> r(Query("SELECT 1 FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'bot_event_hot'"));
        hasHot = r && r->GetRowCount() > 0 && hasSession;
    }
    BotLogHasHotTable.store(hasHot, std::memory_order_relaxed);

    // Parameters: 1 ts (unix seconds, fractional), 2 bot_guid, 3 event_type, 4 severity, 5 reason, 6 summary,
    // 7 level, 8 map_id, 9 zone_id, 10-12 x/y/z, 13 quest_id, 14 target_entry, 15-17 details (same value 3x:
    // stored as JSON if valid, otherwise as a JSON string, so one bad payload cannot fail a whole batch),
    // 18 session_seq (only when the column exists, see BotLogHasSessionSeq).
    std::string const eventSql = hasSession ?
        "INSERT INTO bot_event (ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, "
        "pos_x, pos_y, pos_z, quest_id, target_entry, details, session_seq) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, IF(JSON_VALID(?), ?, JSON_QUOTE(?)), ?)" :
        "INSERT INTO bot_event (ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, "
        "pos_x, pos_y, pos_z, quest_id, target_entry, details) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, IF(JSON_VALID(?), ?, JSON_QUOTE(?)))";
    PrepareStatement(BOTLOG_INS_EVENT, eventSql, CONNECTION_BOTH);

    // Same parameters as BOTLOG_INS_EVENT (with session_seq), hot table (decision, state_change and trace rows, short retention).
    // Without the table the statement targets bot_event like BOTLOG_INS_EVENT (a prepare failure would fail the pool open) and is never used.
    PrepareStatement(BOTLOG_INS_EVENT_HOT, hasHot ?
        std::string("INSERT INTO bot_event_hot (ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, "
        "pos_x, pos_y, pos_z, quest_id, target_entry, details, session_seq) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, IF(JSON_VALID(?), ?, JSON_QUOTE(?)), ?)") : eventSql,
        CONNECTION_BOTH);

    // Parameters: 1 ts (unix seconds, fractional), 2 bot_guid, 3 map_id, 4 zone_id, 5-7 x/y/z, 8 flags (bit0 moving, bit1 combat, bit2 dead)
    PrepareStatement(BOTLOG_INS_POS,
        "INSERT INTO bot_pos (ts, bot_guid, map_id, zone_id, x, y, z, flags) VALUES (FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?)",
        CONNECTION_BOTH);
}

BotLogDatabaseConnection::BotLogDatabaseConnection(MySQLConnectionInfo& connInfo, ConnectionFlags connectionFlags)
    : MySQLConnection(connInfo, connectionFlags)
{
}

BotLogDatabaseConnection::~BotLogDatabaseConnection() = default;
