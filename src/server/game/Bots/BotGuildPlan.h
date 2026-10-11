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

#ifndef TRINITY_BOT_GUILD_PLAN_H
#define TRINITY_BOT_GUILD_PLAN_H

// Guilds for the player bots (Bot.Guild.*, glue in BotGuild.cpp). Bots accept guild invites from players, found and fill guilds of their own, and
// talk in guild chat: they greet members who log in, answer greetings and their own name, congratulate level-ups, speak now and then when a
// player is around, and invite a player who whispers for it. Pure functions over plain data (no Player, no Guild) so they are unit tested
// without a world. See docs/playerbots/feature-bot-guilds-20261008.md.

#include "Define.h"
#include <string>
#include <string_view>
#include <vector>

namespace BotGuildPlan
{
    struct Config
    {
        bool Enabled = false;            // Bot.Guild.Enabled, master switch of everything below
        bool InviteEnabled = true;       // bots accept guild invites
        uint32 AcceptPct = 100;          // chance that a world bot accepts an invite
        uint32 InviteMinLevel = 1;       // lowest level of a bot that accepts an invite
        bool ChatEnabled = true;         // bots speak in guild chat
        uint32 GreetPct = 60;            // chance per bot that it welcomes a member who logs in
        uint32 ReplyPct = 85;            // chance that a bot answers when its name is used
        uint32 SmallTalkPct = 35;        // chance that one bot answers a hello, goodbye, thanks or "how are you" in guild chat
        uint32 CongratsPct = 60;         // chance per bot that it congratulates a level-up
        uint32 CongratsEvery = 5;        // levels 5, 10, 15 ... (0 = never)
        uint32 CooldownSec = 20;         // least time between two lines of one bot
        uint32 DelayMinMs = 1500;        // a bot answers after a delay in this range
        uint32 DelayMaxMs = 6000;
        uint32 IdleMinMin = 25;          // a bot with a player around says something unprompted every 25 to 70 minutes
        uint32 IdleMaxMin = 70;
        bool WhisperInvite = true;       // a bot invites a player who whispers for a guild invite
        bool AutoEnabled = false;        // bots found and fill guilds of their own
        uint32 AutoCheckSec = 120;
        uint32 AutoMaxGuilds = 10;       // guilds led by world bots
        uint32 AutoMaxMembers = 25;      // a bot guild takes no more members than this
        uint32 AutoMinLevel = 10;        // lowest level of a bot that joins
        uint32 AutoFoundMinLevel = 15;   // lowest level of a bot that founds a guild
        uint32 AutoJoinPct = 40;         // chance per check and free bot that it joins a guild with room
        uint32 AutoFoundPct = 15;        // chance per check and free bot that it founds a guild when none has room
        uint32 AutoMaxActions = 2;       // founds and joins per check
    };

    // --- invites -------------------------------------------------------------------------------------------------------------------------

    struct InviteFacts
    {
        bool InviterIsBot = false;
        bool BotIsAlt = false;           // an alt bot belongs to a player account
        bool InviterIsOwner = false;     // same account as the alt bot
        uint32 BotLevel = 1;
        uint32 Roll = 0;                 // 0-99, chosen by the glue
    };

    // nullptr when the bot accepts, else the refusal code (GUILD_INVITE_REFUSED_*). Alt bots take only their owner's invites, world bots
    // anyone's (Config::AcceptPct of the time). Bots never invite bots.
    TC_GAME_API char const* InviteCheck(InviteFacts const& f, Config const& cfg);

    // --- guild chat ----------------------------------------------------------------------------------------------------------------------

    enum class Intent : uint8
    {
        None, Greeting, Farewell, Thanks, HowAreYou, Mention,   // what a member said
        Login, LevelUp, Joined, InviteSent,                     // what happened
        NoGuild, NoRights, AlreadyInGuild,                      // answers to a whispered invite request
        Declined                                                // a bot turns down a guild invite
    };

