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

#ifndef TRINITY_BOT_CONVERSATION_TEXT_H
#define TRINITY_BOT_CONVERSATION_TEXT_H

// Pure half of the bot conversations (docs/playerbots/feature-bot-conversations-20261008.md): what a player said (Classify), who
// it was said to (Addresses), the personality of a bot and the line it answers with (Reply). No config, no world state, no
// randomness of its own (every choice takes a seed), so it is unit tested. The game side is BotConversation.
// Local only: templates and keywords, no external service.

#include "Define.h"
#include <string>
#include <string_view>

namespace BotConversationText
{
enum class Personality : uint8 { Friendly, Cheerful, Grumpy, Stoic, Sarcastic, Nerdy, Count };

TC_GAME_API char const* PersonalityName(Personality p);

// Case insensitive name ("friendly", "grumpy", ...). False for anything else (including "random").
TC_GAME_API bool ParsePersonality(std::string_view name, Personality& out);

// Stable personality of a bot: the same seed (the bot's guid) always gives the same one.
TC_GAME_API Personality PersonalityFromSeed(uint64 seed);

enum class Intent : uint8
{
    None, Greeting, Farewell, Thanks, HowAreYou, WhatDoing, Where, Who, Help, Compliment, Insult, Joke, Sorry, Ready, Wait, Count
};

// What the message is about, by keywords on whole words (case and punctuation ignored). Insult wins over everything, then thanks,
// compliments, questions, greetings. None when nothing matches.
TC_GAME_API Intent Classify(std::string_view text);

// The bot's name appears in the text as a whole word (case insensitive).
TC_GAME_API bool Addresses(std::string_view text, std::string_view botName);

// Intents a bot may answer in party chat without being addressed by name (the social ones, not the questions).
TC_GAME_API bool IsChatter(Intent intent);

struct Facts
{
    std::string Player;      // {player}
    std::string Bot;         // {bot}
    std::string Class;       // {class}
    std::string Zone;        // {zone}
    std::string Activity;    // {activity}, see ActivityText
    uint32 Level = 0;        // {level}
    uint32 HealthPct = 100;  // {hp}
};

// Short phrase of what the bot is doing, for {activity}.
TC_GAME_API char const* ActivityText(bool dead, bool inCombat, uint32 healthPct, bool grouped);

// The line this personality answers with: a template picked by `seed` and its {player} {bot} {class} {zone} {activity} {level} {hp}
// placeholders filled in. Intent::None gives the "did not understand" line of the personality. Never empty.
TC_GAME_API std::string Reply(Personality p, Intent intent, Facts const& facts, uint32 seed);

// Number of templates of a personality/intent (tests, and so a caller can vary the seed).
TC_GAME_API uint32 TemplateCount(Personality p, Intent intent);

// How long the bot "types" before the line is sent: minMs + 30 ms per character + up to 500 ms of jitter from the seed, within [minMs, maxMs].
TC_GAME_API uint32 TypingDelayMs(size_t replyLength, uint32 seed, uint32 minMs, uint32 maxMs);
}

#endif
