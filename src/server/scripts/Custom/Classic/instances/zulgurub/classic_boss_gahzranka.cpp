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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_gahzranka.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_gahzranka

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "classic_zulgurub.h"

namespace
{
enum ClassicZgGahzranka : uint32
{
    // Frost Breath - Slow attack and movement speed, drains mana.
    SPELL_GAHZRANKA_FROSTBREATH     = 16099,
    // Massive Geyser - Tosses everyone into the air for 500 damage, then subsequent fall damage. Temporary aggro wipe.
    SPELL_GAHZRANKA_MASSIVEGEYSER   = 22421,
    // Slam - Inflicts normal damage plus 250 to nearby enemies and knocks them back.
    SPELL_GAHZRANKA_SLAM            = 24326,

    POINT_GAHZRANKA_BEACH_0         = 0,
    POINT_GAHZRANKA_BEACH_1         = 1
};
}

struct classic_boss_gahzranka : public ScriptedAI
{
    classic_boss_gahzranka(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 Frostbreath_Timer = 0;
    uint32 MassiveGeyser_Timer = 0;
    uint32 Slam_Timer = 0;

    void Reset() override
    {
        Frostbreath_Timer = 8000;
        MassiveGeyser_Timer = 25000;
        Slam_Timer = 17000;
        // VMaNGOS resets TYPE_GAHZRANKA to NOT_STARTED here. TC re-creates the creature (and calls Reset) when it is
        // respawned by event_summon_gahzranka, which would clear the IN_PROGRESS summon flag before CheckSpawnStatus;
        // the reset is done after an evade instead (JustReachedHome).
    }

    void JustReachedHome() override
    {
        if (m_pInstance && m_pInstance->GetData(CLASSIC_ZG_TYPE_GAHZRANKA) != DONE)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_GAHZRANKA, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_GAHZRANKA, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_GAHZRANKA, DONE);
    }

    // VMaNGOS: constructor + JustRespawned()
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        CheckSpawnStatus();
    }

    void CheckSpawnStatus()
    {
        if (!m_pInstance)
            return;

        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_GAHZRANKA) != IN_PROGRESS)
        {
            // VMaNGOS DisappearAndDie() + SetRespawnTime(259200)
            me->DespawnOrUnsummon(0s, 259200s);
        }
        else
        {
            me->GetMotionMaster()->MovePoint(POINT_GAHZRANKA_BEACH_0, -11709.3476f, -1749.965f, 8.733f, true, 5.3478f);
            me->SetHomePosition(-11688.95f, -1777.21f, 12.593f, 5.81f);
        }
    }

    void MovementInform(uint32 uiType, uint32 uiPointId) override
    {
        if (uiType != POINT_MOTION_TYPE)
            return;

        if (uiPointId == POINT_GAHZRANKA_BEACH_0) // move to the Beach
            me->GetMotionMaster()->MovePoint(POINT_GAHZRANKA_BEACH_1, -11688.95f, -1777.21f, 12.593f, true, 5.81f);
    }

    void UpdateAI(uint32 diff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        // Frostbreath_Timer
        if (Frostbreath_Timer < diff)
        {
            if (DoCastVictim(SPELL_GAHZRANKA_FROSTBREATH) == SPELL_CAST_OK)
                Frostbreath_Timer = urand(8000, 20000);
        }
        else
            Frostbreath_Timer -= diff;

        // MassiveGeyser_Timer
        if (MassiveGeyser_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(target, SPELL_GAHZRANKA_MASSIVEGEYSER) == SPELL_CAST_OK)
                {
                    MassiveGeyser_Timer = urand(16000, 24000);
                    ResetThreatList();
                }
            }
        }
        else
            MassiveGeyser_Timer -= diff;

        // Slam_Timer
        if (Slam_Timer < diff)
        {
            if (DoCastVictim(SPELL_GAHZRANKA_SLAM) == SPELL_CAST_OK)
                Slam_Timer = urand(12000, 20000);
        }
        else
            Slam_Timer -= diff;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_gahzranka()
{
    RegisterCreatureAI(classic_boss_gahzranka);
}
