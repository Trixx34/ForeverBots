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

#ifndef TRINITY_BOT_QUEST_ASSIST_PLAN_H
#define TRINITY_BOT_QUEST_ASSIST_PLAN_H

// Bots that help a player with group and escort quests (Bot.Quest.Assist.*): when a real player carries a quest that is made for
// several people, idle bots nearby join the player's party, share the quest, follow the player and, on an escort, fight whatever attacks
// the escorted NPC. Everything here is a pure function over plain data (no Player, no Map) so it can be unit tested without a world;
// BotQuestAssist.cpp fills the structs and carries out the answer.
// See docs/playerbots/feature-bot-escort-group-quests-20261008.md.

#include "Define.h"
#include <string>
#include <vector>

namespace BotQuestAssist
{
    struct Config
    {
        bool Enabled = false;
        uint32 MaxHelpers = 2;        // bots that join one player (1-4)
        float RecruitRadius = 60.0f;  // yards between the player and a bot that may be asked
        uint32 MaxLevelGap = 4;       // levels a helper may differ from the player, up or down
        uint32 MinHelperLevel = 5;
        uint32 LifetimeSec = 1800;    // longest help for one quest; the player may ask again afterwards
        float LeashYards = 80.0f;     // a helper farther than this from the player counts as lost ...
        uint32 LeashSec = 45;         // ... after this many seconds
        uint32 DeadSec = 60;          // a helper dead this long is let go (its corpse run belongs to the quest AI again)
        uint32 DoneGraceSec = 30;     // the party lasts this long after the quest is done or abandoned (turn-in walk)
        uint32 PlayerGoneSec = 30;    // ... and ends when the player is dead or away this long
        float GuardRadius = 45.0f;    // escort guard: a helper only takes an attacker closer than this
    };

    enum class Kind : uint8 { None, Group, Escort };

    // Plain facts about a quest in the player's log.
    struct QuestFacts
    {
        uint32 SuggestedPlayers = 0;
        bool Escort = false;          // quest info "escort"
        bool GroupInfo = false;       // quest info "group"
        bool Excluded = false;        // dungeon, raid, PvP and world event quests: other systems own them
    };

    // Escort beats group; excluded and plain quests are None. A group quest is one for two or more players (suggested number) or tagged group.
    TC_GAME_API Kind Classify(QuestFacts const& q);

    // How many bots the player should get. `groupSize` counts the player and everybody already in the party. A group quest wants
    // SuggestedPlayers - 1 companions (at most MaxHelpers), an escort wants MaxHelpers; people already in the party count. Never more
    // than the free places of a five-man party. 0 when nothing is missing.
    TC_GAME_API uint32 HelpersWanted(Kind kind, uint32 suggestedPlayers, uint32 groupSize, Config const& cfg);

    struct PlayerFacts
    {
        uint32 MapId = 0;
        uint32 Level = 1;
        float X = 0.0f, Y = 0.0f;
    };

    struct Candidate
    {
        uint64 Guid = 0;
        uint32 MapId = 0;
        uint32 Level = 1;
        float X = 0.0f, Y = 0.0f;
    };

    // Up to `wanted` bots of the candidates: same map, inside RecruitRadius, level inside MaxLevelGap and at least MinHelperLevel. Closest
    // level first, then closest, then lowest guid, so equal input gives equal output.
    TC_GAME_API std::vector<uint64> PickHelpers(PlayerFacts const& player, std::vector<Candidate> const& candidates, uint32 wanted, Config const& cfg);

    // --- life of one help session ---
    struct HelperState
    {
        uint64 Guid = 0;
        bool Present = true;          // still in the player's group and in the world
        bool Alive = true;
        uint32 DeadSec = 0;           // seconds in a row dead
        bool SameMap = true;
        uint32 FarSec = 0;            // seconds in a row beyond LeashYards of the player (or on another map)
    };

    struct SessionState
    {
        uint32 AgeSec = 0;
        uint32 LifetimeSec = 0;
        bool PlayerPresent = true;    // online and in the world
        uint32 PlayerGoneSec = 0;     // seconds in a row the player was dead, away or offline
        bool QuestOpen = true;        // the quest is still unfinished in the player's log
        uint32 QuestClosedSec = 0;    // seconds in a row it has not been open
        std::vector<HelperState> Helpers;
    };

    struct Verdict
    {
        bool End = false;
        std::string Reason;                       // why the session ends ("" while it goes on)
        std::vector<uint64> Drop;                 // helpers to let go while it goes on
        std::vector<std::string> DropReasons;     // parallel to Drop
    };

    // Session reasons: "player_gone", "quest_done", "lifetime", "no_helpers". Helper reasons: "gone" (left or was removed from the
    // group: the glue reads it as the player sending the bot away), "dead", "lost".
    TC_GAME_API Verdict Evaluate(SessionState const& st, Config const& cfg);

    // --- escort guard ---
    // Creatures that attack the escorted NPC and the helpers who can take them on.
    struct Attacker
    {
        uint64 Guid = 0;
        bool OnEscort = false;        // its victim is the escorted NPC (not a helper or the player)
        float EscortDist = 0.0f;      // yards to the escorted NPC
        float X = 0.0f, Y = 0.0f;
    };

    struct Guard
    {
        uint64 Guid = 0;
        bool Free = true;             // alive and not busy with another fight
        float X = 0.0f, Y = 0.0f;
    };

    struct Assignment
    {
        uint64 Guard = 0;
        uint64 Target = 0;
    };

    // One attacker per free helper: attackers on the escort first, then the closest to the escort. Each helper takes a close attacker
    // (inside GuardRadius) and the attackers are shared out evenly before one gets a second helper. Deterministic.
    TC_GAME_API std::vector<Assignment> AssignGuards(std::vector<Attacker> attackers, std::vector<Guard> guards, Config const& cfg);

    TC_GAME_API char const* KindName(Kind k);
}

#endif
