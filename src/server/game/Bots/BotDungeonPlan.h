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

#ifndef TRINITY_BOT_DUNGEON_PLAN_H
#define TRINITY_BOT_DUNGEON_PLAN_H

// Dungeon groups of bots (Bot.AI.Dungeon.*): who goes into a five-man dungeon together, when the group is ready, which pack it pulls
// next, and where a run stands (gather, travel, clear, wipe recovery, done). Everything here is a pure function over plain data so it
// can be unit tested without a map, a database or a Player: a glue layer fills the structs from the game state and carries out the
// answer. See docs/playerbots/feature-bot-dungeon-groups-20261008.md.

#include "Define.h"
#include <span>
#include <vector>

namespace BotDungeon
{
    // --- roles ---
    enum class Role : uint8 { Tank, Healer, Dps };

    // Class ids as in ChrClasses (the core's Classes enum); the logic does not include the core headers.
    enum ClassId : uint8
    {
        CLS_WARRIOR = 1, CLS_PALADIN = 2, CLS_HUNTER = 3, CLS_ROGUE = 4, CLS_PRIEST = 5,
        CLS_SHAMAN = 7, CLS_MAGE = 8, CLS_WARLOCK = 9, CLS_DRUID = 11
    };

    constexpr uint32 RoleBit(Role r) { return 1u << uint32(r); }

    // Roles a class can fill at all (bit mask of RoleBit). Spec is not read: a warrior tanks, a priest heals, a druid or a paladin
    // can do all three, a shaman tanks no, heals and deals damage.
    TC_GAME_API uint32 RolesOfClass(uint8 classId);

    // The role a class prefers when nobody needs it elsewhere (warrior tank, priest healer, the rest damage).
    TC_GAME_API Role PreferredRole(uint8 classId);

    // --- composition ---
    struct Candidate
    {
        uint64 Guid = 0;
        uint8 ClassId = 0;
        uint8 Level = 1;
        bool Available = true;       // online as a bot, alive, not in a group, not in an instance or battleground
        bool Busy = false;           // in a quest, vendor or corpse run task that should not be interrupted
        float DistanceToLeader = 0;  // yards on the same map, or a large value on another map
        bool Leader = false;         // the player or bot that forms the group; always a member
    };

    struct Composition
    {
        uint32 Size = 5;
        uint32 Tanks = 1;
        uint32 Healers = 1;          // Size - Tanks - Healers places go to damage dealers
    };

    struct Member
    {
        uint64 Guid = 0;
        Role AsRole = Role::Dps;
    };

    struct ComposeConfig
    {
        uint32 MaxLevelSpread = 4;   // highest minus lowest level in the group
        uint32 MinLevel = 15;        // dungeon entry level (the dungeon may ask for more: Compose takes the larger)
        float MaxDistance = 1500.0f; // candidates farther than this from the leader are not called
        bool AllowBusy = false;
    };

    struct ComposeResult
    {
        std::vector<Member> Members;
        uint32 MissingTanks = 0;
        uint32 MissingHealers = 0;
        uint32 MissingDps = 0;
        bool Complete() const { return !MissingTanks && !MissingHealers && !MissingDps; }
    };

    // Picks the group from the candidates for a dungeon with the given level window. The leader is always in and fills the role
    // the composition needs most that the leader can fill. Tanks and healers are filled first (the scarce roles), the damage places
    // last; the level window is anchored on the leader. Among equals the nearer candidate wins, then the lower guid (stable).
    // A candidate is never used for two places. Missing places are counted so the caller can wait or fall back.
    TC_GAME_API ComposeResult Compose(std::span<Candidate const> candidates, Composition const& comp, uint32 dungeonMinLevel,
        uint32 dungeonMaxLevel, ComposeConfig const& cfg);

    // --- readiness ---
    struct MemberState
    {
        uint64 Guid = 0;
        Role AsRole = Role::Dps;
        bool Alive = true;
        bool InCombat = false;
        bool Present = true;         // within range of the leader and on the map
        int32 HealthPct = 100;
        int32 ManaPct = 100;         // 100 for classes without mana
        int32 DurabilityPct = 100;
        bool Eating = false;         // resting with food or drink
    };

