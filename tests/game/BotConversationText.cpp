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

#include "BotConversationText.h"
#include <set>
#include <string>

using namespace BotConversationText;

namespace
{
Facts MakeFacts()
{
    Facts f;
    f.Player = "Yves";
    f.Bot = "Grimbo";
    f.Class = "Warrior";
    f.Zone = "Elwynn Forest";
    f.Activity = "looking around";
    f.Level = 12;
    f.HealthPct = 80;
    return f;
}
}

TEST_CASE("Classify: topics by whole words", "[BotConversationText]")
{
    CHECK(Classify("Hello there!") == Intent::Greeting);
    CHECK(Classify("hey") == Intent::Greeting);
    CHECK(Classify("good morning everyone") == Intent::Greeting);
    CHECK(Classify("bye for now") == Intent::Farewell);
    CHECK(Classify("Thanks a lot") == Intent::Thanks);
    CHECK(Classify("how are you?") == Intent::HowAreYou);
    CHECK(Classify("how's it going") == Intent::HowAreYou);
    CHECK(Classify("what are you doing?") == Intent::WhatDoing);
    CHECK(Classify("where are we") == Intent::Where);
    CHECK(Classify("who are you") == Intent::Who);
    CHECK(Classify("can you help me") == Intent::Help);
    CHECK(Classify("good job!") == Intent::Compliment);
    CHECK(Classify("you are an idiot") == Intent::Insult);
    CHECK(Classify("lol") == Intent::Joke);
    CHECK(Classify("sorry about that") == Intent::Sorry);
    CHECK(Classify("ready?") == Intent::Ready);
    CHECK(Classify("hold on a sec") == Intent::Wait);
}

TEST_CASE("Classify: case, punctuation and whole words only", "[BotConversationText]")
{
    CHECK(Classify("HELLO!!!") == Intent::Greeting);
    CHECK(Classify("...thanks,") == Intent::Thanks);
    CHECK(Classify("") == Intent::None);
    CHECK(Classify("   ") == Intent::None);
    CHECK(Classify("the quest needs 5 wolf pelts") == Intent::None);
    CHECK(Classify("hide behind the tree") == Intent::None);        // "hi" inside "hide" is not a greeting
    CHECK(Classify("this is thankless work") == Intent::None);      // "thanks" inside a longer word
    CHECK(Classify("shipped it") == Intent::None);                  // "hi" and "ship" are not "hi"
}

TEST_CASE("Classify: priority between topics", "[BotConversationText]")
{
    CHECK(Classify("hey you idiot") == Intent::Insult);
    CHECK(Classify("hello and thanks") == Intent::Thanks);
    CHECK(Classify("sorry, good job") == Intent::Sorry);
    CHECK(Classify("hi, how are you") == Intent::HowAreYou);
}

TEST_CASE("Addresses: bot name as a whole word", "[BotConversationText]")
{
    CHECK(Addresses("Grimbo, are you there?", "Grimbo"));
    CHECK(Addresses("hey grimbo", "Grimbo"));
    CHECK(Addresses("GRIMBO!", "Grimbo"));
    CHECK_FALSE(Addresses("grimbos are lazy", "Grimbo"));
    CHECK_FALSE(Addresses("hello everyone", "Grimbo"));
    CHECK_FALSE(Addresses("", "Grimbo"));
    CHECK_FALSE(Addresses("hello", ""));
}

TEST_CASE("IsChatter: only the social topics", "[BotConversationText]")
{
    CHECK(IsChatter(Intent::Greeting));
    CHECK(IsChatter(Intent::Farewell));
    CHECK(IsChatter(Intent::Thanks));
    CHECK(IsChatter(Intent::Joke));
    CHECK_FALSE(IsChatter(Intent::HowAreYou));
    CHECK_FALSE(IsChatter(Intent::Insult));
    CHECK_FALSE(IsChatter(Intent::None));
}

