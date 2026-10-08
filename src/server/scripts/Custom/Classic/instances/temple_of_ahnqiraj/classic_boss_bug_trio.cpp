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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_bug_trio.cpp (Nostalrius / ScriptDev2 lineage, GPL-2)
// Scripts: boss_kri, boss_yauj, boss_vem
/*
AQ40 - The Bug Trio -- NOSTALRIUS VERSION
Reference: http://forum.nostalrius.org/viewtopic.php?f=51&t=35154

Stryg comments:

There is some problem with spell 25807 (Yauj fear) that prevents it from fearing more than 1 person at a time. In the meantime,
the script is using Magmadar's panic spell which has the exact same range and duration, just a different name/icon.
*/

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementDefines.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"

namespace
{
enum ClassicAQ40BugTrio : uint32
{
    // emote
    EMOTE_DEVOUR            = 11115,

    // kri
    SPELL_KRI_THRASH        = 3391,
    SPELL_KRI_CLEAVE        = 19983,
    SPELL_KRI_TOXIC_VOLLEY  = 25812,
    SPELL_KRI_SUMMON_CLOUD  = 25786,    // should be 26590 -> summons 15933 -> casts 25786 ("Toxic Vapors") in EventAI
    // the trigger that spawns was meleeing players and keeping them in combat, some better way to make it non-aggro?
    // yauj
    SPELL_YAUJ_RAVAGE       = 24213,
    SPELL_YAUJ_HEAL         = 25807,
    SPELL_YAUJ_FEAR         = 19408,    // should be spell 25807, but will need to fix that spell fearing 1 person max first

    // vem
    SPELL_VEM_CHARGE        = 26561,
    SPELL_VEM_KNOCKBACK     = 18813,
    SPELL_VEM_KNOCKDOWN     = 19128,
    SPELL_VEM_VENGEANCE     = 25790,
};
}

