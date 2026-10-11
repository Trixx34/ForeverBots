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

#include "BotGuildPlan.h"

#include <algorithm>
#include <set>

using namespace BotGuildPlan;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    return c;
}

std::vector<Candidate> Bots(uint32 n, uint32 since = 1000)
{
    std::vector<Candidate> v;
    for (uint32 i = 0; i < n; ++i)
    {
        Candidate c;
        c.Key = 100 + i;
        c.SinceSpokeSec = since;
        v.push_back(c);
    }
    return v;
}
}

TEST_CASE("guild invite check", "[bots][guild]")
{
    Config cfg = On();
    InviteFacts f;

    SECTION("a world bot accepts a player's invite")
    {
        CHECK(InviteCheck(f, cfg) == nullptr);
    }
    SECTION("off by default and with the invite switch off")
    {
        CHECK(std::string(InviteCheck(f, Config())) == "GUILD_INVITE_REFUSED_DISABLED");
        cfg.InviteEnabled = false;
        CHECK(std::string(InviteCheck(f, cfg)) == "GUILD_INVITE_REFUSED_DISABLED");
    }
    SECTION("bots never invite bots")
    {
        f.InviterIsBot = true;
        CHECK(std::string(InviteCheck(f, cfg)) == "GUILD_INVITE_REFUSED_BOT_INVITER");
    }
    SECTION("an alt bot takes only its owner")
    {
        f.BotIsAlt = true;
        CHECK(std::string(InviteCheck(f, cfg)) == "GUILD_INVITE_REFUSED_NOT_OWNER");
        f.InviterIsOwner = true;
        CHECK(InviteCheck(f, cfg) == nullptr);
    }
    SECTION("the accept chance applies to world bots only")
    {
        cfg.AcceptPct = 30;
        f.Roll = 29;
        CHECK(InviteCheck(f, cfg) == nullptr);
        f.Roll = 30;
        CHECK(std::string(InviteCheck(f, cfg)) == "GUILD_INVITE_DECLINED");
        f.BotIsAlt = true;
        f.InviterIsOwner = true;
        CHECK(InviteCheck(f, cfg) == nullptr);
    }
    SECTION("level floor")
    {
        cfg.InviteMinLevel = 10;
        f.BotLevel = 9;
        CHECK(std::string(InviteCheck(f, cfg)) == "GUILD_INVITE_REFUSED_LEVEL");
        f.BotLevel = 10;
        CHECK(InviteCheck(f, cfg) == nullptr);
    }
}

TEST_CASE("guild chat classification", "[bots][guild]")
{
    CHECK(Classify("hi") == Intent::Greeting);
    CHECK(Classify("Hello everyone!") == Intent::Greeting);
    CHECK(Classify("hey guys") == Intent::Greeting);
    CHECK(Classify("good morning") == Intent::Greeting);
    CHECK(Classify("what's up") == Intent::Greeting);
    CHECK(Classify("bye all") == Intent::Farewell);
    CHECK(Classify("gotta go, cya") == Intent::Farewell);
    CHECK(Classify("thanks!") == Intent::Thanks);
    CHECK(Classify("ty so much") == Intent::Thanks);
    CHECK(Classify("how's it going?") == Intent::HowAreYou);
    CHECK(Classify("hi, how are you all") == Intent::HowAreYou);
    CHECK(Classify("") == Intent::None);
    CHECK(Classify("selling a stack of linen cloth") == Intent::None);

    SECTION("greetings count only in short lines")
    {
        CHECK(Classify("hi can anyone craft me a belt please") == Intent::None);
        CHECK(Classify("thanks for the help with that quest yesterday") == Intent::None);
    }
    SECTION("words, not substrings")
    {
        CHECK(Classify("history") == Intent::None);
        CHECK(Classify("shiny") == Intent::None);
        CHECK(Classify("typo") == Intent::None);
    }
}