TEST_CASE("Personality: names and stable seed", "[BotConversationText]")
{
    Personality p = Personality::Friendly;
    CHECK(ParsePersonality("Grumpy", p));
    CHECK(p == Personality::Grumpy);
    CHECK_FALSE(ParsePersonality("random", p));
    CHECK_FALSE(ParsePersonality("", p));
    for (uint32 i = 0; i < uint32(Personality::Count); ++i)
    {
        Personality back = Personality::Friendly;
        REQUIRE(ParsePersonality(PersonalityName(Personality(i)), back));
        CHECK(uint32(back) == i);
    }

    CHECK(PersonalityFromSeed(1234) == PersonalityFromSeed(1234));
    std::set<uint32> seen;
    for (uint64 guid = 1; guid <= 200; ++guid)
    {
        uint32 const v = uint32(PersonalityFromSeed(guid));
        CHECK(v < uint32(Personality::Count));
        seen.insert(v);
    }
    CHECK(seen.size() == uint32(Personality::Count));   // consecutive guids spread over every personality
}

TEST_CASE("Reply: every personality answers every topic", "[BotConversationText]")
{
    Facts const f = MakeFacts();
    for (uint32 p = 0; p < uint32(Personality::Count); ++p)
        for (uint32 i = 0; i < uint32(Intent::Count); ++i)
        {
            uint32 const n = TemplateCount(Personality(p), Intent(i));
            REQUIRE(n >= 1);
            for (uint32 seed = 0; seed < n; ++seed)
            {
                std::string const line = Reply(Personality(p), Intent(i), f, seed);
                CHECK_FALSE(line.empty());
                CHECK(line.find('{') == std::string::npos);   // every placeholder was filled in
                CHECK(line.size() <= 200);                    // one chat line
            }
        }
}

TEST_CASE("Reply: placeholders and seed", "[BotConversationText]")
{
    Facts const f = MakeFacts();
    CHECK(Reply(Personality::Friendly, Intent::Who, f, 0) == "I am Grimbo, a level 12 Warrior. Pleased to meet you!");
    CHECK(Reply(Personality::Stoic, Intent::Where, f, 0) == "Elwynn Forest.");
    CHECK(Reply(Personality::Friendly, Intent::Greeting, f, 0).find("Yves") != std::string::npos);
    // the seed wraps around the templates and picks deterministically
    uint32 const n = TemplateCount(Personality::Friendly, Intent::Greeting);
    CHECK(Reply(Personality::Friendly, Intent::Greeting, f, 1) == Reply(Personality::Friendly, Intent::Greeting, f, 1 + n));
    CHECK(Reply(Personality::Friendly, Intent::Greeting, f, 0) != Reply(Personality::Friendly, Intent::Greeting, f, 1));
    // personalities sound different
    CHECK(Reply(Personality::Grumpy, Intent::Greeting, f, 0) != Reply(Personality::Cheerful, Intent::Greeting, f, 0));
}

TEST_CASE("Reply: unknown placeholder is left alone", "[BotConversationText]")
{
    Facts f = MakeFacts();
    f.Player = "{zone}";    // a player name never expands twice
    CHECK(Reply(Personality::Friendly, Intent::Thanks, f, 0).find("Elwynn") == std::string::npos);
}

TEST_CASE("ActivityText", "[BotConversationText]")
{
    CHECK(std::string(ActivityText(true, false, 0, true)) == "lying dead on the ground");
    CHECK(std::string(ActivityText(false, true, 90, true)) == "fighting");
    CHECK(std::string(ActivityText(false, true, 10, true)) == "fighting for my life");
    CHECK(std::string(ActivityText(false, false, 10, false)) == "catching my breath");
    CHECK(std::string(ActivityText(false, false, 100, true)) == "tagging along with the group");
    CHECK(std::string(ActivityText(false, false, 100, false)) == "looking around");
}

TEST_CASE("TypingDelayMs: grows with the line, stays within bounds", "[BotConversationText]")
{
    CHECK(TypingDelayMs(0, 0, 800, 3500) == 800);
    CHECK(TypingDelayMs(40, 0, 800, 3500) == 2000);
    CHECK(TypingDelayMs(40, 500, 800, 3500) == 2500);
    CHECK(TypingDelayMs(1000, 0, 800, 3500) == 3500);
    CHECK(TypingDelayMs(40, 0, 500, 100) == 500);   // max below min: min wins
    for (uint32 seed = 0; seed < 2000; seed += 37)
    {
        uint32 const d = TypingDelayMs(25, seed, 800, 3500);
        CHECK(d >= 800);
        CHECK(d <= 3500);
    }
}
