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

#ifndef TRINITY_BOTLOG_DATABASE_H
#define TRINITY_BOTLOG_DATABASE_H

#include "DatabaseWorkerPool.h"
#include "MySQLConnection.h"
#include "PreparedStatement.h"
#include "Transaction.h"
#include <atomic>

// Optional fifth database holding the bot activity log (schema: forever-botlog-setup.sql).
// It is deliberately NOT part of DatabaseEnv.h so that adding it does not recompile the whole core,
// and it is never auto-created or auto-updated (see DBUpdater<BotLogDatabaseConnection>::IsEnabled).
enum BotLogDatabaseStatements : uint32
{
    BOTLOG_REP_BOT,
    BOTLOG_INS_EVENT,
    BOTLOG_INS_EVENT_HOT,   // same columns as BOTLOG_INS_EVENT, table bot_event_hot (only used when BotLogHasHotTable)
    BOTLOG_INS_POS,
    BOTLOG_INS_ROLLUP,      // upsert into bot_event_rollup (only used when BotLogHasRollupTable)

    MAX_BOTLOGDATABASE_STATEMENTS
};

class TC_DATABASE_API BotLogDatabaseConnection : public MySQLConnection
{
public:
    typedef BotLogDatabaseStatements Statements;

    BotLogDatabaseConnection(MySQLConnectionInfo& connInfo, ConnectionFlags connectionFlags);
    ~BotLogDatabaseConnection();

    //- Loads database type specific prepared statements
    void DoPrepareStatements() override;
};

using BotLogDatabasePreparedStatement = PreparedStatement<BotLogDatabaseConnection>;
using BotLogDatabaseTransaction = SQLTransaction<BotLogDatabaseConnection>;

/// True when bot_event has the session_seq column (probed when the statements are prepared); BOTLOG_INS_EVENT then takes it as the 18th parameter.
TC_DATABASE_API extern std::atomic<bool> BotLogHasSessionSeq;

/// True when the bot_event_hot table exists (probed when the statements are prepared): BOTLOG_INS_EVENT_HOT writes to it. Otherwise that
/// statement is prepared against bot_event and the writer never routes to it.
TC_DATABASE_API extern std::atomic<bool> BotLogHasHotTable;

/// True when the bot_event_rollup table exists (probed when the statements are prepared): Bot.Log.Summary.* needs it.
TC_DATABASE_API extern std::atomic<bool> BotLogHasRollupTable;

/// Accessor to the bot log database (only opened when BotLogDatabaseInfo is set in worldserver.conf)
TC_DATABASE_API extern DatabaseWorkerPool<BotLogDatabaseConnection> BotLogDatabase;

#endif