TEST_CASE("guild chat mentions", "[bots][guild]")
{
    CHECK(Mentions("hey Arthas", "Arthas"));
    CHECK(Mentions("arthas, can you help", "Arthas"));
    CHECK(Mentions("@ARTHAS?", "Arthas"));
    CHECK(Mentions("thanks arthas!", "Arthas"));
    CHECK_FALSE(Mentions("arthasian legends", "Arthas"));
    CHECK_FALSE(Mentions("hello there", "Arthas"));
    CHECK_FALSE(Mentions("hello", ""));
}

TEST_CASE("whispered invite requests", "[bots][guild]")
{
    CHECK(WantsInvite("inv"));
    CHECK(WantsInvite("Invite pls"));
    CHECK(WantsInvite("guild invite?"));
    CHECK(WantsInvite("can i join the guild"));
    CHECK_FALSE(WantsInvite("hello"));
    CHECK_FALSE(WantsInvite("what guild is this"));
    CHECK_FALSE(WantsInvite("how many people are in the guild right now and who leads it"));
    CHECK_FALSE(WantsInvite(""));
}

TEST_CASE("guild chat lines", "[bots][guild]")
{
    Intent const all[] = { Intent::Greeting, Intent::Farewell, Intent::Thanks, Intent::HowAreYou, Intent::Mention, Intent::Login, Intent::LevelUp,
        Intent::Joined, Intent::InviteSent, Intent::NoGuild, Intent::NoRights, Intent::AlreadyInGuild, Intent::Declined };

    for (Intent i : all)
    {
        std::set<std::string> distinct;
        for (uint64 seed = 0; seed < 400; ++seed)
        {
            std::string const line = PickLine(i, seed, "Ana", 15, "The Iron Wolves");
            CHECK_FALSE(line.empty());
            CHECK(line.find('{') == std::string::npos);
            CHECK(line == PickLine(i, seed, "Ana", 15, "The Iron Wolves"));
            distinct.insert(line);
        }
        CHECK(distinct.size() >= 6);
    }

    SECTION("placeholders")
    {
        bool name = false, level = false, guild = false;
        for (uint64 seed = 0; seed < 200; ++seed)
        {
            name |= PickLine(Intent::LevelUp, seed, "Ana", 15).find("Ana") != std::string::npos;
            level |= PickLine(Intent::LevelUp, seed, "Ana", 15).find("15") != std::string::npos;
            guild |= PickLine(Intent::InviteSent, seed, "Ana", 0, "Zed Guild").find("Zed Guild") != std::string::npos;
        }
        CHECK(name);
        CHECK(level);
        CHECK(guild);
    }
    SECTION("none says nothing")
    {
        CHECK(PickLine(Intent::None, 1, "Ana").empty());
    }
    SECTION("idle lines vary")
    {
        std::set<std::string> distinct;
        for (uint64 seed = 0; seed < 300; ++seed)
            distinct.insert(IdleLine(seed));
        CHECK(distinct.size() >= 8);
    }
}

