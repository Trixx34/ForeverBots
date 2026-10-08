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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/boss_moam.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_moam
// SD%Complete: 100, SDComment: fix summon mana fiend in core, find out if there is a mana drain

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "QuaternionData.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"

namespace
{
enum ClassicAQ20Moam : uint32
{
    // VMaNGOS uses script_texts -1509000 / -1509001 / -1509028 which have no rows in the VMaNGOS DB;
    // mapped to the matching Classic broadcast texts.
    EMOTE_AQ20_MOAM_AGGRO             = 11441,  // %s senses your fear.
    EMOTE_AQ20_MOAM_MANA_FULL         = 11473,  // %s bristles with energy.
    EMOTE_AQ20_MOAM_DRAIN             = 11474,  // %s drains your mana and turns to stone.

    SPELL_AQ20_MOAM_TRAMPLE           = 15550,
    SPELL_AQ20_MOAM_ARCANEERUPTION    = 25672,
    SPELL_AQ20_MOAM_SUMMON_MANA_FIEND = 25681,  // 25682, 25683 (VMaNGOS only sends the visual)
    SPELL_AQ20_MOAM_ENERGIZE          = 25685,
    SPELL_AQ20_MOAM_DRAINMANA         = 25676,

    // mana fiend
    NPC_AQ20_MANA_FIEND               = 15527,

    GO_AQ20_LARGE_OBSIDIAN_CHUNK      = 181069
};
}

/*######
## boss_moam
######*/

struct classic_boss_moam : public ScriptedAI
{
    classic_boss_moam(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 m_uiTrample_Timer = 0;
    uint32 m_uiSummonManaFiend_Timer = 0;
    uint32 m_uiTurnBackFromStone_Timer = 0;
    uint32 m_uiArmorValue = 0;
    uint32 m_uiDrainMana_Timer = 0;
    ObjectGuid m_OGvictim;          // Memorize last target before turning into stone, then take it back.
    bool m_bIsInCombat = false;

    void Reset() override
    {
        m_uiTrample_Timer = 6000;
        m_uiSummonManaFiend_Timer = 90000;
        m_uiTurnBackFromStone_Timer = 90000;
        m_uiDrainMana_Timer = 5000;

        m_bIsInCombat = false;
        m_uiArmorValue = me->GetArmor(); // VMaNGOS m_creature->GetDefaultArmor()
        me->SetPower(POWER_MANA, 0);
        me->SetCanMelee(true); // in case the group wiped during the stone phase

        m_OGvictim.Clear();

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_MOAM, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
        ClassicScriptText(EMOTE_AQ20_MOAM_AGGRO, me);
        if (!m_bIsInCombat)
        {
            me->SetPower(POWER_MANA, 0);
            m_bIsInCombat = true;
        }

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_MOAM, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        // VMaNGOS: SummonGameObject(181069, ...) + SetRespawnTime(345600)
        me->SummonGameObject(GO_AQ20_LARGE_OBSIDIAN_CHUNK, me->GetPosition(), QuaternionData(), Seconds(CLASSIC_AQ20_RESPAWN_FOUR_DAYS), GO_SUMMON_TIMED_DESPAWN);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_MOAM, DONE);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (Unit* pTarget = SelectTarget(SelectTargetMethod::MaxThreat, 0))
        {
            if (pTarget->IsAlive())
            {
                pSummoned->AI()->AttackStart(pTarget);
                if (pSummoned->GetEntry() == NPC_AQ20_MANA_FIEND)
                {
                    // VMaNGOS: SendSpellGo(pSummoned, 25681) visual animation of the teleportation spell; not available in TC.
                    return;
                }
            }
        }
        // VMaNGOS removes every other summon (and mana fiends without a valid target)
        pSummoned->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim() && !me->HasAura(SPELL_AQ20_MOAM_ENERGIZE))
            return;

