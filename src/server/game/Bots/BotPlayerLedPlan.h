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

#ifndef TRINITY_BOT_PLAYER_LED_PLAN_H
#define TRINITY_BOT_PLAYER_LED_PLAN_H

// Dungeon runs led by a player (Bot.AI.Dungeon.PlayerLed.*): a real player leads a group of bots into a five-man dungeon and the bots
// that follow the player keep up with them: they come in through the entrance after the leader, catch up when they are stuck far behind,
// are raised after a fight and leave with the leader. Everything here is a pure function over plain data so it can be unit tested without
// a map, a database or a Player; BotPlayerLed.cpp fills the structs and carries out the answer.
// See docs/playerbots/feature-bot-player-led-dungeon-20261008.md.

#include "BotDungeonPlan.h"
#include "Define.h"
#include <span>
#include <vector>

namespace BotPlayerLed
{
    struct Config
    {
        uint32 EnterDelayMs = 3000;      // the leader has been on its map this long before bots are brought along (map loading, the
                                         // leader may step back out)
        float CatchUpYards = 60.0f;      // inside a dungeon a bot farther than this from the leader counts as left behind
        uint32 CatchUpMs = 20000;        // ... for this long (walking time is not stuck time)
        uint32 RaiseDelayMs = 10000;     // a dead bot is raised this long after the fight; 0 = never raised
        uint32 ActCooldownMs = 15000;    // minimum time between two moves/raises of one bot
        uint32 NoticeRepeatMs = 60000;   // the leader hears about resting bots again after this long
    };

    // What the glue does with a bot.
    enum class Act : uint8
    {
        None,
        Enter,     // teleport next to the leader, who is inside a dungeon the bot is not in
        Leave,     // teleport next to the leader, who left the dungeon the bot is still in
        CatchUp,   // teleport next to the leader inside the dungeon
        Raise      // resurrect next to the leader (the stand-in for a healer's resurrection and the corpse run)
    };

    struct Facts
    {
        bool Follows = false;            // the bot's follow target is the leader (a bot ordered to stay or go elsewhere is left alone)
        bool BotAlive = true;
        bool BotInCombat = false;
        bool LeaderAlive = true;
        bool LeaderInCombat = false;
        bool LeaderInDungeon = false;    // the leader's map is a five-man dungeon
        bool LeaderOnWorldMap = false;   // the leader's map is a plain world map (no dungeon, battleground or arena)
        bool BotInDungeon = false;
        bool BotInInstanceOfLeader = false;  // same map id and same instance id as the leader
        float Distance = 0.0f;           // yards to the leader, only meaningful with BotInInstanceOfLeader
        uint32 LeaderMapSinceMs = 0;     // how long the leader has been on its current map instance
        uint32 FarForMs = 0;             // how long the bot has been farther than CatchUpYards (0 = not far)
        uint32 DeadForMs = 0;            // how long the bot has been dead (0 = alive)
        uint32 SinceActMs = 0xFFFFFFFFu; // since the last Act on this bot
    };

    struct Decision
    {
        Act What = Act::None;
        char const* Why = "";
    };

    TC_GAME_API Decision Decide(Facts const& f, Config const& cfg);

    TC_GAME_API char const* ActName(Act a);

    // Placement of the index-th bot of a move next to the leader, so the bots do not stand on one point: a ring of bots around the leader.
    TC_GAME_API void Spread(uint32 index, float& dx, float& dy);

    // --- ready notice ---
    // Between pulls the leader should know why the bots do not come along or fight: they rest. The glue reads the bots' states (the leader
    // is not in the list) and tells the leader when the set of bots that need a rest changes.
    enum class NeedWhy : uint8 { Dead, Away, Health, Mana };

    struct Need
    {
        uint64 Guid = 0;
        NeedWhy Why = NeedWhy::Health;
        int32 Value = 0;                 // health or mana percent
    };

    // Bots that are not ready for the next pull (same limits as the bot-led run, BotDungeon::ReadyConfig), most urgent first: dead, away,
    // health, mana; ties by guid. A healer's mana is held to MinHealerManaPct. Eating members are listed (they are on their way).
    TC_GAME_API std::vector<Need> ListNeeds(std::span<BotDungeon::MemberState const> bots, BotDungeon::ReadyConfig const& cfg);

    enum class Notice : uint8 { None, Resting, Ready };

    // Say something to the leader: when the group is out of combat and bots need a rest, once at the start and again every
    // `repeatMs`; once more when everybody is ready again. `wasNeedy` is the state at the last call.
    TC_GAME_API Notice NoticeDecision(bool wasNeedy, bool needy, bool leaderInCombat, uint32 sinceLastNoticeMs, uint32 repeatMs);

    TC_GAME_API char const* NeedText(NeedWhy w);
}

#endif
