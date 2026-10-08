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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_hakkar.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_hakkar, + spell 24324 (Blood Siphon, VMaNGOS core script effect)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"

namespace
{
enum ClassicZgHakkar : uint32
{
    SAY_HAKKAR_AGGRO                = 10447,
    SAY_HAKKAR_FLEEING              = 10635,    // unused
    SAY_HAKKAR_MINION_DESTROY       = 10594,    // where does it belong?
    SAY_HAKKAR_PROTECT_ALTAR        = 10546,    // where does it belong?

    SPELL_HAKKAR_BLOODSIPHON_STUN   = 24324,    // Player stunned
    SPELL_HAKKAR_BLOODSIPHON_DAMAGE = 24323,    // "Your blood is Poisonous!"
    SPELL_HAKKAR_BLOODSIPHON_HEAL   = 24322,    // "Feeding Hakkar 1000 health per second."
    SPELL_HAKKAR_CORRUPTEDBLOOD     = 24328,
    SPELL_HAKKAR_CAUSEINSANITY      = 24327,
    SPELL_HAKKAR_WILLOFHAKKAR       = 24178,
    SPELL_HAKKAR_ENRAGE             = 24318,
    SPELL_HAKKAR_BERSERK            = 27680,

    // Aspects of High Priests
    SPELL_ASPECT_OF_JEKLIK          = 24687,    // silence 4 sec
    SPELL_ASPECT_OF_VENOXIS         = 24688,    // poison
    SPELL_ASPECT_OF_MARLI           = 24686,    // stunned 5 sec
    SPELL_ASPECT_OF_THEKAL          = 24689,    // enrage
    SPELL_ASPECT_OF_ARLOKK          = 24690,    // vanish

    SPELL_HAKKAR_POISONOUS_BLOOD    = 24321
};
}

struct classic_boss_hakkar : public ScriptedAI
{
    classic_boss_hakkar(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 BloodSiphon_Timer = 0;
    uint32 CorruptedBlood_Timer = 0;
    uint32 CauseInsanity_Timer = 0;
    ObjectGuid InsanePlayerGuid;
    float InsanePlayerAggro = 0.0f;
    uint32 Berserk_Timer = 0;
    uint32 CCDelayInsanity_Timer = 0;

    uint32 AspectOfJeklik_Timer = 0;
    uint32 AspectOfVenoxis_Timer = 0;
    uint32 AspectOfMarli_Timer = 0;
    uint32 AspectOfThekal_Timer = 0;
    uint32 AspectOfArlokk_Timer = 0;

    bool Enraged = false;

    void Reset() override
    {
        BloodSiphon_Timer = 90000;
        CorruptedBlood_Timer = 15000;
        CauseInsanity_Timer = 17000;
        InsanePlayerGuid.Clear();
        InsanePlayerAggro = 0;
        Berserk_Timer = 600000;
        CCDelayInsanity_Timer = 4000;

        AspectOfJeklik_Timer = 4000;
        AspectOfVenoxis_Timer = 7000;
        AspectOfMarli_Timer = 12000;
        AspectOfThekal_Timer = 8000;
        AspectOfArlokk_Timer = 18000;

        Enraged = false;

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_ZG_TYPE_HAKKAR, NOT_STARTED);
    }

