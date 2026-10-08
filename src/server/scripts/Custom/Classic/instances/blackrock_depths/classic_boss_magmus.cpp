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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_magmus.cpp
// (ScriptDev2 lineage, GPL-2)
// SDComment: Missing pre-event to open doors

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_blackrock_depths.h"

enum Magmus
{
    SPELL_FIERYBURST        = 13900,
    SPELL_WARSTOMP          = 24375
};

struct classic_boss_magmus : public ScriptedAI
{
    classic_boss_magmus(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        Engaged = false;
        m_uiFieryBurst_Timer = 5000;
        m_uiWarStomp_Timer = 0;
    }

    InstanceScript* m_pInstance;

    uint32 m_uiFieryBurst_Timer;
    uint32 m_uiWarStomp_Timer;
    bool Engaged;

    void Reset() override
    {
        m_uiFieryBurst_Timer = 5000;
        m_uiWarStomp_Timer = 0;

        if (Engaged && m_pInstance)
            m_pInstance->SetData(TYPE_IRON_HALL, FAIL);
        Engaged = false;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        Engaged = true;
        if (m_pInstance)
            m_pInstance->SetData(TYPE_IRON_HALL, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_IRON_HALL, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //FieryBurst_Timer
        if (m_uiFieryBurst_Timer < diff)
        {
            DoCastVictim(SPELL_FIERYBURST);
            m_uiFieryBurst_Timer = 6000;
        }
        else
            m_uiFieryBurst_Timer -= diff;

        //WarStomp_Timer
        if (me->GetHealthPct() < 51.0f)
        {
            if (m_uiWarStomp_Timer < diff)
            {
                DoCastVictim(SPELL_WARSTOMP);
                m_uiWarStomp_Timer = 8000;
            }
            else
                m_uiWarStomp_Timer -= diff;
        }
    }
};

void AddSC_classic_boss_magmus()
{
    RegisterCreatureAI(classic_boss_magmus);
}
