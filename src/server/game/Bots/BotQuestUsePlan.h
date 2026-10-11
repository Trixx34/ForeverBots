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

#ifndef TRINITY_BOT_QUEST_USE_PLAN_H
#define TRINITY_BOT_QUEST_USE_PLAN_H

// "Use this item on that creature" quests (Bot.Quest.UseItems): the quest hands over (or asks for) an item whose use spell needs a
// target, and the objective is credited for the creature. The step decision of the quest AI is a pure function over plain data so it can
// be unit tested without a world; BotQuest.cpp fills the facts and carries the answer out.
// See docs/playerbots/feature-bot-escort-group-quests-20261008.md.

#include "Define.h"

namespace BotQuestUse
{
    struct Config
    {
        uint32 MaxCasts = 4;          // casts on one target without credit before the target is given up
        uint32 RetryMs = 2500;        // wait between two casts (the credit may arrive a moment after the spell)
        uint32 TargetTimeoutMs = 60000;   // longest time spent on one target
        float MinRange = 3.0f;        // cast range is clamped to [MinRange, MaxRange]
        float MaxRange = 30.0f;
    };

    // What the glue knows about the item's use spell.
    struct SpellFacts
    {
        bool OnUse = false;               // the item effect is an "on use" trigger
        bool NeedsUnitTarget = false;     // the spell needs an explicitly chosen unit
        float MaxRange = 0.0f;            // yards, 0 = unknown
    };

    // True for an item that is used on a creature.
    TC_GAME_API bool IsTargetItem(SpellFacts const& s);

    // Distance to cast from: 90 percent of the spell range, inside [MinRange, MaxRange]; MaxRange * 0.5 when the range is unknown.
    TC_GAME_API float CastRange(SpellFacts const& s, Config const& cfg);

    enum class Step : uint8
    {
        Approach,   // walk closer (or into line of sight)
        Cast,       // use the item on the target now
        Wait,       // a cast is running, the last cast is recent, or the bot fights something else
        Done,       // the objective got its credit
        Abort       // give this target up
    };

    struct Facts
    {
        bool TargetAlive = true;
        float Distance = 0.0f;            // yards to the target
        float Range = 0.0f;               // CastRange()
        bool LineOfSight = true;
        bool Casting = false;             // the bot is casting or channelling
        bool BotInCombat = false;
        bool CreditGained = false;        // the objective count rose since the target was picked
        uint32 ElapsedMs = 0;             // since the target was picked
        uint32 Casts = 0;                 // casts on this target
        uint32 SinceCastMs = 0xFFFFFFFFu; // since the last cast, 0xFFFFFFFF = none yet
    };

    struct Decision
    {
        Step What = Step::Wait;
        char const* Why = "";
    };

    TC_GAME_API Decision Decide(Facts const& f, Config const& cfg);

    TC_GAME_API char const* StepName(Step s);
}

#endif
