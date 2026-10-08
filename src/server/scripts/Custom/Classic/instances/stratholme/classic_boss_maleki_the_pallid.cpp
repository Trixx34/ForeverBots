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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_maleki_the_pallid.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ThreatManager.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicMalekiThePallid : uint32
{
    SPELL_MALEKI_FROSTBOLT  = 17503,
    SPELL_MALEKI_DRAIN_LIFE = 17238,
    SPELL_MALEKI_DRAIN_MANA = 17243,
    SPELL_ICETOMB           = 16869
};
}

struct classic_boss_maleki_the_pallid : public ScriptedAI
{
    classic_boss_maleki_the_pallid(Creature* creature) : ScriptedAI(creature) { }

    uint32 Frostbolt_Timer = 0;
    uint32 IceTomb_Timer = 0;
    uint32 Drain_Timer = 0;

    ObjectGuid IcedPlayerGuid;
    float IcedPlayerAggro = 0.0f;

    bool NeedMoveCloser = false;

    void Reset() override
    {
        Frostbolt_Timer = 1000;
        IceTomb_Timer = 12000;
        Drain_Timer = 4000;

        IcedPlayerGuid.Clear();
        IcedPlayerAggro = 0;

        NeedMoveCloser = false;
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_PALLID, DONE);
    }

    float GetManaPercent() const
    {
        int32 maxMana = me->GetMaxPower(POWER_MANA);
        return maxMana > 0 ?(float(me->GetPower(POWER_MANA)) / float(maxMana)) * 100.0f : 0.0f;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (!IcedPlayerGuid.IsEmpty())
        {
            if (Player* pTarget = ObjectAccessor::GetPlayer(*me, IcedPlayerGuid))
            {
                if (!pTarget->HasAura(SPELL_ICETOMB))
                {
                    me->GetThreatManager().AddThreat(pTarget, IcedPlayerAggro, nullptr, true, true);
                    IcedPlayerGuid.Clear();
                    IcedPlayerAggro = 0;
                }
            }
            else
            {
                IcedPlayerGuid.Clear();
                IcedPlayerAggro = 0;
            }
        }

        //Frostbolt
        if (Frostbolt_Timer < diff)
        {
            if (DoCastVictim(SPELL_MALEKI_FROSTBOLT) == SPELL_CAST_OK)
                Frostbolt_Timer = urand(3500, 4500);
        }
        else
            Frostbolt_Timer -= diff;

        //IceTomb
        if (IceTomb_Timer < diff)
        {
            if (Unit* pTarget = me->GetVictim())
            {
                if (DoCast(pTarget, SPELL_ICETOMB) == SPELL_CAST_OK)
                {
                    IcedPlayerGuid = pTarget->GetGUID();
                    IcedPlayerAggro = me->GetThreatManager().GetThreat(pTarget);
                    me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                    IceTomb_Timer = urand(20000, 25000);
                }
            }
        }
        else
            IceTomb_Timer -= diff;

        //DrainLife/Mana
        if ((me->GetHealthPct() < 60.0f) || (GetManaPercent() < 50.0f))
        {
            NeedMoveCloser = true;
            if (Drain_Timer < diff)
            {
                bool ManaTarget = false;
                if (Unit* pTarget = me->GetVictim())
                    if (pTarget->GetPower(POWER_MANA))
                        ManaTarget = true;
                if (DoCastVictim(ManaTarget ? SPELL_MALEKI_DRAIN_MANA : SPELL_MALEKI_DRAIN_LIFE) == SPELL_CAST_OK)
                    Drain_Timer = urand(12000, 18000);
            }
            else
                Drain_Timer -= diff;
        }
        else
            NeedMoveCloser = false;

        if (Unit* pTarget = me->GetVictim())
        {
            if ((pTarget->GetDistance2d(me) > (NeedMoveCloser ? 20.0f : 40.0f)) || (GetManaPercent() < 10.0f))
            {
                SetCombatMovement(true);
                // TODO(classic): VMaNGOS re-issues MoveChase every update; only do it when not already chasing to avoid path spam.
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                    me->GetMotionMaster()->MoveChase(pTarget);
            }
            else
            {
                SetCombatMovement(false);
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                    me->GetMotionMaster()->MoveIdle();
            }
        }
        else
            SetCombatMovement(false);
    }
};

void AddSC_classic_boss_maleki_the_pallid()
{
    RegisterCreatureAI(classic_boss_maleki_the_pallid);
}
