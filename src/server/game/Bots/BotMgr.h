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
#include <string>

// Phase 0 scaffold for the player-bot subsystem (see docs/playerbots/implementation-plan.md).
// This does not yet create or control any bot characters; it only proves the subsystem
// is wired into the build, reachable from a GM command, and ticked from the world update loop.
class TC_GAME_API BotMgr
{
public:
    static BotMgr* instance();

    BotMgr(BotMgr const&) = delete;
    BotMgr(BotMgr&&) = delete;
    BotMgr& operator=(BotMgr const&) = delete;
    BotMgr& operator=(BotMgr&&) = delete;

    // Called once per world update tick (see World::Update). Phase 0: no-op, just counts ticks.
    void Update(uint32 diff);

    // Phase 0 smoke test: returns a status line proving the manager is alive and ticking.
    std::string GetStatus() const;

    uint32 GetBotCount() const { return 0; } // Phase 1 will back this with real bot sessions.

private:
    BotMgr() = default;

    uint32 _ticks = 0;
    uint32 _uptimeMs = 0;
};

#define sBotMgr BotMgr::instance()

#endif
