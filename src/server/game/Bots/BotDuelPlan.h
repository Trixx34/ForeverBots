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

#ifndef TRINITY_BOT_DUEL_PLAN_H
#define TRINITY_BOT_DUEL_PLAN_H

// Duels (Bot.Duel.*, BotDuel.cpp). A player can challenge a bot with the normal Duel spell and the bot answers like a person would: after a
// short pause it accepts or declines (busy, hurt, too far apart in level, just fought), says a line, fights to the end, and reacts to the
// result with an emote. A player can also whisper a bot "duel" and the bot walks up and challenges. Optionally bots challenge each other in
// town while a player is watching. Pure over plain data so it is unit tested without a map or a Player.
// See docs/playerbots/feature-bot-duels-20261008.md.

#include "Define.h"
#include <span>
#include <string_view>

namespace BotDuel
{
    struct Config
    {
        bool Enabled = false;
        uint32 AcceptPct = 90;             // chance that a free bot says yes to a player's challenge
        uint32 ReplyMinMs = 1500;          // the pause before the answer is a per-challenge value in this range
        uint32 ReplyMaxMs = 4500;
        uint32 MaxLevelGap = 5;            // declines when the levels differ by more than this (either way)
        uint32 MinHealthPct = 70;          // declines while hurt
        uint32 CooldownSec = 60;           // declines for this long after the last duel
        uint32 YieldPct = 0;               // 0 = fights to the end; else forfeits against a player below this health percent
        bool Chat = true;                  // says a line on accept, decline and result
        bool Challenge = true;             // answers a whispered "duel" by walking up and challenging the player
        float ChallengeYards = 8.0f;       // casts the challenge from within this distance of the player
        bool BotVsBot = false;             // bots challenge each other in cities
        uint32 BotIntervalSec = 240;       // least time between two looks for a sparring partner (per bot)
        uint32 BotChancePct = 35;          // chance per look that the bot challenges
        float BotSearchYards = 25.0f;      // partner must stand within this distance
        float BotWatchYards = 60.0f;       // a real player must be within this distance (when BotNeedWatcher)
        bool BotNeedWatcher = true;        // bot against bot duels only happen with a real player watching
        uint32 BotTimeoutSec = 240;        // a bot against bot duel that lasts longer ends: the bot that notices it first forfeits
    };

    // Answer to a challenge. Accept is the only yes; the rest name the reason of the no (log reason DUEL_DECLINED_<X>).
    enum class Verdict : uint8 { Accept, Dead, Dueling, Combat, Busy, Hurt, LevelGap, Cooldown, Chance };

    struct ChallengeFacts
    {
        bool Alive = true;
        bool Dueling = false;              // the bot is already in a duel (asked or running)
        bool InCombat = false;
        bool Busy = false;                 // in a group on a dungeon run, flying, in a battleground, mounted on a taxi ...
        uint32 HealthPct = 100;
        uint32 BotLevel = 1;
        uint32 OpponentLevel = 1;
        uint32 SinceLastDuelMs = 0xFFFFFFFFu;   // 0xFFFFFFFF = never
        bool OpponentIsBot = false;        // bot against bot: the sender already rolled the dice
        bool OwnAlt = false;               // an alt bot challenged by its owner: level, cooldown and mood do not matter
    };

    struct Answer
    {
        Verdict What = Verdict::Accept;
        uint32 DelayMs = 0;                // pause before the bot answers
    };

    // The reason code of a verdict (DUEL_ACCEPTED, DUEL_DECLINED_COMBAT ...).
    TC_GAME_API char const* VerdictCode(Verdict v);

    // Decision on a challenge: first matching reason wins, in the order dead, already dueling, in combat, busy, cooldown, hurt, level gap, mood.
    TC_GAME_API Answer Decide(Config const& cfg, ChallengeFacts const& f, uint64 botKey, uint32 nowMs);

    // Pause before the answer, within [ReplyMinMs, ReplyMaxMs], differs per bot and challenge.
    TC_GAME_API uint32 ReplyDelayMs(Config const& cfg, uint64 botKey, uint32 nowMs);

    enum class FightAct : uint8 { Fight, Yield, TimeOut };

    struct FightFacts
    {
        uint32 HealthPct = 100;
        uint32 ElapsedMs = 0;              // since the fight began
        bool OpponentIsBot = false;
    };

    // What a bot in a running duel does: keep fighting, forfeit because it is hurt (YieldPct, only against a player), or end a bot against bot
    // duel that goes nowhere (BotTimeoutSec).
    TC_GAME_API FightAct FightStep(Config const& cfg, FightFacts const& f);

    enum class Outcome : uint8 { Won, Lost };
    enum class Emote : uint8 { None, Bow, Salute, Cheer, Cry, Laugh };

    // Emote after the duel; the winner cheers, bows or salutes, the loser bows, salutes or cries now and then.
    TC_GAME_API Emote ReactionEmote(Outcome o, uint64 botKey, uint32 nowMs);

    enum class Line : uint8
    {
        Accept, Challenge, DeclineCombat, DeclineBusy, DeclineHurt, DeclineLevel, DeclineCooldown, DeclineChance, Win, Lose, Yield, TooFar,
        Count
    };

    // A line for the situation, always non-empty; the variant depends on the bot and the time.
    TC_GAME_API std::string_view PickLine(Line line, uint64 botKey, uint32 nowMs);

    // The line that goes with a verdict (Accept -> Line::Accept).
    TC_GAME_API Line LineFor(Verdict v);

    // True when a whisper asks for a duel: a short message with "duel", "spar", "1v1" or "fight me" in it and no negation.
    TC_GAME_API bool IsDuelRequest(std::string_view text);

    struct Partner
    {
        uint32 Level = 1;
        float Yards = 0.0f;                // distance to the asking bot
        uint32 HealthPct = 100;
        uint32 SinceLastDuelMs = 0xFFFFFFFFu;
        bool Free = true;                  // alive, not in combat, not in a duel, not busy
    };

    // The sparring partner a bot challenges: free, within BotSearchYards, level gap and mood as in Decide; the closest wins, a level
    // apart counts as 8 yards. -1 when nobody fits.
    TC_GAME_API int32 PickPartner(Config const& cfg, uint32 botLevel, std::span<Partner const> others, uint64 botKey, uint32 nowMs);

    // Whether a bot that is free looks for a sparring partner now (interval and chance).
    TC_GAME_API bool WantsSparring(Config const& cfg, uint32 sinceLookMs, bool watcherNear, uint64 botKey, uint32 nowMs);

    // Bot.Duel.*, read once (BotDuel.cpp).
    TC_GAME_API Config const& Cfg();
}

#endif
