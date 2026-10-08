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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_venoxis.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_venoxis

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"
#include <list>

namespace
{
enum ClassicZgVenoxis : uint32
{
    NPC_VENOXIS_RAZZASHI_COBRA  = 11373,
    NPC_VENOXIS_PARASITIC_SERPENT = 14884,

    SAY_VENOXIS_TRANSFORM       = 10026,
    SAY_VENOXIS_DEATH           = 10460,

    // P1 spells
    SPELL_VENOXIS_HOLY_NOVA     = 23858,
    SPELL_VENOXIS_DISPELL       = 23859,
    SPELL_VENOXIS_HOLY_FIRE     = 23860,
    SPELL_VENOXIS_RENEW         = 23895,
    SPELL_VENOXIS_HOLY_WRATH    = 23979,

    SPELL_VENOXIS_SNAKE_FORM    = 23849,

    // P2 spells
    SPELL_VENOXIS_TRASH         = 3391,
    SPELL_VENOXIS_POISON_CLOUD  = 23861,
    SPELL_VENOXIS_VENOMSPIT     = 23862,

    SPELL_VENOXIS_FRENZY        = 8269,
    // Cobra spell
    SPELL_VENOXIS_PARASITIC     = 23865
};
}

struct classic_boss_venoxis : public ScriptedAI
{
    classic_boss_venoxis(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript())
    {
        m_fDefaultSize = me->GetObjectScale();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiHolyFire_Timer = 0;
    uint32 m_uiHolyWrath_Timer = 0;
    uint32 m_uiVenomSpit_Timer = 0;
    uint32 m_uiRenew_Timer = 0;
    uint32 m_uiPoisonCloud_Timer = 0;
    uint32 m_uiHolyNova_Timer = 0;
    uint32 m_uiDispell_Timer = 0;
    uint32 m_uiParasitic_Timer = 0;
    uint32 m_uiTrash_Timer = 0;

    GuidSet lAddsGUIDs;

    uint8 m_uiTargetsInRangeCount = 0;

    bool m_bPhaseTwo = false;
    bool bFrenzy = false;

    float m_fDefaultSize = 1.0f;

    void Reset() override
    {
        m_uiHolyFire_Timer = 10000;
        m_uiHolyWrath_Timer = 30000;
        m_uiVenomSpit_Timer = 5500;
        m_uiRenew_Timer = 30500;
        m_uiPoisonCloud_Timer = 2000;
        m_uiDispell_Timer = 35000;
        m_uiParasitic_Timer = 10000;
        m_uiTrash_Timer = 5000;
        m_uiHolyNova_Timer = 7500;
        bFrenzy = false;

        m_uiTargetsInRangeCount = 0;

        me->SetObjectScale(m_fDefaultSize);
        // VMaNGOS ResetStats(): the snake form is an aura, removed by the evade

        m_bPhaseTwo = false;
        for (ObjectGuid const& guid : lAddsGUIDs)
            if (Creature* pSerpent = ObjectAccessor::GetCreature(*me, guid))
                pSerpent->DespawnOrUnsummon();
        lAddsGUIDs.clear();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_VENOXIS, IN_PROGRESS);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // Despawn snakes immediately when we're running home.
        std::list<Creature*> cobras;
        me->GetCreatureListWithEntryInGrid(cobras, NPC_VENOXIS_RAZZASHI_COBRA, DEFAULT_VISIBILITY_INSTANCE);
        for (Creature* cobra : cobras)
            // VMaNGOS ForcedDespawn() + Respawn() in JustReachedHome(); TC removes despawned creatures from the map,
            // so the respawn is forced here (1s) instead.
            cobra->DespawnOrUnsummon(0s, 1s);

        ScriptedAI::EnterEvadeMode(why);
    }

