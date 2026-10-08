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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_tomb_of_seven.cpp
// (ScriptDev2 lineage, GPL-2)
// SDComment: Learning Smelt Dark Iron if tribute quest rewarded. Basic event implemented. Correct order and timing of event is unknown.

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "classic_blackrock_depths.h"
#include "classic_script_text.h"

enum TombOfSeven
{
    BRD_TOMB_FACTION_FRIENDLY           = 35,
    BRD_TOMB_FACTION_HOSTILE            = 54,

    SPELL_SHADOWBOLTVOLLEY              = 15245,
    SPELL_IMMOLATE                      = 12742,
    SPELL_CURSEOFWEAKNESS               = 12493,
    SPELL_DEMONARMOR                    = 13787,
    SPELL_SUMMON_VOIDWALKERS            = 15092,

    MAX_DWARF                           = 7,

    // VMaNGOS gossip_menu_option 1947/0 action script 1947 (DB): say 4894 + set TYPE_TOMB_OF_SEVEN to IN_PROGRESS
    GOSSIP_MENU_DOOMREL                 = 1947,
    SAY_DOOMREL_START                   = 4894
};

struct classic_boss_doomrel : public ScriptedAI
{
    classic_boss_doomrel(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_bInitalized = false;
        Initialize();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiShadowVolley_Timer;
    uint32 m_uiImmolate_Timer;
    uint32 m_uiCurseOfWeakness_Timer;
    uint32 m_uiDemonArmor_Timer;
    uint32 m_uiCallToFight_Timer;
    uint32 m_uiWipeCheck_Timer;
    uint8 m_uiDwarfRound;
    bool m_bHasSummoned;
    bool m_bInitalized;

    void Initialize()
    {
        m_uiShadowVolley_Timer = 10000;
        m_uiImmolate_Timer = 18000;
        m_uiCurseOfWeakness_Timer = 5000;
        m_uiDemonArmor_Timer = 16000;
        m_uiCallToFight_Timer = 0;
        m_uiWipeCheck_Timer = 25000;
        m_uiDwarfRound = 0;
        m_bHasSummoned = false;
    }

    void Reset() override
    {
        Initialize();
    }

    // Not part of the VMaNGOS C++ script: VMaNGOS starts the event from the DB gossip action script 1947.
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 /*gossipListId*/) override
    {
        if (menuId != GOSSIP_MENU_DOOMREL)
            return false;

        CloseGossipMenuFor(player);
        ClassicScriptText(SAY_DOOMREL_START, me, player);
        if (m_pInstance)
            m_pInstance->SetData(TYPE_TOMB_OF_SEVEN, IN_PROGRESS);
        return true;
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_TOMB_OF_SEVEN, FAIL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_TOMB_OF_SEVEN, DONE);

        //me->SummonGameObject ( 169243, 1274.655640f, -283.507874f, -78.219254f, 2.365980, 0, 0, 0, 0, 0);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            summoned->AI()->AttackStart(target);
    }

    Creature* GetDwarfForPhase(uint8 phase)
    {
        switch (phase)
        {
            case 0:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_ANGERREL));
            case 1:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_SEETHREL));
            case 2:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_DOPEREL));
            case 3:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_GLOOMREL));
            case 4:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_VILEREL));
            case 5:
                return me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_HATEREL));
            case 6:
                return me;
        }
        return nullptr;
    }

    void CallToFight(bool startFight)
    {
        if (Creature* dwarf = GetDwarfForPhase(m_uiDwarfRound))
        {
            if (startFight && dwarf->IsAlive())
            {
                dwarf->SetImmuneToPC(false);
                dwarf->SetFaction(BRD_TOMB_FACTION_HOSTILE);
                CreatureAI::DoZoneInCombat(dwarf);          // attackstart (VMaNGOS SetInCombatWithZone)
            }
            else
            {
                // TODO(classic): with TC dynamic spawns a dead dwarf may already be removed from the map (not found here)
                if (!dwarf->IsAlive() || dwarf->isDead())
                    dwarf->Respawn();

                dwarf->SetImmuneToPC(true);
                dwarf->SetFaction(BRD_TOMB_FACTION_FRIENDLY);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_pInstance)
        {
            if (m_pInstance->GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS)
            {
                if (m_uiDwarfRound < MAX_DWARF)
                {
                    if (m_uiCallToFight_Timer < diff)
                    {
                        CallToFight(true);
                        ++m_uiDwarfRound;
                        m_uiCallToFight_Timer = 30000;
                        m_uiWipeCheck_Timer = 25000;
                    }
                    else
                        m_uiCallToFight_Timer -= diff;

                    if (m_uiWipeCheck_Timer < diff)
                    {
                        if (Creature* dwarf = GetDwarfForPhase(m_uiDwarfRound - 1))
                        {
                            if (dwarf->IsAlive())
                            {
                                // VMaNGOS !SelectHostileTarget() || !GetVictim()
                                if (!dwarf->IsEngaged() || !dwarf->GetVictim())
                                {
                                    m_pInstance->SetData(TYPE_TOMB_OF_SEVEN, FAIL);
                                }
                            }
                        }
                        m_uiWipeCheck_Timer = 50000;
                    }
                    else
                        m_uiWipeCheck_Timer -= diff;
                }
            }
            else if (m_pInstance->GetData(TYPE_TOMB_OF_SEVEN) == FAIL ||
                     (m_pInstance->GetData(TYPE_TOMB_OF_SEVEN) == NOT_STARTED && !m_bInitalized))
            {
                for (m_uiDwarfRound = 0; m_uiDwarfRound < MAX_DWARF; ++m_uiDwarfRound)
                    CallToFight(false);

                m_uiDwarfRound = 0;
                m_uiCallToFight_Timer = 0;
                m_bInitalized = true;

                if (m_pInstance->GetData(TYPE_TOMB_OF_SEVEN) == FAIL)
                    m_pInstance->SetData(TYPE_TOMB_OF_SEVEN, NOT_STARTED);
            }
        }

        if (!UpdateVictim())
            return;

        //ShadowVolley_Timer
        if (m_uiShadowVolley_Timer < diff)
        {
            DoCastVictim(SPELL_SHADOWBOLTVOLLEY);
            m_uiShadowVolley_Timer = 12000;
        }
        else
            m_uiShadowVolley_Timer -= diff;

        //Immolate_Timer
        if (m_uiImmolate_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_IMMOLATE);

            m_uiImmolate_Timer = 25000;
        }
        else
            m_uiImmolate_Timer -= diff;

        //CurseOfWeakness_Timer
        if (m_uiCurseOfWeakness_Timer < diff)
        {
            DoCastVictim(SPELL_CURSEOFWEAKNESS);
            m_uiCurseOfWeakness_Timer = 45000;
        }
        else
            m_uiCurseOfWeakness_Timer -= diff;

        //DemonArmor_Timer
        if (m_uiDemonArmor_Timer < diff)
        {
            DoCastSelf(SPELL_DEMONARMOR);
            m_uiDemonArmor_Timer = 300000;
        }
        else
            m_uiDemonArmor_Timer -= diff;

        //Summon Voidwalkers
        if (!m_bHasSummoned && me->GetHealthPct() <= 50.0f)
        {
            me->CastSpell(me, SPELL_SUMMON_VOIDWALKERS, true);
            m_bHasSummoned = true;
        }
    }
};

void AddSC_classic_boss_tomb_of_seven()
{
    RegisterCreatureAI(classic_boss_doomrel);
}
