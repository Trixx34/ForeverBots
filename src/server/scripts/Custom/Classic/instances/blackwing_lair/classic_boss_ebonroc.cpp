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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackwing_lair/boss_ebonroc.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_ebonroc

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "classic_blackwing_lair.h"

namespace
{
enum ClassicBwlEbonroc : uint32
{
    CLASSIC_BWL_SPELL_EBON_SHADOW_FLAME      = 22539,
    CLASSIC_BWL_SPELL_EBON_WING_BUFFET       = 23339,
    CLASSIC_BWL_SPELL_SHADOW_OF_EBONROC      = 23340,
    CLASSIC_BWL_SPELL_EBON_THRASH            = 3391
};
}

struct classic_boss_ebonroc : public ScriptedAI
{
    classic_boss_ebonroc(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiShadowFlameTimer = 0;
    uint32 m_uiWingBuffetTimer = 0;
    uint32 m_uiShadowOfEbonrocTimer = 0;

    void Reset() override
    {
        m_uiShadowFlameTimer     = 16000;               // These times are probably wrong
        m_uiWingBuffetTimer      = 30000;
        m_uiShadowOfEbonrocTimer = 8000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_EBONROC, IN_PROGRESS);

        DoZoneInCombat();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_EBONROC, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_EBONROC, FAIL);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == CLASSIC_BWL_SPELL_EBON_WING_BUFFET)
        {
            Unit* pTarget = target ? target->ToUnit() : nullptr;
            if (!pTarget || pTarget->GetTypeId() != TYPEID_PLAYER)
                return;
            if (me->GetThreatManager().GetThreat(pTarget))
                ModifyThreatByPercent(pTarget, -50);
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // Shadow Flame Timer
        if (m_uiShadowFlameTimer < uiDiff)
        {
            if (DoCastSelf(CLASSIC_BWL_SPELL_EBON_SHADOW_FLAME) == SPELL_CAST_OK)
                m_uiShadowFlameTimer = 16000;
        }
        else
            m_uiShadowFlameTimer -= uiDiff;

        // Wing Buffet Timer
        if (m_uiWingBuffetTimer < uiDiff)
        {
            if (DoCastVictim(CLASSIC_BWL_SPELL_EBON_WING_BUFFET) == SPELL_CAST_OK)
                m_uiWingBuffetTimer = 30000;
        }
        else
            m_uiWingBuffetTimer -= uiDiff;

        // Shadow of Ebonroc Timer
        if (m_uiShadowOfEbonrocTimer < uiDiff)
        {
            // VMaNGOS: CF_AURA_NOT_PRESENT
            if (Unit* pVictim = me->GetVictim())
                if (!pVictim->HasAura(CLASSIC_BWL_SPELL_SHADOW_OF_EBONROC) && DoCast(pVictim, CLASSIC_BWL_SPELL_SHADOW_OF_EBONROC) == SPELL_CAST_OK)
                    m_uiShadowOfEbonrocTimer = 8000;
        }
        else
            m_uiShadowOfEbonrocTimer -= uiDiff;

        // VMaNGOS: Thrash when a melee swing is ready (1 in 3 chance)
        if (me->isAttackReady() && !urand(0, 2))
            DoCastSelf(CLASSIC_BWL_SPELL_EBON_THRASH);

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_ebonroc()
{
    RegisterCreatureAI(classic_boss_ebonroc);
}
