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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/boss_houndmaster_loksey.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_houndmaster_loksey (3974)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"

namespace
{
enum ClassicHoundmasterLoksey
{
    SAY_LOKSEY_AGGRO                = 2655, // Release the hounds!
    SPELL_LOKSEY_SUMMONSCARLETHOUND = 17164,
    SPELL_LOKSEY_BLOODLUST          = 6742
};
}

struct classic_boss_houndmaster_loksey : public ScriptedAI
{
    classic_boss_houndmaster_loksey(Creature* creature) : ScriptedAI(creature), _bloodLustTimer(20000) { }

    void Reset() override
    {
        _bloodLustTimer = 20000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_LOKSEY_AGGRO, me);
        DoCastSelf(SPELL_LOKSEY_SUMMONSCARLETHOUND);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_bloodLustTimer < diff)
        {
            DoCastSelf(SPELL_LOKSEY_BLOODLUST);
            _bloodLustTimer = 20000;
        }
        else
            _bloodLustTimer -= diff;
    }

private:
    uint32 _bloodLustTimer;
};

void AddSC_classic_boss_houndmaster_loksey()
{
    RegisterCreatureAI(classic_boss_houndmaster_loksey);
}
