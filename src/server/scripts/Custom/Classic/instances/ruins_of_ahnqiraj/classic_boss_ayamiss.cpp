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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/boss_ayamiss.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_ayamiss, mob_zara_larva
// SD%Complete: 80, SDComment: evade return to start position missing

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MapReference.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"
#include <list>
#include <vector>

namespace
{
enum ClassicAQ20Ayamiss : uint32
{
    SPELL_AQ20_AYAMISS_STINGERSPRAY  = 25749,
    SPELL_AQ20_AYAMISS_POISONSTINGER = 25748,   // only used in phase1
    SPELL_AQ20_AYAMISS_PARALYZE      = 25725,
    SPELL_AQ20_AYAMISS_TRASH         = 3391,
    SPELL_AQ20_AYAMISS_FRENZY        = 8269,
    SPELL_AQ20_AYAMISS_LASH          = 25852,

    EMOTE_AQ20_AYAMISS_FRENZY        = 10645,   // %s goes into a frenzy!

    SPELL_AQ20_AYAMISS_FEED          = 25721,

    NPC_AQ20_HIVEZARA_LARVA          = 15555,
    NPC_AQ20_HIVEZARA_HORNET         = 15934,
    NPC_AQ20_HIVEZARA_SWARMER        = 15546
};

struct ClassicAQ20AyamissLocation
{
    float x, y, z;
};

ClassicAQ20AyamissLocation const ClassicAQ20AyamissLarva[] =
{
    { -9700.200195f, 1567.980835f, 23.901463f }, // old value: {-9695.0f, 1585.0f, 25.0f}
    { -9659.252930f, 1530.876587f, 22.336987f }  // old value: {-9627.0f, 1538.0f, 21.44f}
};

ClassicAQ20AyamissLocation const ClassicAQ20AyamissSwarmers[] =
{
    { -9650.0f, 1577.0f, 47.0f }
};

/** Waypoint used by Larva from the Hive'Zara to reach player sacrified */
ClassicAQ20AyamissLocation const ClassicAQ20AyamissLarvaMove[] =
{
    { -9696.986328f, 1537.282959f, 21.444189f },
    { -9703.679688f, 1530.602783f, 21.444435f },
    { -9711.226562f, 1523.762695f, 27.463711f },
    { -9715.258789f, 1519.577881f, 27.468229f }
};
}

/*######
## boss_ayamiss
######*/

