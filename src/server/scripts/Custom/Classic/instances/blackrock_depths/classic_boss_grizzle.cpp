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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_grizzle.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"

enum Grizzle
{
    EMOTE_GENERIC_FRENZY_KILL   = 7797,

    SPELL_GROUNDTREMOR          = 6524,
    SPELL_FRENZY                = 8269
};

struct classic_boss_grizzle : public ScriptedAI
{
    classic_boss_grizzle(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    uint32 GroundTremor_Timer;
    uint32 Frenzy_Timer;

    void Initialize()
    {
        GroundTremor_Timer = 12000;
        Frenzy_Timer = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //GroundTremor_Timer
        if (GroundTremor_Timer < diff)
        {
            DoCastSelf(SPELL_GROUNDTREMOR);
            GroundTremor_Timer = 8000;
        }
        else GroundTremor_Timer -= diff;

        //Frenzy_Timer
        if (me->GetHealthPct() < 51.0f)
        {
            if (Frenzy_Timer < diff)
            {
                if (DoCastSelf(SPELL_FRENZY) == SPELL_CAST_OK)
                {
                    ClassicScriptText(EMOTE_GENERIC_FRENZY_KILL, me);
                    Frenzy_Timer = 15000;
                }
            }
            else Frenzy_Timer -= diff;
        }
    }
};

void AddSC_classic_boss_grizzle()
{
    RegisterCreatureAI(classic_boss_grizzle);
}
