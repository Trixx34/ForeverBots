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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/westfall/deadmines/boss_mr_smite.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_mr_smite

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "classic_deadmines.h"
#include "classic_script_text.h"

enum ClassicMrSmite
{
    SAY_SMITE_PHASE_2               = 1344,
    SAY_SMITE_PHASE_3               = 1345,

    EQUIP_ID_SMITE_SWORD            = 2179,
    EQUIP_ID_SMITE_AXE              = 2183,
    EQUIP_ID_SMITE_HAMMER           = 10756,

    SPELL_SMITE_NIBLE_REFLEXES      = 6433,                 // removed after phase 1
    SPELL_SMITE_SLAM                = 6435,                 // only casted in phase 3
    SPELL_SMITE_STOMP               = 6432,
    SPELL_SMITE_HAMMER              = 6436,                 // unclear, not casted in
    SPELL_SMITE_THRASH              = 3391,                 // only casted in phase 2; 3391 directly casted instead of proc aura

    GO_SMITE_CHEST                  = 144111,

    SMITE_PHASE_1                   = 1,
    SMITE_PHASE_2                   = 2,
    SMITE_PHASE_3                   = 3,
    SMITE_PHASE_EQUIP_NULL          = 4,
    SMITE_PHASE_EQUIP_START         = 5,
    SMITE_PHASE_EQUIP_PROCESS       = 6,
    SMITE_PHASE_EQUIP_END           = 7,
};

struct classic_boss_mr_smite : public ScriptedAI
{
    classic_boss_mr_smite(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        equiping = false;
        inSpline = false;
        m_uiPhase = SMITE_PHASE_1;
        m_uiEquipTimer = 0;
        m_uiSlamTimer = 9000;
        m_uiThrashTimer = 4000;
    }

    uint32 m_uiPhase;
    uint32 m_uiEquipTimer;
    uint32 m_uiSlamTimer;
    uint32 m_uiThrashTimer;
    bool equiping; // Basicaly when he is not chasing the player.
    bool inSpline; // true while running to chest.

    void Reset() override
    {
        Initialize();
        me->LoadEquipment(me->GetOriginalEquipmentId(), true);
    }

    // VMaNGOS AttackedBy + AttackStart: no attacking while changing weapons
    void AttackStart(Unit* who) override
    {
        if (m_uiPhase > SMITE_PHASE_3)
            return;

        if (!who)
            return;

        equiping = false;
        ScriptedAI::AttackStart(who);
    }

    void MovementInform(uint32 motionType, uint32 /*pointId*/) override
    {
        if (motionType != POINT_MOTION_TYPE)
            return;

        if (inSpline)
        {
            SplineFinished();
            inSpline = false;
        }

        if (!equiping)
        {
            if (Unit* target = me->GetVictim())
                me->GetMotionMaster()->MoveChase(target);
            return;
        }

        me->SetSheath(SHEATH_STATE_UNARMED);
        me->SetStandState(UNIT_STAND_STATE_KNEEL);

        m_uiEquipTimer = 3000;
        m_uiPhase = SMITE_PHASE_EQUIP_PROCESS;
    }

    void SplineFinished()
    {
        if (!equiping)
            return;
        me->LoadEquipment(0, true);
        me->SetSheath(SHEATH_STATE_UNARMED);
        me->SetStandState(UNIT_STAND_STATE_KNEEL);

        m_uiEquipTimer = 3000;
        m_uiPhase = SMITE_PHASE_EQUIP_PROCESS;
    }

    void PhaseEquipStart()
    {
        GameObject* chest = me->FindNearestGameObject(GO_SMITE_CHEST, 150.0f);
        if (!chest)
        {
            m_uiPhase = SMITE_PHASE_EQUIP_PROCESS;
            return;
        }

        m_uiPhase = SMITE_PHASE_EQUIP_NULL;

        float x, y, z;
        chest->GetContactPoint(me, x, y, z, CONTACT_DISTANCE);

        me->GetMotionMaster()->Clear();
        me->SetFacingToObject(chest);

        inSpline = true;
        me->GetMotionMaster()->MovePoint(0, x, y, z);
    }

