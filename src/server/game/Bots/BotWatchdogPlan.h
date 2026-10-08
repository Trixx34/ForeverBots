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

#ifndef TRINITY_BOT_WATCHDOG_PLAN_H
#define TRINITY_BOT_WATCHDOG_PLAN_H

// Stall detection of the bot watchdog (Bot.AI.Watchdog.*, BotWatchdog.cpp). A bot makes progress when it moves, gains xp, money or
// levels, or changes its quest log; fighting, flying, being dead or in a group dungeon also count, because those have their own
// recovery. When none of that happens for StallSec the tracker answers with an escalating recovery. Pure over plain data so it is
// unit tested without a map or a Player. See docs/playerbots/feature-bot-watchdog-20261008.md.

#include "Define.h"
#include <deque>

namespace BotWatchdog
{
    struct Config
    {
        uint32 StallSec = 600;        // no progress for this long: stage 1 (clear the goal)
        uint32 HearthMul = 2;         // stage 2 (hearthstone) at StallSec * HearthMul
        uint32 HomeMul = 4;           // stage 3 (teleport to the home bind) at StallSec * HomeMul
        float MoveYards = 10.0f;      // moving farther than this from the last mark counts as progress
        uint32 MaxPerHour = 6;        // recoveries (stage 3 included) per bot and hour; beyond it only the report is written
    };

    // What the glue reads from the game, once per check.
    struct Snapshot
    {
        uint32 MapId = 0;
        float X = 0.0f, Y = 0.0f;
        uint64 Xp = 0;                // level and experience folded into one number (any change is progress)
        uint64 Money = 0;
        uint32 QuestKey = 0;          // hash of the quest log (ids and states) plus the number of rewarded quests
        bool Busy = false;            // dead, fighting, on a taxi, in a dungeon group: not a stall whatever else is unchanged
    };

    enum class Step : uint8
    {
        None,
        ClearGoal,    // stage 1: drop the movement goal and let the planners choose again
        Hearth,       // stage 2: use the hearthstone (the glue falls through to Home when there is none)
        Home          // stage 3: teleport to the home bind
    };

    struct Verdict
    {
        Step Act = Step::None;
        uint32 StalledSec = 0;        // time since the last progress
        bool Exhausted = false;       // a recovery was due but the hourly cap is used up (report only)
    };

    class TC_GAME_API Tracker
    {
    public:
        // `nowMs` is the AI clock. Answers at most one step per stage per stall: after stage 3 the stall clock restarts.
        Verdict Update(uint32 nowMs, Snapshot const& s, Config const& cfg);
        void Reset() { *this = Tracker(); }
        uint32 Stage() const { return _stage; }
        uint32 RecoveriesInWindow() const { return uint32(_recoveries.size()); }

    private:
        bool _started = false;
        Snapshot _mark;               // the state at the last progress
        uint32 _markMs = 0;
        uint32 _stage = 0;            // 0 none, 1..3 steps already handed out for the current stall
        std::deque<uint32> _recoveries;   // AI-clock times of recent recoveries
    };
}

#endif
