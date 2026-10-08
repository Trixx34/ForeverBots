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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/dire_maul/boss_zevrim.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_zevrim

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_dire_maul.h"

enum ClassicDMZevrim
{
    SPELL_INTENSIVE_PAIN =   22478,
    SPELL_SACRIFICE      =   22651
};

struct classic_boss_zevrim : public ScriptedAI
{
    classic_boss_zevrim(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;
    uint32 m_uiIntensePainTimer = 0;
    uint32 m_uiSacrificeTimer = 0;

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_BOSS_ZEVRIM, DONE);
    }

    void Reset() override
    {
        m_uiIntensePainTimer    = urand(5000, 9000);
        m_uiSacrificeTimer      = urand(9000, 12000);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || me->IsNonMeleeSpellCast(false))
            return;

        if (m_uiIntensePainTimer < diff)
        {
            DoCastSelf(SPELL_INTENSIVE_PAIN);
            m_uiIntensePainTimer = urand(20000, 26000);
        }
        else
            m_uiIntensePainTimer -= diff;

        if (m_uiSacrificeTimer < diff)
        {
            // VMaNGOS SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_NO_TOTEM)
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, [](Unit const* unit) { return !unit->IsTotem(); }))
                if (!target->IsPet())
                {
                    DoCast(target, SPELL_SACRIFICE);
                    m_uiSacrificeTimer = urand(15000, 18000);
                }
        }
        else
            m_uiSacrificeTimer -= diff;
    }
};

void AddSC_classic_boss_zevrim()
{
    RegisterCreatureAI(classic_boss_zevrim);
}
