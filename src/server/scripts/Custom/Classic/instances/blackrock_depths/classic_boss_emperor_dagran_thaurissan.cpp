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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_emperor_dagran_thaurissan.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_emperor_dagran_thaurissan, boss_moira_bronzebeard

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "classic_blackrock_depths.h"
#include "classic_script_text.h"

enum EmperorDagranThaurissan
{
    BRD_EMPEROR_FACTION_FRIENDLY = 35,
    SAY_AGGRO                   = 5457,
    SAY_SLAY                    = 5431,
    EMOTE_SHAKEN                = 5429,

    SPELL_HANDOFTHAURISSAN      = 17492,
    SPELL_AVATAROFFLAME         = 15636,
    SPELL_IRONFOE               = 15642
};

struct classic_boss_emperor_dagran_thaurissan : public ScriptedAI
{
    classic_boss_emperor_dagran_thaurissan(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        Initialize();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiHandOfThaurissan_Timer;
    uint32 m_uiAvatarOfFlame_Timer;
    uint32 m_uiIronfoeTimer;
    uint32 m_uiCallForHelp_Timer;

    void Initialize()
    {
        m_uiHandOfThaurissan_Timer        = urand(5000, 7500);
        m_uiAvatarOfFlame_Timer           = 18000;
        m_uiIronfoeTimer                  = 9000;
        m_uiCallForHelp_Timer             = 8000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_AGGRO, me);
        me->CallForHelp(VISIBLE_RANGE);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        if (Creature* princess = me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_PRINCESS)))
        {
            if (princess->IsAlive())
            {
                princess->SetFaction(BRD_EMPEROR_FACTION_FRIENDLY);
                princess->AI()->EnterEvadeMode();
                ClassicScriptText(EMOTE_SHAKEN, princess);
            }
        }
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        ClassicScriptText(SAY_SLAY, me);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiHandOfThaurissan_Timer < diff)
        {
            if (SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
            {
                if (DoCastVictim(SPELL_HANDOFTHAURISSAN) == SPELL_CAST_OK)
                    m_uiHandOfThaurissan_Timer = urand(10000, 15000);
            }
        }
        else
            m_uiHandOfThaurissan_Timer -= diff;

        if (m_uiAvatarOfFlame_Timer < diff)
        {
            DoCastSelf(SPELL_AVATAROFFLAME);
            m_uiAvatarOfFlame_Timer = 18000;
        }
        else
            m_uiAvatarOfFlame_Timer -= diff;

        if (m_uiCallForHelp_Timer < diff)
        {
            me->CallForHelp(VISIBLE_RANGE);
            m_uiCallForHelp_Timer = 20000;
        }
        else
            m_uiCallForHelp_Timer -= diff;

        /*
        if (m_uiIronfoeTimer < diff)
        {
            if (me->CanReachWithMeleeAutoAttack(me->GetVictim()))
                if (DoCastSelf(SPELL_IRONFOE) == SPELL_CAST_OK)
                    m_uiIronfoeTimer = urand(20000, 25000);
        }
        else
            m_uiIronfoeTimer -= diff;
        */
    }
};

/*######
## boss_moira_bronzebeard
######*/

enum MoiraBronzebeard
{
    SPELL_HEAL                  = 15586,
    SPELL_RENEW                 = 10929,
    SPELL_SHIELD                = 10901,
    SPELL_MINDBLAST             = 15587,
    SPELL_SHADOWWORDPAIN        = 15654,
    SPELL_SMITE                 = 10934,
    SPELL_SHADOW_BOLT           = 15537
};

struct classic_boss_moira_bronzebeard : public ScriptedAI
{
    classic_boss_moira_bronzebeard(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        Initialize();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiHeal_Timer;
    uint32 m_uiMindBlast_Timer;
    uint32 m_uiShadowWordPain_Timer;
    uint32 m_uiSmite_Timer;

    void Initialize()
    {
        m_uiHeal_Timer = 12000;                                 //These times are probably wrong
        m_uiMindBlast_Timer = 16000;
        m_uiShadowWordPain_Timer = 2000;
        m_uiSmite_Timer = 8000;
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

        //MindBlast_Timer
        if (m_uiMindBlast_Timer < diff)
        {
            DoCastVictim(SPELL_MINDBLAST);
            m_uiMindBlast_Timer = 14000;
        }
        else
            m_uiMindBlast_Timer -= diff;

        //ShadowWordPain_Timer
        if (m_uiShadowWordPain_Timer < diff)
        {
            DoCastVictim(SPELL_SHADOWWORDPAIN);
            m_uiShadowWordPain_Timer = 18000;
        }
        else
            m_uiShadowWordPain_Timer -= diff;

        //Smite_Timer
        if (m_uiSmite_Timer < diff)
        {
            DoCastVictim(SPELL_SMITE);
            m_uiSmite_Timer = 10000;
        }
        else
            m_uiSmite_Timer -= diff;

        //healTimer
        if (m_uiHeal_Timer < diff)
        {
            if (m_pInstance)
            {
                if (Creature* emperor = me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_EMPEROR)))
                {
                    if (emperor->IsAlive() && emperor->GetHealthPct() != 100.0f)
                        DoCast(emperor, SPELL_HEAL);
                }
            }

            m_uiHeal_Timer = 10000;
        }
        else
            m_uiHeal_Timer -= diff;

        // DoMeleeAttackIfReady(); //Sredna found proof. (TC: auto melee)
    }
};

void AddSC_classic_boss_emperor_dagran_thaurissan()
{
    RegisterCreatureAI(classic_boss_emperor_dagran_thaurissan);
    RegisterCreatureAI(classic_boss_moira_bronzebeard);
}
