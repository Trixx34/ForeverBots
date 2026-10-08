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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/dire_maul/boss_tendris_warpwood.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_tendris_warpwood

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_dire_maul.h"
#include "classic_script_text.h"
#include <list>

enum ClassicDMTendris
{
    SAY_TENDRIS_AGGRO          = 11727,

    SPELL_TRAMPLE              = 5568,
    SPELL_UPPERCUT             = 22916,
    SPELL_GRASPING_VINES       = 22924,
    SPELL_ENCHEVETREMENT       = 22994,
    SPELL_ENRAGE               = 8269,

    NPC_IRONBARK_PROTECTOR     = 11459,
    NPC_ANCIENT_EQUINE_SPIRIT  = 14566
};

struct classic_boss_tendris_warpwood : public ScriptedAI
{
    classic_boss_tendris_warpwood(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;
    uint32 m_uiTrampleTimer = 0;
    uint32 m_uiUppercutTimer = 0;
    uint32 m_uiGraspingVinesTimer = 0;
    uint32 m_uiInvocation_Timer = 0;
    bool   m_uiAggroProtector = false;

    static bool ManageTimer(uint32 diff, uint32* timer, uint32 cooldown)
    {
        if ((*timer) < diff)
        {
            (*timer) = cooldown;
            return true;
        }
        (*timer) -= diff;
        return false;
    }

    void JustDied(Unit* killer) override
    {
        if (!killer)
            return;

        me->SummonCreature(NPC_ANCIENT_EQUINE_SPIRIT,
                           killer->GetPositionX(),
                           killer->GetPositionY(),
                           killer->GetPositionZ(),
                           killer->GetOrientation(),
                           TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!m_uiAggroProtector)
        {
            // World of Warcraft Client Patch 1.10.0 (2006-03-28)
            // - Tendris Warpwood will now call upon any protectors still alive to aid him.
            // VMaNGOS: sWorld.GetWowPatch() >= WOW_PATCH_110 (always true for the Classic 1.60 content patch)
            std::list<Creature*> aggroList;
            me->GetCreatureListWithEntryInGrid(aggroList, NPC_IRONBARK_PROTECTOR, 1800.0f);
            for (Creature* protector : aggroList)
            {
                if (protector->IsAlive())
                    CreatureAI::DoZoneInCombat(protector);
            }
            m_uiAggroProtector = true;
            ClassicScriptText(SAY_TENDRIS_AGGRO, me);
        }
    }

    void Reset() override
    {
        m_uiInvocation_Timer       = 0;
        m_uiTrampleTimer           = urand(5000, 9000);
        m_uiUppercutTimer          = urand(2000, 4000);
        m_uiGraspingVinesTimer     = urand(9000, 12000);
        m_uiAggroProtector         = false;
    }

    void AttackStart(Unit* who) override
    {
        ScriptedAI::AttackStart(who);
        if (m_pInstance)
            m_pInstance->SetData(DATA_TENDRIS_AGGRO, IN_PROGRESS);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || me->IsNonMeleeSpellCast(false))
            return;

        if (ManageTimer(diff, &m_uiTrampleTimer, urand(9000, 14000)))
            DoCastSelf(SPELL_TRAMPLE);

        if (ManageTimer(diff, &m_uiUppercutTimer, urand(12000, 15000)))
            DoCastVictim(SPELL_UPPERCUT);

        if (ManageTimer(diff, &m_uiGraspingVinesTimer, urand(17000, 22000)))
            DoCastSelf(SPELL_GRASPING_VINES);

        if (me->GetHealthPct() < 30.0f && !me->HasAura(SPELL_ENRAGE))
            DoCastSelf(SPELL_ENRAGE, true);

        /** Invoke player in front of him */
        if (m_uiInvocation_Timer < diff)
        {
            Unit* unit = me->GetVictim();
            if (unit && me->GetDistance(unit) > 7.0f)
            {
                float x = me->GetPositionX();
                float y = me->GetPositionY();
                float z = me->GetPositionZ() + 1;
                float orientation = unit->GetOrientation();
                // TODO(classic): VMaNGOS SendSpellGo(unit, 25681) (summon visual only)
                unit->NearTeleportTo(x, y, z, orientation);
                m_uiInvocation_Timer = urand(10000, 15000);
                DoCast(unit, SPELL_ENCHEVETREMENT);
            }
        }
        else
            m_uiInvocation_Timer -= diff;
    }
};

void AddSC_classic_boss_tendris_warpwood()
{
    RegisterCreatureAI(classic_boss_tendris_warpwood);
}
