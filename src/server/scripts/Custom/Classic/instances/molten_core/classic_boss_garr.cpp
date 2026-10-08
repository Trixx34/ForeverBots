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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_garr.cpp (GPL-2)
// Scripts: boss_garr, mob_firesworn

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "classic_molten_core.h"
#include "classic_script_text.h"
#include <algorithm>
#include <list>
#include <vector>

namespace
{
enum ClassicMcGarr : uint32
{
    // Spells
    SPELL_GARR_ANTIMAGICPULSE     = 19492,
    SPELL_GARR_MAGMASHACKLES      = 19496,
    SPELL_GARR_ENRAGE             = 19516, // stacks up to 10 times
    SPELL_GARR_SEPARATION_ANXIETY = 23487,
    SPELL_GARR_ERUPTION_TRIGGER   = 20482,
    SPELL_GARR_ENRAGE_TRIGGER     = 19515,

    // Texts
    EMOTE_GARR_MASSIVE_ERUPTION   = 8254,

    // Events
    EVENT_GARR_ANTIMAGICPULSE     = 1,
    EVENT_GARR_MAGMASHACKLES      = 2,
    EVENT_GARR_MASSIVE_ERUPTION   = 3
};

enum ClassicMcGarrAdds : uint32
{
    // Spells
    SPELL_FIRESWORN_THRASH           = 3391,
    SPELL_FIRESWORN_IMMOLATE         = 15732,
    SPELL_FIRESWORN_ADD_ERUPTION     = 19497,
    SPELL_FIRESWORN_MASSIVE_ERUPTION = 20483,

    // Events
    EVENT_FIRESWORN_IMMOLATE         = 1
};
}

