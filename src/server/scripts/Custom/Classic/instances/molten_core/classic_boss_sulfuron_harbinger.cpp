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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_sulfuron_harbinger.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_sulfuron

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_molten_core.h"
#include <iterator>
#include <list>

namespace
{
enum ClassicMcSulfuron : uint32
{
    SPELL_SULFURON_DARKSTRIKE          = 19777,
    SPELL_SULFURON_DEMORALIZINGSHOUT   = 19778,
    SPELL_SULFURON_INSPIRE             = 19779,
    SPELL_SULFURON_KNOCKDOWN           = 19780,
    SPELL_SULFURON_FLAMESPEAR          = 19781
};
}

// Sulfuron should walk to a random point ~10 yds behind him before casting Flamespear (VMaNGOS note)
struct classic_boss_sulfuron : public ScriptedAI
{
    classic_boss_sulfuron(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    uint32 Darkstrike_Timer = 0;
    uint32 DemoralizingShout_Timer = 0;
    uint32 Inspire_Timer = 0;
    uint32 Knockdown_Timer = 0;
    uint32 Flamespear_Timer = 0;
    InstanceScript* m_pInstance;

    void Reset() override
    {
        Darkstrike_Timer        = 10000;                     // These times are probably wrong
        DemoralizingShout_Timer = 15000;
        Inspire_Timer           = 13000;
        Knockdown_Timer         = 6000;
        Flamespear_Timer        = 2000;

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_MC_TYPE_SULFURON, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_SULFURON, IN_PROGRESS);
        DoZoneInCombat();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_SULFURON, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // DemoralizingShout_Timer
        if (DemoralizingShout_Timer < diff)
        {
            if (DoCastVictim(SPELL_SULFURON_DEMORALIZINGSHOUT) == SPELL_CAST_OK)
                DemoralizingShout_Timer = urand(15000, 20000);
        }
        else
            DemoralizingShout_Timer -= diff;

        // Inspire_Timer
        if (Inspire_Timer < diff)
        {
            Creature* target = nullptr;
            std::list<Creature*> pList = DoFindFriendlyMissingBuff(45.0f, SPELL_SULFURON_INSPIRE);
            if (!pList.empty())
            {
                auto i = pList.begin();
                std::advance(i, urand(0, uint32(pList.size()) - 1));
                target = *i;
            }

            if (target)
            {
                if (DoCast(target, SPELL_SULFURON_INSPIRE) != SPELL_CAST_OK)
                    return;
            }

            if (DoCastSelf(SPELL_SULFURON_INSPIRE) == SPELL_CAST_OK)
                Inspire_Timer = urand(20000, 26000);
        }
        else
            Inspire_Timer -= diff;

        // Knockdown_Timer
        if (Knockdown_Timer < diff)
        {
            if (DoCastVictim(SPELL_SULFURON_KNOCKDOWN) == SPELL_CAST_OK)
                Knockdown_Timer = urand(12000, 15000);
        }
        else
            Knockdown_Timer -= diff;

        // Flamespear_Timer
        if (Flamespear_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(target, SPELL_SULFURON_FLAMESPEAR) == SPELL_CAST_OK)
                    Flamespear_Timer = urand(12000, 16000);
            }
        }
        else
            Flamespear_Timer -= diff;

        // DarkStrike_Timer
        if (Darkstrike_Timer < diff)
        {
            if (DoCastVictim(SPELL_SULFURON_DARKSTRIKE) == SPELL_CAST_OK)
                Darkstrike_Timer = urand(15000, 18000);
        }
        else
            Darkstrike_Timer -= diff;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_sulfuron_harbinger()
{
    RegisterCreatureAI(classic_boss_sulfuron);
}