struct classic_boss_ayamiss : public ScriptedAI
{
    classic_boss_ayamiss(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 m_uiStingerSpray_Timer = 0;
    uint32 m_uiPoisonStinger_Timer = 0;
    uint32 m_uiSummonSwarmer_Timer = 0;
    uint32 m_uiSummonPlayer_Timer = 0;
    uint32 m_uiTrash_Timer = 0;
    uint32 m_uiRelocate_Timer = 0;
    uint32 m_uiLash_Timer = 0;
    uint32 m_uiSacrifice_Timer = 0;

    bool m_bIsInPhaseTwo = false;
    bool m_bIsEnraged = false;
    bool m_bRelocated = false;
    bool m_bPhaseTwoBeforeTeleport = false;

    ObjectGuid m_uiSacrificeGuid;
    float m_fSacrificeAggro = 0.0f;

    void Reset() override
    {
        m_uiStingerSpray_Timer = 10000;
        m_uiPoisonStinger_Timer = 5000;
        m_uiSummonSwarmer_Timer = 60000;
        m_uiSummonPlayer_Timer = 10000;
        m_uiTrash_Timer = 10000;
        m_uiRelocate_Timer = 5000;
        m_uiLash_Timer = 15000;
        m_uiSacrifice_Timer = 0;

        m_bIsInPhaseTwo = false;
        m_bIsEnraged = false;
        m_bRelocated = false;
        m_bPhaseTwoBeforeTeleport = false;

        m_uiSacrificeGuid.Clear();
        m_fSacrificeAggro = 0;

        /** Configure Ayamiss into flying mode */
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetWalk(false);
        if (IsCombatMovementAllowed())
            SetCombatMovement(false);
        me->SetCanMelee(false); // VMaNGOS only calls DoMeleeAttackIfReady in phase 2

        /** Force despawn of invocated Hornet and Larva from Hive'Zara */
        std::list<Creature*> GardiensListe;
        GetCreatureListWithEntryInGrid(GardiensListe, me, NPC_AQ20_HIVEZARA_HORNET, 300.0f);
        for (Creature* itr : GardiensListe)
        {
            if (itr->IsAlive())
                itr->DespawnOrUnsummon();
        }
        GetCreatureListWithEntryInGrid(GardiensListe, me, NPC_AQ20_HIVEZARA_SWARMER, 300.0f);
        for (Creature* itr : GardiensListe)
        {
            if (itr->IsAlive())
                itr->DespawnOrUnsummon();
        }

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_AYAMISS, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_AYAMISS, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_AYAMISS, DONE);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_AQ20_AYAMISS_PARALYZE)
        {
            Player* pPlayer = target->ToPlayer();
            if (!pPlayer)
                return;

            m_uiSacrificeGuid = pPlayer->GetGUID();
            m_fSacrificeAggro = me->GetThreatManager().GetThreat(pPlayer);
            me->GetThreatManager().ModifyThreatByPercent(pPlayer, -100);
            m_bPhaseTwoBeforeTeleport = m_bIsInPhaseTwo;
        }
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->IsAIEnabled())
            pSummoned->AI()->DoZoneInCombat();
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (!m_bRelocated && m_uiRelocate_Timer < uiDiff && !m_bIsInPhaseTwo)
        {
            // VMaNGOS: MovePoint + MonsterMove 20 yards straight up (no pathfinding)
            me->GetMotionMaster()->MovePoint(0, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + 20.0f, false);
            m_bRelocated = true;
        }
        else
            m_uiRelocate_Timer -= uiDiff;

        if (me->GetHealthPct() <= 70.0f && !m_bIsInPhaseTwo)
        {
            SetCombatMovement(true);
            me->SetCanMelee(true);
            if (Unit* victim = me->GetVictim())
                me->GetMotionMaster()->MoveChase(victim);
            me->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING); // pathfinding desactivation
            m_bIsInPhaseTwo = true;

            /** Aggro list reset */
            std::vector<Player*> threatPlayers;
            for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                if (Player* pUnit = ref->GetVictim()->ToPlayer())
                    threatPlayers.push_back(pUnit);
            for (Player* pUnit : threatPlayers)
                me->GetThreatManager().ModifyThreatByPercent(pUnit, -100);
        }

        if (me->GetHealthPct() <= 20.0f && !m_bIsEnraged)
        {
            if (DoCastSelf(SPELL_AQ20_AYAMISS_FRENZY) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_AQ20_AYAMISS_FRENZY, me);
                m_bIsEnraged = true;
            }
        }

        if (m_uiSummonSwarmer_Timer < uiDiff)
        {
            for (uint8 i = 0; i < 20; ++i)
            {
                me->SummonCreature(NPC_AQ20_HIVEZARA_SWARMER,
                                   ClassicAQ20AyamissSwarmers[0].x + urand(0, 9),
                                   ClassicAQ20AyamissSwarmers[0].y + urand(0, 9),
                                   ClassicAQ20AyamissSwarmers[0].z + urand(0, 9),
                                   0.0f,
                                   TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT,
                                   15s);
            }
            std::list<Creature*> SwarmerList;
            GetCreatureListWithEntryInGrid(SwarmerList, me, NPC_AQ20_HIVEZARA_SWARMER, 300.0f);
            for (Creature* itr : SwarmerList)
                itr->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);

            m_uiSummonSwarmer_Timer = 60000;
        }
        else
            m_uiSummonSwarmer_Timer -= uiDiff;

        if (m_uiSummonPlayer_Timer < uiDiff)
        {
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(pTarget, SPELL_AQ20_AYAMISS_PARALYZE) == SPELL_CAST_OK)
                {
                    uint32 random = urand(0, 1);
                    if (Creature* pLarva = me->SummonCreature(NPC_AQ20_HIVEZARA_LARVA,
                        ClassicAQ20AyamissLarva[random].x, ClassicAQ20AyamissLarva[random].y, ClassicAQ20AyamissLarva[random].z,
                        0.0f, TEMPSUMMON_TIMED_DESPAWN, 15s))
                    {
                        pLarva->SetCanFly(false);
                        pLarva->SetDisableGravity(false);
                        pLarva->SetWalk(true);
                    }

                    m_uiSummonPlayer_Timer = 15000;
                    m_uiSacrifice_Timer = 10000;
                }
            }
        }
        else
            m_uiSummonPlayer_Timer -= uiDiff;

        if (m_uiSacrifice_Timer < uiDiff && !m_uiSacrificeGuid.IsEmpty())
        {
            if (!m_bPhaseTwoBeforeTeleport)
                if (Player* player = ObjectAccessor::GetPlayer(*me, m_uiSacrificeGuid))
                    if (player->IsAlive())
                        me->GetThreatManager().AddThreat(player, m_fSacrificeAggro, nullptr, true, true);
            m_uiSacrificeGuid.Clear();
            m_fSacrificeAggro = 0;
        }
        else
            m_uiSacrifice_Timer -= uiDiff;

        if (!m_bIsInPhaseTwo)
        {
            if (m_uiPoisonStinger_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_AQ20_AYAMISS_POISONSTINGER) == SPELL_CAST_OK)
                    m_uiPoisonStinger_Timer = 3000;
            }
            else
                m_uiPoisonStinger_Timer -= uiDiff;

            if (m_uiStingerSpray_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_AQ20_AYAMISS_STINGERSPRAY) == SPELL_CAST_OK)
                    m_uiStingerSpray_Timer = urand(10000, 15000);
            }
            else
                m_uiStingerSpray_Timer -= uiDiff;
        }
        else
        {
            if (m_uiStingerSpray_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_AQ20_AYAMISS_STINGERSPRAY) == SPELL_CAST_OK)
                    m_uiStingerSpray_Timer = urand(10000, 15000);
            }
            else
                m_uiStingerSpray_Timer -= uiDiff;

            if (m_uiLash_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_AQ20_AYAMISS_LASH) == SPELL_CAST_OK)
                    m_uiLash_Timer = 10000 + urand(0, 9999);
            }
            else
                m_uiLash_Timer -= uiDiff;

            if (m_uiTrash_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_AQ20_AYAMISS_TRASH) == SPELL_CAST_OK)
                    m_uiTrash_Timer = 10000 + urand(0, 9999);
            }
            else
                m_uiTrash_Timer -= uiDiff;
        }
    }
};

