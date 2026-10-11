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

#include "BotConversationText.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace BotConversationText
{
namespace
{
using Lines = std::vector<char const*>;

constexpr uint32 PERSONALITIES = uint32(Personality::Count);
constexpr uint32 INTENTS = uint32(Intent::Count);

struct Entry { Intent I; Lines L; };

// One table per personality. A missing intent falls back to Friendly's line for it.
std::vector<Entry> const kFriendly =
{
    { Intent::None,      { "Sorry {player}, I did not catch that. Say it again?", "Hm? I lost you there, {player}." } },
    { Intent::Greeting,  { "Hello {player}! Good to see you.", "Hey {player}! Ready when you are.", "Hi {player}, how is the adventure going?" } },
    { Intent::Farewell,  { "Take care, {player}! See you soon.", "Bye {player}, safe travels!" } },
    { Intent::Thanks,    { "Any time, {player}!", "You are welcome, glad to help.", "No problem at all." } },
    { Intent::HowAreYou, { "Doing well, thanks for asking! {hp}% health and in good spirits.", "Pretty good, {player}. How about you?" } },
    { Intent::WhatDoing, { "Just {activity}. You?", "Right now I am {activity}, {player}." } },
    { Intent::Where,     { "We are in {zone}, {player}.", "Right here in {zone}!" } },
    { Intent::Who,       { "I am {bot}, a level {level} {class}. Pleased to meet you!", "{bot}, level {level} {class}, at your service." } },
    { Intent::Help,      { "I will gladly help, {player}. Tell me what you need.", "Of course! Point me at the problem." } },
    { Intent::Compliment,{ "Aw, thank you {player}, that is kind of you!", "You are too nice, thanks!" } },
    { Intent::Insult,    { "That was not very nice, {player}.", "Ouch. I will pretend I did not hear that." } },
    { Intent::Joke,      { "Ha! Good one, {player}.", "Hehe, you got me." } },
    { Intent::Sorry,     { "No worries, {player}, it happens.", "All forgiven!" } },
    { Intent::Ready,     { "Ready whenever you are!", "All set, {player}. Lead the way." } },
    { Intent::Wait,      { "Sure, I will wait for you.", "Take your time, {player}." } },
};

std::vector<Entry> const kCheerful =
{
    { Intent::None,      { "Ooh, say that again, {player}?", "I zoned out for a second! What was that?" } },
    { Intent::Greeting,  { "Heeey {player}! What a day for an adventure!", "Hi hi hi, {player}!", "Hello! I was hoping you would show up!" } },
    { Intent::Farewell,  { "Bye {player}! Come back soon, this was fun!", "See you later, {player}!!" } },
    { Intent::Thanks,    { "My pleasure, {player}! Anytime!", "Happy to help!!", "Yay, glad that worked out!" } },
    { Intent::HowAreYou, { "Fantastic! {hp}% health and the sun is shining in {zone}!", "Great, {player}! Everything is great!" } },
    { Intent::WhatDoing, { "Just {activity}, and loving it!", "I am {activity}! Want to join in?" } },
    { Intent::Where,     { "We are in {zone}, isn't it lovely?", "{zone}! Best place around!" } },
    { Intent::Who,       { "I am {bot}! Level {level} {class}, and proud of it!", "{bot} the {class}, nice to meet you!" } },
    { Intent::Help,      { "Yes yes, I can help! What do you need?", "Helping is my favourite thing, {player}!" } },
    { Intent::Compliment,{ "Aww, you are the best, {player}!", "Stop, you are making me blush!" } },
    { Intent::Insult,    { "Hey, that hurt a little, {player}. Let's be friends!", "Oh no, did I do something wrong?" } },
    { Intent::Joke,      { "Hahaha, that is great, {player}!", "Ha! Tell me another one!" } },
    { Intent::Sorry,     { "Don't worry about it at all, {player}!", "It is fine, really! Hugs!" } },
    { Intent::Ready,     { "Born ready, {player}!", "Yes! Let's go let's go!" } },
    { Intent::Wait,      { "Of course, I will be right here!", "No rush, {player}!" } },
};

std::vector<Entry> const kGrumpy =
{
    { Intent::None,      { "What? Speak up, {player}.", "I have no idea what you want." } },
    { Intent::Greeting,  { "Hmph. Hello, {player}.", "Oh, it is you.", "Yeah yeah, hi." } },
    { Intent::Farewell,  { "Finally some quiet. Bye, {player}.", "Fine. Go." } },
    { Intent::Thanks,    { "Yeah, yeah. Don't mention it.", "Hmph. You are welcome, I suppose." } },
    { Intent::HowAreYou, { "{hp}% health and my feet hurt. How do you think?", "Could be worse. Could be better. Mostly worse." } },
    { Intent::WhatDoing, { "{activity}. Unlike some people.", "What does it look like? {activity}." } },
    { Intent::Where,     { "{zone}. Did you not read the sign?", "We are in {zone}. Keep up." } },
    { Intent::Who,       { "{bot}. {class}, level {level}. Anything else?", "Level {level} {class}. That is all you get." } },
    { Intent::Help,      { "Ugh, fine. What is it?", "I suppose I can help. Be quick." } },
    { Intent::Compliment,{ "Flattery won't get you far, {player}.", "Hmph. Fine, thanks." } },
    { Intent::Insult,    { "Say that again and see what happens, {player}.", "Charming. Real charming." } },
    { Intent::Joke,      { "That was not funny. ...Okay, a little.", "Hmph. I did not laugh." } },
    { Intent::Sorry,     { "Don't do it again, {player}.", "Fine. Forgotten. Mostly." } },
    { Intent::Ready,     { "Been ready. You are the one dawdling.", "Finally. Let's go." } },
    { Intent::Wait,      { "Great, more standing around.", "Fine, I'll wait. Again." } },
};

std::vector<Entry> const kStoic =
{
    { Intent::None,      { "I do not understand.", "Say again." } },
    { Intent::Greeting,  { "Greetings, {player}.", "Well met.", "Hello." } },
    { Intent::Farewell,  { "Farewell, {player}.", "Until next time." } },
    { Intent::Thanks,    { "It is nothing.", "You are welcome." } },
    { Intent::HowAreYou, { "{hp}% health. I endure.", "Well enough." } },
    { Intent::WhatDoing, { "{activity}.", "I am {activity}. Nothing more." } },
    { Intent::Where,     { "{zone}.", "We stand in {zone}." } },
    { Intent::Who,       { "{bot}. {class}. Level {level}.", "A level {level} {class}." } },
    { Intent::Help,      { "Name the task.", "I will help. Speak." } },
    { Intent::Compliment,{ "I accept your words.", "Thank you. It is noted." } },
    { Intent::Insult,    { "Words are wind, {player}.", "I will not respond to that." } },
    { Intent::Joke,      { "...Ha.", "Amusing." } },
    { Intent::Sorry,     { "It is forgotten.", "No harm done." } },
    { Intent::Ready,     { "Ready.", "Lead on, {player}." } },
    { Intent::Wait,      { "I will wait.", "Patience is a virtue." } },
};

std::vector<Entry> const kSarcastic =
{
    { Intent::None,      { "Wow, riveting. I understood none of that.", "Sure, {player}. Whatever that meant." } },
    { Intent::Greeting,  { "Oh look, {player}. What a surprise.", "Hello, {player}. Try to contain my excitement.", "Hey. Yes, I am still here." } },
    { Intent::Farewell,  { "Leaving already? I'll try to cope.", "Bye, {player}. Don't let the door hit you." } },
    { Intent::Thanks,    { "Oh, you're welcome. Truly my life's purpose.", "Anything for you, {player}. Anything." } },
    { Intent::HowAreYou, { "Oh, spectacular. {hp}% health and a dream of a day.", "Peachy. Thanks for asking, finally." } },
    { Intent::WhatDoing, { "Oh, you know. {activity}. The thrill never ends.", "{activity}. Living the dream." } },
    { Intent::Where,     { "{zone}, {player}. It is on the map, you know.", "Somewhere in {zone}. Shocking, I know." } },
    { Intent::Who,       { "{bot}, level {level} {class}. We have met. A lot.", "I am {bot}. You are welcome." } },
    { Intent::Help,      { "Oh sure, because I had nothing better to do. What?", "Help? With what, your charm?" } },
    { Intent::Compliment,{ "Wow, flattery. I almost believe you.", "Careful, my ego needs more room." } },
    { Intent::Insult,    { "Wow. Original. Did you think of that yourself?", "Ouch. I'm wounded. Truly." } },
    { Intent::Joke,      { "Ha. Ha. Ha. I'm laughing on the inside.", "Did you write that yourself? Cute." } },
    { Intent::Sorry,     { "Apology accepted, I guess. For now.", "Aww, someone has manners after all." } },
    { Intent::Ready,     { "Ready? I've been standing here for ages.", "Oh, now you are ready. Great." } },
    { Intent::Wait,      { "Sure, take all the time you want. I love standing here.", "Waiting. My favourite." } },
};

std::vector<Entry> const kNerdy =
{
    { Intent::None,      { "That does not parse, {player}. Could you rephrase?", "Input not recognized. Try again?" } },
    { Intent::Greeting,  { "Greetings, {player}! Did you know {zone} has fascinating lore?", "Hello! I was just reading up on {class} talent theory.", "Hi {player}! Ready for some optimal play?" } },
    { Intent::Farewell,  { "Goodbye, {player}. I'll recompute my route while you are away.", "Farewell! May your drops be statistically favourable." } },
    { Intent::Thanks,    { "You are welcome! Cooperation is a force multiplier.", "Happy to help, {player}. Efficiency matters." } },
    { Intent::HowAreYou, { "Nominal. {hp}% health, all systems fine.", "Good! My numbers look healthy, {player}." } },
    { Intent::WhatDoing, { "Currently {activity}. Fascinating, really.", "{activity}, while analysing the encounter." } },
    { Intent::Where,     { "{zone}. Known for interesting spawn tables.", "We are in {zone}, {player}. Coordinates available on request." } },
    { Intent::Who,       { "{bot}, level {level} {class}. Specialised, efficient.", "A level {level} {class}, {player}. Pleased to meet you." } },
    { Intent::Help,      { "Certainly. State the problem and I will reason about it.", "Help is my specialty. What is the issue?" } },
    { Intent::Compliment,{ "Thank you! I have been optimizing for that.", "Why, thank you {player}. Your assessment is correct." } },
    { Intent::Insult,    { "That is not a valid argument, {player}.", "Ad hominem noted. Moving on." } },
    { Intent::Joke,      { "Ha! Elegant. I'll log that one.", "Amusing. The punchline has good latency." } },
    { Intent::Sorry,     { "Apology accepted. No further action needed.", "No problem, {player}. Margin of error." } },
    { Intent::Ready,     { "All preparations complete.", "Ready, {player}. Cooldowns checked." } },
    { Intent::Wait,      { "Understood. Standing by.", "Holding position until you are ready, {player}." } },
};

std::vector<Entry> const& Table(Personality p)
{
    switch (p)
    {
        case Personality::Cheerful: return kCheerful;
        case Personality::Grumpy: return kGrumpy;
        case Personality::Stoic: return kStoic;
        case Personality::Sarcastic: return kSarcastic;
        case Personality::Nerdy: return kNerdy;
        default: return kFriendly;
    }
}

Lines const* FindLines(std::vector<Entry> const& table, Intent i)
{
    for (Entry const& e : table)
        if (e.I == i)
            return &e.L;
    return nullptr;
}

Lines const& LinesFor(Personality p, Intent i)
{
    static Lines const none;
    if (Lines const* l = FindLines(Table(p), i))
        return *l;
    if (Lines const* l = FindLines(kFriendly, i))
        return *l;
    return none;
}

std::string Lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Words of a message: runs of letters and digits, apostrophes kept inside a word ("how's").
std::vector<std::string> Words(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { while (!cur.empty() && cur.back() == '\'') cur.pop_back(); if (!cur.empty()) out.push_back(std::move(cur)); cur.clear(); };
    for (char ch : text)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
            cur.push_back(char(std::tolower(c)));
        else if (ch == '\'' && !cur.empty())
            cur.push_back(ch);
        else
            flush();
    }
    flush();
    return out;
}

// Phrase = space separated words, matched as a contiguous run of the message's words.
bool HasPhrase(std::vector<std::string> const& words, char const* phrase)
{
    std::vector<std::string> p = Words(phrase);
    if (p.empty() || p.size() > words.size())
        return false;
    for (size_t i = 0; i + p.size() <= words.size(); ++i)
        if (std::equal(p.begin(), p.end(), words.begin() + i))
            return true;
    return false;
}

struct Rule { Intent I; Lines Phrases; };

// Checked in this order: the first intent with a matching phrase wins.
std::vector<Rule> const& Rules()
{
    static std::vector<Rule> const rules =
    {
        { Intent::Insult,     { "idiot", "stupid", "useless", "shut up", "dumb", "you suck", "noob", "moron", "trash", "garbage", "hate you", "stfu" } },
        { Intent::Thanks,     { "thanks", "thank you", "thx", "ty", "cheers", "much appreciated", "appreciate it" } },
        { Intent::Sorry,      { "sorry", "my bad", "apologies", "oops", "forgive me" } },
        { Intent::Compliment, { "good job", "well done", "nice one", "great job", "nice work", "good bot", "you rock", "awesome", "you are great", "you're great", "love you", "amazing" } },
        { Intent::Joke,       { "lol", "lmao", "haha", "hehe", "rofl", "joke", "funny", "knock knock" } },
        { Intent::HowAreYou,  { "how are you", "how are u", "how's it going", "hows it going", "how is it going", "how you doing", "how do you do", "how's life", "whats up", "what's up", "wassup", "sup" } },
        { Intent::WhatDoing,  { "what are you doing", "what are u doing", "what you doing", "what're you doing", "whatcha doing", "what do you do", "busy" } },
        { Intent::Where,      { "where are we", "where are you", "where am i", "what zone", "which zone", "what place", "where is this" } },
        { Intent::Who,        { "who are you", "who r u", "what are you", "your name", "your class", "what class", "what level", "your level", "introduce yourself" } },
        { Intent::Help,       { "help", "assist", "can you help", "need a hand", "give me a hand", "could you help" } },
        { Intent::Ready,      { "ready", "lets go", "let's go", "go go", "move out", "pull" } },
        { Intent::Wait,       { "wait", "hold on", "one sec", "brb", "afk", "hang on", "one moment", "gimme a sec", "give me a sec" } },
        { Intent::Farewell,   { "bye", "goodbye", "good night", "gn", "cya", "see you", "see ya", "farewell", "later", "gtg", "g2g", "logging off", "log off" } },
        { Intent::Greeting,   { "hi", "hello", "hey", "yo", "howdy", "greetings", "hiya", "heya", "good morning", "good evening", "good afternoon", "well met", "hello there", "hey there", "hi there" } },
    };
    return rules;
}

std::string Expand(std::string_view tpl, Facts const& f)
{
    std::string out;
    out.reserve(tpl.size() + 24);
    for (size_t i = 0; i < tpl.size();)
    {
        size_t const close = tpl[i] == '{' ? tpl.find('}', i) : std::string_view::npos;
        if (close != std::string_view::npos)
        {
            std::string_view const key = tpl.substr(i + 1, close - i - 1);
            std::string value;
            bool known = true;
            if (key == "player") value = f.Player;
            else if (key == "bot") value = f.Bot;
            else if (key == "class") value = f.Class;
            else if (key == "zone") value = f.Zone;
            else if (key == "activity") value = f.Activity;
            else if (key == "level") value = std::to_string(f.Level);
            else if (key == "hp") value = std::to_string(f.HealthPct);
            else known = false;
            if (known)
            {
                out += value;
                i = close + 1;
                continue;
            }
        }
        out.push_back(tpl[i++]);
    }
    return out;
}
}

