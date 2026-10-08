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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_lucifron.cpp (Elysium Project, GPL-2)
// Scripts: boss_lucifron

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_molten_core.h"

namespace
{
enum ClassicMcLucifron : uint32
{
    SPELL_LUCIFRON_IMPENDING_DOOM = 19702,   // Inflicts 2000 Shadow damage to nearby enemies after 10 sec. Radius: 40 yards.
    SPELL_LUCIFRON_CURSE          = 19703,   // Curses nearby enemies, increasing the costs of their spells and abilities by 100% for 5 min. Radius: 40 yards.
    SPELL_LUCIFRON_SHADOW_SHOCK   = 19460,   // Instantly lashes nearby enemies with dark magic, inflicting Shadow damage. Radius: 20 yards.

    EVENT_LUCIFRON_IMPENDING_DOOM = 1,
    EVENT_LUCIFRON_CURSE          = 2,
    EVENT_LUCIFRON_SHADOW_SHOCK   = 3
};
}

struct classic_boss_lucifron : public ScriptedAI
{
    classic_boss_lucifron(Creature* creature) : ScriptedAI(creature), m_Instance(creature->GetInstanceScript()) { }

    void Reset() override
    {
        m_Events.Reset(); // wipe existing events or old timers are executed again on subsequent attempts
        m_Events.ScheduleEvent(EVENT_LUCIFRON_IMPENDING_DOOM, 10s);   // Zerix: 10s Initial Cast, Repeats every 20s.
        m_Events.ScheduleEvent(EVENT_LUCIFRON_CURSE, 20s);            // Zerix: 20s Initial Cast, Repeats every 15s.
        m_Events.ScheduleEvent(EVENT_LUCIFRON_SHADOW_SHOCK, 6s);      // Zerix: 6s Initial Cast, Repeats every 6s.

        if (m_Instance && me->IsAlive())
            m_Instance->SetData(CLASSIC_MC_TYPE_LUCIFRON, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_Instance)
            m_Instance->SetData(CLASSIC_MC_TYPE_LUCIFRON, IN_PROGRESS);

        DoZoneInCombat();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_Instance)
            m_Instance->SetData(CLASSIC_MC_TYPE_LUCIFRON, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        m_Events.Update(diff);
        while (uint32 eventId = m_Events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_LUCIFRON_IMPENDING_DOOM:
                {
                    if (DoCastSelf(SPELL_LUCIFRON_IMPENDING_DOOM) == SPELL_CAST_OK)
                        m_Events.Repeat(20s);
                    else
                        m_Events.Repeat(100ms);
                    break;
                }
                case EVENT_LUCIFRON_CURSE:
                {
                    if (DoCastSelf(SPELL_LUCIFRON_CURSE) == SPELL_CAST_OK)
                        m_Events.Repeat(15s);
                    else
                        m_Events.Repeat(100ms);
                    break;
                }
                case EVENT_LUCIFRON_SHADOW_SHOCK:
                {
                    // VMaNGOS: the event is not rescheduled if the cast fails
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        if (DoCast(target, SPELL_LUCIFRON_SHADOW_SHOCK) == SPELL_CAST_OK)
                            m_Events.Repeat(6s);
                    break;
                }
                default:
                    break;
            }
        }

        // melee: TC master auto-melee
    }

private:
    EventMap m_Events;
    InstanceScript* m_Instance;
};

void AddSC_classic_boss_lucifron()
{
    RegisterCreatureAI(classic_boss_lucifron);
}
