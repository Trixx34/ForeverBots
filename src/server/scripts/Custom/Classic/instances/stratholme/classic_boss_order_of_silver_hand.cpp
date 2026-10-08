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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_order_of_silver_hand.cpp (ScriptDev2 lineage, GPL-2)
// Basic script for the 5 Order of the Silver Hand members (Horde paladin epic mount, quest 9737 - TBC content).

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicOrderOfSilverHand : uint32
{
    SH_GREGOR               = 17910,
    SH_CATHELA              = 17911,
    SH_NEMAS                = 17912,
    SH_AELMAR               = 17913,
    SH_VICAR                = 17914,
    SH_QUEST_CREDIT         = 17915,

    SPELL_SH_HOLY_LIGHT     = 25263,
    SPELL_SH_DIVINE_SHIELD  = 13874
};
}

struct classic_boss_silver_hand_bosses : public ScriptedAI
{
    classic_boss_silver_hand_bosses(Creature* creature) : ScriptedAI(creature) { }

    uint32 HolyLight_Timer = 0;
    uint32 DivineShield_Timer = 0;

    void Reset() override
    {
        HolyLight_Timer = 20000;
        DivineShield_Timer = 20000;

        if (InstanceScript* instance = me->GetInstanceScript())
        {
            switch (me->GetEntry())
            {
                case SH_AELMAR:
                    instance->SetData(TYPE_SH_AELMAR, 0);
                    break;
                case SH_CATHELA:
                    instance->SetData(TYPE_SH_CATHELA, 0);
                    break;
                case SH_GREGOR:
                    instance->SetData(TYPE_SH_GREGOR, 0);
                    break;
                case SH_NEMAS:
                    instance->SetData(TYPE_SH_NEMAS, 0);
                    break;
                case SH_VICAR:
                    instance->SetData(TYPE_SH_VICAR, 0);
                    break;
                default:
                    break;
            }
        }
    }

    void JustDied(Unit* killer) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            switch (me->GetEntry())
            {
                case SH_AELMAR:
                    instance->SetData(TYPE_SH_AELMAR, 2);
                    break;
                case SH_CATHELA:
                    instance->SetData(TYPE_SH_CATHELA, 2);
                    break;
                case SH_GREGOR:
                    instance->SetData(TYPE_SH_GREGOR, 2);
                    break;
                case SH_NEMAS:
                    instance->SetData(TYPE_SH_NEMAS, 2);
                    break;
                case SH_VICAR:
                    instance->SetData(TYPE_SH_VICAR, 2);
                    break;
                default:
                    break;
            }
            if (instance->GetData(TYPE_SH_QUEST) && killer)
                if (Player* player = killer->ToPlayer())
                    player->KilledMonsterCredit(SH_QUEST_CREDIT, me->GetGUID());
        }
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        if (HolyLight_Timer < diff)
        {
            if (me->GetHealthPct() < 20.0f)
            {
                DoCast(me, SPELL_SH_HOLY_LIGHT);
                HolyLight_Timer = 20000;
            }
        }
        else
            HolyLight_Timer -= diff;

        if (DivineShield_Timer < diff)
        {
            if (me->GetHealthPct() < 5.0f)
            {
                DoCast(me, SPELL_SH_DIVINE_SHIELD);
                DivineShield_Timer = 40000;
            }
        }
        else
            DivineShield_Timer -= diff;
    }
};

void AddSC_classic_boss_order_of_silver_hand()
{
    RegisterCreatureAI(classic_boss_silver_hand_bosses);
}
