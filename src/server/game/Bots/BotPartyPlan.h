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

#ifndef TRINITY_BOT_PARTY_PLAN_H
#define TRINITY_BOT_PARTY_PLAN_H

// Bots forming their own parties out in the world (Bot.AI.Party.*): who joins whom, and when a party breaks up. Pure functions over plain
// data (no Player, no Map) so they are unit tested without a world; the glue is BotParty.cpp. Bots that are near each other, of a similar
// level and carry the same unfinished quest team up so that kills count for all of them; the party ends when the shared quest is done, the
// time is up, or a member is lost. See docs/playerbots/feature-bot-parties-20261008.md.

#include "Define.h"
#include <string>
#include <vector>

namespace BotParty
{
    struct Config
    {
        bool Enabled = false;
        uint32 MinSize = 2;           // smallest party (2-5)
        uint32 MaxSize = 3;           // largest party (2-5)
        float JoinRadius = 40.0f;     // yards between the seed bot and a joiner (2D)
        uint32 MaxLevelSpread = 3;    // highest level minus lowest level in a party
        uint32 MinSharedQuests = 1;   // unfinished quests two bots must have in common
        uint32 FormChancePct = 25;    // chance per check that a bot looks for company (0-100)
        uint32 LifetimeSec = 1200;    // longest life of a party; each party gets between 50 and 100 percent of it
        float LeashYards = 70.0f;     // a member farther than this from the leader counts as lost ...
        uint32 LeashSec = 45;         // ... after this many seconds
        uint32 LeaderGoneSec = 30;    // the party ends when its leader is dead or away this long
    };

    struct Candidate
    {
        uint64 Guid = 0;
        uint32 MapId = 0;
        uint32 Level = 1;
        float X = 0.0f, Y = 0.0f;
        std::vector<uint32> Quests;   // unfinished quests in the log (any order)
    };

    struct Formed
    {
        uint64 Leader = 0;
        std::vector<uint64> Members;  // without the leader
        uint32 SharedQuest = 0;       // the quest most of the party has in common
    };

    // Number of quest ids both lists hold (each list is read as a set).
    TC_GAME_API uint32 SharedCount(std::vector<uint32> const& a, std::vector<uint32> const& b);

    // Parties out of the free bots. `salt` changes with every check (the glue passes a counter): it decides which bots look for company
    // and keeps the same pair from always meeting the same way. Deterministic for equal input. A bot is in at most one party; the leader
    // is the highest level of a party (lower guid on a tie).
    TC_GAME_API std::vector<Formed> FormParties(std::vector<Candidate> candidates, Config const& cfg, uint32 salt);

    // Life of one party in seconds, 50-100 percent of cfg.LifetimeSec, fixed by the leader and the salt.
    TC_GAME_API uint32 LifetimeFor(Config const& cfg, uint64 leaderGuid, uint32 salt);

    struct MemberState
    {
        uint64 Guid = 0;
        bool Present = true;          // still in the group and in the world
        bool Alive = true;            // a dead member is dropped at once (its corpse run belongs to the quest AI again)
        bool SameMap = true;
        uint32 FarSec = 0;            // seconds in a row beyond Config::LeashYards of the leader (the glue counts it)
        bool SharesQuest = true;      // still has an unfinished quest in common with the leader
        uint32 Level = 1;
    };

    struct PartyState
    {
        uint32 AgeSec = 0;
        uint32 LifetimeSec = 0;
        bool LeaderPresent = true;    // online and in the group (false ends the party at once)
        uint32 LeaderGoneSec = 0;     // seconds in a row the leader was dead, away (the glue counts it)
        uint32 LeaderLevel = 1;
        std::vector<MemberState> Members;   // without the leader
    };

    struct Verdict
    {
        bool Disband = false;
        std::string Reason;                       // why the party ends ("" while it stays)
        std::vector<uint64> Drop;                 // members to remove while the party stays
        std::vector<std::string> DropReasons;     // parallel to Drop
    };

    // Reasons: "lifetime", "leader_gone", "too_small" for the party; "gone", "dead", "quest_done", "lost", "level" for a member.
    TC_GAME_API Verdict Evaluate(PartyState const& st, Config const& cfg);
}

#endif