    void PhaseEquipProcess()
    {
        if (me->GetHealthPct() < 33.0f)
        {
            // It's Hammer, go Hammer!
            me->SetVirtualItem(BASE_ATTACK, EQUIP_ID_SMITE_HAMMER);
            me->SetVirtualItem(OFF_ATTACK, 0);
            me->SetVirtualItem(RANGED_ATTACK, 0);
            DoCastSelf(SPELL_SMITE_HAMMER);
        }
        else
        {
            // It's double Axe.
            me->SetVirtualItem(BASE_ATTACK, EQUIP_ID_SMITE_AXE);
            me->SetVirtualItem(OFF_ATTACK, EQUIP_ID_SMITE_AXE);
            me->SetVirtualItem(RANGED_ATTACK, 0);
        }

        me->SetStandState(UNIT_STAND_STATE_STAND);
        m_uiPhase = SMITE_PHASE_EQUIP_END;
        m_uiEquipTimer = 1000;
    }

    void PhaseEquipEnd()
    {
        // We don't have a victim, so select from threat list
        Unit* victim = SelectTarget(SelectTargetMethod::MaxThreat, 0);

        if (!victim)
        {
            EnterEvadeMode();
            return;
        }

        me->SetSheath(SHEATH_STATE_MELEE);

        m_uiPhase = me->GetHealthPct() < 33.0f ? SMITE_PHASE_3 : SMITE_PHASE_2;

        equiping = false;
        AttackStart(victim);
    }

    void StartEquipPhase()
    {
        m_uiPhase = SMITE_PHASE_EQUIP_START;
        m_uiEquipTimer = 2500;

        // will clear the victim
        me->GetMotionMaster()->Clear();
        equiping = true;
        me->AttackStop();
    }

    void UpdateAI(uint32 diff) override
    {
        // UpdateVictim() returns false while weapons are changed (AttackStart is blocked, so there is no victim)
        if (!UpdateVictim())
        {
            if (!me->IsEngaged())
                return;

            if (m_uiEquipTimer)
            {
                // decrease the cooldown in between equipment change phases
                if (m_uiEquipTimer > diff)
                {
                    m_uiEquipTimer -= diff;
                    return;
                }
                else
                    m_uiEquipTimer = 0;
            }

            switch (m_uiPhase)
            {
                case SMITE_PHASE_EQUIP_START:
                    PhaseEquipStart();
                    break;
                case SMITE_PHASE_EQUIP_PROCESS:
                    PhaseEquipProcess();
                    break;
                case SMITE_PHASE_EQUIP_END:
                    PhaseEquipEnd();
                    break;
                default:
                    break;
            }

            return;
        }

        // the normal combat phases
        switch (m_uiPhase)
        {
            case SMITE_PHASE_1:
            {
                if (me->GetHealthPct() < 66.0f)
                {
                    if (DoCastSelf(SPELL_SMITE_STOMP) == SPELL_CAST_OK)
                    {
                        ClassicScriptText(me->GetHealthPct() < 33.0f ? SAY_SMITE_PHASE_3 : SAY_SMITE_PHASE_2, me);
                        StartEquipPhase();
                        me->RemoveAurasDueToSpell(SPELL_SMITE_NIBLE_REFLEXES);
                    }
                    return;
                }
                break;
            }
            case SMITE_PHASE_2:
            {
                if (m_uiThrashTimer < diff) // instead of the aura, because the aura procs too much
                {
                    if (DoCastVictim(SPELL_SMITE_THRASH) == SPELL_CAST_OK)
                        m_uiThrashTimer = urand(1500, 4000);
                }
                else
                    m_uiThrashTimer -= diff;

                if (me->GetHealthPct() < 33.0f)
                {
                    if (DoCastSelf(SPELL_SMITE_STOMP) == SPELL_CAST_OK)
                    {
                        ClassicScriptText(SAY_SMITE_PHASE_3, me);
                        StartEquipPhase();
                    }
                    return;
                }
                break;
            }
            case SMITE_PHASE_3:
            {
                if (m_uiSlamTimer < diff)
                {
                    if (DoCastVictim(SPELL_SMITE_SLAM) == SPELL_CAST_OK)
                        m_uiSlamTimer = 11000;
                }
                else
                    m_uiSlamTimer -= diff;
                break;
            }
            default:
                break;
        }
        // VMaNGOS also moved Smite to a random attack point when he could not reach his target in melee
        // (core chase workaround); TC's chase movement handles this.
    }
};

void AddSC_classic_boss_mr_smite()
{
    RegisterCreatureAI(classic_boss_mr_smite);
}