    void JustReachedHome() override
    {
        // Respawn snakes
        std::list<Creature*> cobras;
        me->GetCreatureListWithEntryInGrid(cobras, NPC_VENOXIS_RAZZASHI_COBRA, DEFAULT_VISIBILITY_INSTANCE);
        for (Creature* cobra : cobras)
            if (cobra && !cobra->IsAlive())
                cobra->Respawn();

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_VENOXIS, NOT_STARTED);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_VENOXIS_DEATH, me);
        me->CastSpell(me, SPELL_VENOXIS_POISON_CLOUD, true);

        me->SetObjectScale(m_fDefaultSize);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_VENOXIS, DONE);

        // Remove a Hakkar Power stack.
        me->CastSpell(me, CLASSIC_ZG_SPELL_HAKKAR_POWER_DOWN, true);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;
        if (me->IsNonMeleeSpellCast(false))
            return;

        /** Prevent exploit */
        if (me->GetPositionZ() > 43.0f || me->GetPositionZ() < 27.0f)
        {
            EnterEvadeMode(EvadeReason::Boundary);
            return;
        }

        // Handle phase change
        if (!m_bPhaseTwo && me->GetHealthPct() < 50.0f)
        {
            ClassicScriptText(SAY_VENOXIS_TRANSFORM, me);

            me->InterruptNonMeleeSpells(false);
            me->CastSpell(me, SPELL_VENOXIS_SNAKE_FORM, true);

            me->SetObjectScale(m_fDefaultSize * 2);
            ResetThreatList();
            m_bPhaseTwo = true;
        }

        if (!m_bPhaseTwo)
        {
            // Phase 1
            if (m_uiHolyNova_Timer < uiDiff)
            {
                DoCastSelf(SPELL_VENOXIS_HOLY_NOVA);
                m_uiHolyNova_Timer = urand(14000, 16000);
            }
            else
                m_uiHolyNova_Timer -= uiDiff;

            if (m_uiDispell_Timer < uiDiff)
            {
                DoCastSelf(SPELL_VENOXIS_DISPELL);
                m_uiDispell_Timer = urand(16000, 18000);
            }
            else
                m_uiDispell_Timer -= uiDiff;

            if (m_uiHolyFire_Timer < uiDiff)
            {
                DoCastVictim(SPELL_VENOXIS_HOLY_FIRE);
                m_uiHolyFire_Timer = urand(8000, 12000);
            }
            else
                m_uiHolyFire_Timer -= uiDiff;

            if (m_uiRenew_Timer < uiDiff)
            {
                DoCastSelf(SPELL_VENOXIS_RENEW);
                m_uiRenew_Timer = urand(20000, 22000);
            }
            else
                m_uiRenew_Timer -= uiDiff;

            if (m_uiHolyWrath_Timer < uiDiff)
            {
                DoCastVictim(SPELL_VENOXIS_HOLY_WRATH);
                m_uiHolyWrath_Timer = urand(15000, 25000);
            }
            else
                m_uiHolyWrath_Timer -= uiDiff;
        }
        else
        {
            // Phase 2
            if (m_uiPoisonCloud_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_VENOXIS_POISON_CLOUD) == SPELL_CAST_OK)
                    m_uiPoisonCloud_Timer = urand(7000, 10000);
            }
            else
                m_uiPoisonCloud_Timer -= uiDiff;

            if (m_uiTrash_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_VENOXIS_TRASH) == SPELL_CAST_OK)
                    m_uiTrash_Timer = urand(10000, 20000);
            }
            else
                m_uiTrash_Timer -= uiDiff;

            if (m_uiVenomSpit_Timer < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                    if (DoCast(pTarget, SPELL_VENOXIS_VENOMSPIT) == SPELL_CAST_OK)
                        m_uiVenomSpit_Timer = urand(15000, 20000);
            }
            else
                m_uiVenomSpit_Timer -= uiDiff;

            if (m_uiParasitic_Timer < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                {
                    if (DoCast(pTarget, SPELL_VENOXIS_PARASITIC) == SPELL_CAST_OK)
                    {
                        // VMaNGOS SummonCreatureAndAttack(14884, pTarget)
                        // TODO(classic): VMaNGOS SummonCreatureAndAttack() summon type/duration unknown (core not available)
                        if (Creature* pSerpent = me->SummonCreature(NPC_VENOXIS_PARASITIC_SERPENT, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s))
                        {
                            if (pSerpent->AI())
                                pSerpent->AI()->AttackStart(pTarget);
                            lAddsGUIDs.insert(pSerpent->GetGUID());
                        }
                        m_uiParasitic_Timer = 10000;
                    }
                }
                else
                    m_uiParasitic_Timer = 1000;
            }
            else
                m_uiParasitic_Timer -= uiDiff;
        }

        // FRENZY
        if (!bFrenzy && me->GetHealthPct() < 20.0f)
        {
            if (!me->HasAura(SPELL_VENOXIS_FRENZY) && DoCastSelf(SPELL_VENOXIS_FRENZY) == SPELL_CAST_OK)  // CF_AURA_NOT_PRESENT
                bFrenzy = true;
        }

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_venoxis()
{
    RegisterCreatureAI(classic_boss_venoxis);
}