    // Intent of a guild chat line (lower case, word boundaries). Greetings, goodbyes and thanks count only in a short line (five words or
    // fewer) so that "hi, can anyone craft a belt" is not answered as a hello.
    TC_GAME_API Intent Classify(std::string_view text);

    // Whether `text` uses the bot's name as a word (case insensitive, "Name," "Name:" and "@Name" count, "Namesake" does not).
    TC_GAME_API bool Mentions(std::string_view text, std::string_view botName);

    // Whether a whisper asks for a guild invite ("inv", "invite pls", "guild invite", "can i join the guild").
    TC_GAME_API bool WantsInvite(std::string_view text);

    // Pure: the line a bot says. `{n}` becomes `name` (the member talked to), `{l}` the level, `{g}` the guild name. Fixed by the intent
    // and the seed; every intent has at least six lines.
    TC_GAME_API std::string PickLine(Intent intent, uint64 seed, std::string_view name, uint32 level = 0, std::string_view guild = {});

    // A line a bot says unprompted, fixed by the seed.
    TC_GAME_API std::string IdleLine(uint64 seed);

    struct Candidate
    {
        uint64 Key = 0;                  // bot guid
        bool Named = false;              // the message used its name
        uint32 SinceSpokeSec = 1000000;  // seconds since the bot last said something in guild chat
    };

    struct Responder
    {
        uint64 Key = 0;
        uint32 DelayMs = 0;
    };

    // Who answers. Login: up to two bots greet (GreetPct each). LevelUp: up to two congratulate (CongratsPct each). Mention: every bot
    // that was named, at most two (ReplyPct each). Greeting / Farewell / Thanks / HowAreYou: if a bot was named it answers like Mention,
    // else one bot of those off cooldown answers with SmallTalkPct. A bot on cooldown (CooldownSec; a named bot gets half of it) stays
    // silent. Delays are inside [DelayMinMs, DelayMaxMs] and grow with each responder so the lines do not arrive together.
    TC_GAME_API std::vector<Responder> PickResponders(std::vector<Candidate> const& bots, Intent intent, Config const& cfg, uint64 seed);

    // Whether the level-up is one to congratulate (Config::CongratsEvery).
    TC_GAME_API bool CongratsLevel(uint32 level, Config const& cfg);

    // Whether a bot has something to say unprompted: its personal interval, 25-70 minutes by default, since it last spoke has passed.
    TC_GAME_API bool IdleDue(uint32 sinceSpokeSec, uint64 botKey, Config const& cfg);

    // --- bot guilds ----------------------------------------------------------------------------------------------------------------------

    struct BotInfo
    {
        uint64 Guid = 0;
        uint8 Faction = 0;               // 0 alliance, 1 horde
        uint32 Level = 1;
        bool Free = true;                // alive, out of combat, not in a dungeon or a group: may be moved
        bool InGuild = false;
    };

    struct GuildInfo
    {
        uint64 Id = 0;
        uint8 Faction = 0;
        uint32 Members = 0;
        bool BotLed = false;             // led by a world bot: only these are filled and counted
    };

    struct Action
    {
        enum Kind : uint8 { Found, Join } What = Join;
        uint64 Bot = 0;
        uint64 Guild = 0;                // Join: guild id
    };

    // Founds and joins for one check, at most AutoMaxActions. A free unguilded bot of at least AutoMinLevel joins the bot-led guild of its
    // faction with the fewest members and room left (AutoJoinPct); if none has room it founds one (AutoFoundPct) while fewer than AutoMaxGuilds
    // bot-led guilds exist and its level is at least AutoFoundMinLevel. `salt` changes with every check. Deterministic for equal input.
    TC_GAME_API std::vector<Action> PlanAuto(std::vector<BotInfo> const& bots, std::vector<GuildInfo> const& guilds, Config const& cfg, uint64 salt);

    // A guild name of 24 letters and spaces at most, fixed by the seed and the attempt (the glue asks again when the name is taken).
    TC_GAME_API std::string GuildName(uint64 seed, uint32 attempt);
}

#endif
