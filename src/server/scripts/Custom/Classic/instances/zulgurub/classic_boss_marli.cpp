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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_marli.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_marli

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"
#include <list>

namespace
{
enum ClassicZgMarli : uint32
{
    GO_MARLI_EGG                    = 179985,

    // the spider
    NPC_MARLI_SPAWN_OF_MARLI        = 15041,

    SAY_MARLI_TRANSFORM             = 10443,
    SAY_MARLI_SPIDER_SPAWN          = 10448,
    SAY_MARLI_DEATH                 = 10459,

    SPELL_MARLI_CHARGE              = 22911,
    SPELL_MARLI_ENVELOPINGWEBS      = 24110,
    SPELL_MARLI_POISONVOLLEY        = 24099,
    SPELL_MARLI_SPIDER_FORM         = 24084,
    SPELL_MARLI_DRAIN_LIFE          = 24300,
    SPELL_MARLI_CORROSIVE_POISON    = 24111,
    SPELL_MARLI_TRANSFORM_BACK      = 24085,
    SPELL_MARLI_TRASH               = 3391,
    SPELL_MARLI_HATCH               = 24083,    // visual
    SPELL_MARLI_AGGRANDIR           = 24109
};

// VMaNGOS SetBaseWeaponDamage(default + pct) / ResetStats()
void ClassicZgMarliSetDamagePct(Creature* me, float pct)
{
    me->SetStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, pct);
    me->UpdateDamagePhysical(BASE_ATTACK);
}
}

struct classic_boss_marli : public ScriptedAI
{
    classic_boss_marli(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript())
    {
        m_uiDefaultModel = me->GetDisplayId();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiPoisonVolley_Timer = 0;
    uint32 m_uiSpawnSpider_Timer = 0;
    uint32 m_uiCharge_Timer = 0;
    uint32 m_uiAspect_Timer = 0;
    uint32 m_uiTransformBack_Timer = 0;
    uint32 m_uiDrainLife_Timer = 0;
    uint32 m_uiCorrosivePoison_Timer = 0;
    uint32 m_uiWebs_Timer = 0;
    uint32 m_uiTrash_Timer = 0;
    uint32 m_uiAggrandir_Timer = 0;

    bool m_bFirstSpidersAreSpawned = false;
    bool m_bIsInPhaseTwo = false;
    bool m_bHasWebbed = false;

    uint32 m_uiDefaultModel = 0;

    void Reset() override
    {
        m_uiPoisonVolley_Timer = 15000;
        m_uiSpawnSpider_Timer = 20000;
        m_uiAspect_Timer = 12000;
        m_uiTransformBack_Timer = 35000;
        m_uiDrainLife_Timer = 30000;
        m_uiCorrosivePoison_Timer = 1000;
        m_uiWebs_Timer = 5000;
        m_uiTrash_Timer = 5000;
        m_uiAggrandir_Timer = 0;
        m_uiCharge_Timer = 0;

        m_bFirstSpidersAreSpawned = false;
        m_bIsInPhaseTwo = false;
        m_bHasWebbed = false;

        if (m_pInstance)
            if (m_pInstance->GetData(CLASSIC_ZG_TYPE_MARLI) != DONE)
                m_pInstance->SetData(CLASSIC_ZG_TYPE_MARLI, NOT_STARTED);

        std::list<GameObject*> lSpiderEggs;
        me->GetGameObjectListWithEntryInGrid(lSpiderEggs, GO_MARLI_EGG, DEFAULT_VISIBILITY_INSTANCE);
        if (lSpiderEggs.empty())
            TC_LOG_DEBUG("scripts", "classic_boss_marli, no Eggs with the entry {} were found", uint32(GO_MARLI_EGG));
        else
        {
            for (GameObject* pGo : lSpiderEggs)
            {
                if (pGo->GetGoState() == GO_STATE_ACTIVE)
                    pGo->SetGoState(GO_STATE_READY);
            }
        }

        // World of Warcraft Client Patch 1.8.0 (2005-10-11)
        // - High Priestess Mar'li will now despawn her summoned spiders when she returns from combat.
        std::list<Creature*> lSummonedSpiders;
        me->GetCreatureListWithEntryInGrid(lSummonedSpiders, NPC_MARLI_SPAWN_OF_MARLI, DEFAULT_VISIBILITY_INSTANCE);
        for (Creature* pSpider : lSummonedSpiders)
        {
            if (TempSummon* summon = pSpider->ToTempSummon())
                summon->UnSummon();
        }

        // Cancel the phase transition stat changes.
        ClassicZgMarliSetDamagePct(me, 1.0f);   // VMaNGOS ResetStats()
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            if (m_pInstance->GetData(CLASSIC_ZG_TYPE_MARLI) != IN_PROGRESS)
                m_pInstance->SetData(CLASSIC_ZG_TYPE_MARLI, IN_PROGRESS);

        if (!m_bFirstSpidersAreSpawned)
        {
            ClassicScriptText(SAY_MARLI_SPIDER_SPAWN, me);
            DoCastSelf(SPELL_MARLI_HATCH);

            for (uint8 i = 0; i < 4; ++i)
            {
                if (GameObject* pEgg = SelectNextEgg())
                {
                    pEgg->SetGoState(GO_STATE_ACTIVE);
                    me->SummonCreature(NPC_MARLI_SPAWN_OF_MARLI, pEgg->GetPositionX(), pEgg->GetPositionY(), pEgg->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s);
                }
            }

            m_bFirstSpidersAreSpawned = true;
        }
    }

    GameObject* SelectNextEgg()
    {
        std::list<GameObject*> lEggs;
        me->GetGameObjectListWithEntryInGrid(lEggs, GO_MARLI_EGG, DEFAULT_VISIBILITY_INSTANCE);
        if (lEggs.empty())
            TC_LOG_DEBUG("scripts", "classic_boss_marli, no Eggs with the entry {} were found", uint32(GO_MARLI_EGG));
        else
        {
            lEggs.sort(Trinity::ObjectDistanceOrderPred(me));
            for (GameObject* pEgg : lEggs)
            {
                if (pEgg->GetGoState() == GO_STATE_READY)
                    return pEgg;
            }
        }
        return nullptr;
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() == NPC_MARLI_SPAWN_OF_MARLI)
        {
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                if (pSummoned->AI())
                    pSummoned->AI()->AttackStart(pTarget);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_MARLI_DEATH, me);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_MARLI, DONE);

