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

#include "BotDummyStats.h"
#include <algorithm>

namespace BotDummyStats
{
void Run::Begin(uint32 nowMs)
{
    *this = Run();
    _beginMs = _endMs = _firstMs = _lastMs = nowMs;
}

void Run::NoteHit(uint32 spellId, uint32 damage, uint32 nowMs)
{
    if (!_hits)
        _firstMs = nowMs;
    else
        _gapMs = std::max(_gapMs, nowMs - _lastMs);
    _lastMs = nowMs;
    ++_hits;
    _total += damage;
    for (SpellRow& r : _rows)
        if (r.SpellId == spellId)
        {
            ++r.Hits;
            r.Damage += damage;
            return;
        }
    _rows.push_back({ spellId, 1, damage });
}

void Run::End(uint32 nowMs)
{
    _endMs = nowMs;
}

double Run::Dps() const
{
    uint32 const ms = DurationMs();
    return ms ? double(_total) * 1000.0 / double(ms) : 0.0;
}

uint32 Run::LongestGapMs() const
{
    if (!_hits)
        return DurationMs();
    return std::max({ _gapMs, _firstMs - _beginMs, _endMs - _lastMs });
}

double Percent(uint64 part, uint64 whole)
{
    return whole ? 100.0 * double(part) / double(whole) : 0.0;
}
}
