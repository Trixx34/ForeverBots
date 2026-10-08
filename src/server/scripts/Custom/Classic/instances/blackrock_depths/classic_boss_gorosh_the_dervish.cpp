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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_gorosh_the_dervish.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

enum GoroshTheDervish
{
    SPELL_WHIRLWIND             = 15589,
    SPELL_MORTALSTRIKE          = 15708,
    SPELL_BLOODLUST             = 21049
};

struct classic_boss_gorosh_the_dervish : public ScriptedAI
{
    classic_boss_gorosh_the_dervish(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    uint32 WhirlWind_Timer;
    uint32 MortalStrike_Timer;
    uint32 Bloodlust_Timer;

    void Initialize()
    {
        WhirlWind_Timer = 12000;
        MortalStrike_Timer = 22000;
        Bloodlust_Timer = 0;
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

        //WhirlWind_Timer
        if (WhirlWind_Timer < diff)
        {
            DoCastSelf(SPELL_WHIRLWIND);
            WhirlWind_Timer = 15000;
        }
        else WhirlWind_Timer -= diff;

        //MortalStrike_Timer
        if (MortalStrike_Timer < diff)
        {
            DoCastVictim(SPELL_MORTALSTRIKE);
            MortalStrike_Timer = 15000;
        }
        else MortalStrike_Timer -= diff;

        //Bloodlust_Timer
        if (me->GetHealthPct() < 51.0f)
        {
            if (Bloodlust_Timer < diff)
            {
                DoCastSelf(SPELL_BLOODLUST);
                Bloodlust_Timer = 45000;
            }
            else Bloodlust_Timer -= diff;
        }
    }
};

void AddSC_classic_boss_gorosh_the_dervish()
{
    RegisterCreatureAI(classic_boss_gorosh_the_dervish);
}
