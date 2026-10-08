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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_ramstein_the_gorger.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "ThreatManager.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicRamsteinTheGorger : uint32
{
    SPELL_RAMSTEIN_TRAMPLE  = 5568,
    SPELL_RAMSTEIN_KNOCKOUT = 17307
};
}

struct classic_boss_ramstein_the_gorger : public ScriptedAI
{
    classic_boss_ramstein_the_gorger(Creature* creature) : ScriptedAI(creature) { }

    uint32 Trample_Timer = 0;
    uint32 Knockout_Timer = 0;
    bool Engaged = false;

    void Reset() override
    {
        Trample_Timer = 3000;
        Knockout_Timer = 12000;
        if (Engaged)
            if (InstanceScript* instance = me->GetInstanceScript())
                instance->SetData(TYPE_RAMSTEIN, FAIL);
        Engaged = false;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        Engaged = true;
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_RAMSTEIN, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_RAMSTEIN, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        //Trample
        if (Trample_Timer < diff)
        {
            DoCast(me, SPELL_RAMSTEIN_TRAMPLE);
            Trample_Timer = 7000;
        }
        else
            Trample_Timer -= diff;

        //Knockout
        if (Knockout_Timer < diff)
        {
            if (DoCastVictim(SPELL_RAMSTEIN_KNOCKOUT) == SPELL_CAST_OK)
            {
                if (Unit* victim = me->GetVictim())
                    me->GetThreatManager().ModifyThreatByPercent(victim, -100);
                Knockout_Timer = 10000;
            }
        }
        else
            Knockout_Timer -= diff;
    }
};

void AddSC_classic_boss_ramstein_the_gorger()
{
    RegisterCreatureAI(classic_boss_ramstein_the_gorger);
}
