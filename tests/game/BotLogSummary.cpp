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

#include "BotLogSummary.h"

using namespace BotLogSummary;

namespace
{
Policy Pol()
{
    Policy p;
    p.Enabled = true;
    p.WindowSec = 60;
    p.KeepSeverity = 2;
    p.Types = ParseList("cast, AURA", Lower);
    p.ReasonPrefixes = ParseList("goto_,quest_walk_", Upper);
    p.KeepPrefixes = ParseList("DUNGEON_,TRAVEL_,DUMMY_", Upper);
    p.KeyBySummary = ParseList("cast,aura", Lower);
    return p;
}

Input Ev(char const* type, char const* reason, double ts, char const* summary = "")
{
    Input i;
    i.BotGuid = 7;
    i.Type = type;
    i.Reason = reason;
    i.Summary = summary;
    i.Timestamp = ts;
    return i;
}
}

TEST_CASE("Lists are trimmed and normalised", "[BotLogSummary]")
{
    auto v = ParseList(" a , ,B,c ", Lower);
    REQUIRE(v.size() == 3);
    CHECK(v[1] == "b");
    CHECK(ParseList("", Lower).empty());
}

TEST_CASE("Only configured high-volume events are summarized", "[BotLogSummary]")
{
    Policy p = Pol();
    CHECK(ShouldSummarize(p, "cast", "CAST_OK", 1));
    CHECK(ShouldSummarize(p, "decision", "GOTO_START", 1));
    CHECK_FALSE(ShouldSummarize(p, "decision", "QUEST_TURNIN", 1));
    CHECK_FALSE(ShouldSummarize(p, "death", "DIED", 2));
}

TEST_CASE("Keep prefixes and severity beat the summarize lists", "[BotLogSummary]")
{
    Policy p = Pol();
    CHECK_FALSE(ShouldSummarize(p, "decision", "DUNGEON_PACK_START", 1));
    CHECK_FALSE(ShouldSummarize(p, "decision", "dummy_summary", 1));
    CHECK_FALSE(ShouldSummarize(p, "cast", "CAST_FAILED", 2)); // warn and above stays detailed
    p.KeepSeverity = 4;
    CHECK(ShouldSummarize(p, "cast", "CAST_FAILED", 3));
    p.Enabled = false;
    CHECK_FALSE(ShouldSummarize(p, "cast", "CAST_OK", 1));
}

TEST_CASE("Same key in one window is counted, different spells stay apart", "[BotLogSummary]")
{
    Policy p = Pol();
    Aggregator a;
    CHECK(a.Add(p, Ev("cast", "CAST_OK", 100.0, "cast Fireball"), 100));
    CHECK(a.Add(p, Ev("cast", "CAST_OK", 110.5, "cast Fireball"), 100));
    CHECK(a.Add(p, Ev("cast", "CAST_OK", 111.0, "cast Frostbolt"), 100));
    CHECK(a.Add(p, Ev("decision", "GOTO_START", 111.0, "walking"), 100));
    CHECK(a.Add(p, Ev("decision", "GOTO_START", 111.0, "different text"), 100)); // summary is not part of the key here
    CHECK(a.Size() == 3);

    auto rows = a.Drain(1000.0, 60, false);
    REQUIRE(rows.size() == 3);
    uint32 total = 0;
    for (Row const& r : rows)
    {
        total += r.Count;
        if (r.K.SummaryKey == "cast Fireball")
        {
            CHECK(r.Count == 2);
            CHECK(r.FirstTs == 100.0);
            CHECK(r.LastTs == 110.5);
            CHECK(r.K.Window == 60);
        }
    }
    CHECK(total == 5);
    CHECK(a.Size() == 0);
}

TEST_CASE("Windows are closed by time, forced drain takes everything", "[BotLogSummary]")
{
    Policy p = Pol();
    Aggregator a;
    a.Add(p, Ev("cast", "CAST_OK", 10.0, "x"), 100);   // window 0
    a.Add(p, Ev("cast", "CAST_OK", 70.0, "x"), 100);   // window 60
    CHECK(a.Drain(59.0, 60, false).empty());
    CHECK(a.Drain(60.0, 60, false).size() == 1);       // window 0 ended
    CHECK(a.Size() == 1);
    CHECK(a.Drain(61.0, 60, true).size() == 1);
}

TEST_CASE("A full table refuses new keys but keeps counting existing ones", "[BotLogSummary]")
{
    Policy p = Pol();
    Aggregator a;
    CHECK(a.Add(p, Ev("cast", "CAST_OK", 1.0, "a"), 1));
    CHECK_FALSE(a.Add(p, Ev("cast", "CAST_OK", 1.0, "b"), 1));
    CHECK(a.Add(p, Ev("cast", "CAST_OK", 2.0, "a"), 1));
    CHECK(a.Drain(100.0, 60, true)[0].Count == 2);
}
