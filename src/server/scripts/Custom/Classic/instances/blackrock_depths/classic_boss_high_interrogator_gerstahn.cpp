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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_high_interrogator_gerstahn.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

enum HighInterrogatorGerstahn
{
    SPELL_SHADOWWORDPAIN        = 14032,
    SPELL_MANABURN              = 14033,
    SPELL_PSYCHICSCREAM         = 13704,
    SPELL_SHADOWSHIELD          = 12040
};

struct classic_boss_high_interrogator_gerstahn : public ScriptedAI
{
    classic_boss_high_interrogator_gerstahn(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    uint32 m_uiShadowWordPain_Timer;
    uint32 m_uiManaBurn_Timer;
    uint32 m_uiPsychicScream_Timer;
    uint32 m_uiShadowShield_Timer;

    void Initialize()
    {
        m_uiShadowWordPain_Timer = 4000;
        m_uiManaBurn_Timer = 14000;
        m_uiPsychicScream_Timer = 32000;
        m_uiShadowShield_Timer = 8000;
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

        //ShadowWordPain_Timer
        if (m_uiShadowWordPain_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_SHADOWWORDPAIN);

            m_uiShadowWordPain_Timer = 7000;
        }
        else
            m_uiShadowWordPain_Timer -= diff;

        //ManaBurn_Timer
        if (m_uiManaBurn_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_MANABURN);

            m_uiManaBurn_Timer = 10000;
        }
        else
            m_uiManaBurn_Timer -= diff;

        //PsychicScream_Timer
        if (m_uiPsychicScream_Timer < diff)
        {
            DoCastVictim(SPELL_PSYCHICSCREAM);
            m_uiPsychicScream_Timer = 30000;
        }
        else
            m_uiPsychicScream_Timer -= diff;

        //ShadowShield_Timer
        if (m_uiShadowShield_Timer < diff)
        {
            DoCastSelf(SPELL_SHADOWSHIELD);
            m_uiShadowShield_Timer = 25000;
        }
        else
            m_uiShadowShield_Timer -= diff;
    }
};

void AddSC_classic_boss_high_interrogator_gerstahn()
{
    RegisterCreatureAI(classic_boss_high_interrogator_gerstahn);
}
