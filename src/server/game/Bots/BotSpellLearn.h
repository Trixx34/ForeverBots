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

#ifndef TRINITY_BOT_SPELL_LEARN_H
#define TRINITY_BOT_SPELL_LEARN_H

// Auto-learning of class spells on level up (Bot.AI.AutoLearnSpells). BotQuest.cpp collects the class trainer spell lists and
// teaches the result; the selection and the due check are pure and unit tested without a database.

#include "Define.h"
#include <vector>

namespace BotSpellLearn
{
    struct Candidate
    {
        uint32 SpellId = 0;
        uint32 ReqLevel = 0;
        bool Available = false;   // trainer state Available and teachable (class and race fit, skill and ability requirements met, not known)
        bool InfoValid = false;   // the spell exists in the client data (an id missing from the data is dropped, like the combat table does)
    };

    // Spells to teach now, in teaching order: valid, available, requirement level <= botLevel, each spell id once (several trainers
    // list the same spell), lowest requirement level first, then lowest spell id (so ranks come in order).
    TC_GAME_API std::vector<uint32> Select(std::vector<Candidate> const& candidates, uint32 botLevel);

    // True when a learning pass is due: the feature is on and the bot is at a level it has not been checked at yet.
    // lastLevel 0 means never checked, which is the catch-up on login.
    TC_GAME_API bool Due(bool enabled, uint32 botLevel, uint32 lastLevel);
}

#endif
