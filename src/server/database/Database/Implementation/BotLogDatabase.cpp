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

void BotLogDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_BOTLOGDATABASE_STATEMENTS);

    PrepareStatement(BOTLOG_REP_BOT,
        "REPLACE INTO bot (guid, name, class_id, race_id, faction) VALUES (?, ?, ?, ?, ?)",
        CONNECTION_ASYNC);

    // Parameters: 1 ts (unix seconds, fractional), 2 bot_guid, 3 event_type, 4 severity, 5 reason, 6 summary,
    // 7 level, 8 map_id, 9 zone_id, 10-12 x/y/z, 13 quest_id, 14 target_entry, 15-17 details (same value 3x:
    // stored as JSON if valid, otherwise as a JSON string, so one bad payload cannot fail a whole batch).
    PrepareStatement(BOTLOG_INS_EVENT,
        "INSERT INTO bot_event (ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, "
        "pos_x, pos_y, pos_z, quest_id, target_entry, details) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, IF(JSON_VALID(?), ?, JSON_QUOTE(?)))",
        CONNECTION_ASYNC);
}

BotLogDatabaseConnection::BotLogDatabaseConnection(MySQLConnectionInfo& connInfo, ConnectionFlags connectionFlags)
    : MySQLConnection(connInfo, connectionFlags)
{
}

BotLogDatabaseConnection::~BotLogDatabaseConnection() = default;
