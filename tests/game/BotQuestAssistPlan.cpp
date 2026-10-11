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

#include "BotQuestAssistPlan.h"
#include <algorithm>

using namespace BotQuestAssist;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    return c;
}

Candidate Bot(uint64 guid, uint32 level, float x, uint32 map = 0)
{
    Candidate c;
    c.Guid = guid;
    c.Level = level;
    c.X = x;
    c.MapId = map;
    return c;
}

PlayerFacts Player10()
{
    PlayerFacts p;
    p.Level = 10;
    return p;
}
}

TEST_CASE("BotQuestAssist: quest kind", "[BotQuestAssist]")
{
    QuestFacts q;
    CHECK(Classify(q) == Kind::None);

    q.SuggestedPlayers = 1;
    CHECK(Classify(q) == Kind::None);
    q.SuggestedPlayers = 3;
    CHECK(Classify(q) == Kind::Group);

    q = QuestFacts();
    q.GroupInfo = true;
    CHECK(Classify(q) == Kind::Group);

    q = QuestFacts();
    q.Escort = true;
    CHECK(Classify(q) == Kind::Escort);

    q.SuggestedPlayers = 3;
    CHECK(Classify(q) == Kind::Escort);   // escort wins

    q.Excluded = true;
    CHECK(Classify(q) == Kind::None);     // dungeon, raid, PvP
    CHECK(std::string(KindName(Kind::Escort)) == "escort");
}

TEST_CASE("BotQuestAssist: helpers wanted", "[BotQuestAssist]")
{
    Config cfg = On();
    cfg.MaxHelpers = 2;

    SECTION("off wants nothing")
    {
        cfg.Enabled = false;
        CHECK(HelpersWanted(Kind::Group, 3, 1, cfg) == 0);
    }
    SECTION("a plain quest wants nothing")
    {
        CHECK(HelpersWanted(Kind::None, 3, 1, cfg) == 0);
    }
    SECTION("a quest for three needs two companions")
    {
        CHECK(HelpersWanted(Kind::Group, 3, 1, cfg) == 2);
    }
    SECTION("a quest for two needs one")
    {
        CHECK(HelpersWanted(Kind::Group, 2, 1, cfg) == 1);
    }
    SECTION("a quest for five is capped by MaxHelpers")
    {
        CHECK(HelpersWanted(Kind::Group, 5, 1, cfg) == 2);
    }
    SECTION("people already in the party count")
    {
        CHECK(HelpersWanted(Kind::Group, 3, 2, cfg) == 1);
        CHECK(HelpersWanted(Kind::Group, 3, 3, cfg) == 0);
    }
    SECTION("a group tag without a number wants one")
    {
        CHECK(HelpersWanted(Kind::Group, 0, 1, cfg) == 1);
    }
    SECTION("an escort wants MaxHelpers")
    {
        CHECK(HelpersWanted(Kind::Escort, 0, 1, cfg) == 2);
        CHECK(HelpersWanted(Kind::Escort, 0, 2, cfg) == 1);
    }
    SECTION("a four-companion cap still leaves the party size")
    {
        cfg.MaxHelpers = 4;
        CHECK(HelpersWanted(Kind::Escort, 0, 4, cfg) == 1);
        CHECK(HelpersWanted(Kind::Escort, 0, 5, cfg) == 0);
    }
    SECTION("a full party wants nothing")
    {
        CHECK(HelpersWanted(Kind::Escort, 0, 5, cfg) == 0);
    }
}

TEST_CASE("BotQuestAssist: picking helpers", "[BotQuestAssist]")
{
    Config cfg = On();
    PlayerFacts p = Player10();

    SECTION("closest level first, then closest, then guid")
    {
        std::vector<Candidate> c = { Bot(5, 12, 10), Bot(3, 10, 30), Bot(4, 10, 20), Bot(2, 10, 20) };
        std::vector<uint64> out = PickHelpers(p, c, 3, cfg);
        REQUIRE(out.size() == 3);
        CHECK(out[0] == 2);
        CHECK(out[1] == 4);
        CHECK(out[2] == 3);
    }
    SECTION("other maps are skipped")
    {
        CHECK(PickHelpers(p, { Bot(1, 10, 5, 1) }, 1, cfg).empty());
    }
    SECTION("too far is skipped")
    {
        CHECK(PickHelpers(p, { Bot(1, 10, cfg.RecruitRadius + 1) }, 1, cfg).empty());
        CHECK(PickHelpers(p, { Bot(1, 10, cfg.RecruitRadius) }, 1, cfg).size() == 1);
    }
    SECTION("level gap")
    {
        CHECK(PickHelpers(p, { Bot(1, 15, 5) }, 1, cfg).empty());
        CHECK(PickHelpers(p, { Bot(1, 14, 5) }, 1, cfg).size() == 1);
        CHECK(PickHelpers(p, { Bot(1, 6, 5) }, 1, cfg).size() == 1);
        CHECK(PickHelpers(p, { Bot(1, 5, 5) }, 1, cfg).empty());
    }
    SECTION("minimum helper level")
    {
        PlayerFacts low;
        low.Level = 6;
        cfg.MinHelperLevel = 5;
        CHECK(PickHelpers(low, { Bot(1, 4, 5) }, 1, cfg).empty());
        CHECK(PickHelpers(low, { Bot(1, 5, 5) }, 1, cfg).size() == 1);
    }
    SECTION("nothing wanted, nothing picked")
    {
        CHECK(PickHelpers(p, { Bot(1, 10, 5) }, 0, cfg).empty());
    }
    SECTION("a bot listed twice joins once")
    {
        CHECK(PickHelpers(p, { Bot(1, 10, 5), Bot(1, 10, 5) }, 2, cfg).size() == 1);
    }
}