//  Base class for handling shared mechanics
struct classic_aq40_bug_trio_baseAI : public ScriptedAI
{
    classic_aq40_bug_trio_baseAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = pCreature->GetInstanceScript();
        m_uiDevourTimer = 0;
        // VMaNGOS base Reset() (the bug specific Reset() overrides do not call it)
        m_bIsEating = false;
        m_uiEvadeCheckTimer = 2500;
    }

    InstanceScript* m_pInstance;
    uint32 m_uiDevourTimer;
    uint32 m_uiEvadeCheckTimer;
    bool   m_bIsEating;

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_BUG_TRIO, FAIL);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // If somehow a raid wipes during devour phase, restore normal speed/behavior
        me->UpdateSpeed(MOVE_RUN);
        me->SetCanMelee(true);
        ScriptedAI::EnterEvadeMode(why);
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        // Reset the slain bug count on pull
        if (m_pInstance)
            m_pInstance->SetData(TYPE_BUG_TRIO, IN_PROGRESS);
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // The bug trio have a larger than normal aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 60.0f) && me->IsWithinLOSInMap(pWho)
            && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH) && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    static void TriggerDevourOn(Creature* bug, Unit* corpse)
    {
        if (!bug || !bug->IsAIEnabled())
            return;
        if (classic_aq40_bug_trio_baseAI* pFakerAI = dynamic_cast<classic_aq40_bug_trio_baseAI*>(bug->AI()))
            pFakerAI->TriggerDevour(corpse);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        if (!m_pInstance)
            return;

        // If another bug is still alive, prevent looting and trigger corpse despawn
        m_pInstance->SetData(TYPE_BUG_TRIO, SPECIAL);
        if (m_pInstance->GetData(TYPE_BUG_TRIO) != DONE)
        {
            me->SetTappedBy(nullptr);
            me->DespawnOrUnsummon(4s);

            if (ClassicTempleOfAhnQirajInstanceScript* aq40 = GetClassicTempleOfAhnQirajInstance(me))
            {
                TriggerDevourOn(aq40->GetSingleCreatureFromStorage(NPC_KRI), me);
                TriggerDevourOn(aq40->GetSingleCreatureFromStorage(NPC_PRINCESS_YAUJ), me);
                TriggerDevourOn(aq40->GetSingleCreatureFromStorage(NPC_VEM), me);
            }
        }
    }

    void CorpseRemoved(uint32& /*uiRespawnDelay*/) override
    {
        // Emote on devoured
        ClassicScriptText(EMOTE_DEVOUR, me);
    }

    void MovementInform(uint32 uiMotionType, uint32 /*uiPointId*/) override
    {
        if (uiMotionType != POINT_MOTION_TYPE)
            return;

        // Stop movement after reaching corpse
        me->GetMotionMaster()->MoveIdle();
    }

    void TriggerDevour(Unit* pWho)
    {
        if (me->IsAlive())
        {
            m_uiDevourTimer = 4000;
            m_bIsEating = true;

            // Sprint to dead bug and clear target
            // (VMaNGOS UpdateSpeed(MOVE_RUN, true, 2.7f): normal speed * 2.7; no melee while eating)
            me->UpdateSpeed(MOVE_RUN);
            me->SetSpeedRate(MOVE_RUN, me->GetSpeedRate(MOVE_RUN) * 2.7f);
            me->SetCanMelee(false);
            me->GetMotionMaster()->MovePoint(1, pWho->GetPositionX(), pWho->GetPositionY(), pWho->GetPositionZ());
            me->SetTarget(ObjectGuid::Empty);
        }
    }

    static void LeashBug(Creature* bug)
    {
        if (!bug)
            return;
        if (bug->isDead())
            bug->Respawn();
        else if (bug->IsAIEnabled())
            bug->AI()->EnterEvadeMode();
    }

    void LeashEncounter()
    {
        // Force evade all 3 bugs and respawn any that are already dead. We have to do this manually or linked bugs don't evade/respawn on region triggered EnterEvadeMode.
        if (ClassicTempleOfAhnQirajInstanceScript* aq40 = GetClassicTempleOfAhnQirajInstance(me))
        {
            LeashBug(aq40->GetSingleCreatureFromStorage(NPC_KRI));
            LeashBug(aq40->GetSingleCreatureFromStorage(NPC_PRINCESS_YAUJ));
            LeashBug(aq40->GetSingleCreatureFromStorage(NPC_VEM));
        }
    }

    virtual bool UpdateBugAI(uint32 /*uiDiff*/) { return true; }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_bIsEating)
        {
            if (m_uiDevourTimer < uiDiff)
            {
                // Reset threat, heal to full, restore target, and remove temporary speed buff
                ResetThreatList();
                m_bIsEating = false;
                me->SetFullHealth();
                me->SetTarget(me->GetVictim()->GetGUID());
                me->UpdateSpeed(MOVE_RUN);
                me->SetCanMelee(true);
                me->GetMotionMaster()->MoveChase(me->GetVictim());
            }
            else
            {
                m_uiDevourTimer -= uiDiff;
                return;
            }
        }

        // Call bug specific virtual function
        if (!UpdateBugAI(uiDiff))
            return;

        // Evade if any bug leaves the room
        if (m_uiEvadeCheckTimer < uiDiff)
        {
            m_uiEvadeCheckTimer = 2500;
            if (me->GetPositionY() < 2060 && me->GetPositionX() > -8600)
                LeashEncounter();
        }
        else
            m_uiEvadeCheckTimer -= uiDiff;
    }
};

//##########
// LORD KRI
//##########

struct classic_boss_kri : public classic_aq40_bug_trio_baseAI
{
    classic_boss_kri(Creature* pCreature) : classic_aq40_bug_trio_baseAI(pCreature) { Reset(); }

    uint32 m_uiThrashTimer;
    uint32 m_uiCleaveTimer;
    uint32 m_uiToxicVolleyTimer;