char const* PersonalityName(Personality p)
{
    switch (p)
    {
        case Personality::Friendly: return "friendly";
        case Personality::Cheerful: return "cheerful";
        case Personality::Grumpy: return "grumpy";
        case Personality::Stoic: return "stoic";
        case Personality::Sarcastic: return "sarcastic";
        case Personality::Nerdy: return "nerdy";
        default: return "friendly";
    }
}

bool ParsePersonality(std::string_view name, Personality& out)
{
    std::string const n = Lower(name);
    for (uint32 i = 0; i < PERSONALITIES; ++i)
        if (n == PersonalityName(Personality(i)))
        {
            out = Personality(i);
            return true;
        }
    return false;
}

Personality PersonalityFromSeed(uint64 seed)
{
    // splitmix64 finalizer so consecutive guids do not walk the list in order
    seed += 0x9E3779B97F4A7C15ull;
    seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ull;
    seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBull;
    seed ^= seed >> 31;
    return Personality(seed % PERSONALITIES);
}

Intent Classify(std::string_view text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty())
        return Intent::None;
    for (Rule const& r : Rules())
        for (char const* phrase : r.Phrases)
            if (HasPhrase(words, phrase))
                return r.I;
    return Intent::None;
}

bool Addresses(std::string_view text, std::string_view botName)
{
    std::vector<std::string> const name = Words(botName);
    if (name.size() != 1)
        return false;
    for (std::string const& w : Words(text))
        if (w == name[0])
            return true;
    return false;
}

