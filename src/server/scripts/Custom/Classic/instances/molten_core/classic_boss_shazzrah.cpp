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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_shazzrah.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_shazzrah, spell_shazzrah_gate (23138)

#include "ScriptMgr.h"
#include "Containers.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "classic_molten_core.h"

namespace
{
enum ClassicMcShazzrah : uint32
{
    SPELL_SHAZZRAH_ARCANEEXPLOSION  = 19712,
    SPELL_SHAZZRAH_CURSE            = 19713,
    SPELL_SHAZZRAH_DEADENMAGIC      = 19714,
    SPELL_SHAZZRAH_COUNTERSPELL     = 19715,
    SPELL_SHAZZRAH_GATE_DUMMY       = 23138                 // effect spell: 23139
};
}

struct classic_boss_shazzrah : public ScriptedAI
{
    classic_boss_shazzrah(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    uint32 ArcaneExplosion_Timer = 0;
    uint32 ShazzrahCurse_Timer = 0;
    uint32 DeadenMagic_Timer = 0;
    uint32 Countspell_Timer = 0;
    uint32 Blink_Timer = 0;

    InstanceScript* m_pInstance;

    void Reset() override
    {
        ArcaneExplosion_Timer = 2000;
        ShazzrahCurse_Timer = 10000;
        DeadenMagic_Timer = 5000;
        Countspell_Timer = 15000;
        Blink_Timer = urand(25000, 30000);

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_MC_TYPE_SHAZZRAH, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_SHAZZRAH, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_SHAZZRAH, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // ArcaneExplosion_Timer
        if (ArcaneExplosion_Timer < diff)
        {
            if (DoCastVictim(SPELL_SHAZZRAH_ARCANEEXPLOSION) == SPELL_CAST_OK)
                ArcaneExplosion_Timer = urand(3000, 5000);
        }
        else
            ArcaneExplosion_Timer -= diff;

        // ShazzrahCurse_Timer
        if (ShazzrahCurse_Timer < diff)
        {
            // VMaNGOS: CF_AURA_NOT_PRESENT
            if (Unit* victim = me->GetVictim())
                if (!victim->HasAura(SPELL_SHAZZRAH_CURSE) && DoCast(victim, SPELL_SHAZZRAH_CURSE) == SPELL_CAST_OK)
                    ShazzrahCurse_Timer = 20000;
        }
        else
            ShazzrahCurse_Timer -= diff;

        // DeadenMagic_Timer
        if (DeadenMagic_Timer < diff)
        {
            if (DoCastSelf(SPELL_SHAZZRAH_DEADENMAGIC) == SPELL_CAST_OK)
                DeadenMagic_Timer = urand(7000, 14000);
        }
        else
            DeadenMagic_Timer -= diff;

        // Countspell_Timer
        if (Countspell_Timer < diff)
        {
            if (DoCastVictim(SPELL_SHAZZRAH_COUNTERSPELL) == SPELL_CAST_OK)
                Countspell_Timer = urand(16000, 18000);
        }
        else
            Countspell_Timer -= diff;

        // Blink_Timer
        if (Blink_Timer < diff)
        {
            // Teleporting him to a random gamer and casting Arcane Explosion after that.
            if (DoCastSelf(SPELL_SHAZZRAH_GATE_DUMMY, true) == SPELL_CAST_OK)
            {
                // manual, until added effect of dummy properly
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                {
                    ResetThreatList();
                    me->NearTeleportTo(pTarget->GetPosition());
                    AttackStart(pTarget);   // VMaNGOS: Attack(pTarget, true)
                }

                Blink_Timer = urand(25000, 35000);
            }
        }
        else
            Blink_Timer -= diff;

        // melee: TC master auto-melee
    }
};

// 23138 - Gate of Shazzrah (MC, Shazzrah)
// VMaNGOS limits the area target selection to a single unit.
// TODO(classic): verify the 1.60 client data still uses TARGET_UNIT_SRC_AREA_ENEMY (VMaNGOS: effect 0 dummy, targets 22/15).
class classic_spell_shazzrah_gate : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Trinity::Containers::RandomResize(targets, 1);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_shazzrah_gate::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

void AddSC_classic_boss_shazzrah()
{
    RegisterCreatureAI(classic_boss_shazzrah);
    RegisterSpellScript(classic_spell_shazzrah_gate);
}
