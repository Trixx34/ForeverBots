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

#include "BotPartyPlan.h"
#include <algorithm>

using namespace BotParty;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    c.FormChancePct = 100;
    return c;
}

Candidate Bot(uint64 guid, uint32 level, float x, std::vector<uint32> quests, uint32 map = 0)
{
    Candidate c;
    c.Guid = guid;
    c.Level = level;
    c.X = x;
    c.MapId = map;
    c.Quests = std::move(quests);
    return c;
}
}

TEST_CASE("BotParty: shared quests", "[BotParty]")
{
    CHECK(SharedCount({ 1, 2, 3 }, { 3, 4, 2 }) == 2);
    CHECK(SharedCount({ 1, 1, 2 }, { 1 }) == 1);
    CHECK(SharedCount({}, { 1 }) == 0);
}

TEST_CASE("BotParty: forming", "[BotParty]")
{
    Config cfg = On();

    SECTION("off does nothing")
    {
        cfg.Enabled = false;
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 5 }) }, cfg, 1).empty());
    }

    SECTION("two bots with a quest in common team up, the higher level leads")
    {
        auto r = FormParties({ Bot(1, 9, 0, { 5 }), Bot(2, 10, 5, { 5, 6 }) }, cfg, 1);
        REQUIRE(r.size() == 1);
        CHECK(r[0].Leader == 2);
        REQUIRE(r[0].Members.size() == 1);
        CHECK(r[0].Members[0] == 1);
        CHECK(r[0].SharedQuest == 5);
    }

    SECTION("equal levels: the lower guid leads")
    {
        auto r = FormParties({ Bot(7, 10, 0, { 5 }), Bot(3, 10, 5, { 5 }) }, cfg, 1);
        REQUIRE(r.size() == 1);
        CHECK(r[0].Leader == 3);
    }

    SECTION("no common quest, too far, another map or too different in level: no party")
    {
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 6 }) }, cfg, 1).empty());
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 41, { 5 }) }, cfg, 1).empty());
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 5 }, 1) }, cfg, 1).empty());
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 14, 5, { 5 }) }, cfg, 1).empty());
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 13, 5, { 5 }) }, cfg, 1).size() == 1);
    }

    SECTION("party size is capped and the rest stay free")
    {
        cfg.MaxSize = 3;
        auto r = FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 1, { 5 }), Bot(3, 10, 2, { 5 }), Bot(4, 10, 3, { 5 }) }, cfg, 1);
        REQUIRE(!r.empty());
        CHECK(r[0].Members.size() == 2);
        size_t total = 0;
        for (Formed const& f : r)
            total += 1 + f.Members.size();
        CHECK(total <= 4);
    }

    SECTION("a bot is in one party only")
    {
        auto r = FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 1, { 5 }), Bot(3, 10, 2, { 5 }), Bot(4, 10, 3, { 5 }) }, cfg, 1);
        std::vector<uint64> seen;
        for (Formed const& f : r)
        {
            seen.push_back(f.Leader);
            for (uint64 m : f.Members)
                seen.push_back(m);
        }
        std::sort(seen.begin(), seen.end());
        CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    }

    SECTION("level spread holds for the whole party, not only against the seed")
    {
        cfg.MaxLevelSpread = 2;
        // 1 is the seed at level 10; 2 (level 8) and 3 (level 12) are each within 2 of it but 4 apart from each other
        auto r = FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 8, 1, { 5 }), Bot(3, 12, 2, { 5 }) }, cfg, 1);
        REQUIRE(r.size() == 1);
        CHECK(r[0].Members.size() == 1);
    }

    SECTION("zero chance means nobody looks for company")
    {
        cfg.FormChancePct = 0;
        CHECK(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 5 }) }, cfg, 1).empty());
    }

    SECTION("the same input gives the same parties")
    {
        std::vector<Candidate> in = { Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 5 }), Bot(3, 11, 9, { 5, 7 }), Bot(4, 9, 12, { 7 }) };
        cfg.FormChancePct = 50;
        auto a = FormParties(in, cfg, 9);
        auto b = FormParties(in, cfg, 9);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].Leader == b[i].Leader);
            CHECK(a[i].Members == b[i].Members);
        }
    }

    SECTION("the chance picks about that share of bots")
    {
        cfg.FormChancePct = 30;
        uint32 formed = 0;
        for (uint32 salt = 0; salt < 400; ++salt)
            formed += uint32(FormParties({ Bot(1, 10, 0, { 5 }), Bot(2, 10, 5, { 5 }) }, cfg, salt).size());
        // the pair forms when either of them looks: about 1 - 0.7^2 = 51 percent
        CHECK(formed > 140);
        CHECK(formed < 270);
    }
}

