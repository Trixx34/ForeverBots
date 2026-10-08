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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_faerlina.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_faerlina, spell_faerlina_poison_bolt_volley (28796)

#include "ScriptMgr.h"
#include "Creature.h"
#include "Log.h"
#include "Object.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include <algorithm>
#include <list>

namespace
{
enum FaerlinaData
{
    SAY_FAER_PULL               = 12856, // slay them in the masters name
    SAY_FAER_ENRAGE1            = 12857, // you cannot hide from me!
    SAY_FAER_ENRAGE2            = 12858, // kneel before me, worm!
    SAY_FAER_ENRAGE3            = 12859, // Run while you can!
    SAY_FAER_SLAY1              = 12854, // You have failed!
    SAY_FAER_SLAY2              = 12855, // Pathethic Wretch
    SAY_FAER_DEATH              = 12853, // the master... will avenge me!

    SPELL_FAER_POSIONBOLT_VOLLEY = 28796,
    SPELL_FAER_ENRAGE            = 28798,

    SPELL_FAER_RAINOFFIRE        = 28794,   //Not sure if targeted AoEs work if casted directly upon a pPlayer

    SPELL_FAER_WIDOWS_EMBRACE    = 28732,   // Used by worshippers. ToDo: Spell does NOT add the attackspeed reduction, or is it just castspeed?

    MOB_FAER_FOLLOWER            = 16505,   // TODO: should aoe silence (small range, 8-10yd)
    MOB_FAER_WORSHIPPER          = 16506
};

/*
https://www.youtube.com/watch?v=pVjB7pCX3XM
https://www.youtube.com/watch?v=iTUc8xUeLgw
^ Around 7-10sec cooldown. Times she's not casting it for 30+sec she is silenced by worshipper sacrifice.
  Might be fixed 8sec cast, but slightly delayed sometimes due to rain of fire or other reasons.
*/

uint32 FAER_POSIONBOLT_VOLLEY_CD() { return urand(10000, 12000); }
constexpr uint32 FAER_INITIAL_POISONBOLT_VOLLEY_CD = 8000;

/*
https://www.youtube.com/watch?v=pVjB7pCX3XM
https://www.youtube.com/watch?v=iTUc8xUeLgw
^ in both videos, happens somewhere between 8 and 20 seconds, though mostly between 8 and 12.
  possibly a rain we dont see when it happens after 20sec

  Initial cd seems to be around 16sec
*/

uint32 FAER_RAINOFFIRE_CD() { return urand(8000, 12000); }
constexpr uint32 FAER_RAINOFFIRE_INITIAL_CD = 16000;
}

struct classic_boss_faerlina : public ScriptedAI
{
    classic_boss_faerlina(Creature* pCreature) : ScriptedAI(pCreature), m_pInstance(GetClassicNaxxInstance(pCreature)),
        m_uiPoisonBoltVolleyTimer(FAER_INITIAL_POISONBOLT_VOLLEY_CD), m_uiRainOfFireTimer(FAER_RAINOFFIRE_INITIAL_CD),
        m_uiEnrageTimer(60000)
    {
        if (!m_pInstance)
            TC_LOG_ERROR("scripts", "classic_boss_faerlina::ctor failed to get classic_instance_naxxramas");
    }

    classic_instance_naxxramas_InstanceScript* m_pInstance;

    uint32 m_uiPoisonBoltVolleyTimer;
    uint32 m_uiRainOfFireTimer;
    uint32 m_uiEnrageTimer;

    void Reset() override
    {
        m_uiPoisonBoltVolleyTimer   = FAER_INITIAL_POISONBOLT_VOLLEY_CD;
        m_uiRainOfFireTimer         = FAER_RAINOFFIRE_INITIAL_CD;
        m_uiEnrageTimer             = 60000;
    }

