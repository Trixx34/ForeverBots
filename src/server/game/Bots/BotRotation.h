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

#ifndef TRINITY_BOT_ROTATION_H
#define TRINITY_BOT_ROTATION_H

// Conditions of the class spell rotations of the bots (Bot.AI.Rotation.*). A rotation row of BotCombat.cpp carries a Cond and a
// parameter; Allowed() answers whether the row may be cast now from plain facts about the bot and its target. Pure function over plain
// data so it is unit tested without a map or a Player: BotCombat.cpp fills the Facts from the game state.
// See docs/playerbots/feature-bot-class-rotations-20261008.md.

#include "Define.h"

namespace BotRotation
{
    enum class Cond : uint8
    {
        Always,
        TargetHpAbove,     // Param: percent. Damage over time and debuffs are not worth casting on a mob about to die
        TargetHpBelow,     // Param: percent. Execute-type finishers
        SelfHpBelow,       // Param: percent. Emergency heals and defensive cooldowns
        SelfPowerBelow,    // Param: percent of the main power (Bloodrage, Life Tap with the health check below, Evocation)
        SelfPowerAbove,    // Param: percent. Spend only with a reserve (optional spells of casters)
        EnemiesAtLeast,    // Param: count of hostile units attacking the bot. Area attacks
        TargetCasting,     // interrupts
        TargetFleeing,     // snares and roots
        Opener,            // the first Param seconds of the fight (Charge, Hunter's Mark, curse)
        TargetStrong,      // the target is an elite or Param or more levels above the bot: use cooldowns and debuffs on it
        LifeTapSafe        // Life Tap: health above 60 percent and mana below Param percent
    };

    struct Facts
    {
        int32 SelfHpPct = 100;
        int32 SelfPowerPct = 100;      // mana, rage or energy percent of the main power
        int32 TargetHpPct = 100;
        uint32 EnemiesOnBot = 1;       // hostile units attacking the bot (the target included)
        bool TargetCasting = false;
        bool TargetFleeing = false;
        bool TargetElite = false;
        int32 TargetLevelDiff = 0;     // target level minus bot level
        uint32 FightMs = 0;            // time since the fight began
        bool Enabled = true;           // Bot.AI.Rotation.Enabled; with it off only Cond::Always rows pass (the old rotation)
    };

    struct Rule
    {
        Cond Condition = Cond::Always;
        int32 Param = 0;
    };

    TC_GAME_API bool Allowed(Rule const& rule, Facts const& f);

    // Name of a condition for the log.
    TC_GAME_API char const* CondName(Cond c);
}

#endif