TEST_CASE("BotParty: lifetime", "[BotParty]")
{
    Config cfg = On();
    for (uint32 salt = 0; salt < 50; ++salt)
    {
        uint32 const l = LifetimeFor(cfg, 1000 + salt, salt);
        CHECK(l >= cfg.LifetimeSec / 2);
        CHECK(l <= cfg.LifetimeSec);
    }
    CHECK(LifetimeFor(cfg, 5, 3) == LifetimeFor(cfg, 5, 3));
}

TEST_CASE("BotParty: breaking up", "[BotParty]")
{
    Config cfg = On();
    PartyState st;
    st.LifetimeSec = 600;
    st.LeaderLevel = 10;
    MemberState m;
    m.Guid = 2;
    m.Level = 10;
    st.Members = { m };

    SECTION("a healthy party stays")
    {
        Verdict v = Evaluate(st, cfg);
        CHECK(!v.Disband);
        CHECK(v.Drop.empty());
    }

    SECTION("time is up")
    {
        st.AgeSec = 600;
        Verdict v = Evaluate(st, cfg);
        CHECK(v.Disband);
        CHECK(v.Reason == "lifetime");
    }

    SECTION("the leader is gone")
    {
        st.LeaderPresent = false;
        CHECK(Evaluate(st, cfg).Reason == "leader_gone");
        st.LeaderPresent = true;
        st.LeaderGoneSec = cfg.LeaderGoneSec;
        CHECK(Evaluate(st, cfg).Reason == "leader_gone");
        st.LeaderGoneSec = cfg.LeaderGoneSec - 1;
        CHECK(!Evaluate(st, cfg).Disband);
    }

    SECTION("the only member finishing the shared quest ends the party")
    {
        st.Members[0].SharesQuest = false;
        Verdict v = Evaluate(st, cfg);
        CHECK(v.Disband);
        CHECK(v.Reason == "too_small");
    }

    SECTION("a lost member is dropped from a larger party and the party stays")
    {
        MemberState m3 = m;
        m3.Guid = 3;
        st.Members.push_back(m3);
        st.Members[0].FarSec = cfg.LeashSec;
        Verdict v = Evaluate(st, cfg);
        CHECK(!v.Disband);
        REQUIRE(v.Drop.size() == 1);
        CHECK(v.Drop[0] == 2);
        CHECK(v.DropReasons[0] == "lost");
    }

    SECTION("short wandering off is not lost")
    {
        st.Members[0].FarSec = cfg.LeashSec - 1;
        CHECK(!Evaluate(st, cfg).Disband);
    }

    SECTION("another map, a vanished member and a level gap are each dropped")
    {
        MemberState a = m, b = m, c = m;
        a.Guid = 3; a.SameMap = false;
        b.Guid = 4; b.Present = false;
        c.Guid = 5; c.Level = 10 + cfg.MaxLevelSpread + 3;
        st.Members = { m, a, b, c };
        Verdict v = Evaluate(st, cfg);
        REQUIRE(v.Drop.size() == 3);
        CHECK(v.DropReasons[0] == "lost");
        CHECK(v.DropReasons[1] == "gone");
        CHECK(v.DropReasons[2] == "level");
        CHECK(!v.Disband);
    }

    SECTION("a dead member is dropped")
    {
        MemberState m3 = m;
        m3.Guid = 3;
        st.Members.push_back(m3);
        st.Members[1].Alive = false;
        Verdict v = Evaluate(st, cfg);
        CHECK(!v.Disband);
        REQUIRE(v.Drop.size() == 1);
        CHECK(v.DropReasons[0] == "dead");
    }

    SECTION("a larger minimum size disbands sooner")
    {
        cfg.MinSize = 3;
        CHECK(Evaluate(st, cfg).Reason == "too_small");
    }
}