    void SpellHit(WorldObject* /*pCaster*/, SpellInfo const* pSpell) override
    {
        /*
        note from wowhead:
        --
        Note: You must sacrifice the worshiper AFTER she enrages if you want to stop her for the full 60 seconds.
        If you sacrifice the Worshiper before the enrage, it will merely delay the enrage for 30 seconds.
        --
        Above note makes it seem that if she is not already enraged when widows embrace hits, we should do enrageTimer+=30000;
        while if she is enraged we set the timer to 60000 again.
        */
        if (pSpell->Id == SPELL_FAER_WIDOWS_EMBRACE)
        {
            m_uiEnrageTimer = std::max(m_uiEnrageTimer, uint32(30000));
            me->RemoveAurasDueToSpell(SPELL_FAER_ENRAGE);
        }
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_FAERLINA, FAIL);
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        ClassicScriptText(SAY_FAER_PULL, me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_FAERLINA, IN_PROGRESS);
    }

    // VMaNGOS MoveInLineOfSight override only contained a dead "//todo aggro range" check before
    // calling the base class, so the default ScriptedAI::MoveInLineOfSight is used here.

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->GetTypeId() != TYPEID_PLAYER)
            return;

        ClassicScriptText(urand(0, 1) ? SAY_FAER_SLAY1 : SAY_FAER_SLAY2, me);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        ClassicScriptText(SAY_FAER_DEATH, me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_FAERLINA, DONE);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // Evades over the balcony edge (z > 266), see HandleEvadeOutOfHome
        if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        // Poison Bolt Volley
        if (m_uiPoisonBoltVolleyTimer < uiDiff)
        {
            if (me->HasAura(SPELL_FAER_WIDOWS_EMBRACE))
            {
                m_uiPoisonBoltVolleyTimer = 2500; // retrying in 2.5sec
            }
            else
            {
                if (DoCastVictim(SPELL_FAER_POSIONBOLT_VOLLEY) == SPELL_CAST_OK)
                    m_uiPoisonBoltVolleyTimer = FAER_POSIONBOLT_VOLLEY_CD();
            }
        }
        else
            m_uiPoisonBoltVolleyTimer -= uiDiff;

        // Rain Of Fire
        if (m_uiRainOfFireTimer < uiDiff)
        {
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(pTarget, SPELL_FAER_RAINOFFIRE) == SPELL_CAST_OK)
                    m_uiRainOfFireTimer = FAER_RAINOFFIRE_CD();
            }
        }
        else
            m_uiRainOfFireTimer -= uiDiff;

        //Enrage_Timer
        if (m_uiEnrageTimer < uiDiff)
        {
            // widows embrace can be used as a preventive method rather than dispelling method for the enrage as well,
            // but then it only prevents the enrage for the duration of the debuff.
            if (!me->HasAura(SPELL_FAER_WIDOWS_EMBRACE))
            {
                if (DoCastSelf(SPELL_FAER_ENRAGE) == SPELL_CAST_OK)
                {
                    m_uiEnrageTimer = 60000;
                    ClassicScriptText(urand(SAY_FAER_ENRAGE1, SAY_FAER_ENRAGE3), me);
                }
            }
        }
        else
            m_uiEnrageTimer -= uiDiff;
    }
};

// 28796 - Poison Bolt Volley (Naxx, Faerlina)
// VMaNGOS OnSetTargetMap: unMaxTargets = 10 (both effects: school damage + periodic poison aura)
class classic_spell_faerlina_poison_bolt_volley : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        constexpr size_t maxTargets = 10;
        if (targets.size() <= maxTargets)
            return;

        // Deterministic distance-cut so both spell effects hit the same 10 targets
        // (same approach as classic_spell_noth_curse_of_the_plaguebringer).
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));
        targets.resize(maxTargets);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_faerlina_poison_bolt_volley::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

void AddSC_classic_boss_faerlina()
{
    RegisterCreatureAI(classic_boss_faerlina);
    RegisterSpellScript(classic_spell_faerlina_poison_bolt_volley);
}
