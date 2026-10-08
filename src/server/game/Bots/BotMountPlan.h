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

#ifndef TRINITY_BOT_MOUNT_PLAN_H
#define TRINITY_BOT_MOUNT_PLAN_H

// Mounts for bots that follow a player (Bot.AI.Mount.*, BotMount.cpp). A bot that follows a mounted leader mounts too, so it keeps up;
// a bot that has fallen far behind a leader on foot mounts to catch up; a mounted bot dismounts when the leader is on foot again and
// close, and always when a fight starts. A mount the player ordered in chat (`mount`) is left to the player. Pure over plain data so it
// is unit tested without a map or a Player; BotMount.cpp fills the structs and carries out the answer.
// See docs/playerbots/feature-bot-mounts-20261008.md.

#include "BotChat.h"
#include "Define.h"
#include <span>

namespace BotMount
{
    using BotChat::MountOption;

    struct Config
    {
        bool Enabled = false;
        uint32 MountDelayMs = 1500;      // the leader has been mounted this long before the bot follows suit (no flicker on a quick hop)
        uint32 DismountDelayMs = 2500;   // the leader has been on foot this long before the bot gets off
        float DismountYards = 15.0f;     // a bot only gets off once it is this close to the leader on foot (a far bot keeps riding)
        float CatchUpYards = 60.0f;      // a bot farther than this from a leader on foot counts as left behind
        uint32 CatchUpMs = 8000;         // ... for this long (a walking pause is not being left behind)
        uint32 ActCooldownMs = 4000;     // least time between two mount or dismount attempts of one bot
        uint32 FailBackoffMs = 30000;    // after a refused mount (indoors, no space, forbidden here) the bot waits this long
    };

    enum class Act : uint8 { None, Mount, Dismount };

    struct Facts
    {
        bool Follows = false;            // the bot's follow target is a player on its map
        bool BotAlive = true;
        bool BotInCombat = false;
        bool BotBusy = false;            // casting, on a taxi, being teleported
        bool BotMounted = false;
        bool Ordered = false;            // the current mount was ordered by a player in chat
        bool HasMount = false;           // the bot knows a ground mount spell
        bool CanMountHere = true;        // outdoors, not in a dungeon, a battleground or the water
        bool LeaderInCombat = false;
        bool LeaderMounted = false;
        uint32 LeaderMountedMs = 0;      // how long the leader has been mounted (0 = not)
        uint32 LeaderOnFootMs = 0;       // how long the leader has been on foot (0 = mounted)
        float Distance = 0.0f;           // yards to the leader
        uint32 FarForMs = 0;             // how long the bot has been farther than CatchUpYards from the leader
        uint32 SinceActMs = 0xFFFFFFFFu; // since the last attempt
        uint32 SinceFailMs = 0xFFFFFFFFu; // since the last refused mount
    };

    struct Decision
    {
        Act What = Act::None;
        char const* Why = "";
    };

    TC_GAME_API Decision Decide(Facts const& f, Config const& cfg);

    // Index of the ground mount to use: the slowest one that is at least as fast as the leader's (percent over the walking speed,
    // `wantSpeed` <= 0 = unknown), else the fastest. Lower spell id on a tie. Flying mounts are skipped. -1 when there is none.
    TC_GAME_API int32 PickMatching(std::span<MountOption const> options, int32 wantSpeed);

    // Percent over the walking speed that a player moves at, from its run speed rate (1.0 = walking pace); never negative.
    TC_GAME_API int32 SpeedPctFromRate(float rate);

    // Bot.AI.Mount.*, read once (BotMount.cpp).
    TC_GAME_API Config const& Cfg();
}

#endif
