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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/npc_sandstalker.cpp (Nostalrius, GPL-2)
// Scripts: npc_sandstalker
// The VMaNGOS ScriptedAI helpers EnterVanish / LeaveVanish / Ambush are inlined below.

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "classic_ruins_of_ahnqiraj.h"
#include <cmath>

namespace
{
enum ClassicAQ20Sandstalker : uint32
{
    SPELL_AQ20_SANDSTALKER_BURROW = 26381
};
}

/*######
## npc_sandstalker
######*/

struct classic_npc_sandstalker : public ScriptedAI
{
    classic_npc_sandstalker(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiBurrow_Timer = 5000;

    // VMaNGOS ScriptedAI::EnterVanish
    void EnterVanish()
    {
        me->SetVisible(false);
        // VMaNGOS UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_SPAWNING
        me->SetUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));
        // VMaNGOS also interrupts spells being cast at the creature (InterruptSpellsCastedOnMe); no TC equivalent
        me->RemoveAllAttackers();     // VMaNGOS InterruptAttacksOnMe
        me->AttackStop();
        me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        // VMaNGOS DoResetThreat (modifyThreatPercent -100 on every threat list entry)
        me->GetThreatManager().ResetAllThreat();
    }

    // VMaNGOS ScriptedAI::LeaveVanish (its flag mask is buggy; the intent is to undo EnterVanish)
    void LeaveVanish()
    {
        me->SetVisible(true);
        me->RemoveUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));
    }

    // VMaNGOS ScriptedAI::Ambush: teleport 4 yards behind the new victim and open with a spell
    void Ambush(Unit* pNewVictim, uint32 ambushSpellId)
    {
        if (!pNewVictim)
            return;

        // Se position derriere (VMaNGOS GetRelativePositions(-4.0f, 0.0f, 0.0f))
        float const x = pNewVictim->GetPositionX() - 4.0f * std::cos(pNewVictim->GetOrientation());
        float const y = pNewVictim->GetPositionY() - 4.0f * std::sin(pNewVictim->GetOrientation());
        me->NearTeleportTo(x, y, pNewVictim->GetPositionZ(), 0.0f);
        me->SetFacingToObject(pNewVictim);
        // Embush
        if (ambushSpellId)
            me->CastSpell(pNewVictim, ambushSpellId, true);
        AttackStart(pNewVictim);
    }

    void JustReachedHome() override
    {
        EnterVanish();
    }

    void Reset() override
    {
        EnterVanish();
        m_uiBurrow_Timer = 5000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        Unit* pVanishTarget = SelectTarget(SelectTargetMethod::Random, 0);
        LeaveVanish();
        Ambush(pVanishTarget, SPELL_AQ20_SANDSTALKER_BURROW);
    }

    void JustDied(Unit* /*killer*/) override
    {
        LeaveVanish();
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!me->IsInCombat())
            EnterVanish();

        if (!UpdateVictim())
            return;

        if (m_uiBurrow_Timer < uiDiff)
        {
            EnterVanish();
            Unit* pVanishTarget = SelectTarget(SelectTargetMethod::Random, 0);
            LeaveVanish();
            Ambush(pVanishTarget, SPELL_AQ20_SANDSTALKER_BURROW);
            m_uiBurrow_Timer = urand(5000, 10000);
        }
        else
            m_uiBurrow_Timer -= uiDiff;
    }
};

void AddSC_classic_npc_sandstalker()
{
    RegisterCreatureAI(classic_npc_sandstalker);
}
