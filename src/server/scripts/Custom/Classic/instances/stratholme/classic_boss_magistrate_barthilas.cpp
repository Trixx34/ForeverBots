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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_magistrate_barthilas.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"

namespace
{
enum ClassicMagistrateBarthilas : uint32
{
    SPELL_DRAININGBLOW      = 16793,
    SPELL_CROWDPUMMEL       = 10887,
    SPELL_MIGHTYBLOW        = 14099,
    SPELL_FURIOUS_ANGER     = 16791,

    MODEL_BARTHILAS_NORMAL  = 10433,
    MODEL_BARTHILAS_HUMAN   = 3637
};
}

struct classic_boss_magistrate_barthilas : public ScriptedAI
{
    classic_boss_magistrate_barthilas(Creature* creature) : ScriptedAI(creature) { }

    uint32 DrainingBlow_Timer = 0;
    uint32 CrowdPummel_Timer = 0;
    uint32 MightyBlow_Timer = 0;
    uint32 FuriousAnger_Timer = 0;
    uint32 AngerCount = 0;

    void Reset() override
    {
        DrainingBlow_Timer = 16000;
        CrowdPummel_Timer = 12000;
        MightyBlow_Timer = 8000;
        FuriousAnger_Timer = 5000;
        AngerCount = 0;

        if (me->IsAlive())
            me->SetDisplayId(MODEL_BARTHILAS_NORMAL);
        else
            me->SetDisplayId(MODEL_BARTHILAS_HUMAN);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // VMaNGOS UNIT_FLAG_SPAWNING (set by the instance script) -> UNIT_FLAG_NON_ATTACKABLE
        if (who->IsPlayer() && me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) && me->IsWithinDistInMap(who, 10.0f))
            me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);

        ScriptedAI::MoveInLineOfSight(who);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->SetDisplayId(MODEL_BARTHILAS_HUMAN);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (FuriousAnger_Timer < diff)
        {
            FuriousAnger_Timer = 4000;
            if (AngerCount > 25)
                return;

            ++AngerCount;
            me->CastSpell(me, SPELL_FURIOUS_ANGER, false);
        }
        else
            FuriousAnger_Timer -= diff;

        //DrainingBlow
        if (DrainingBlow_Timer < diff)
        {
            DoCastVictim(SPELL_DRAININGBLOW);
            DrainingBlow_Timer = 15000;
        }
        else
            DrainingBlow_Timer -= diff;

        //CrowdPummel
        if (CrowdPummel_Timer < diff)
        {
            DoCastVictim(SPELL_CROWDPUMMEL);
            CrowdPummel_Timer = 15000;
        }
        else
            CrowdPummel_Timer -= diff;

        //MightyBlow
        if (MightyBlow_Timer < diff)
        {
            DoCastVictim(SPELL_MIGHTYBLOW);
            MightyBlow_Timer = 20000;
        }
        else
            MightyBlow_Timer -= diff;
    }
};

void AddSC_classic_boss_magistrate_barthilas()
{
    RegisterCreatureAI(classic_boss_magistrate_barthilas);
}
