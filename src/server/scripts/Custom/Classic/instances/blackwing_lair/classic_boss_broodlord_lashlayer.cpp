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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackwing_lair/boss_broodlord_lashlayer.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_broodlord

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "classic_blackwing_lair.h"
#include "classic_script_text.h"
#include <list>

namespace
{
enum ClassicBwlBroodlord : uint32
{
    CLASSIC_BWL_SAY_BROODLORD_AGGRO      = 9967,
    CLASSIC_BWL_SAY_BROODLORD_LEASH      = 9968,

    CLASSIC_BWL_SPELL_BROODLORD_CLEAVE   = 15284,   // 26350
    CLASSIC_BWL_SPELL_KNOCK_AWAY         = 18670,   // 25778
    CLASSIC_BWL_SPELL_BLAST_WAVE         = 23331,

    // World of Warcraft Client Patch 1.8.0 (2005-10-11)
    // - Fixed a bug that was causing Broodlord Lashlayer to do less damage
    //   than intended with his Mortal Strike.
    // VMaNGOS uses 23847 for pre-1.8 clients (added to dbc in 1.6.0, removed in 1.8.0) and 24573 afterwards.
    CLASSIC_BWL_SPELL_MORTAL_STRIKE      = 24573
};
}

struct classic_boss_broodlord : public ScriptedAI
{
    classic_boss_broodlord(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_bMobsDesactives = false;
    }

    InstanceScript* m_pInstance;

    uint32 m_uiCleaveTimer = 0;
    uint32 m_uiBlastWaveTimer = 0;
    uint32 m_uiMortalStrikeTimer = 0;
    uint32 m_uiKnockAwayTimer = 0;
    uint32 m_uiInCombatTimer = 0;
    uint32 m_uiEvadeCheckCooldown = 2500;
    bool m_bMobsDesactives;

    void Reset() override
    {
        m_uiCleaveTimer       = 8000;   // These times are probably wrong
        m_uiBlastWaveTimer    = 20000;
        m_uiMortalStrikeTimer = 25000;
        m_uiKnockAwayTimer    = urand(20000, 25000);
        m_uiInCombatTimer     = 2000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_LASHLAYER, IN_PROGRESS);

        if (me->IsAlive())
        {
            SetMobsDesactivated(true);
            ClassicScriptText(CLASSIC_BWL_SAY_BROODLORD_AGGRO, me);
            DoZoneInCombat();
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_LASHLAYER, DONE);

        SetMobsDesactivated(false);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_BWL_TYPE_LASHLAYER, FAIL);

        SetMobsDesactivated(false);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER && me->IsWithinDistInMap(who, 40.0f) && me->IsWithinLOSInMap(who) && !me->IsInCombat()
                && who->isInAccessiblePlaceFor(me) && !who->HasStealthAura())
            DoZoneInCombat();
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        Unit* pTarget = target ? target->ToUnit() : nullptr;
        if (!pTarget)
            return;

        if (spellInfo->Id == CLASSIC_BWL_SPELL_KNOCK_AWAY)
            ModifyThreatByPercent(pTarget, -50);
    }

    void SetMobsDesactivated(bool on)
    {
        if (m_bMobsDesactives == on)
            return;

        uint32 const mobsEntries[] =
        {
            CLASSIC_BWL_NPC_BLACKWING_WARLOCK,
            CLASSIC_BWL_NPC_BLACKWING_TECHNICIAN,
            CLASSIC_BWL_NPC_BLACKWING_SPELLBINDER,
            CLASSIC_BWL_NPC_DEATH_TALON_OVERSEER
        };

        for (uint32 entry : mobsEntries)
        {
            std::list<Creature*> tmpMobsList;
            me->GetCreatureListWithEntryInGrid(tmpMobsList, entry, 300.0f);
            for (Creature* curr : tmpMobsList)
            {
                // VMaNGOS: UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_SPAWNING | UNIT_FLAG_IMMUNE_TO_NPC
                if (on)
                {
                    curr->SetUninteractible(true);
                    curr->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    curr->SetImmuneToNPC(true);
                }
                else
                {
                    curr->SetUninteractible(false);
                    curr->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    curr->SetImmuneToNPC(false);
                }
            }
        }
        m_bMobsDesactives = on;
    }

    // VMaNGOS ScriptedAI::EnterEvadeIfOutOfCombatArea, NPC_BROODLORD case (not move down stairs)
    bool EnterEvadeIfOutOfCombatArea(uint32 uiDiff)
    {
        if (m_uiEvadeCheckCooldown < uiDiff)
            m_uiEvadeCheckCooldown = 2500;
        else
        {
            m_uiEvadeCheckCooldown -= uiDiff;
            return false;
        }

        if (me->IsInEvadeMode() || !me->GetVictim())
            return false;

        if (me->GetPositionZ() > 448.60f)
            return false;

        EnterEvadeMode(EvadeReason::Boundary);
        return true;
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiInCombatTimer < uiDiff)
        {
            DoZoneInCombat();
            m_uiInCombatTimer = 2000;
        }
        else
            m_uiInCombatTimer -= uiDiff;

        // Cleave Timer
        if (m_uiCleaveTimer < uiDiff)
        {
            if (DoCastVictim(CLASSIC_BWL_SPELL_BROODLORD_CLEAVE) == SPELL_CAST_OK)
                m_uiCleaveTimer = urand(13000, 20000);
        }
        else
            m_uiCleaveTimer -= uiDiff;

        // Blast Wave
        if (m_uiBlastWaveTimer < uiDiff)
        {
            if (DoCastSelf(CLASSIC_BWL_SPELL_BLAST_WAVE) == SPELL_CAST_OK)
                m_uiBlastWaveTimer = urand(20000, 35000);
        }
        else
            m_uiBlastWaveTimer -= uiDiff;

        // Mortal Strike Timer
        if (m_uiMortalStrikeTimer < uiDiff)
        {
            if (DoCastVictim(CLASSIC_BWL_SPELL_MORTAL_STRIKE) == SPELL_CAST_OK)
                m_uiMortalStrikeTimer = urand(20000, 30000);
        }
        else
            m_uiMortalStrikeTimer -= uiDiff;

        // Knock Away Timer
        if (m_uiKnockAwayTimer < uiDiff)
        {
            if (DoCastVictim(CLASSIC_BWL_SPELL_KNOCK_AWAY) == SPELL_CAST_OK)
                m_uiKnockAwayTimer = urand(12000, 25000);
        }
        else
            m_uiKnockAwayTimer -= uiDiff;

        // melee: TC master auto-melee

        if (EnterEvadeIfOutOfCombatArea(uiDiff))
            ClassicScriptText(CLASSIC_BWL_SAY_BROODLORD_LEASH, me);
    }
};

void AddSC_classic_boss_broodlord_lashlayer()
{
    RegisterCreatureAI(classic_boss_broodlord);
}
