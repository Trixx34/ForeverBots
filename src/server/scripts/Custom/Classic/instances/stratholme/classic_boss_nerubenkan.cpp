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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_nerubenkan.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicNerubenkan : uint32
{
    SPELL_ENCASINGWEBS          = 4962,
    SPELL_PIERCEARMOR           = 6016,
    SPELL_RAISEUNDEADSCARAB     = 17235,

    NPC_CRYPT_SCARAB            = 10577,
    NPC_UNDEAD_SCARAB           = 10876
};
}

struct classic_boss_nerubenkan : public ScriptedAI
{
    classic_boss_nerubenkan(Creature* creature) : ScriptedAI(creature) { }

    uint32 EncasingWebs_Timer = 0;
    uint32 PierceArmor_Timer = 0;
    uint32 RaiseUndeadScarab_Timer = 0;

    ObjectGuid WebbedPlayerGuid;
    float WebbedPlayerAggro = 0.0f;

    void Reset() override
    {
        EncasingWebs_Timer = 7000;
        PierceArmor_Timer = 15000;
        RaiseUndeadScarab_Timer = 3000;

        WebbedPlayerGuid.Clear();
        WebbedPlayerAggro = 0;
    }

    // VMaNGOS DoSpawnCreature(entry, dist, type, despawn): summon at a random point within dist
    Creature* SpawnScarab(uint32 entry)
    {
        return me->SummonCreature(entry, me->GetRandomPoint(me->GetPosition(), 10.0f), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10000ms);
    }

    void RaiseUndeadScarab(Unit* victim, bool crypt)
    {
        if (crypt)
        {
            int amount = 0;
            switch (urand(0, 2))
            {
                case 0:
                    amount = 4;
                    break;
                case 1:
                    amount = 6;
                    break;
                case 2:
                    amount = 8;
                    break;
                default:
                    break;
            }
            for (int i = 0; i < amount; i++)
            {
                if (Creature* summoned = SpawnScarab(NPC_CRYPT_SCARAB))
                    if (summoned->AI())
                        summoned->AI()->AttackStart(victim);
            }
        }
        else
        {
            if (Creature* summoned = SpawnScarab(NPC_UNDEAD_SCARAB))
                if (summoned->AI())
                    summoned->AI()->AttackStart(victim);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_NERUB, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (!WebbedPlayerGuid.IsEmpty())
        {
            if (Player* pTarget = ObjectAccessor::GetPlayer(*me, WebbedPlayerGuid))
            {
                if (!pTarget->HasAura(SPELL_ENCASINGWEBS))
                {
                    me->GetThreatManager().AddThreat(pTarget, WebbedPlayerAggro, nullptr, true, true);
                    WebbedPlayerGuid.Clear();
                    WebbedPlayerAggro = 0;
                }
            }
            else
            {
                WebbedPlayerGuid.Clear();
                WebbedPlayerAggro = 0;
            }
        }

        //EncasingWebs
        if (EncasingWebs_Timer < diff)
        {
            if (Unit* pTarget = me->GetVictim())
            {
                if (DoCast(pTarget, SPELL_ENCASINGWEBS) == SPELL_CAST_OK)
                {
                    WebbedPlayerGuid = pTarget->GetGUID();
                    WebbedPlayerAggro = me->GetThreatManager().GetThreat(pTarget);
                    me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                    EncasingWebs_Timer = urand(10000, 15000);
                }
            }
        }
        else
            EncasingWebs_Timer -= diff;

        //PierceArmor
        if (PierceArmor_Timer < diff)
        {
            if (DoCastVictim(SPELL_PIERCEARMOR) == SPELL_CAST_OK)
                PierceArmor_Timer = urand(15000, 20000);
        }
        else
            PierceArmor_Timer -= diff;

        //RaiseUndeadScarab
        if (RaiseUndeadScarab_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                RaiseUndeadScarab(target, (urand(0, 1) != 0));
                RaiseUndeadScarab_Timer = urand(6000, 10000);
            }
        }
        else
            RaiseUndeadScarab_Timer -= diff;
    }
};

void AddSC_classic_boss_nerubenkan()
{
    RegisterCreatureAI(classic_boss_nerubenkan);
}
