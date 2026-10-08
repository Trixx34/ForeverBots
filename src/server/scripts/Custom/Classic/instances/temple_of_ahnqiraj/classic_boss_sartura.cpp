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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_sartura.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_sartura, mob_sartura_royal_guard, mob_vekniss_guardian

/*
 * Notes:
 * Whirlwind - Sartura does a physical AoE that does 3k+ damage to everyone within 10 yards of her.
 * During this time she is immune to stuns. She tends to use this ability after a stun fades.
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementDefines.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <list>

namespace
{
enum ClassicAQ40Sartura : uint32
{
    SAY_SARTURA_AGGRO               = 11442,
    SAY_SARTURA_SLAY                = 11443,
    SAY_SARTURA_DEATH               = 11444,

    SPELL_SARTURA_WHIRLWIND         = 26083,

    // Sartura
    SPELL_SARTURA_CLEAVE            = 25174,
    SPELL_SARTURA_ENRAGE            = 26527,
    SPELL_SARTURA_ENRAGEHARD        = 27680,

    EMOTE_SARTURA_ENRAGE            = 2384,
    EMOTE_SARTURA_ENRAGEHARD        = 4428,

    // Royal Guard
    SPELL_GUARD_KNOCKBACK           = 19813,
    SPELL_GUARD_WHIRLWIND           = 26038,
};

// VMaNGOS VISIBLE_RANGE
constexpr float CLASSIC_AQ40_SARTURA_VISIBLE_RANGE = 166.0f;

void ClassicSarturaAssignThreat(Creature* me, Unit* target)
{
    me->GetThreatManager().ResetAllThreat();
    me->GetThreatManager().AddThreat(target, float(urand(1000, 2000)), nullptr, true, true);
}

// Respawn dead / evade living Sartura's Royal Guards
void ClassicSarturaLeashGuards(Creature* me)
{
    ClassicTempleOfAhnQirajInstanceScript* aq40 = GetClassicTempleOfAhnQirajInstance(me);
    if (!aq40)
        return;

    GuidList m_lRoyalGuardsGuid;
    aq40->GetRoyalGuardGUIDList(m_lRoyalGuardsGuid);
    for (ObjectGuid const& guid : m_lRoyalGuardsGuid)
    {
        if (Creature* pRoyalGuard = me->GetMap()->GetCreature(guid))
        {
            if (pRoyalGuard->isDead())
                pRoyalGuard->Respawn();
            else if (pRoyalGuard->IsAIEnabled())
                pRoyalGuard->AI()->EnterEvadeMode();
        }
    }
}
}

// ********************
// Battleguard Sartura
// ********************

struct classic_boss_sartura : public ScriptedAI
{
    classic_boss_sartura(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = pCreature->GetInstanceScript();
        Reset();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiCleaveTimer;
    uint32 m_uiWhirlWindTimer;
    uint32 m_uiWhirlWindEndTimer;           //15s
    uint32 m_uiAggroResetTimer;
    uint32 m_uiEnrageHardTimer;
    uint32 m_uiEvadeCheckTimer;

    bool m_bIsEnraged;
    bool m_bAttackOff;

    void Reset() override
    {
        m_uiCleaveTimer = 4000;
        m_uiWhirlWindTimer = urand(8000, 12000);
        m_uiWhirlWindEndTimer = 0;
        m_uiAggroResetTimer = urand(5000, 7500);

        m_uiEnrageHardTimer = 10 * 60000;
        m_bIsEnraged = false;
        m_bAttackOff = false;

        m_uiEvadeCheckTimer = 2500;
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // Sartura has a very large aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 85.0f) && me->IsWithinLOSInMap(pWho)
            && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH) && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        LeashEncounter();
        ScriptedAI::EnterEvadeMode(why);
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        ClassicScriptText(SAY_SARTURA_AGGRO, me);
        DoZoneInCombat();

        if (m_pInstance)
            m_pInstance->SetData(TYPE_SARTURA, IN_PROGRESS);
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        ClassicScriptText(SAY_SARTURA_SLAY, me);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        ClassicScriptText(SAY_SARTURA_DEATH, me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_SARTURA, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_SARTURA, FAIL);
    }

    void AssignRandomThreat()
    {
        if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
        {
            if (me->IsWithinDist(pTarget, CLASSIC_AQ40_SARTURA_VISIBLE_RANGE))
                ClassicSarturaAssignThreat(me, pTarget);
        }
    }

    void LeashEncounter()
    {
        ClassicSarturaLeashGuards(me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiWhirlWindEndTimer)                          // Is in Whirlwind
        {
            // While in whirlwind, switch to random targets often
            if (m_uiAggroResetTimer < uiDiff)
            {
                AssignRandomThreat();
                m_uiAggroResetTimer = urand(1000, 2000);
            }
            else
                m_uiAggroResetTimer -= uiDiff;

            // End Whirlwind Phase
            if (m_uiWhirlWindEndTimer <= uiDiff)
            {
                m_uiWhirlWindEndTimer = 0;
                m_uiWhirlWindTimer = urand(5000, 10000);
                m_uiAggroResetTimer = urand(3000, 7000);
                // Remove the negative haste modifier from Whirlwind to restore Sartura's auto attack
                // (VMaNGOS ApplyAttackTimePercentMod(BASE_ATTACK, 0, true) is a no-op value change)
                me->setAttackTimer(BASE_ATTACK, 100);
            }
            else
                m_uiWhirlWindEndTimer -= uiDiff;
        }
        else // if (!m_uiWhirlWindEndTimer)                 // Is not in whirlwind
        {
            // Enter Whirlwind Phase
            if (m_uiWhirlWindTimer < uiDiff)
            {
                if (DoCastSelf(SPELL_SARTURA_WHIRLWIND) == SPELL_CAST_OK)
                {
                    AssignRandomThreat();
                    m_uiWhirlWindEndTimer = 15000;
                    m_uiAggroResetTimer = urand(1000, 2000);
                }
            }
            else
                m_uiWhirlWindTimer -= uiDiff;

            // Aquire a new target sometimes
            if (m_uiAggroResetTimer < uiDiff)
            {
                AssignRandomThreat();
                m_uiAggroResetTimer = urand(3000, 7000);
            }
            else
                m_uiAggroResetTimer -= uiDiff;

            // Sundering Cleave
            if (m_uiCleaveTimer < uiDiff)
            {
                if (DoCastVictim(SPELL_SARTURA_CLEAVE) == SPELL_CAST_OK)
                    m_uiCleaveTimer = urand(3000, 4000);
            }
            else
                m_uiCleaveTimer -= uiDiff;
        }

        // If she is <20% enrage
        if (!m_bIsEnraged && me->GetHealthPct() <= 20.0f)
        {
            if (DoCastSelf(SPELL_SARTURA_ENRAGE, CastSpellExtraArgs(m_uiWhirlWindEndTimer != 0)) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_SARTURA_ENRAGE, me);
                m_bIsEnraged = true;
            }
        }

        // After 10 minutes hard enrage
        if (m_uiEnrageHardTimer)
        {
            if (m_uiEnrageHardTimer <= uiDiff)
            {
                if (DoCastSelf(SPELL_SARTURA_ENRAGEHARD, CastSpellExtraArgs(m_uiWhirlWindEndTimer != 0)) == SPELL_CAST_OK)
                {
                    ClassicScriptText(EMOTE_SARTURA_ENRAGEHARD, me);
                    m_uiEnrageHardTimer = 0;
                    m_bIsEnraged = true;
                }
            }
            else
                m_uiEnrageHardTimer -= uiDiff;
        }

        // Leash check
        if (m_uiEvadeCheckTimer < uiDiff)
        {
            m_uiEvadeCheckTimer = 2500;
            if (me->GetPositionY() > 1780)
            {
                EnterEvadeMode(EvadeReason::Boundary);
                LeashEncounter();
            }
        }
        else
            m_uiEvadeCheckTimer -= uiDiff;
    }
};

// *********************
// Sartura's Royal Guard
// *********************

struct classic_mob_sartura_royal_guard : public ScriptedAI
{
    classic_mob_sartura_royal_guard(Creature* pCreature) : ScriptedAI(pCreature)
    {
        Reset();
    }

    uint32 m_uiKnockbackTimer;
    uint32 m_uiWhirlWindTimer;
    uint32 m_uiWhirlWindEndTimer;           //15s
    uint32 m_uiAggroResetTimer;
    uint32 m_uiEvadeCheckTimer;

    void Reset() override
    {
        m_uiKnockbackTimer = urand(6000, 12000);
        m_uiWhirlWindTimer = urand(8000, 10000);
        m_uiWhirlWindEndTimer = 0;
        m_uiAggroResetTimer = urand(5000, 7500);
        m_uiEvadeCheckTimer = 2500;
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        DoZoneInCombat();
    }

    void AssignRandomThreat()
    {
        if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
            ClassicSarturaAssignThreat(me, pTarget);
    }

    void LeashEncounter()
    {
        ClassicTempleOfAhnQirajInstanceScript* aq40 = GetClassicTempleOfAhnQirajInstance(me);
        if (!aq40)
            return;

        if (Creature* pSartura = aq40->GetSingleCreatureFromStorage(NPC_BATTLEGUARD_SARTURA))
        {
            if (pSartura->IsAlive())
            {
                if (pSartura->IsAIEnabled())
                    pSartura->AI()->EnterEvadeMode();

                ClassicSarturaLeashGuards(me);
            }
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiWhirlWindEndTimer)                          // Is in Whirlwind
        {
            // While in whirlwind, switch to random targets often
            if (m_uiAggroResetTimer < uiDiff)
            {
                AssignRandomThreat();
                m_uiAggroResetTimer = urand(1000, 2000);
            }
            else
                m_uiAggroResetTimer -= uiDiff;

            // End Whirlwind Phase
            if (m_uiWhirlWindEndTimer <= uiDiff)
            {
                m_uiWhirlWindEndTimer = 0;
                m_uiWhirlWindTimer = urand(2000, 6000);
                m_uiAggroResetTimer = urand(3000, 7000);
            }
            else
                m_uiWhirlWindEndTimer -= uiDiff;
        }
        else // if (!m_uiWhirlWindEndTimer)                 // Is not in whirlwind
        {
            // Enter Whirlwind Phase
            if (m_uiWhirlWindTimer < uiDiff)
            {
                if (DoCastSelf(SPELL_GUARD_WHIRLWIND) == SPELL_CAST_OK)
                {
                    AssignRandomThreat();
                    m_uiWhirlWindEndTimer = 8000;
                    m_uiAggroResetTimer = urand(1000, 2000);
                }
            }
            else
                m_uiWhirlWindTimer -= uiDiff;

            // Aquire a new target sometimes
            if (m_uiAggroResetTimer < uiDiff)
            {
                AssignRandomThreat();
                m_uiAggroResetTimer = urand(3000, 7000);
            }
            else
                m_uiAggroResetTimer -= uiDiff;

            // Knockback
            if (m_uiKnockbackTimer < uiDiff)
            {
                if (me->IsWithinMeleeRange(me->GetVictim()))
                    if (DoCastVictim(SPELL_GUARD_KNOCKBACK) == SPELL_CAST_OK)
                        m_uiKnockbackTimer = urand(8000, 14000);
            }
            else
                m_uiKnockbackTimer -= uiDiff;
        }

        // Leash check
        if (m_uiEvadeCheckTimer < uiDiff)
        {
            m_uiEvadeCheckTimer = 2500;
            if (me->GetPositionY() > 1780)
                LeashEncounter();
        }
        else
            m_uiEvadeCheckTimer -= uiDiff;
    }
};

// ****************
// Vekniss Guardian
// ****************

namespace
{
enum ClassicAQ40VeknissGuardian : uint32
{
    SPELL_GUARDIAN_IMPALE       = 26025,
    SPELL_GUARDIAN_FRENZY       = 8599,

    EMOTE_GUARDIAN_EMIT         = 10755,
    EMOTE_GUARDIAN_FRENZY       = 10645,
    SOUND_GUARDIAN_CHARGE       = 3330,
};

// array of GUIDs permitted to emote on aggro (VMaNGOS creature guids; TC spawn id = 20000000 + VMaNGOS guid)
uint32 const aEmoteGUIDs[8] = { 87595, 87671, 87610, 87611, 87618, 87627, 87628, 87641 };
constexpr uint32 CLASSIC_VMANGOS_CREATURE_GUID_OFFSET = 20000000;
}

struct classic_mob_vekniss_guardian : public ScriptedAI
{
    classic_mob_vekniss_guardian(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_uiImpaleTimer = 0;
        m_bIsAlone = false;
        Reset();
    }

    uint32 m_uiImpaleTimer;
    uint32 m_uiEmoteTimer;
    uint32 m_uiEvadeCheckTimer;

    bool m_bCalledForHelp;
    bool m_bFrenzied;
    bool m_bIsAlone;

    void Reset() override
    {
        m_bCalledForHelp = false;
        m_bFrenzied = false;
        m_uiEmoteTimer = 0;
        m_uiEvadeCheckTimer = 2500;
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        for (uint32 i : aEmoteGUIDs)
        {
            if (me->GetSpawnId() == CLASSIC_VMANGOS_CREATURE_GUID_OFFSET + i)
            {
                m_uiEmoteTimer = 2500;
                break;
            }
        }
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // Increased aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 50.0f) && me->IsWithinLOSInMap(pWho)
            && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH) && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        me->UpdateSpeed(MOVE_RUN);
        ScriptedAI::EnterEvadeMode(why);
    }

    void ImpaleAssist(Unit* pWho)
    {
        // VMaNGOS UpdateSpeed(MOVE_RUN, true, 2.5): normal speed * 2.5
        me->UpdateSpeed(MOVE_RUN);
        me->SetSpeedRate(MOVE_RUN, me->GetSpeedRate(MOVE_RUN) * 2.5f);
        me->GetMotionMaster()->MovePoint(1, pWho->GetPositionX(), pWho->GetPositionY(), pWho->GetPositionZ());
        me->PlayDistanceSound(SOUND_GUARDIAN_CHARGE);
    }

    void MovementInform(uint32 uiMotionType, uint32 /*uiPointId*/) override
    {
        if (uiMotionType != POINT_MOTION_TYPE)
            return;

        DoCastSelf(SPELL_GUARDIAN_IMPALE);
        me->UpdateSpeed(MOVE_RUN);
        if (Unit* victim = me->GetVictim())
            me->GetMotionMaster()->MoveChase(victim);
    }

    void DamageTaken(Unit* /*pDoneBy*/, uint32& /*uiDamage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (me->GetHealthPct() < 25.0f && !m_bCalledForHelp)
        {
            m_bCalledForHelp = true;
            m_bIsAlone = true;
            std::list<Creature*> lAssistList;
            GetCreatureListWithEntryInGrid(lAssistList, me, NPC_VEKNISS_GUARDIAN, 45.0f);

            for (Creature* itr : lAssistList)
            {
                if (itr->GetGUID() == me->GetGUID())
                    continue;

                if (itr->IsAlive() && me->IsWithinLOSInMap(itr))
                {
                    if (m_bIsAlone)
                        m_bIsAlone = false;
                    if (classic_mob_vekniss_guardian* pVeknissAI = dynamic_cast<classic_mob_vekniss_guardian*>(itr->AI()))
                        pVeknissAI->ImpaleAssist(me);
                }
            }
            if (m_bIsAlone)
                DoCastSelf(SPELL_GUARDIAN_IMPALE);
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // TODO(classic): VMaNGOS relocated the guardian onto its victim every 2.5s while IsInEvadeMode() (anti evade-bug hack);
        // TC's UpdateVictim() never lets the AI run while evading, so the hack is dropped.
        if (m_uiEvadeCheckTimer < uiDiff)
            m_uiEvadeCheckTimer = 2500;
        else
            m_uiEvadeCheckTimer -= uiDiff;

        if (m_uiEmoteTimer)
        {
            if (m_uiEmoteTimer < uiDiff)
            {
                ClassicScriptText(EMOTE_GUARDIAN_EMIT, me);
                m_uiEmoteTimer = 0;
            }
            else
                m_uiEmoteTimer -= uiDiff;
        }

        if (me->GetHealthPct() < 30.0f && !m_bFrenzied)
        {
            if (DoCastSelf(SPELL_GUARDIAN_FRENZY) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_GUARDIAN_FRENZY, me);
                m_bFrenzied = true;
            }
        }

        if (m_uiImpaleTimer)                                                                    // stop chasing momentarily after casting impale to prevent z-axis problems
        {
            if (m_uiImpaleTimer < uiDiff)
                m_uiImpaleTimer = 0;
            else
                m_uiImpaleTimer -= uiDiff;
        }
    }
};

void AddSC_classic_boss_sartura()
{
    RegisterCreatureAI(classic_boss_sartura);
    RegisterCreatureAI(classic_mob_sartura_royal_guard);
    RegisterCreatureAI(classic_mob_vekniss_guardian);
}