TEST_CASE("BotQuestAssist: session life", "[BotQuestAssist]")
{
    Config cfg = On();
    SessionState st;
    st.LifetimeSec = 1800;
    HelperState h1;
    h1.Guid = 1;
    HelperState h2;
    h2.Guid = 2;
    st.Helpers = { h1, h2 };

    SECTION("a healthy session goes on")
    {
        Verdict v = Evaluate(st, cfg);
        CHECK_FALSE(v.End);
        CHECK(v.Drop.empty());
    }
    SECTION("player gone")
    {
        st.PlayerGoneSec = cfg.PlayerGoneSec;
        CHECK(Evaluate(st, cfg).Reason == "player_gone");
        st.PlayerGoneSec = 0;
        st.PlayerPresent = false;
        CHECK(Evaluate(st, cfg).Reason == "player_gone");
    }
    SECTION("quest done after the grace time")
    {
        st.QuestOpen = false;
        st.QuestClosedSec = cfg.DoneGraceSec - 1;
        CHECK_FALSE(Evaluate(st, cfg).End);
        st.QuestClosedSec = cfg.DoneGraceSec;
        CHECK(Evaluate(st, cfg).Reason == "quest_done");
    }
    SECTION("lifetime")
    {
        st.AgeSec = 1800;
        CHECK(Evaluate(st, cfg).Reason == "lifetime");
    }
    SECTION("a helper that left is dropped")
    {
        st.Helpers[0].Present = false;
        Verdict v = Evaluate(st, cfg);
        CHECK_FALSE(v.End);
        REQUIRE(v.Drop.size() == 1);
        CHECK(v.Drop[0] == 1);
        CHECK(v.DropReasons[0] == "gone");
    }
    SECTION("a helper dead for long is dropped, a fresh death is not")
    {
        st.Helpers[0].Alive = false;
        st.Helpers[0].DeadSec = cfg.DeadSec - 1;
        CHECK(Evaluate(st, cfg).Drop.empty());
        st.Helpers[0].DeadSec = cfg.DeadSec;
        Verdict v = Evaluate(st, cfg);
        REQUIRE(v.Drop.size() == 1);
        CHECK(v.DropReasons[0] == "dead");
    }
    SECTION("a lost helper is dropped")
    {
        st.Helpers[1].FarSec = cfg.LeashSec;
        Verdict v = Evaluate(st, cfg);
        REQUIRE(v.Drop.size() == 1);
        CHECK(v.Drop[0] == 2);
        CHECK(v.DropReasons[0] == "lost");
    }
    SECTION("the session ends when no helper is left")
    {
        st.Helpers[0].Present = false;
        st.Helpers[1].Present = false;
        Verdict v = Evaluate(st, cfg);
        CHECK(v.End);
        CHECK(v.Reason == "no_helpers");
        CHECK(v.Drop.empty());
    }
}

TEST_CASE("BotQuestAssist: escort guard", "[BotQuestAssist]")
{
    Config cfg = On();

    auto atk = [](uint64 guid, bool onEscort, float escortDist, float x)
    {
        Attacker a;
        a.Guid = guid;
        a.OnEscort = onEscort;
        a.EscortDist = escortDist;
        a.X = x;
        return a;
    };
    auto guard = [](uint64 guid, float x, bool free = true)
    {
        Guard g;
        g.Guid = guid;
        g.X = x;
        g.Free = free;
        return g;
    };

    SECTION("nothing attacks, nothing to do")
    {
        CHECK(AssignGuards({}, { guard(1, 0) }, cfg).empty());
        CHECK(AssignGuards({ atk(9, true, 1, 0) }, {}, cfg).empty());
    }
    SECTION("one attacker, one helper")
    {
        std::vector<Assignment> out = AssignGuards({ atk(9, true, 1, 5) }, { guard(1, 0) }, cfg);
        REQUIRE(out.size() == 1);
        CHECK(out[0].Guard == 1);
        CHECK(out[0].Target == 9);
    }
    SECTION("attackers are shared out before one gets two helpers")
    {
        std::vector<Assignment> out = AssignGuards({ atk(8, true, 2, 0), atk(9, true, 3, 0) }, { guard(1, 0), guard(2, 0), guard(3, 0) }, cfg);
        REQUIRE(out.size() == 3);
        CHECK(out[0].Target == 8);
        CHECK(out[1].Target == 9);
        CHECK(out[2].Target == 8);
    }
    SECTION("attackers on the escort come before the others")
    {
        std::vector<Assignment> out = AssignGuards({ atk(7, false, 1, 0), atk(9, true, 20, 0) }, { guard(1, 0) }, cfg);
        REQUIRE(out.size() == 1);
        CHECK(out[0].Target == 9);
    }
    SECTION("a busy helper is left alone")
    {
        CHECK(AssignGuards({ atk(9, true, 1, 0) }, { guard(1, 0, false) }, cfg).empty());
    }
    SECTION("a far attacker is not taken")
    {
        CHECK(AssignGuards({ atk(9, true, 1, cfg.GuardRadius + 1) }, { guard(1, 0) }, cfg).empty());
    }
    SECTION("deterministic for any input order")
    {
        std::vector<Assignment> a = AssignGuards({ atk(8, true, 2, 0), atk(9, true, 3, 0) }, { guard(2, 0), guard(1, 0) }, cfg);
        std::vector<Assignment> b = AssignGuards({ atk(9, true, 3, 0), atk(8, true, 2, 0) }, { guard(1, 0), guard(2, 0) }, cfg);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].Guard == b[i].Guard);
            CHECK(a[i].Target == b[i].Target);
        }
    }
}
