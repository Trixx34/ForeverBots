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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_anubshiah.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

enum Anubshiah
{
    SPELL_SHADOWBOLT            = 15472,
    SPELL_CURSEOFTONGUES        = 15470,
    SPELL_CURSEOFWEAKNESS       = 12493,
    SPELL_DEMONARMOR            = 13787,
    SPELL_ENVELOPINGWEB         = 15471
};

struct classic_boss_anubshiah : public ScriptedAI
{
    classic_boss_anubshiah(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    uint32 ShadowBolt_Timer;
    uint32 CurseOfTongues_Timer;
    uint32 CurseOfWeakness_Timer;
    uint32 DemonArmor_Timer;
    uint32 EnvelopingWeb_Timer;

    void Initialize()
    {
        ShadowBolt_Timer = 7000;
        CurseOfTongues_Timer = 24000;
        CurseOfWeakness_Timer = 12000;
        DemonArmor_Timer = 3000;
        EnvelopingWeb_Timer = 16000;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //ShadowBolt_Timer
        if (ShadowBolt_Timer < diff)
        {
            DoCastVictim(SPELL_SHADOWBOLT);
            ShadowBolt_Timer = 7000;
        }
        else ShadowBolt_Timer -= diff;

        //CurseOfTongues_Timer
        if (CurseOfTongues_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_CURSEOFTONGUES);
            CurseOfTongues_Timer = 18000;
        }
        else CurseOfTongues_Timer -= diff;

        //CurseOfWeakness_Timer
        if (CurseOfWeakness_Timer < diff)
        {
            DoCastVictim(SPELL_CURSEOFWEAKNESS);
            CurseOfWeakness_Timer = 45000;
        }
        else CurseOfWeakness_Timer -= diff;

        //DemonArmor_Timer
        if (DemonArmor_Timer < diff)
        {
            DoCastSelf(SPELL_DEMONARMOR);
            DemonArmor_Timer = 300000;
        }
        else DemonArmor_Timer -= diff;

        //EnvelopingWeb_Timer
        if (EnvelopingWeb_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_ENVELOPINGWEB);
            EnvelopingWeb_Timer = 12000;
        }
        else EnvelopingWeb_Timer -= diff;
    }
};

void AddSC_classic_boss_anubshiah()
{
    RegisterCreatureAI(classic_boss_anubshiah);
}
