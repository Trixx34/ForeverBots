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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_gehennas.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_gehennas

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_molten_core.h"

namespace
{
enum ClassicMcGehennas : uint32
{
    SPELL_GEHENNAS_CURSE                 = 19716,
    SPELL_GEHENNAS_RAIN_OF_FIRE          = 19717,
    SPELL_GEHENNAS_SHADOW_BOLT_RANDOM    = 19728,
    SPELL_GEHENNAS_SHADOW_BOLT_TARGET    = 19729
};
}

struct classic_boss_gehennas : public ScriptedAI
{
    classic_boss_gehennas(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    uint32 m_uiGehennasCurseTimer = 0;
    uint32 m_uiRainOfFireTimer = 0;
    uint32 m_uiShadowBoltRandomTimer = 0;
    uint32 m_uiShadowBoltTargetTimer = 0;

    InstanceScript* m_pInstance;

    void Reset() override
    {
        m_uiGehennasCurseTimer = urand(5 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
        m_uiRainOfFireTimer = urand(6 * IN_MILLISECONDS, 12 * IN_MILLISECONDS);
        m_uiShadowBoltRandomTimer = urand(3 * IN_MILLISECONDS, 6 * IN_MILLISECONDS);
        m_uiShadowBoltTargetTimer = urand(3 * IN_MILLISECONDS, 6 * IN_MILLISECONDS);

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_MC_TYPE_GEHENNAS, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GEHENNAS, IN_PROGRESS);
        DoZoneInCombat();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GEHENNAS, DONE);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // Rain of Fire
        if (m_uiRainOfFireTimer < uiDiff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                if (DoCast(target, SPELL_GEHENNAS_RAIN_OF_FIRE) == SPELL_CAST_OK)
                    m_uiRainOfFireTimer = urand(6 * IN_MILLISECONDS, 12 * IN_MILLISECONDS);
        }
        else
            m_uiRainOfFireTimer -= uiDiff;

        // Gehennas' Curse
        if (m_uiGehennasCurseTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_GEHENNAS_CURSE) == SPELL_CAST_OK)
                m_uiGehennasCurseTimer = urand(25 * IN_MILLISECONDS, 30 * IN_MILLISECONDS);
        }
        else
            m_uiGehennasCurseTimer -= uiDiff;

        // Shadow Bolt (random)
        if (m_uiShadowBoltRandomTimer < uiDiff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                if (DoCast(target, SPELL_GEHENNAS_SHADOW_BOLT_RANDOM) == SPELL_CAST_OK)
                    m_uiShadowBoltRandomTimer = urand(3 * IN_MILLISECONDS, 6 * IN_MILLISECONDS);
        }
        else
            m_uiShadowBoltRandomTimer -= uiDiff;

        // Shadow Bolt (target)
        if (m_uiShadowBoltTargetTimer < uiDiff)
        {
            if (DoCastVictim(SPELL_GEHENNAS_SHADOW_BOLT_TARGET) == SPELL_CAST_OK)
                m_uiShadowBoltTargetTimer = urand(3 * IN_MILLISECONDS, 6 * IN_MILLISECONDS);
        }
        else
            m_uiShadowBoltTargetTimer -= uiDiff;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_gehennas()
{
    RegisterCreatureAI(classic_boss_gehennas);
}
