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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_renataki.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_renataki

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "ThreatManager.h"
#include "classic_zulgurub.h"
#include <cmath>

namespace
{
enum ClassicZgRenataki : uint32
{
    SPELL_RENATAKI_TRASH            = 3391,
    SPELL_RENATAKI_ENRAGE           = 8269,
    SPELL_RENATAKI_RED_LIGHTNING    = 24240,
    SPELL_RENATAKI_AMBUSH           = 24337,
    SPELL_RENATAKI_THOUSANDBLADES   = 24649,
    SPELL_RENATAKI_SURINER_ZONE     = 24698
};

// VMaNGOS ScriptedAI::EnterVanish / LeaveVanish / Ambush
void ClassicZgRenatakiEnterVanish(Creature* me)
{
    me->SetVisible(false);
    me->SetUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));
    // TODO(classic): VMaNGOS also InterruptSpellsCastedOnMe(true) - no TC equivalent
    me->RemoveAllAttackers();
    me->AttackStop();
    me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    me->SetCanMelee(false);     // VMaNGOS returns before DoMeleeAttackIfReady() while invisible
    me->GetThreatManager().ResetAllThreat();
}

void ClassicZgRenatakiLeaveVanish(Creature* me)
{
    me->SetVisible(true);
    me->RemoveUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));
    me->SetCanMelee(true);
}

void ClassicZgRenatakiAmbush(Creature* me, Unit* pNewVictim, uint32 embushSpellId)
{
    if (!pNewVictim)
        return;
    float o = pNewVictim->GetOrientation();
    float x = pNewVictim->GetPositionX() - 4.0f * std::cos(o);
    float y = pNewVictim->GetPositionY() - 4.0f * std::sin(o);
    float z = pNewVictim->GetPositionZ();
    me->NearTeleportTo(x, y, z, 0.0f);
    me->SetFacingToObject(pNewVictim);
    if (embushSpellId)
        me->CastSpell(pNewVictim, embushSpellId, true);
    if (me->AI())
        me->AI()->AttackStart(pNewVictim);
}
}

struct classic_boss_renataki : public ScriptedAI
{
    classic_boss_renataki(Creature* creature) : ScriptedAI(creature) { }

    uint32 TickTimer = 0;
    uint32 Invisible_Timer = 0;
    uint32 Suriner_Timer = 0;
    uint32 Visible_Timer = 0;
    uint32 Aggro_Timer = 0;
    uint32 ThousandBlades_Timer = 0;

    bool Invisible = false;
    bool Light = false;

    void Reset() override
    {
        TickTimer = 1000;
        Invisible_Timer = urand(28000, 32000);
        Suriner_Timer = urand(9000, 11000);
        Visible_Timer = 20000;
        Aggro_Timer = urand(15000, 25000);
        ThousandBlades_Timer = urand(4000, 8000);

        if (Invisible)
            ClassicZgRenatakiLeaveVanish(me);
        Invisible = false;
        // TODO(classic): VMaNGOS forces UNIT_VIRTUAL_ITEM_SLOT_DISPLAY 31818 / UNIT_VIRTUAL_ITEM_INFO (218171138, 3) here;
        // TC uses item ids for virtual items - set the weapon through creature_equip_template instead.
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicZgRenatakiLeaveVanish(me);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!Light)
        {
            Light = true;
            me->CastSpell(me, SPELL_RENATAKI_RED_LIGHTNING, true);
        }

        if (!UpdateVictim())
            return;

        if (me->GetHealthPct() < 30.0f)
            if (!me->HasAura(SPELL_RENATAKI_ENRAGE))  // VMaNGOS CF_AURA_NOT_PRESENT
                DoCastSelf(SPELL_RENATAKI_ENRAGE);

        if (Invisible)
        {
            if (Visible_Timer < diff)
            {
                Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0);
                ClassicZgRenatakiLeaveVanish(me);
                ClassicZgRenatakiAmbush(me, pTarget, SPELL_RENATAKI_AMBUSH);

                Invisible = false;
                Visible_Timer = 20000;
            }
            else
                Visible_Timer -= diff;
            return;
        }

        if (Suriner_Timer < diff)
        {
            if (DoCastSelf(SPELL_RENATAKI_SURINER_ZONE) == SPELL_CAST_OK)
                Suriner_Timer = urand(9000, 11000);
        }
        else
            Suriner_Timer -= diff;

        // Invisible_Timer
        if (Invisible_Timer < diff)
        {
            ClassicZgRenatakiEnterVanish(me);
            Invisible = true;

            Invisible_Timer = urand(30000, 42000);
        }
        else
            Invisible_Timer -= diff;

        // Resetting some aggro so he attacks other gamers
        if (Aggro_Timer < diff)
        {
            Unit* target = SelectTarget(SelectTargetMethod::Random, 1);

            if (Unit* victim = me->GetVictim())
                if (GetThreat(victim))
                    ModifyThreatByPercent(victim, -50);

            if (target)
                AttackStart(target);

            Aggro_Timer = urand(7000, 20000);
        }
        else
            Aggro_Timer -= diff;

        if (ThousandBlades_Timer < diff)
        {
            DoCastVictim(SPELL_RENATAKI_THOUSANDBLADES);
            ThousandBlades_Timer = urand(7000, 12000);
        }
        else
            ThousandBlades_Timer -= diff;

        if (me->isAttackReady() && !urand(0, 2))
            me->CastSpell(me, SPELL_RENATAKI_TRASH, true);

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_renataki()
{
    RegisterCreatureAI(classic_boss_renataki);
}
