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

#include "BotWatchdogPlan.h"
#include <algorithm>
#include <cmath>

namespace BotWatchdog
{
Verdict Tracker::Update(uint32 nowMs, Snapshot const& s, Config const& cfg)
{
    Verdict v;
    auto restart = [&]()
    {
        _mark = s;
        _markMs = nowMs;
        _stage = 0;
    };
    if (!_started)
    {
        _started = true;
        restart();
        return v;
    }

    float const dx = s.X - _mark.X, dy = s.Y - _mark.Y;
    bool const moved = s.MapId != _mark.MapId || dx * dx + dy * dy >= cfg.MoveYards * cfg.MoveYards;
    if (s.Busy || moved || s.Xp != _mark.Xp || s.Money != _mark.Money || s.QuestKey != _mark.QuestKey)
    {
        restart();
        return v;
    }

    uint32 const stallMs = nowMs - _markMs;
    v.StalledSec = stallMs / 1000;
    uint64 const stall = std::max<uint32>(cfg.StallSec, 1);
    uint64 const t1 = stall * 1000;
    uint64 const t2 = t1 * std::max<uint32>(cfg.HearthMul, 1);
    uint64 const t3 = t1 * std::max<uint32>(cfg.HomeMul, std::max<uint32>(cfg.HearthMul, 1));

    Step next = Step::None;
    uint32 nextStage = _stage;
    if (_stage < 3 && stallMs >= t3)
    {
        next = Step::Home;
        nextStage = 3;
    }
    else if (_stage < 2 && stallMs >= t2)
    {
        next = Step::Hearth;
        nextStage = 2;
    }
    else if (_stage < 1 && stallMs >= t1)
    {
        next = Step::ClearGoal;
        nextStage = 1;
    }
    if (next == Step::None)
        return v;

    // hourly cap on the strong recoveries; the goal reset is free
    while (!_recoveries.empty() && nowMs - _recoveries.front() >= 3600u * 1000u)
        _recoveries.pop_front();
    if (next != Step::ClearGoal)
    {
        if (_recoveries.size() >= cfg.MaxPerHour)
        {
            v.Exhausted = true;
            _stage = nextStage;     // report once per stage, not on every check
            if (nextStage == 3)
                restart();
            return v;
        }
        _recoveries.push_back(nowMs);
    }
    v.Act = next;
    _stage = nextStage;
    if (nextStage == 3)
        restart();                  // a new stall is measured from the teleport
    return v;
}
}
