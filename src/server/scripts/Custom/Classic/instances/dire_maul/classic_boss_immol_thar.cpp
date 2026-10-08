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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/dire_maul/boss_immol_thar.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_immol_thar

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_dire_maul.h"

enum ClassicDMImmolThar
{
    SPELL_TRAMPLE              = 5568,
    SPELL_INFECTED_BITE        = 16128,
    SPELL_EYE_OF_IMMOL_THAR    = 22899,
    SPELL_PORTAL_OF_IMMOL_THAR = 22950,
    SPELL_ENRAGE               = 8269,

    NPC_EYE_OF_IMMOL_THAR      = 14396,
    SPELL_VISUAL_SUMMON_PLAYER = 25681  // VMaNGOS SendSpellGo(target, 25681): visual only
};

struct classic_boss_immol_thar : public ScriptedAI
{
    classic_boss_immol_thar(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;
    uint32 m_uiTrampleTimer = 0;
    uint32 m_uiInfectedBiteTimer = 0;
    uint32 m_uiEyeOfImmolTharTimer = 0;
    uint32 m_uiPortalOfImmolTharTimer = 0;
    uint32 m_uiEnrageTimer = 0;
    uint32 CheckBug_Timer = 0;
    bool   m_bEngage = false;

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

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_IMMOL_THAR, DONE);
    }

    void Reset() override
    {
        m_uiEnrageTimer            = 50000;
        m_uiTrampleTimer           = urand(5000, 9000);
        m_uiInfectedBiteTimer      = urand(2000, 4000);
        m_uiEyeOfImmolTharTimer    = urand(7000, 12000);
        m_uiPortalOfImmolTharTimer = urand(10000, 14000);
        CheckBug_Timer = 0;
        m_bEngage = false;
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // VMaNGOS m_creature->RemoveGuardians()
        me->RemoveAllMinionsByEntry(NPC_EYE_OF_IMMOL_THAR);

        ScriptedAI::EnterEvadeMode(why);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!m_bEngage)
            m_bEngage = true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            // In case of a pathfinding bug
            // (VMaNGOS runs this out of combat too; TC's evade out of combat would reset the motion every tick)
            if (IsEngaged())
            {
                CheckBug_Timer += diff;
                if (CheckBug_Timer > 5000)
                {
                    EnterEvadeMode(EvadeReason::Other);
                    me->CombatStop();
                    me->SetFullHealth();
                }
            }
            return;
        }
        else
            CheckBug_Timer = 0;

        if (me->IsNonMeleeSpellCast(false))
            return;

        if (ManageTimer(diff, &m_uiTrampleTimer, urand(9000, 14000)))
            DoCastSelf(SPELL_TRAMPLE);

        if (ManageTimer(diff, &m_uiInfectedBiteTimer, urand(8000, 12000)))
            DoCastVictim(SPELL_INFECTED_BITE);

        if (ManageTimer(diff, &m_uiEyeOfImmolTharTimer, urand(15000, 22000)))
        {
            if (Creature* eye = me->SummonCreature(NPC_EYE_OF_IMMOL_THAR,
                me->GetPositionX(),
                me->GetPositionY(),
                me->GetPositionZ(),
                me->GetOrientation(),
                TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s))
            {
                // TODO(classic): VMaNGOS SendSpellGo(eye, 25681) (summon visual only)
                if (eye->AI())
                    eye->AI()->AttackStart(me->GetVictim());
            }
        }

        if (ManageTimer(diff, &m_uiPortalOfImmolTharTimer, urand(17000, 24000)))
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                /** Invoke player in front of him */
                float x = me->GetPositionX();
                float y = me->GetPositionY();
                float z = me->GetPositionZ() + 1;
                float orientation = target->GetOrientation();
                // TODO(classic): VMaNGOS SendSpellGo(target, 25681) (summon visual only)
                target->NearTeleportTo(x, y, z, orientation);
                ModifyThreatByPercent(target, -100);
            }
        }

        if (m_uiEnrageTimer < diff)
        {
            if (!me->HasAura(SPELL_ENRAGE) && m_bEngage)
                DoCastSelf(SPELL_ENRAGE, true);
        }
        else
            m_uiEnrageTimer -= diff;
    }
};

void AddSC_classic_boss_immol_thar()
{
    RegisterCreatureAI(classic_boss_immol_thar);
}
