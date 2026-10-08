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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_huhuran.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_huhuran, spell_huhuran_wyvern_sting (26180), spell_huhuran_poison_bolt_volley (26052)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <list>

namespace
{
enum ClassicAQ40Huhuran : uint32
{
    EMOTE_GENERIC_FRENZY_KILL   = 7797,
    EMOTE_GENERIC_BERSERK       = 4428,

    SPELL_HUHURAN_ACIDSPIT      = 26050,
    SPELL_HUHURAN_FRENZY        = 26051,
    SPELL_HUHURAN_NOXIOUSPOISON = 26053,
    SPELL_HUHURAN_BERSERK       = 26068,
    SPELL_HUHURAN_WYVERNSTING   = 26180
};
}

struct classic_boss_huhuran : public ScriptedAI
{
    classic_boss_huhuran(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = pCreature->GetInstanceScript();
        Reset();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiFrenzyTimer;
    uint32 m_uiWyvernTimer;
    uint32 m_uiSpitTimer;
    uint32 m_uiNoxiousPoisonTimer;
    uint32 m_uiBerserkTimer;

    bool m_bBerserk;

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (me->IsValidAttackTarget(pWho)
            && !me->IsInCombat()
            && me->IsWithinDistInMap(pWho, 80.0f)
            && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_HUHURAN, IN_PROGRESS);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_HUHURAN, FAIL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_HUHURAN, DONE);
    }

    void Reset() override
    {
        m_uiFrenzyTimer        = urand(25000, 35000);
        m_uiWyvernTimer        = urand(18000, 28000);
        m_uiSpitTimer          = 8000;
        m_uiNoxiousPoisonTimer = urand(10000, 20000);
        m_uiBerserkTimer       = 5 * MINUTE * IN_MILLISECONDS;

        m_bBerserk             = false;
    }

    void UpdateAI(uint32 uiDiff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        // m_uiFrenzyTimer (VMaNGOS logic kept as is, including the timer wrap while the frenzy aura is still up)
        if (m_uiFrenzyTimer < uiDiff && !me->HasAura(SPELL_HUHURAN_FRENZY))
        {
            if (DoCastSelf(SPELL_HUHURAN_FRENZY) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_GENERIC_FRENZY_KILL, me);
                m_uiFrenzyTimer = urand(25000, 35000);
            }
        }
        else
            m_uiFrenzyTimer -= uiDiff;

        // No longer cast wyvern string during enrage
        if (!m_bBerserk)
        {
            // Wyvern Timer
            if (m_uiWyvernTimer < uiDiff)
            {
                if (DoCastVictim(SPELL_HUHURAN_WYVERNSTING) == SPELL_CAST_OK)
                    m_uiWyvernTimer = urand(15000, 32000);
            }
            else
                m_uiWyvernTimer -= uiDiff;
        }

        // Spit Timer
        if (m_uiSpitTimer < uiDiff)
        {
            if (DoCastVictim(SPELL_HUHURAN_ACIDSPIT) == SPELL_CAST_OK)
                m_uiSpitTimer = urand(5000, 10000);
        }
        else
            m_uiSpitTimer -= uiDiff;

        // Noxious Poison
        if (m_uiNoxiousPoisonTimer < uiDiff)
        {
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                if (DoCast(pTarget, SPELL_HUHURAN_NOXIOUSPOISON) == SPELL_CAST_OK)
                    m_uiNoxiousPoisonTimer = urand(12000, 24000);
        }
        else
            m_uiNoxiousPoisonTimer -= uiDiff;

        if (m_bBerserk)
        {
            // VMaNGOS CF_AURA_NOT_PRESENT
            if (!me->HasAura(SPELL_HUHURAN_BERSERK))
                DoCastSelf(SPELL_HUHURAN_BERSERK);
        }
        else if (me->GetHealthPct() < 31.0f || m_uiBerserkTimer < uiDiff)
        {
            ClassicScriptText(EMOTE_GENERIC_BERSERK, me);
            m_bBerserk = true;
        }
        else
            m_uiBerserkTimer -= uiDiff;
    }
};

// VMaNGOS OnSetTargetMap selectClosestTargets = true: keep the closest targets instead of random ones
static void ClassicHuhuranSelectClosestTargets(SpellScript const* script, std::list<WorldObject*>& targets)
{
    Unit* caster = script->GetCaster();
    if (!caster)
        return;

    uint32 maxTargets = script->GetSpellInfo()->MaxAffectedTargets;
    if (!maxTargets || targets.size() <= maxTargets)
        return;

    targets.sort(Trinity::ObjectDistanceOrderPred(caster));
    targets.resize(maxTargets);
}

// 26180 - Wyvern Sting (AQ40, Princess Huhuran)
class classic_spell_huhuran_wyvern_sting : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        ClassicHuhuranSelectClosestTargets(this, targets);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_huhuran_wyvern_sting::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// 26052 - Poison Bolt Volley (AQ40, Princess Huhuran)
class classic_spell_huhuran_poison_bolt_volley : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        ClassicHuhuranSelectClosestTargets(this, targets);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_huhuran_poison_bolt_volley::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

void AddSC_classic_boss_huhuran()
{
    RegisterCreatureAI(classic_boss_huhuran);
    RegisterSpellScript(classic_spell_huhuran_wyvern_sting);
    RegisterSpellScript(classic_spell_huhuran_poison_bolt_volley);
}
