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

#ifndef TRINITY_BOT_DUNGEON_DATA_H
#define TRINITY_BOT_DUNGEON_DATA_H

// Static data of the dungeons the bot groups run (Bot.AI.Dungeon.*) and the clustering of a dungeon's creature spawns into packs.
// Pure: no map, no database. BotDungeonRun.cpp reads the spawns of the instance map from the world database, calls ClusterPacks once per
// dungeon and caches the result. See docs/playerbots/feature-bot-dungeon-runs-20261008.md.

#include "Define.h"
#include <span>
#include <string_view>
#include <vector>

namespace BotDungeon
{
    struct DungeonInfo
    {
        char const* Key;         // lowercase short name, the first alias
        char const* Aliases;     // space separated lowercase aliases
        char const* Name;
        uint32 MapId;
        uint8 MinLevel;
        uint8 MaxLevel;
        uint32 FinalBoss;        // creature entry whose death ends the run (0 = the run ends when every pack is dead)
    };

    // The five-man dungeons of Classic, lowest level first.
    TC_GAME_API std::span<DungeonInfo const> Dungeons();

    // By key, alias, name (case-insensitive, spaces and apostrophes ignored) or map id as text. nullptr when unknown.
    TC_GAME_API DungeonInfo const* FindDungeon(std::string_view text);

    // The dungeons a group of this average level can run (MinLevel - slack <= level <= MaxLevel), best fit (closest to the middle of the
    // range) first.
    TC_GAME_API std::vector<DungeonInfo const*> DungeonsForLevel(int32 level, int32 slackLow = 0);

    struct SpawnPoint
    {
        uint64 SpawnId = 0;
        uint32 Entry = 0;
        float X = 0, Y = 0, Z = 0;
        int32 Level = 1;
        bool Elite = false;
        bool Boss = false;       // dungeon boss (creature flag)
        bool Patrol = false;     // waypoint movement
    };

    struct PackSpec
    {
        uint32 Id = 0;           // 1-based, stable for a given spawn list
        std::vector<SpawnPoint> Points;
        float X = 0, Y = 0, Z = 0;   // centroid
        uint32 Mobs = 0;
        uint32 Elites = 0;
        int32 MaxLevel = 1;
        bool Boss = false;
        bool Patrols = false;
        bool HasFinalBoss = false;
    };

    // Groups spawns that stand within `linkRadius` yards of each other (3D distance, chained). A group of more than `maxPack` spawns
    // that holds no boss is cut into smaller packs (seed: the spawn with the lowest x then y, members: those within linkRadius of the
    // seed, nearest first). Packs are numbered in order of their lowest spawn id so the numbering does not depend on input order.
    TC_GAME_API std::vector<PackSpec> ClusterPacks(std::span<SpawnPoint const> spawns, float linkRadius, uint32 maxPack, uint32 finalBossEntry = 0);
}

#endif
