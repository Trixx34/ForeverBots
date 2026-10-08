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

#include "tc_catch2.h"

#include "BotWatchdogPlan.h"

using namespace BotWatchdog;

namespace
{
Config Cfg()
{
    Config c;
    c.StallSec = 100;
    c.HearthMul = 2;
    c.HomeMul = 4;
    c.MoveYards = 10.0f;
    c.MaxPerHour = 6;
    return c;
}

Snapshot Still()
{
    Snapshot s;
    s.MapId = 1;
    s.X = 100.0f;
    s.Y = 100.0f;
    s.Xp = 50;
    s.Money = 10;
    s.QuestKey = 7;
    return s;
}

constexpr uint32 SEC = 1000;
}

TEST_CASE("watchdog: a bot that keeps moving is never stalled", "[BotWatchdog]")
{
    Tracker t;
    Snapshot s = Still();
    t.Update(0, s, Cfg());
    for (uint32 i = 1; i <= 50; ++i)
    {
        s.X += 20.0f;
        CHECK(t.Update(i * 60 * SEC, s, Cfg()).Act == Step::None);
    }
}

TEST_CASE("watchdog: stages come in order and once each", "[BotWatchdog]")
{
    Tracker t;
    Snapshot s = Still();
    t.Update(0, s, Cfg());
    CHECK(t.Update(99 * SEC, s, Cfg()).Act == Step::None);
    CHECK(t.Update(100 * SEC, s, Cfg()).Act == Step::ClearGoal);
    CHECK(t.Update(105 * SEC, s, Cfg()).Act == Step::None);
    CHECK(t.Update(200 * SEC, s, Cfg()).Act == Step::Hearth);
    CHECK(t.Update(210 * SEC, s, Cfg()).Act == Step::None);
    Verdict const v = t.Update(400 * SEC, s, Cfg());
    CHECK(v.Act == Step::Home);
    CHECK(v.StalledSec == 400);
}

TEST_CASE("watchdog: a late first check hands out the strongest due stage only", "[BotWatchdog]")
{
    Tracker t;
    Snapshot const s = Still();
    t.Update(0, s, Cfg());
    CHECK(t.Update(500 * SEC, s, Cfg()).Act == Step::Home);
}

TEST_CASE("watchdog: xp, money, quest log, map change and busy states are progress", "[BotWatchdog]")
{
    for (int kind = 0; kind < 5; ++kind)
    {
        Tracker t;
        Snapshot s = Still();
        t.Update(0, s, Cfg());
        CHECK(t.Update(90 * SEC, s, Cfg()).Act == Step::None);
        switch (kind)
        {
            case 0: s.Xp += 1; break;
            case 1: s.Money += 1; break;
            case 2: s.QuestKey += 1; break;
            case 3: s.MapId = 0; break;
            default: s.Busy = true; break;
        }
        CHECK(t.Update(150 * SEC, s, Cfg()).Act == Step::None);
        s.Busy = false;
        // the stall clock restarted at 150 s
        CHECK(t.Update(240 * SEC, s, Cfg()).Act == Step::None);
        CHECK(t.Update(250 * SEC, s, Cfg()).Act == Step::ClearGoal);
    }
}

TEST_CASE("watchdog: a few yards of drift is not progress, a real move is", "[BotWatchdog]")
{
    Tracker t;
    Snapshot s = Still();
    t.Update(0, s, Cfg());
    s.X += 4.0f;
    CHECK(t.Update(100 * SEC, s, Cfg()).Act == Step::ClearGoal);
    s.X += 30.0f;
    CHECK(t.Update(110 * SEC, s, Cfg()).Act == Step::None);
    CHECK(t.Update(209 * SEC, s, Cfg()).Act == Step::None);
    CHECK(t.Update(210 * SEC, s, Cfg()).Act == Step::ClearGoal);
}

TEST_CASE("watchdog: the stall clock restarts after the home teleport", "[BotWatchdog]")
{
    Tracker t;
    Snapshot const s = Still();
    t.Update(0, s, Cfg());
    CHECK(t.Update(400 * SEC, s, Cfg()).Act == Step::Home);
    CHECK(t.Update(499 * SEC, s, Cfg()).Act == Step::None);
    CHECK(t.Update(500 * SEC, s, Cfg()).Act == Step::ClearGoal);
}

TEST_CASE("watchdog: the hourly cap turns strong recoveries into a report", "[BotWatchdog]")
{
    Config c = Cfg();
    c.MaxPerHour = 2;
    Tracker t;
    Snapshot const s = Still();
    uint32 now = 0;
    t.Update(now, s, c);
    uint32 homes = 0, exhausted = 0;
    for (int i = 0; i < 6; ++i)
    {
        now += 400 * SEC;
        Verdict const v = t.Update(now, s, c);
        homes += v.Act == Step::Home;
        exhausted += v.Exhausted;
    }
    CHECK(homes == 2);
    CHECK(exhausted >= 1);
    CHECK(t.RecoveriesInWindow() == 2);
}

TEST_CASE("watchdog: recoveries leave the window after an hour", "[BotWatchdog]")
{
    Config c = Cfg();
    c.MaxPerHour = 1;
    Tracker t;
    Snapshot const s = Still();
    t.Update(0, s, c);
    CHECK(t.Update(400 * SEC, s, c).Act == Step::Home);
    CHECK(t.Update(800 * SEC, s, c).Exhausted);
    CHECK(t.Update(4100 * SEC, s, c).Act == Step::Home);
}
