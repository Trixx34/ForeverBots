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

#ifndef TRINITY_BOT_PROFESSION_H
#define TRINITY_BOT_PROFESSION_H

// Profession plan for bots (BotQuest.cpp does the trainer visits and gathering). Pure functions, unit tested without a database.
// Scope v1: gathering (mining, herbalism, skinning) plus first aid and cooking. Fishing and crafting come later.

#include "Define.h"
#include <vector>

namespace BotProfession
{
    // SkillLine ids (SkillLine.db2, same as the client)
    enum SkillId : uint32
    {
        SKILL_FIRST_AID = 129,
        SKILL_HERBALISM = 182,
        SKILL_COOKING = 185,
        SKILL_MINING = 186,
        SKILL_SKINNING = 393
    };

    struct Info
    {
        uint32 Skill;
        char const* Name;
        bool Gathering;
        uint8 MinLevel;   // earliest bot level that goes to a trainer for it (copper is scarce before this)
    };

    TC_GAME_API std::vector<Info> const& All();
    TC_GAME_API Info const* Find(uint32 skill);

    // The professions a bot wants, in the order it learns them. Deterministic per bot (seed = guid counter): first aid and cooking
    // for everyone, and two of the three gathering skills (mining+skinning, herbalism+skinning, mining+herbalism).
    TC_GAME_API std::vector<uint32> Plan(uint64 seed);

    // First planned skill the bot does not have yet and may go to a trainer for at this level; 0 when there is none.
    TC_GAME_API uint32 NextToLearn(std::vector<uint32> const& plan, std::vector<uint32> const& known, uint8 level);

    // True when a new gathering node of this skill is worth detouring for: the bot has the skill and the node is not grey for it
    // (required skill within reach of the current value), so it can still skill up or at least loot.
    TC_GAME_API bool NodeWorthIt(uint32 skillValue, uint32 reqSkill);
}

#endif