TEST_CASE("who answers in guild chat", "[bots][guild]")
{
    Config cfg = On();
    cfg.GreetPct = cfg.ReplyPct = cfg.SmallTalkPct = cfg.CongratsPct = 100;

    SECTION("nothing when off or without bots")
    {
        CHECK(PickResponders(Bots(3), Intent::Login, Config(), 1).empty());
        cfg.ChatEnabled = false;
        CHECK(PickResponders(Bots(3), Intent::Login, cfg, 1).empty());
        cfg.ChatEnabled = true;
        CHECK(PickResponders({}, Intent::Login, cfg, 1).empty());
    }
    SECTION("a login is greeted by at most two bots")
    {
        auto r = PickResponders(Bots(6), Intent::Login, cfg, 5);
        CHECK(r.size() == 2);
        CHECK(r[0].Key != r[1].Key);
    }
    SECTION("zero percent means silence")
    {
        cfg.GreetPct = 0;
        CHECK(PickResponders(Bots(6), Intent::Login, cfg, 5).empty());
    }
    SECTION("bots on cooldown stay silent")
    {
        auto bots = Bots(3, 5);   // spoke 5 seconds ago, cooldown 20
        CHECK(PickResponders(bots, Intent::Login, cfg, 5).empty());
        bots[1].SinceSpokeSec = 30;
        auto r = PickResponders(bots, Intent::Login, cfg, 5);
        REQUIRE(r.size() == 1);
        CHECK(r[0].Key == bots[1].Key);
    }
    SECTION("a plain hello is answered by one bot")
    {
        for (uint64 seed = 0; seed < 30; ++seed)
            CHECK(PickResponders(Bots(5), Intent::Greeting, cfg, seed).size() == 1);
        cfg.SmallTalkPct = 0;
        CHECK(PickResponders(Bots(5), Intent::Greeting, cfg, 1).empty());
    }
    SECTION("small talk is spread over the bots")
    {
        std::set<uint64> seen;
        for (uint64 seed = 0; seed < 60; ++seed)
            for (Responder const& r : PickResponders(Bots(5), Intent::Thanks, cfg, seed))
                seen.insert(r.Key);
        CHECK(seen.size() >= 3);
    }
    SECTION("a named bot answers, and only the named bot")
    {
        auto bots = Bots(4);
        bots[2].Named = true;
        for (uint64 seed = 0; seed < 20; ++seed)
        {
            auto r = PickResponders(bots, Intent::Greeting, cfg, seed);
            REQUIRE(r.size() == 1);
            CHECK(r[0].Key == bots[2].Key);
            auto m = PickResponders(bots, Intent::Mention, cfg, seed);
            REQUIRE(m.size() == 1);
            CHECK(m[0].Key == bots[2].Key);
        }
    }
    SECTION("a mention without a named bot is not answered")
    {
        CHECK(PickResponders(Bots(4), Intent::Mention, cfg, 3).empty());
    }
    SECTION("a named bot on cooldown is not replaced by another one")
    {
        auto bots = Bots(4);
        bots[1].Named = true;
        bots[1].SinceSpokeSec = 3;   // below half of the cooldown
        CHECK(PickResponders(bots, Intent::Greeting, cfg, 4).empty());
        bots[1].SinceSpokeSec = 12;  // above half of the cooldown
        CHECK(PickResponders(bots, Intent::Greeting, cfg, 4).size() == 1);
    }
    SECTION("two named bots both answer")
    {
        auto bots = Bots(4);
        bots[0].Named = bots[3].Named = true;
        CHECK(PickResponders(bots, Intent::Mention, cfg, 8).size() == 2);
    }
    SECTION("delays stay in range and grow")
    {
        for (uint64 seed = 0; seed < 50; ++seed)
        {
            auto r = PickResponders(Bots(6), Intent::LevelUp, cfg, seed);
            REQUIRE(r.size() == 2);
            for (Responder const& x : r)
            {
                CHECK(x.DelayMs >= cfg.DelayMinMs);
                CHECK(x.DelayMs <= cfg.DelayMaxMs);
            }
            CHECK(r[0].DelayMs <= r[1].DelayMs);
        }
    }
    SECTION("deterministic")
    {
        auto a = PickResponders(Bots(6), Intent::Login, cfg, 77);
        auto b = PickResponders(Bots(6), Intent::Login, cfg, 77);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].Key == b[i].Key);
            CHECK(a[i].DelayMs == b[i].DelayMs);
        }
    }
    SECTION("other intents do not choose responders")
    {
        CHECK(PickResponders(Bots(3), Intent::None, cfg, 1).empty());
        CHECK(PickResponders(Bots(3), Intent::Joined, cfg, 1).empty());
    }
}