struct classic_boss_garr : public ScriptedAI
{
    classic_boss_garr(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    EventMap m_CombatEvents;
    InstanceScript* m_pInstance;
    std::vector<ObjectGuid> m_lFiresworn;

    void Reset() override
    {
        m_CombatEvents.Reset();
        m_lFiresworn.clear();

        if (me->IsAlive() && m_pInstance && m_pInstance->GetData(CLASSIC_MC_TYPE_GARR) != DONE)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GARR, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!m_pInstance)
            return;

        if (m_pInstance->GetData(CLASSIC_MC_TYPE_GARR) == DONE)
        {
            me->DespawnOrUnsummon();
            return;
        }

        m_pInstance->SetData(CLASSIC_MC_TYPE_GARR, IN_PROGRESS);
        DoZoneInCombat();

        // Store add guids
        std::list<Creature*> adds;
        me->GetCreatureListWithEntryInGrid(adds, CLASSIC_MC_NPC_FIRESWORN, 150.0f);
        m_lFiresworn.clear();
        for (Creature* add : adds)
        {
            m_lFiresworn.push_back(add->GetGUID());

            if (!add->HasAura(SPELL_GARR_SEPARATION_ANXIETY))
                DoCast(add, SPELL_GARR_SEPARATION_ANXIETY, true);
        }

        ScheduleCombatEvents();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GARR, DONE);
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_GARR_ENRAGE_TRIGGER)
            DoCastSelf(SPELL_GARR_ENRAGE, true);
    }

    void FireswornJustDied(ObjectGuid addGuid)
    {
        // Remove add from guid list
        auto it = std::find(m_lFiresworn.begin(), m_lFiresworn.end(), addGuid);
        if (it != m_lFiresworn.end())
            m_lFiresworn.erase(it);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        m_CombatEvents.Update(diff);
        UpdateEvents();
    }

    void ScheduleCombatEvents()
    {
        m_CombatEvents.RescheduleEvent(EVENT_GARR_ANTIMAGICPULSE,   15s);
        m_CombatEvents.RescheduleEvent(EVENT_GARR_MAGMASHACKLES,    10s);
        m_CombatEvents.RescheduleEvent(EVENT_GARR_MASSIVE_ERUPTION, 6min);
    }

    void UpdateEvents()
    {
        while (uint32 const eventId = m_CombatEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_GARR_ANTIMAGICPULSE:
                {
                    // Garr lets out anti-magic pulses, removing a beneficial effect from players in the raid.
                    if (DoCastSelf(SPELL_GARR_ANTIMAGICPULSE) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(Seconds(urand(15, 20)));
                        return;
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                case EVENT_GARR_MAGMASHACKLES:
                {
                    // Garr reduces the movement speed of nearby enemies by 60% for 15 seconds.
                    if (DoCastSelf(SPELL_GARR_MAGMASHACKLES) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(Seconds(urand(10, 15)));
                        return;
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                case EVENT_GARR_MASSIVE_ERUPTION:
                {
                    // After 6 minutes, Garr will let his adds explode and deal massive damage.
                    // Every further 20s in fight, another add will explode.
                    if (!m_lFiresworn.empty())
                    {
                        uint32 const randomIndex = urand(0, uint32(m_lFiresworn.size()) - 1);
                        if (Creature* pRandomFiresworn = me->GetMap()->GetCreature(m_lFiresworn[randomIndex]))
                        {
                            if (!pRandomFiresworn->HasAuraType(SPELL_AURA_MOD_STUN))
                            {
                                ClassicScriptText(EMOTE_GARR_MASSIVE_ERUPTION, me);
                                me->CastSpell(pRandomFiresworn, SPELL_GARR_ERUPTION_TRIGGER, true);
                            }
                        }
                    }
                    m_CombatEvents.Repeat(20s);
                    return;
                }
                default:
                    break;
            }
        }
    }
};

struct classic_mob_firesworn : public ScriptedAI
{
    classic_mob_firesworn(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    EventMap m_CombatEvents;
    InstanceScript* m_pInstance;

    bool m_bForceExplosion = false;

    void Reset() override
    {
        m_bForceExplosion = false;
        m_CombatEvents.Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!me->HasAura(SPELL_FIRESWORN_THRASH))
            DoCastSelf(SPELL_FIRESWORN_THRASH, true);
        DoZoneInCombat();
        ScheduleCombatEvents();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        // Garr gains 9% attack speed for every Firesworn slain.
        if (Creature* pGarr = me->GetMap()->GetCreature(m_pInstance->GetGuidData(CLASSIC_MC_DATA_GARR)))
        {
            if (pGarr->IsAlive())
            {
                me->CastSpell(pGarr, SPELL_GARR_ENRAGE_TRIGGER, true);
                if (classic_boss_garr* pGarrAI = dynamic_cast<classic_boss_garr*>(pGarr->AI()))
                    pGarrAI->FireswornJustDied(me->GetGUID());
            }
        }

        if (!m_bForceExplosion)
        {
            // On death, Firesworns will explode, dealing massive Fire damage and knocking players back.
            me->CastSpell(me, SPELL_FIRESWORN_ADD_ERUPTION, true);
        }
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_GARR_ERUPTION_TRIGGER)
        {
            m_bForceExplosion = true;
            me->CastSpell(me, SPELL_FIRESWORN_MASSIVE_ERUPTION, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        m_CombatEvents.Update(diff);
        UpdateEvents();
    }

    void ScheduleCombatEvents()
    {
        m_CombatEvents.RescheduleEvent(EVENT_FIRESWORN_IMMOLATE, 10s);
    }

    void UpdateEvents()
    {
        while (uint32 const eventId = m_CombatEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FIRESWORN_IMMOLATE:
                {
                    if (DoCastVictim(SPELL_FIRESWORN_IMMOLATE) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(20s);
                        return;
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                default:
                    break;
            }
        }
    }
};

void AddSC_classic_boss_garr()
{
    RegisterCreatureAI(classic_boss_garr);
    RegisterCreatureAI(classic_mob_firesworn);
}
