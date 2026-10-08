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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/boss_kurinnaxx.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_kurinnaxx

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "QuaternionData.h"
#include "ScriptedCreature.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"

namespace
{
enum ClassicAQ20Kurinnaxx : uint32
{
    EMOTE_AQ20_KURINNAXX_FRENZY        = 10645,

    GO_AQ20_KURINNAXX_TRAP             = 180647,

    SPELL_AQ20_KURINNAXX_MORTALWOUND   = 25646,
    SPELL_AQ20_KURINNAXX_SUMMON_SANDTRAP = 26524,
    SPELL_AQ20_KURINNAXX_ENRAGE        = 26527,
    SPELL_AQ20_KURINNAXX_WIDE_SLASH    = 25814,
    SPELL_AQ20_KURINNAXX_TRASH         = 3391,

    SAY_AQ20_BREACHED                  = 11720   // yelled by Ossirian
};
}

struct classic_boss_kurinnaxx : public ScriptedAI
{
    classic_boss_kurinnaxx(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 m_uiMortalWound_Timer = 0;
    uint32 m_uiSandTrap_Timer = 0;
    uint32 m_uiCleanSandTrap_Timer = 0;
    uint32 m_uiTrash_Timer = 0;
    uint32 m_uiWideSlash_Timer = 0;
    bool m_bHasEnraged = false;

    void Reset() override
    {
        m_uiMortalWound_Timer = 7000;
        m_uiSandTrap_Timer = 7000;
        m_uiCleanSandTrap_Timer = 0;
        m_uiTrash_Timer = 10000;
        m_uiWideSlash_Timer = 15000;
        m_bHasEnraged = false;
    }

    // VMaNGOS JustRespawned (TC: JustAppeared also runs on first spawn / grid load; DONE is never overwritten)
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_KURINNAXX, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_KURINNAXX, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        // VMaNGOS DoOrSimulateScriptTextForMap(SAY_BREACHED, NPC_OSSIRIAN, map)
        // TODO(classic): VMaNGOS simulates the yell with Ossirian's name when his grid is not loaded; here it is skipped then.
        if (Creature* pOssirian = me->GetMap()->GetCreature(m_pInstance->GetGuidData(CLASSIC_AQ20_DATA_OSSIRIAN)))
            ClassicScriptText(SAY_AQ20_BREACHED, pOssirian);

        m_pInstance->SetData(CLASSIC_AQ20_TYPE_KURINNAXX, DONE);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        // if no one gets to the trap in 5 seconds delete the trap
        if (m_uiCleanSandTrap_Timer < uiDiff)
        {
            if (GameObject* pTrap = GetClosestGameObjectWithEntry(me, GO_AQ20_KURINNAXX_TRAP, me->GetVisibilityRange()))
            {
                pTrap->SendGameObjectDespawn();
                pTrap->Delete();
            }
        }
        else
            m_uiCleanSandTrap_Timer -= uiDiff;

        if (!UpdateVictim())
            return;

        /* Enrage */
        if (me->GetHealthPct() <= 30.0f && !m_bHasEnraged)
        {
            if (DoCastSelf(SPELL_AQ20_KURINNAXX_ENRAGE) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_AQ20_KURINNAXX_FRENZY, me);
                m_bHasEnraged = true;
            }
        }

        /* Mortal wound */
        if (m_uiMortalWound_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_KURINNAXX_MORTALWOUND) == SPELL_CAST_OK)
                m_uiMortalWound_Timer = 9000;
        }
        else
            m_uiMortalWound_Timer -= uiDiff;

        // TODO: Should use 26524 instead (VMaNGOS note)
        /* Summon trap */
        if (m_uiSandTrap_Timer < uiDiff)
        {
            if (Unit* pUnit = SelectTarget(SelectTargetMethod::Random, 0))
            {
                // TC: the summoner (Kurinnaxx) becomes the trap owner (VMaNGOS SetOwnerGuid)
                me->SummonGameObject(GO_AQ20_KURINNAXX_TRAP, pUnit->GetPositionX(), pUnit->GetPositionY(), pUnit->GetPositionZ(), 0.0f,
                    QuaternionData(), 0s);
                m_uiSandTrap_Timer = urand(5100, 7000); /** Random timer for sandtrap between 1 and 7s */
                m_uiCleanSandTrap_Timer = 5000;
            }
        }
        else
            m_uiSandTrap_Timer -= uiDiff;

        /* WideSlash */
        if (m_uiWideSlash_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_KURINNAXX_WIDE_SLASH) == SPELL_CAST_OK)
                m_uiWideSlash_Timer = 10000 + urand(0, 9999);
        }
        else
            m_uiWideSlash_Timer -= uiDiff;

        /* Trash */
        if (m_uiTrash_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_KURINNAXX_TRASH) == SPELL_CAST_OK)
                m_uiTrash_Timer = 10000 + urand(0, 9999);
        }
        else
            m_uiTrash_Timer -= uiDiff;
    }
};

void AddSC_classic_boss_kurinnaxx()
{
    RegisterCreatureAI(classic_boss_kurinnaxx);
}