    // VMaNGOS applies the Hakkar Power stacks from instance OnCreatureCreate(); TC does it once Hakkar is in the world
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_HAKKAR_POWER, 0);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_HAKKAR, IN_PROGRESS);
        ClassicScriptText(SAY_HAKKAR_AGGRO, me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_HAKKAR, DONE);
    }

    bool CastIfAuraNotPresent(Unit* target, uint32 spellId)
    {
        // VMaNGOS CF_AURA_NOT_PRESENT
        if (target->HasAura(spellId))
            return false;
        return DoCast(target, spellId) == SPELL_CAST_OK;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance || !UpdateVictim())
            return;

        /** Prevent exploit */
        if (me->GetPositionZ() > 57.28f || me->GetPositionZ() < 45.8f)
        {
            EnterEvadeMode(EvadeReason::Boundary);
            return;
        }

        if (CCDelayInsanity_Timer < diff)
        {
            if (!InsanePlayerGuid.IsEmpty())
            {
                if (Player* pTarget = ObjectAccessor::GetPlayer(*me, InsanePlayerGuid))
                {
                    if (!pTarget->HasAura(SPELL_HAKKAR_CAUSEINSANITY))
                    {
                        me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                        me->GetThreatManager().AddThreat(pTarget, InsanePlayerAggro, nullptr, true, true);

                        InsanePlayerGuid.Clear();
                        InsanePlayerAggro = 0;
                    }
                }
                else
                {
                    InsanePlayerGuid.Clear();
                    InsanePlayerAggro = 0;
                }
            }
        }
        else
            CCDelayInsanity_Timer -= diff;

        if (me->IsNonMeleeSpellCast(false))
            return;

        // BLOODSIPHON
        if (BloodSiphon_Timer < diff)
        {
            if (DoCastSelf(SPELL_HAKKAR_BLOODSIPHON_STUN) == SPELL_CAST_OK)
                BloodSiphon_Timer = 90000;
        }
        else
            BloodSiphon_Timer -= diff;

        // CORRUPTEDBLOOD
        if (CorruptedBlood_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(target, SPELL_HAKKAR_CORRUPTEDBLOOD) == SPELL_CAST_OK)
                    CorruptedBlood_Timer = urand(14000, 16000);
            }
        }
        else
            CorruptedBlood_Timer -= diff;

        // CAUSEINSANITY
        if (CauseInsanity_Timer < diff)
        {
            if (Unit* pTarget = me->GetVictim())
            {
                InsanePlayerGuid = pTarget->GetGUID();
                InsanePlayerAggro = GetThreat(pTarget);

                if (DoCast(pTarget, SPELL_HAKKAR_CAUSEINSANITY) == SPELL_CAST_OK)
                {
                    CCDelayInsanity_Timer = 4000;
                    //ClassicScriptText(SAY_HAKKAR_FLEEING, me);
                    CauseInsanity_Timer = urand(20000, 25000);
                }
            }
        }
        else
            CauseInsanity_Timer -= diff;

        // BERSERK
        if (Berserk_Timer < diff)
        {
            if (CastIfAuraNotPresent(me, SPELL_HAKKAR_BERSERK))
                Berserk_Timer = 2000;
        }
        else
            Berserk_Timer -= diff;

        Unit* victim = me->GetVictim();
        if (!victim)
            return;

        // Checking if Jeklik is dead. If not we cast her Aspect
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_JEKLIK) != DONE)
        {
            if (AspectOfJeklik_Timer < diff)
            {
                if (DoCast(victim, SPELL_ASPECT_OF_JEKLIK) == SPELL_CAST_OK)
                    AspectOfJeklik_Timer = urand(10000, 14000);
            }
            else
                AspectOfJeklik_Timer -= diff;
        }

        // Checking if Venoxis is dead. If not we cast his Aspect
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_VENOXIS) != DONE)
        {
            if (AspectOfVenoxis_Timer < diff)
            {
                if (DoCast(victim, SPELL_ASPECT_OF_VENOXIS) == SPELL_CAST_OK)
                    AspectOfVenoxis_Timer = 8000;
            }
            else
                AspectOfVenoxis_Timer -= diff;
        }

        // Checking if Marli is dead. If not we cast her Aspect
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_MARLI) != DONE)
        {
            if (AspectOfMarli_Timer < diff)
            {
                if (DoCast(victim, SPELL_ASPECT_OF_MARLI) == SPELL_CAST_OK)
                    AspectOfMarli_Timer = 10000;
            }
            else
                AspectOfMarli_Timer -= diff;
        }

        // Checking if Thekal is dead. If not we cast his Aspect
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_THEKAL) != DONE)
        {
            if (AspectOfThekal_Timer < diff)
            {
                if (DoCastSelf(SPELL_ASPECT_OF_THEKAL) == SPELL_CAST_OK)
                    AspectOfThekal_Timer = 15000;
            }
            else
                AspectOfThekal_Timer -= diff;
        }

        // Checking if Arlokk is dead. If not we cast her Aspect
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_ARLOKK) != DONE)
        {
            if (AspectOfArlokk_Timer < diff)
            {
                if (DoCastSelf(SPELL_ASPECT_OF_ARLOKK) == SPELL_CAST_OK)
                    AspectOfArlokk_Timer = urand(10000, 15000);
            }
            else
                AspectOfArlokk_Timer -= diff;
        }

        // melee: TC master auto-melee
    }
};

// 24324 - Blood Siphon (Hakkar)
// VMaNGOS core script effect: every hit player feeds Hakkar (24322), or poisons him (24323) when carrying Poisonous Blood (24321).
// TODO(classic): verify the VMaNGOS core behaviour and that 24322/24323 (TARGET_UNIT_NEARBY_ENTRY) accept Hakkar as explicit
// target in TC (may need a conditions row: SourceTypeOrReferenceId 13, target 14834).
class classic_spell_zg_hakkar_blood_siphon : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HAKKAR_BLOODSIPHON_DAMAGE, SPELL_HAKKAR_BLOODSIPHON_HEAL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        if (target->HasAura(SPELL_HAKKAR_POISONOUS_BLOOD))
            target->CastSpell(caster, SPELL_HAKKAR_BLOODSIPHON_DAMAGE, true);
        else
            target->CastSpell(caster, SPELL_HAKKAR_BLOODSIPHON_HEAL, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(classic_spell_zg_hakkar_blood_siphon::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_classic_boss_hakkar()
{
    RegisterCreatureAI(classic_boss_hakkar);
    RegisterSpellScript(classic_spell_zg_hakkar_blood_siphon);
}