bool IsChatter(Intent intent)
{
    return intent == Intent::Greeting || intent == Intent::Farewell || intent == Intent::Thanks || intent == Intent::Joke ||
        intent == Intent::Compliment || intent == Intent::Ready;
}

char const* ActivityText(bool dead, bool inCombat, uint32 healthPct, bool grouped)
{
    if (dead)
        return "lying dead on the ground";
    if (inCombat)
        return healthPct < 35 ? "fighting for my life" : "fighting";
    if (healthPct < 35)
        return "catching my breath";
    return grouped ? "tagging along with the group" : "looking around";
}

uint32 TemplateCount(Personality p, Intent intent)
{
    return uint32(LinesFor(p, intent).size());
}

std::string Reply(Personality p, Intent intent, Facts const& facts, uint32 seed)
{
    Lines const* lines = &LinesFor(p, intent);
    if (lines->empty())
        lines = &LinesFor(p, Intent::None);
    if (lines->empty())
        return "...";
    return Expand((*lines)[seed % lines->size()], facts);
}

uint32 TypingDelayMs(size_t replyLength, uint32 seed, uint32 minMs, uint32 maxMs)
{
    if (maxMs < minMs)
        maxMs = minMs;
    uint64 const ms = uint64(minMs) + uint64(replyLength) * 30 + (seed % 501);
    return uint32(std::clamp<uint64>(ms, minMs, maxMs));
}
}
