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

#ifndef TRINITY_BOT_DUMMY_STATS_H
#define TRINITY_BOT_DUMMY_STATS_H

// Numbers of one training dummy run (Bot.AI.Dummy.*, chat verb "dummy"): damage per spell, time to the first hit, longest gap
// between hits. Pure data and arithmetic so it is unit tested without a map or a Player. BotDummy.cpp feeds it from the dummy's
// DamageTaken hook and writes the DUMMY_SUMMARY event. See docs/playerbots/feature-bot-dummy-20261008.md.

#include "Define.h"
#include <vector>

namespace BotDummyStats
{
    struct SpellRow
    {
        uint32 SpellId = 0;     // 0 = melee swings (white damage)
        uint32 Hits = 0;
        uint64 Damage = 0;
    };

    class Run
    {
    public:
        void Begin(uint32 nowMs);
        // One damage event on the dummy. Zero damage still counts as a hit (an absorbed or resisted-to-nothing cast is a cast).
        void NoteHit(uint32 spellId, uint32 damage, uint32 nowMs);
        void End(uint32 nowMs);

        uint32 DurationMs() const { return _endMs - _beginMs; }
        uint64 TotalDamage() const { return _total; }
        uint32 TotalHits() const { return _hits; }
        // Damage per second over the whole run (idle time included). 0 for an empty run.
        double Dps() const;
        // -1 when nothing hit the dummy.
        int64 FirstHitMs() const { return _hits ? int64(_firstMs - _beginMs) : -1; }
        // Longest time without a hit, counting from the begin of the run to the first hit and from the last hit to the end.
        uint32 LongestGapMs() const;
        std::vector<SpellRow> const& Rows() const { return _rows; }   // in order of first appearance

    private:
        uint32 _beginMs = 0, _endMs = 0, _firstMs = 0, _lastMs = 0, _gapMs = 0, _hits = 0;
        uint64 _total = 0;
        std::vector<SpellRow> _rows;
    };


    TC_GAME_API double Percent(uint64 part, uint64 whole);
}

#endif
