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

#ifndef TRINITY_BOT_REPUTATION_PLAN_H
#define TRINITY_BOT_REPUTATION_PLAN_H

// Reputation weight of a quest for the quest picker (Bot.Quest.Reputation.*, BotQuest.cpp). Pure over plain data so it is unit tested
// without a Player. See docs/playerbots/feature-bot-reputation-20261008.md.

#include "Define.h"
#include <span>

namespace BotReputation
{
    // Standing thresholds of the reputation ranks (absolute standing, Hated .. Exalted)
    constexpr int32 StandingUnfriendly = -3000;
    constexpr int32 StandingExalted = 42000;
    constexpr int32 RankRevered = 6;
    constexpr int32 RankExalted = 7;

    struct Config
    {
        int32 BonusPct = 25;          // a quest with a full reputation gain scores this many percent higher
        int32 LossPenaltyPct = 40;    // a quest with a full reputation loss scores this many percent lower
        int32 FullPoints = 250;       // a reward of this many points (or more) counts as a full gain or loss
    };

    struct Reward
    {
        int32 Amount = 0;             // reputation points the quest hands out, negative = takes away
        int32 Standing = 0;           // current absolute standing of the bot with that faction
        int32 Rank = 3;               // current rank, 0 Hated .. 7 Exalted
        int32 CapRank = 0;            // the quest gives nothing once the bot reaches this rank, 0 = no cap
        bool OtherSide = false;       // faction of the opposing side, the core skips it for this player
    };

    // Multiplier for the quest score: 0 when the quest would push the bot into Hostile or worse with a faction, above 1 for
    // gains that still count (not capped, not Exalted; Revered counts half), below 1 for losses. Same for all rewards of one quest.
    TC_GAME_API float QuestWeight(std::span<Reward const> rewards, Config const& cfg);
}

#endif
