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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_timmy_the_cruel.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

namespace
{
enum ClassicTimmyTheCruel : uint32
{
    SAY_TIMMY_SPAWN             = 6150,
    SPELL_RAVENOUSCLAW          = 17470,
    SPELL_TIMMY_ENRAGE          = 8599,
    TIMMY_ENTRY                 = 10808,

    SPELL_GUARDSMAN_DISARM      = 6713,
    SPELL_GUARDSMAN_SHIELD_BASH = 11972,
    SPELL_GUARDSMAN_SHIELD_CHARGE = 15749,

    // VMaNGOS creature guid 54070 (the Crimson Guardsman that spawns Timmy) -> TC spawn id 20000000 + guid
    TIMMY_SPAWNER_SPAWN_ID      = 20000000 + 54070
};
}

struct classic_boss_timmy_the_cruel : public ScriptedAI
{
    classic_boss_timmy_the_cruel(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiRavenousClawTimer = 0;

    void Reset() override
    {
        m_uiRavenousClawTimer = 7000;
    }

    void UpdateAI(uint32 diff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        // Ravenous Claw
        if (m_uiRavenousClawTimer < diff)
        {
            if (DoCastVictim(SPELL_RAVENOUSCLAW) == SPELL_CAST_OK)
                m_uiRavenousClawTimer = 12000;
        }
        else
            m_uiRavenousClawTimer -= diff;

        if (me->GetHealthPct() < 10.0f && !me->HasAura(SPELL_TIMMY_ENRAGE))
            me->CastSpell(me, SPELL_TIMMY_ENRAGE, true);
    }

    void CorpseRemoved(uint32& /*respawnDelay*/) override
    {
        // VMaNGOS: m_creature->DeleteLater() (never respawn). The summoned Timmy is unsummoned; a DB spawn keeps TC's
        // normal instance respawn handling.
        // TODO(classic): for a DB-spawned Timmy, VMaNGOS deleted the creature instead of letting it respawn.
        if (TempSummon* summon = me->ToTempSummon())
            summon->UnSummon();
    }
};

struct classic_npc_crimson_guardsman : public ScriptedAI
{
    classic_npc_crimson_guardsman(Creature* creature) : ScriptedAI(creature)
    {
        m_bIsTimmySpawner = creature->GetSpawnId() == TIMMY_SPAWNER_SPAWN_ID;
    }

    bool m_bHasFled = false;
    bool m_bIsTimmySpawner = false;
    uint32 m_uiDisarmTimer = 0;
    uint32 m_uiShieldBashTimer = 0;
    uint32 m_uiShieldChargeTimer = 0;

    void Reset() override
    {
        m_bHasFled = false;
        m_uiDisarmTimer = 6000;
        m_uiShieldBashTimer = 4000;
        m_uiShieldChargeTimer = 1000;
    }

    void JustDied(Unit* killer) override
    {
        if (m_bIsTimmySpawner)
        {
            if (TempSummon* pTimmy = me->SummonCreature(TIMMY_ENTRY, 3614.7f, -3187.64f, 131.406f, 0.0f, TEMPSUMMON_MANUAL_DESPAWN, 0s))
            {
                pTimmy->Yell(SAY_TIMMY_SPAWN, killer);
                pTimmy->SetRespawnTime(9999999);
            }

            m_bIsTimmySpawner = false;
            // VMaNGOS: m_creature->DeleteLater()
            // TODO(classic): VMaNGOS removes the guardsman immediately (no corpse/loot); here it despawns right after death.
            me->DespawnOrUnsummon(1ms);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        if (!m_bHasFled && me->GetHealthPct() < 15.0f)
        {
            m_bHasFled = true;
            // TODO(classic): VMaNGOS Creature::DoFlee(); TC equivalent used.
            me->DoFleeToGetAssistance();
            return;
        }

        if (m_uiDisarmTimer < diff)
        {
            if (DoCastVictim(SPELL_GUARDSMAN_DISARM) == SPELL_CAST_OK)
                m_uiDisarmTimer = 15000;
        }
        else
            m_uiDisarmTimer -= diff;

        if (m_uiShieldBashTimer < diff)
        {
            if (DoCastVictim(SPELL_GUARDSMAN_SHIELD_BASH) == SPELL_CAST_OK)
                m_uiShieldBashTimer = 8000;
        }
        else
            m_uiShieldBashTimer -= diff;

        if (m_uiShieldChargeTimer < diff)
        {
            if (DoCastVictim(SPELL_GUARDSMAN_SHIELD_CHARGE) == SPELL_CAST_OK)
                m_uiShieldChargeTimer = 12000;
        }
        else
            m_uiShieldChargeTimer -= diff;
    }
};

void AddSC_classic_boss_timmy_the_cruel()
{
    RegisterCreatureAI(classic_boss_timmy_the_cruel);
    RegisterCreatureAI(classic_npc_crimson_guardsman);
}