        // Once Moam got 100% mana, take back last target and launch arcane eruption
        if (me->HasAura(SPELL_AQ20_MOAM_ENERGIZE))
        {
            // VMaNGOS decrements the uint32 unconditionally (wraps) and compares == 0; clamped here so the
            // 90 seconds stone phase actually ends on the timer, as intended
            if (m_uiTurnBackFromStone_Timer <= uiDiff)
                m_uiTurnBackFromStone_Timer = 0;
            else
                m_uiTurnBackFromStone_Timer -= uiDiff;

            if (me->GetPower(POWER_MANA) >= me->GetMaxPower(POWER_MANA) || m_uiTurnBackFromStone_Timer == 0)
            {
                // Check if a victim was memorize, in case of error, take a random one.
                if (Unit* victim = ObjectAccessor::GetUnit(*me, m_OGvictim))
                    AttackStart(victim);
                // VMaNGOS only selects (and discards) a random target when the memorized victim is gone

                me->RemoveAurasDueToSpell(SPELL_AQ20_MOAM_ENERGIZE);
                me->SetCanMelee(true); // VMaNGOS resumes DoMeleeAttackIfReady
                if (Unit* victim = me->GetVictim())
                    DoCast(victim, SPELL_AQ20_MOAM_ARCANEERUPTION, true);
                ClassicScriptText(EMOTE_AQ20_MOAM_MANA_FULL, me);
                me->SetArmor(int32(m_uiArmorValue), 0);
            }
        }

        if (me->HasAura(SPELL_AQ20_MOAM_ENERGIZE))
            return;

        // Cast arcane eruption spell if not in energize mode and if mana is at 100%
        if (me->GetPower(POWER_MANA) == me->GetMaxPower(POWER_MANA))
        {
            if (Unit* victim = me->GetVictim())
                DoCast(victim, SPELL_AQ20_MOAM_ARCANEERUPTION);
            ClassicScriptText(EMOTE_AQ20_MOAM_MANA_FULL, me);
        }

        // m_uiSummonManaFiend_Timer
        if (m_uiSummonManaFiend_Timer < uiDiff)
        {
            if (DoCastSelf(SPELL_AQ20_MOAM_ENERGIZE) == SPELL_CAST_OK)
            {
                // TODO: Not sure if the armor increase is Blizzlike, please investigate. (VMaNGOS note)
                me->SetArmor(18000, 0);
                for (uint8 i = 0; i < 3; ++i)
                {
                    // Summon a Mana fiend which will disappear if Moam is reset
                    me->SummonCreature(NPC_AQ20_MANA_FIEND,
                                       me->GetPositionX() + 2.0f,
                                       me->GetPositionY(),
                                       me->GetPositionZ(),
                                       me->GetOrientation(),
                                       TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s);
                }

                m_uiSummonManaFiend_Timer = 90000;
                m_uiTurnBackFromStone_Timer = 90000;
                /** Memorize actual target to take it back, once the end of SPELL_ENERGIZE */
                if (Unit* victim = me->GetVictim())
                    m_OGvictim = victim->GetGUID();
                me->AttackStop();
                me->SetCanMelee(false); // VMaNGOS skips DoMeleeAttackIfReady during the stone phase
                ClassicScriptText(EMOTE_AQ20_MOAM_DRAIN, me);
            }
        }
        else
            m_uiSummonManaFiend_Timer -= uiDiff;

        //m_uiTrample_Timer
        if (m_uiTrample_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_MOAM_TRAMPLE) == SPELL_CAST_OK)
                m_uiTrample_Timer = 15000;
        }
        else
            m_uiTrample_Timer -= uiDiff;

        // m_uiDrainMana_Timer
        if (m_uiDrainMana_Timer < uiDiff)
        {
            DoCastSelf(SPELL_AQ20_MOAM_DRAINMANA);
            m_uiDrainMana_Timer = 7000;
        }
        else
            m_uiDrainMana_Timer -= uiDiff;
    }
};

void AddSC_classic_boss_moam()
{
    RegisterCreatureAI(classic_boss_moam);
}
