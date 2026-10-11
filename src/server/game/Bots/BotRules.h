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

#ifndef TRINITY_BOT_RULES_H
#define TRINITY_BOT_RULES_H

// Settings and shared state of the additional rules N-01 to N-09 (Bot.Rules.*, Bot.Log.Diet.*; docs/playerbots/additional-rules-20261011.md).
// The decisions are in BotProgressRules.h (pure, unit tested); the glue is in the modules the rules act on.

#include "BotProgressRules.h"
#include "Define.h"
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace BotRules
{
    struct Settings
    {
        uint32 ArmPct = 0;              // Bot.Rules.ArmPct: 0 = every bot follows the rules; N = only bots with guid % 100 < N do (A/B run)

        bool XpStall = true;            // Bot.Rules.XpStall.Enabled (N-01)
        bool XpStallRelog = true;       // Bot.Rules.XpStall.Relog
        BotRule::StallConfig Stall;

        bool ZoneFence = true;          // Bot.Rules.ZoneFence.Enabled (N-02)
        int32 FenceMargin = 4;          // Bot.Rules.ZoneFence.Margin
        float FenceStepYd = 150.0f;     // Bot.Rules.ZoneFence.StepYards
        uint32 FenceMaxPoints = 12;

        bool PullGate = true;           // Bot.Rules.PullGate.Enabled (N-03)
        BotRule::PullConfig Pull;

        bool NoDamage = true;           // Bot.Rules.NoDamage.Enabled (N-04)
        BotRule::NoDamageConfig NoDmg;

        bool BagPressure = true;        // Bot.Rules.BagPressure.Enabled (N-05)
        BotRule::BagConfig Bag;

        bool Crowd = true;              // Bot.Rules.Crowd.Enabled (N-06)
        uint32 CrowdCap = 8;
        float CrowdRadius = 150.0f;
        float CrowdFactor = 0.35f;

        bool ZoneLeave = true;          // Bot.Rules.ZoneLeave.Enabled (N-07)
        BotRule::ZoneLeaveConfig Leave;

        bool ClassMargin = false;       // Bot.Rules.ClassMargin.Enabled (N-08)
        uint32 ClassMarginUntil = 10;
        int32 ClassMarginWeak = 0;
        int32 ClassMarginSturdy = 1;

        bool BagFree = true;            // Bot.Rules.BagFree.Enabled (R-V2-04)
        BotRule::BagFreeConfig BagFreeCfg;
        bool BagDestroy = true;         // Bot.Rules.BagFree.Destroy.Enabled
        uint32 BagDestroyMaxCopper = 50; // Bot.Rules.BagFree.DestroyMaxCopper

        bool DeathLoop = true;          // Bot.Rules.DeathLoop.Enabled (R-V2-05)
        BotRule::DeathLoopConfig DeathLoopCfg;

        bool FleeEscape = true;         // Bot.Rules.FleeEscape.Enabled (R-V2-06)
        BotRule::FleeEscapeConfig Escape;

        bool TargetPick = true;         // Bot.Rules.TargetPick.Enabled (R-V2-07)
        BotRule::TargetPickConfig Pick;

        bool QuestFilter = true;        // Bot.Rules.QuestFilter.Enabled (R-V2-08)
        std::vector<uint32> QuestBlocked;   // Bot.Rules.QuestFilter.BlockedQuests
        bool QuestFilterObjective = true;   // Bot.Rules.QuestFilter.UnsupportedObjectives
        uint32 QuestLevelSlack = 0;         // Bot.Rules.QuestFilter.LevelSlack: offered when MinLevel <= bot level + slack
        uint32 QuestGlobalSec = 3600;       // Bot.Rules.QuestFilter.GlobalQuarantineSec: how long a globally dead quest stays skipped
        uint32 QuestLocalFails = 3;         // Bot.Rules.QuestFilter.LocalFails: failures of one bot before it quarantines the quest

        bool LogDiet = true;            // Bot.Log.Diet.Enabled (N-09)
        std::vector<std::pair<std::string, uint32>> HourlyCaps;
        bool AuraDeathOnly = true;      // Bot.Log.Diet.AuraDeathOnly
    };

    TC_GAME_API Settings const& Cfg();

    // True when the rules apply to this bot (always, unless Bot.Rules.ArmPct splits the bots into a rule arm and a control arm).
    TC_GAME_API bool Applies(uint64 guidCounter);
    TC_GAME_API bool Applies(Player const* bot);

    // Shared state: one stall clock per bot, and the crowd claims of all bots.
    TC_GAME_API BotRule::StallBook& Stalls();
    TC_GAME_API BotRule::CrowdBook& Crowds();
    // R-V2-05: entries many bots had to block after a death loop.
    TC_GAME_API BotRule::ShunBook& Shunned();

    // N-02: the zone at (x, y, z) on the bot's map is above the bot's band (its minimum level is more than Margin above the bot).
    // Fills the zone id and band when it says true. False when the rule is off, the bot is exempt or the zone is not a leveling zone.
    TC_GAME_API bool Fenced(Player* bot, float x, float y, float z, uint32& zone, uint32& minLevel);
    // N-02 for a whole leg: samples the line from the bot to (x, y, z) too. Fills the first fenced zone found.
    TC_GAME_API bool LegFenced(Player* bot, float x, float y, float z, uint32& zone, uint32& minLevel);
}

#endif