        // Remove a Hakkar Power stack.
        me->CastSpell(me, CLASSIC_ZG_SPELL_HAKKAR_POWER_DOWN, true);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (!target)
            return;

        if (spellInfo->Id == SPELL_MARLI_ENVELOPINGWEBS)
        {
            Unit* unit = target->ToUnit();
            if (!unit || unit->GetTypeId() != TYPEID_PLAYER)
                return;

            ModifyThreatByPercent(unit, -100);
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // Troll
        if (!m_bIsInPhaseTwo)
        {
            if (m_uiPoisonVolley_Timer < uiDiff)
            {
                DoCastVictim(SPELL_MARLI_POISONVOLLEY);
                m_uiPoisonVolley_Timer = urand(10000, 20000);
            }
            else
                m_uiPoisonVolley_Timer -= uiDiff;

            if (m_uiDrainLife_Timer < uiDiff)
            {
                DoCastVictim(SPELL_MARLI_DRAIN_LIFE);
                m_uiDrainLife_Timer = urand(20000, 50000);
            }
            else
                m_uiDrainLife_Timer -= uiDiff;

            if (m_uiSpawnSpider_Timer < uiDiff)
            {
                // Mar'li summons between 1 and 4 spawns
                uint32 uiInvocCount = urand(1, 4);
                for (uint8 i = 0; i < uiInvocCount; ++i)
                {
                    if (GameObject* pEgg = SelectNextEgg())
                    {
                        pEgg->SetGoState(GO_STATE_ACTIVE);
                        me->SummonCreature(NPC_MARLI_SPAWN_OF_MARLI, pEgg->GetPositionX(), pEgg->GetPositionY(), pEgg->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s);
                    }
                }
                m_uiSpawnSpider_Timer = urand(20000, 30000);
            }
            else
                m_uiSpawnSpider_Timer -= uiDiff;

            if (m_uiAggrandir_Timer < uiDiff)
            {
                if (!me->HasAura(SPELL_MARLI_AGGRANDIR))
                    DoCastSelf(SPELL_MARLI_AGGRANDIR);

                m_uiAggrandir_Timer = urand(10000, 20000);
            }
            else
                m_uiAggrandir_Timer -= uiDiff;
        }
        // Spider
        else
        {
            if (!m_bHasWebbed && m_uiWebs_Timer < uiDiff)
            {
                DoCastVictim(SPELL_MARLI_ENVELOPINGWEBS);
                m_uiWebs_Timer = urand(10000, 15000);
                m_uiCharge_Timer = 1000;
                m_bHasWebbed = true;
            }
            else
                m_uiWebs_Timer -= uiDiff;   // sic (VMaNGOS: may wrap while webbed; reset on the charge)

            if (m_bHasWebbed && m_uiCharge_Timer < uiDiff)
            {
                // Shouldn't be random target but highestaggro not Webbed player
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::MaxThreat, 0))
                {
                    DoCast(pTarget, SPELL_MARLI_CHARGE);
                    ResetThreatList();
                    AttackStart(pTarget);
                    m_bHasWebbed = false;
                }
                m_uiWebs_Timer = urand(10000, 20000);
            }
            else
                m_uiCharge_Timer -= uiDiff;

            if (m_uiCorrosivePoison_Timer < uiDiff)
            {
                DoCastVictim(SPELL_MARLI_CORROSIVE_POISON);
                m_uiCorrosivePoison_Timer = urand(25000, 35000);
            }
            else
                m_uiCorrosivePoison_Timer -= uiDiff;
        }

        if (m_uiTransformBack_Timer < uiDiff)
        {
            if (!m_bIsInPhaseTwo)
            {
                if (me->IsNonMeleeSpellCast(false))
                    me->InterruptNonMeleeSpells(false);

                ClassicScriptText(SAY_MARLI_TRANSFORM, me);
                DoCastSelf(SPELL_MARLI_SPIDER_FORM);

                ClassicZgMarliSetDamagePct(me, 1.35f);

                ResetThreatList();

                m_bIsInPhaseTwo = true;
            }
            else
            {
                if (me->IsNonMeleeSpellCast(false))
                    me->InterruptNonMeleeSpells(false);

                DoCastSelf(SPELL_MARLI_TRANSFORM_BACK);

                me->SetDisplayId(m_uiDefaultModel);

                ClassicZgMarliSetDamagePct(me, 1.01f);

                m_bIsInPhaseTwo = false;
            }

            m_uiTransformBack_Timer = 35000;
        }
        else
            m_uiTransformBack_Timer -= uiDiff;

        if (m_uiTrash_Timer < uiDiff)
        {
            DoCastVictim(SPELL_MARLI_TRASH);
            m_uiTrash_Timer = urand(10000, 20000);
        }
        else
            m_uiTrash_Timer -= uiDiff;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_marli()
{
    RegisterCreatureAI(classic_boss_marli);
}
