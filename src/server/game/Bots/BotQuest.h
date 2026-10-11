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

#ifndef TRINITY_BOT_QUEST_H
#define TRINITY_BOT_QUEST_H

// Bot questing ("quest" strategy, NonCombat engine): pick a quest by level and position, walk to the giver, accept, work the
// objectives (kill / loot / talk), turn in with a reward choice. Every blocked case is logged as quest_blocked with a stable
// reason code (docs/playerbots/quest-design.md section 6.2). See docs/playerbots/progress.md "Quest pipeline".
//
// The static data (starter grid, spawn points, kill-credit map, quest-item drop sources) is built once on the world thread by
// EnsureIndex() and is read-only afterwards, so map threads never query the database.

#include "Define.h"
#include <string>

class BotAI;
class Player;
class Quest;
struct ItemTemplate;

namespace BotQuest
{
    // World thread, before any bot AI exists (called from BotMgr). Idempotent. One synchronous query for the quest-item loot
    // tables, everything else comes from the ObjectMgr caches.
    TC_GAME_API void EnsureIndex();
    TC_GAME_API bool IsReady();
    // Returns a missing faction ID used by a quest's reputation prerequisite, or 0 when both IDs are valid.
    TC_GAME_API uint32 GetUnknownReputationFaction(Quest const* quest);
    // One-line summary of the bot's current quest task (console / diagnostics). Map thread or a quiet world thread.
    TC_GAME_API std::string DescribeTask(BotAI* ai);
    // BotAI::OnLogout: gives back the resource claims the bot's quest task holds. Map thread.
    TC_GAME_API void OnLogout(BotAI* ai);
    // BotAI::OnDying: remembers where a bot died, so the improved loot task (Bot.AI.Loot.Improved.*) keeps away from that area for a while.
    TC_GAME_API void NoteDeath(Player* bot);
    // True while the bot has nothing to quest for and grinds mobs instead (the travel module reads it as "this zone is done").
    TC_GAME_API bool IsGrinding(BotAI* ai);
    // Current task for logging: quest id (0 = none) and a short task name ("kill", "grind", "trip_vendor", ...; "none" when idle or the
    // strategy is not active). Map thread.
    TC_GAME_API void CurrentTask(BotAI* ai, uint32& questId, char const*& task);
    // Per-state time ledger buckets: CurrentActivity returns an index below ACTIVITY_COUNT, ActivityName its stable name.
    constexpr uint32 ACTIVITY_COUNT = 16;
    TC_GAME_API uint32 CurrentActivity(BotAI* ai);
    TC_GAME_API char const* ActivityName(uint32 index);
    // Position of the nearest quest hub in a zone of the bot's map that has quests for the bot's level; false when there is none.
    // True when `item` would replace worn gear in its best slot by the class-role gear score (BotGear.h). For group loot rolls.
    TC_GAME_API bool IsGearUpgrade(Player* bot, ItemTemplate const* item);
    TC_GAME_API bool FindHubInZone(Player* bot, uint32 zoneId, float& x, float& y, float& z);

    // What the combat loop guard (BotCombat approach escalation, R-V2-02) needs to know about the gear: the main hand holds a profession
    // tool or nothing, and a ranged weapon has no ammo left.
    struct GearInspect
    {
        uint32 MainEntry = 0;
        bool MainNeedsWeapon = false;
        bool RangedNeedsAmmo = false;
    };
    TC_GAME_API GearInspect InspectCombatGear(Player* bot);
    // Swaps the best combat weapon of the bags into the main hand when it holds a profession tool (R-V2-01). True when it swapped.
    TC_GAME_API bool RestoreCombatWeapon(BotAI* ai, Player* bot, char const* why);
    // Plans a vendor visit that buys ammo at once (needs Bot.AI.Ammo.Enabled). False when ammo trips are off.
    TC_GAME_API bool RequestAmmoTrip(BotAI* ai);
    // The quest and the kind of task the bot works on right now, for the death and combat start log rows: "quest", "grind", "trip" or "idle".
    TC_GAME_API void ActiveTask(BotAI* ai, uint32& questId, std::string& task);
    // N-01 (XP stall): the latest ten quest layer rows as a JSON array, for the STALL_XP row.
    TC_GAME_API std::string RecentDecisions(BotAI* ai);
    // N-07: BotAI::OnXpGain, a kill gave `xp`. Ten minutes of grind kills below Bot.Rules.ZoneLeave.MinXpPerKill ask the travel module to leave the zone.
    TC_GAME_API void NoteKillXp(BotAI* ai, Player* bot, uint32 xp);
    // N-01 stage 1: drops the task and every blacklist, ignore list and trip guard of the quest layer, so the planners choose afresh.
    TC_GAME_API void ClearForStall(BotAI* ai, Player* bot);
    // N-01 stage 3: starts a walk to the best quest hub on the bot's map for its level. False when no hub fits.
    TC_GAME_API bool SendToHub(BotAI* ai, Player* bot);
}

#endif