    void Reset() override
    {
        m_uiCleaveTimer = urand(4000, 8000);
        m_uiThrashTimer = urand(4000, 7000);
        m_uiToxicVolleyTimer = urand(8000, 10000);
    }

    void JustDied(Unit* pKiller) override
    {
        // Spawn Poison Cloud on death
        DoCastSelf(SPELL_KRI_SUMMON_CLOUD, true);
        classic_aq40_bug_trio_baseAI::JustDied(pKiller);
    }

    bool UpdateBugAI(uint32 uiDiff) override
    {
        // Cleave
        if (m_uiCleaveTimer < uiDiff)
        {
            if (DoCastVictim(SPELL_KRI_CLEAVE) == SPELL_CAST_OK)
                m_uiCleaveTimer = urand(5000, 12000);
        }
        else
            m_uiCleaveTimer -= uiDiff;

        // Toxic Volley
        if (m_uiToxicVolleyTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_KRI_TOXIC_VOLLEY) == SPELL_CAST_OK)
                m_uiToxicVolleyTimer = urand(8000, 14000);
        }
        else
            m_uiToxicVolleyTimer -= uiDiff;

        // Thrash
        if (m_uiThrashTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_KRI_THRASH) == SPELL_CAST_OK)
                m_uiThrashTimer = urand(2000, 8000);
        }
        else
            m_uiThrashTimer -= uiDiff;

        return true;
    }
};

//###############
// PRINCESS YAUJ
//###############

struct classic_boss_yauj : public classic_aq40_bug_trio_baseAI
{
    classic_boss_yauj(Creature* pCreature) : classic_aq40_bug_trio_baseAI(pCreature) { Reset(); }

    uint32 m_uiHealTimer;
    uint32 m_uiFearTimer;
    uint32 m_uiRavageTimer;

    void Reset() override
    {
        m_uiHealTimer = urand(10000, 20000);
        m_uiFearTimer = urand(10000, 20000);
        m_uiRavageTimer = urand(4000, 9000);
    }

    void SpellHit(WorldObject* /*pCaster*/, SpellInfo const* /*pSpellEntry*/) override
    {
        // Yauj is immune to Curse of Tongues and Mind-Numbing Poison. These spells don't have a mechanic type to set an immunity mask to in vanilla so we simply remove them.
        if (me->HasAuraType(SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK))
            me->RemoveAurasByType(SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK);
    }

    void JustDied(Unit* pKiller) override
    {
        // Spawn 10 Yauj Brood on death
        float const aCenterLoc[3] = { -8590.0f, 2138.0f, 0.0f };                    // define a central point in the room to use for LOS check

        uint32 attempts = 0; // TC safety cap: VMaNGOS re-rolls without limit
        for (int i = 0; i < 10 && attempts < 1000; ++i, ++attempts)
        {
            Position pos = me->GetRandomPoint(me->GetPosition(), 40.0f);
            // prevent Yauj Brood from spawning under the world or in walls -- re-roll if random point is not in LOS
            if (me->GetMap()->isInLineOfSight(me->GetPhaseShift(), aCenterLoc[0], aCenterLoc[1], aCenterLoc[2], pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(),
                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                me->SummonCreature(NPC_YAUJ_BROOD, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s);
            else
                --i;
        }
        classic_aq40_bug_trio_baseAI::JustDied(pKiller);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() != NPC_YAUJ_BROOD)
            return;

        CreatureAI::DoZoneInCombat(pSummoned);
    }

    bool UpdateBugAI(uint32 uiDiff) override
    {
        // Fear
        if (m_uiFearTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_YAUJ_FEAR) == SPELL_CAST_OK)
            {
                ResetThreatList();
                m_uiFearTimer = 20000;
            }
        }
        else
            m_uiFearTimer -= uiDiff;