/*######
## mob_zara_larva
######*/

struct classic_mob_zara_larva : public ScriptedAI
{
    classic_mob_zara_larva(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiActive_Timer = 2000;
    uint8 m_waypoint = 0;
    ObjectGuid m_victimGuid;   // VMaNGOS keeps a raw Unit* pointer

    void Reset() override
    {
        m_victimGuid.Clear();
        m_waypoint = 0;
        m_uiActive_Timer = 2000;
        SetCombatMovement(false);
        me->SetCanMelee(false); // VMaNGOS AI never calls DoMeleeAttackIfReady
    }

    // VMaNGOS MonsterMove ends exactly on the destination; TC MovePoint (no pathfinding) does too - small tolerance kept
    bool IsAtLarvaPoint(uint8 index) const
    {
        ClassicAQ20AyamissLocation const& loc = ClassicAQ20AyamissLarvaMove[index];
        return me->GetExactDist(loc.x, loc.y, loc.z) < 0.5f;
    }

    void MoveToLarvaPoint(uint8 index)
    {
        ClassicAQ20AyamissLocation const& loc = ClassicAQ20AyamissLarvaMove[index];
        me->GetMotionMaster()->MovePoint(index, loc.x, loc.y, loc.z, false);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (m_uiActive_Timer > uiDiff)
        {
            m_uiActive_Timer -= uiDiff;
            return;
        }

        for (MapReference const& i : me->GetMap()->GetPlayers())
        {
            Player* pPlayer = i.GetSource();
            if (!pPlayer)
                continue;

            if (pPlayer->HasAura(SPELL_AQ20_AYAMISS_PARALYZE) && IsAtLarvaPoint(0) && m_waypoint == 1)
            {
                MoveToLarvaPoint(1);
                ++m_waypoint;
            }
            else if (pPlayer->HasAura(SPELL_AQ20_AYAMISS_PARALYZE) && IsAtLarvaPoint(1) && m_waypoint == 2)
            {
                MoveToLarvaPoint(2);
                ++m_waypoint;
            }
            else if (pPlayer->HasAura(SPELL_AQ20_AYAMISS_PARALYZE) && IsAtLarvaPoint(2) && m_waypoint == 3)
            {
                MoveToLarvaPoint(3);
                ++m_waypoint;
            }
            else if (pPlayer->HasAura(SPELL_AQ20_AYAMISS_PARALYZE) && m_waypoint == 0)
            {
                MoveToLarvaPoint(0);
                ++m_waypoint;
                m_victimGuid = pPlayer->GetGUID();
                AttackStart(pPlayer);
                me->GetThreatManager().AddThreat(pPlayer, 5000000000.0f, nullptr, true, true);
            }
            else if (!m_victimGuid.IsEmpty())
            {
                if (pPlayer->HasAura(SPELL_AQ20_AYAMISS_PARALYZE) && IsAtLarvaPoint(3) && m_waypoint == 4)
                {
                    // Spell which summon a Hornet from Hive'Zara
                    if (Unit* pVictim = ObjectAccessor::GetUnit(*me, m_victimGuid))
                        me->CastSpell(pVictim, SPELL_AQ20_AYAMISS_FEED, true);
                }
            }
        }
    }
};

void AddSC_classic_boss_ayamiss()
{
    RegisterCreatureAI(classic_boss_ayamiss);
    RegisterCreatureAI(classic_mob_zara_larva);
}