TEST_CASE("congratulated levels and idle chat", "[bots][guild]")
{
    Config cfg = On();
    CHECK(CongratsLevel(5, cfg));
    CHECK(CongratsLevel(60, cfg));
    CHECK_FALSE(CongratsLevel(7, cfg));
    CHECK_FALSE(CongratsLevel(1, cfg));
    cfg.CongratsEvery = 0;
    CHECK_FALSE(CongratsLevel(10, cfg));
    cfg.CongratsEvery = 1;
    CHECK(CongratsLevel(2, cfg));

    cfg = On();
    CHECK_FALSE(IdleDue(60, 1, cfg));
    CHECK(IdleDue(71 * 60, 1, cfg));
    uint32 first = 0;
    for (uint32 s = 0; s <= 71 * 60; s += 60)
        if (IdleDue(s, 1, cfg))
        {
            first = s;
            break;
        }
    CHECK(first >= 25 * 60);
    CHECK(first <= 70 * 60);
    // bots differ in their interval
    std::set<uint32> intervals;
    for (uint64 key = 1; key <= 40; ++key)
        for (uint32 s = 0; s <= 71 * 60; s += 60)
            if (IdleDue(s, key, cfg))
            {
                intervals.insert(s);
                break;
            }
    CHECK(intervals.size() >= 5);
}

namespace
{
BotInfo B(uint64 guid, uint8 faction = 0, uint32 level = 30)
{
    BotInfo b;
    b.Guid = guid;
    b.Faction = faction;
    b.Level = level;
    return b;
}

GuildInfo G(uint64 id, uint32 members, uint8 faction = 0, bool botLed = true)
{
    GuildInfo g;
    g.Id = id;
    g.Members = members;
    g.Faction = faction;
    g.BotLed = botLed;
    return g;
}

Config Auto()
{
    Config c = On();
    c.AutoEnabled = true;
    c.AutoJoinPct = 100;
    c.AutoFoundPct = 100;
    return c;
}
}