        // Heal
        if (m_uiHealTimer < uiDiff)
        {
            // Yauj prioritizes herself with heal when under 93%
            if (me->GetHealthPct() <= 93.0f)
            {
                if (DoCastSelf(SPELL_YAUJ_HEAL) == SPELL_CAST_OK)
                    m_uiHealTimer = 12000;
            }
            else if (Unit* pTarget = DoSelectLowestHpFriendly(100.0f))
            {
                if (DoCast(pTarget, SPELL_YAUJ_HEAL) == SPELL_CAST_OK)
                    m_uiHealTimer = 12000;
            }
        }
        else
            m_uiHealTimer -= uiDiff;

        // Ravage
        if (m_uiRavageTimer < uiDiff)
        {
            if (DoCastVictim(SPELL_YAUJ_RAVAGE) == SPELL_CAST_OK)
                m_uiRavageTimer = urand(12000, 20000);
        }
        else
            m_uiRavageTimer -= uiDiff;

        return true;
    }
};

//#####
// VEM
//#####

struct classic_boss_vem : public classic_aq40_bug_trio_baseAI
{
    classic_boss_vem(Creature* pCreature) : classic_aq40_bug_trio_baseAI(pCreature) { Reset(); }

    uint32 m_uiChargeTimer;
    uint32 m_uiKnockBackTimer;
    uint32 m_uiKnockdownTimer;

    void Reset() override
    {
        m_uiChargeTimer = urand(10000, 15000);
        m_uiKnockBackTimer = urand(15000, 20000);
        m_uiKnockdownTimer = urand(5000, 8000);
    }

    void JustDied(Unit* pKiller) override
    {
        // Enrage the other bugs on death
        DoCastSelf(SPELL_VEM_VENGEANCE, true);
        classic_aq40_bug_trio_baseAI::JustDied(pKiller);
    }

    void SpellHitTarget(WorldObject* pTarget, SpellInfo const* pSpell) override
    {
        if (pSpell->Id == SPELL_VEM_KNOCKBACK && pTarget->IsPlayer())
        {
            if (Unit* victim = me->GetVictim())
                if (me->GetThreatManager().GetThreat(victim))
                    me->GetThreatManager().ModifyThreatByPercent(victim, -80);
        }
    }

    bool UpdateBugAI(uint32 uiDiff) override
    {
        // Charge
        if (m_uiChargeTimer < uiDiff)
        {
            // Only charge targets outside of melee range
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u) { return !me->IsWithinMeleeRange(u); }))
            {
                if (DoCast(pTarget, SPELL_VEM_CHARGE) == SPELL_CAST_OK)
                    m_uiChargeTimer = urand(15000, 20000);
            }
        }
        else
            m_uiChargeTimer -= uiDiff;

        // Knock Away
        if (m_uiKnockBackTimer < uiDiff)
        {
            if (me->IsWithinMeleeRange(me->GetVictim()) && !me->GetVictim()->HasUnitState(UNIT_STATE_STUNNED))
            {
                if (DoCastVictim(SPELL_VEM_KNOCKBACK) == SPELL_CAST_OK)
                    m_uiKnockBackTimer = urand(10000, 14000);
            }
        }
        else
            m_uiKnockBackTimer -= uiDiff;

        // Knockdown
        if (m_uiKnockdownTimer < uiDiff)
        {
            if (SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u) { return me->IsWithinMeleeRange(u); }))
            {
                if (DoCastVictim(SPELL_VEM_KNOCKDOWN) == SPELL_CAST_OK)
                    m_uiKnockdownTimer = urand(15000, 20000);
            }
        }
        else
            m_uiKnockdownTimer -= uiDiff;

        return true;
    }
};

void AddSC_classic_boss_bug_trio()
{
    RegisterCreatureAI(classic_boss_kri);
    RegisterCreatureAI(classic_boss_yauj);
    RegisterCreatureAI(classic_boss_vem);
}
