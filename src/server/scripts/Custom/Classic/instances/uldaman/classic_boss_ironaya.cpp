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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/uldaman/boss_ironaya.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "classic_uldaman.h"
#include "classic_script_text.h"

enum IronayaTexts
{
    SAY_AGGRO           = 3261
};

struct classic_boss_ironaya : public ScriptedAI
{
    classic_boss_ironaya(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
        _hasMoved = false;
        _arcingTimer = 0;   // TODO(classic): uninitialized in VMaNGOS (never set in Reset either)
        _hasCastedKnockaway = false;
        _hasCastedWstomp = false;
    }

    void Reset() override
    {
        _hasCastedKnockaway = false;
        _hasCastedWstomp = false;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_AGGRO, me);
        DoZoneInCombat();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsImmuneToPC() && !me->IsImmuneToNPC() && !_hasMoved)
        {
            if (_instance)
                if (Unit* target = ObjectAccessor::GetUnit(*me, _instance->GetGuidData(DATA64_IRONAYA_WAKER)))
                    AttackStart(target);
            _hasMoved = true;
        }

        //Return since we have no target
        if (!UpdateVictim())
            return;

        //If we are <50% hp do knockaway ONCE
        if (!_hasCastedKnockaway && me->GetHealthPct() < 50.0f)
        {
            me->CastSpell(me->GetVictim(), SPELL_KNOCKAWAY, false);
            ModifyThreatByPercent(me->GetVictim(), -100);

            // current aggro target is knocked away pick new target
            Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0);

            if (!target || target == me->GetVictim())
                target = SelectTarget(SelectTargetMethod::MaxThreat, 1);

            if (target)
                AttackStart(target);

            //Shouldn't cast this again
            _hasCastedKnockaway = true;
        }

        //Arcing_Timer
        if (_arcingTimer < diff)
        {
            DoCastSelf(SPELL_ARCINGSMASH);
            _arcingTimer = 13000;
        }
        else
            _arcingTimer -= diff;

        if (!_hasCastedWstomp && me->GetHealthPct() < 25.0f)
        {
            DoCastSelf(SPELL_WSTOMP);
            _hasCastedWstomp = true;
        }
    }

private:
    InstanceScript* _instance;
    uint32 _arcingTimer;
    bool _hasCastedWstomp;
    bool _hasCastedKnockaway;
    bool _hasMoved;
};

void AddSC_classic_boss_ironaya()
{
    RegisterCreatureAI(classic_boss_ironaya);
}