TEST_CASE("bot guild planning", "[bots][guild]")
{
    Config cfg = Auto();

    SECTION("off by default")
    {
        CHECK(PlanAuto({ B(1) }, {}, On(), 1).empty());
        CHECK(PlanAuto({ B(1) }, {}, Config(), 1).empty());
    }
    SECTION("a free bot founds a guild when none exists")
    {
        auto a = PlanAuto({ B(1) }, {}, cfg, 1);
        REQUIRE(a.size() == 1);
        CHECK(a[0].What == Action::Found);
        CHECK(a[0].Bot == 1);
    }
    SECTION("only one founder per faction and check")
    {
        auto a = PlanAuto({ B(1, 0), B(2, 0), B(3, 1) }, {}, cfg, 1);
        REQUIRE(a.size() == 2);
        CHECK(a[0].What == Action::Found);
        CHECK(a[1].What == Action::Found);
        CHECK(((a[0].Bot == 3) != (a[1].Bot == 3)));
    }
    SECTION("a bot joins the bot guild of its faction with the fewest members")
    {
        auto a = PlanAuto({ B(1, 1) }, { G(10, 5, 0), G(11, 9, 1), G(12, 4, 1), G(13, 2, 1, false) }, cfg, 1);
        REQUIRE(a.size() == 1);
        CHECK(a[0].What == Action::Join);
        CHECK(a[0].Guild == 12);
    }
    SECTION("guilds led by players are never filled")
    {
        auto a = PlanAuto({ B(1) }, { G(10, 1, 0, false) }, cfg, 1);
        REQUIRE(a.size() == 1);
        CHECK(a[0].What == Action::Found);
    }
    SECTION("a full guild takes nobody")
    {
        cfg.AutoMaxMembers = 5;
        auto a = PlanAuto({ B(1) }, { G(10, 5) }, cfg, 1);
        CHECK(a.size() == 1);
        CHECK(a[0].What == Action::Found);
        cfg.AutoMaxGuilds = 1;
        CHECK(PlanAuto({ B(1) }, { G(10, 5) }, cfg, 1).empty());
    }
    SECTION("the guild limit is respected, planned founds included")
    {
        cfg.AutoMaxGuilds = 2;
        auto a = PlanAuto({ B(1, 0), B(2, 1), B(3, 0) }, { G(10, 25, 0), G(11, 25, 1), G(12, 3, 0, false) }, cfg, 1);
        CHECK(a.empty());
        cfg.AutoMaxGuilds = 3;
        a = PlanAuto({ B(1, 0), B(2, 1), B(3, 0) }, { G(10, 25, 0), G(11, 25, 1), G(12, 3, 0, false) }, cfg, 1);
        REQUIRE(a.size() == 1);
        CHECK(a[0].What == Action::Found);
    }
    SECTION("bots in a guild, busy, or too low stay out")
    {
        BotInfo guilded = B(1);
        guilded.InGuild = true;
        BotInfo busy = B(2);
        busy.Free = false;
        BotInfo low = B(3, 0, 5);
        CHECK(PlanAuto({ guilded, busy, low }, { G(10, 2) }, cfg, 1).empty());
    }
    SECTION("a bot below the founding level only joins")
    {
        BotInfo mid = B(1, 0, 12);   // joins from 10, founds from 15
        CHECK(PlanAuto({ mid }, {}, cfg, 1).empty());
        auto a = PlanAuto({ mid }, { G(10, 2) }, cfg, 1);
        REQUIRE(a.size() == 1);
        CHECK(a[0].What == Action::Join);
    }
    SECTION("the actions per check are limited and fill the same guild evenly")
    {
        cfg.AutoMaxActions = 2;
        auto a = PlanAuto({ B(1), B(2), B(3), B(4) }, { G(10, 3), G(11, 3) }, cfg, 9);
        REQUIRE(a.size() == 2);
        CHECK(a[0].Guild != a[1].Guild);
        cfg.AutoMaxActions = 0;
        CHECK(PlanAuto({ B(1) }, { G(10, 3) }, cfg, 9).empty());
    }
    SECTION("chances")
    {
        cfg.AutoJoinPct = 0;
        CHECK(PlanAuto({ B(1) }, { G(10, 3) }, cfg, 1).empty());
        cfg = Auto();
        cfg.AutoFoundPct = 0;
        CHECK(PlanAuto({ B(1) }, {}, cfg, 1).empty());
        cfg.AutoFoundPct = 50;
        uint32 founds = 0;
        for (uint64 salt = 0; salt < 400; ++salt)
            founds += PlanAuto({ B(1) }, {}, cfg, salt).size();
        CHECK(founds > 120);
        CHECK(founds < 280);
    }
    SECTION("deterministic and salted")
    {
        std::vector<BotInfo> bots;
        for (uint64 i = 1; i <= 20; ++i)
            bots.push_back(B(i));
        cfg.AutoMaxActions = 1;
        auto a = PlanAuto(bots, { G(10, 3) }, cfg, 5);
        auto b = PlanAuto(bots, { G(10, 3) }, cfg, 5);
        REQUIRE(a.size() == 1);
        CHECK(a[0].Bot == b[0].Bot);
        std::set<uint64> picked;
        for (uint64 salt = 0; salt < 40; ++salt)
            picked.insert(PlanAuto(bots, { G(10, 3) }, cfg, salt)[0].Bot);
        CHECK(picked.size() >= 6);
    }
}

TEST_CASE("generated guild names", "[bots][guild]")
{
    std::set<std::string> distinct;
    for (uint64 seed = 0; seed < 300; ++seed)
        for (uint32 attempt = 0; attempt < 3; ++attempt)
        {
            std::string const name = GuildName(seed, attempt);
            CHECK_FALSE(name.empty());
            CHECK(name.size() <= 24);
            CHECK(std::all_of(name.begin(), name.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == ' '; }));
            CHECK(name.front() != ' ');
            CHECK(name.back() != ' ');
            CHECK(name == GuildName(seed, attempt));
            distinct.insert(name);
        }
    CHECK(distinct.size() > 300);
    CHECK(GuildName(1, 0) != GuildName(1, 1));
}