    struct ReadyConfig
    {
        int32 MinHealthPct = 85;
        int32 MinManaPct = 70;
        int32 MinHealerManaPct = 85; // the healer starts the next pull with more mana
        int32 MinDurabilityPct = 20; // below this the group goes to repair first
    };

    enum class Ready : uint8
    {
        Go,              // everybody is ready
        WaitForMembers,  // somebody is missing, far away or dead
        Rest,            // health or mana too low: sit, eat and drink
        Repair,          // a member's gear is nearly broken
        InCombat         // somebody is fighting
    };

    TC_GAME_API Ready CheckReady(std::span<MemberState const> members, ReadyConfig const& cfg);

    // --- pulls ---
    struct Pack
    {
        uint32 Id = 0;
        uint32 Mobs = 1;
        uint32 Elites = 0;
        int32 MaxMobLevel = 1;
        float Distance = 0;          // path distance from the group
        bool Patrols = false;
        bool HasCaster = false;
        bool Boss = false;
        bool Done = false;           // already killed in this run
        bool Linked = false;         // pulls its neighbour when attacked (the glue marks packs closer than the social range)
    };

    struct PullConfig
    {
        uint32 MaxMobsPerPull = 4;   // a larger pack is only taken when it is a boss
        int32 MaxLevelOverGroup = 2; // mob level above the group's average level
        bool SkipPatrols = true;     // wait for a patrol to leave instead of pulling it
        bool BossNeedsFullGroup = true;
    };

    enum class PullKind : uint8
    {
        Pull,         // pull this pack
        Rest,         // not yet: the group is not ready (see PullChoice::Why)
        SkipAll,      // every pack left is too strong or unreachable: end the run
        Finished      // nothing left to kill
    };

    struct PullChoice
    {
        PullKind Kind = PullKind::Finished;
        uint32 PackId = 0;
        char const* Why = "";
    };

    // Next thing for the group to do: the nearest pack that is not done and not too strong, with trash cleared before a boss when
    // trash is left. `avgLevel` is the group's average level. Ready comes from CheckReady.
    TC_GAME_API PullChoice ChoosePull(std::span<Pack const> packs, int32 avgLevel, Ready ready, uint32 alive, uint32 groupSize,
        PullConfig const& cfg);

    // --- run ---
    enum class Phase : uint8
    {
        Gather,       // the members walk to the leader / the entrance
        Travel,       // to the entrance
        Clear,        // inside: pull, fight, rest
        Loot,         // after a boss: loot and split
        Recover,      // after a wipe: release, run back, regroup
        Done,         // finished, the group disbands or goes on to the next task
        Aborted       // given up, see RunResult::Why
    };

    struct RunFacts
    {
        Phase Current = Phase::Gather;
        uint32 NowMs = 0;
        uint32 StartedMs = 0;        // run start (0 = not started)
        uint32 PhaseSinceMs = 0;     // when the current phase began
        uint32 Wipes = 0;
        uint32 Alive = 5;
        uint32 Size = 5;
        uint32 Present = 5;          // members on the leader's map near the leader
        bool AtEntrance = false;
        bool Inside = false;         // the group is in the dungeon map
        bool BossKilled = false;     // the last boss is dead
        bool LootPending = false;    // corpses with loot left near the group
        bool PacksLeft = true;
        bool LeaderIsPlayer = false; // a player leads: bots follow, the run does not time out
    };

    struct RunConfig
    {
        uint32 GatherTimeoutMs = 300000;
        uint32 TravelTimeoutMs = 900000;
        uint32 RunTimeoutMs = 5400000;    // whole run: 90 minutes
        uint32 RecoverTimeoutMs = 600000;
        uint32 MaxWipes = 3;
    };

    struct RunResult
    {
        Phase Next = Phase::Gather;
        char const* Why = "";
    };

    // The next phase: the state machine of a run. Pure; the caller logs a change of phase.
    TC_GAME_API RunResult AdvanceRun(RunFacts const& f, RunConfig const& cfg);

    TC_GAME_API char const* PhaseName(Phase p);
    TC_GAME_API char const* RoleName(Role r);
}

#endif
