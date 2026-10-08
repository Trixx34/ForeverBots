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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/ubrs_trash.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: npc_blackhand_veteran
// NOTE: the VMaNGOS file also mentions "npc_blackhand_summoner" (9818) in an SQL comment only; no script exists for it.

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_blackrock_spire.h"

using namespace ClassicBlackrockSpire;

namespace
{
    enum ClassicBlackhandVeteranSpells : uint32
    {
        SPELL_CHARGE_BOUCLIER       = 15749,    // Shield Charge
        SPELL_COUP_BOUCLIER         = 11972,    // Shield Bash
        SPELL_FRAPPE                = 14516     // Strike
    };
}

struct classic_npc_blackhand_veteran : public ScriptedAI
{
    classic_npc_blackhand_veteran(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
    {
        Initialize();
    }

    void Initialize()
    {
        _chargeBouclierTimer = 0;
        _coupBouclierTimer = 2000;
        _frappeTimer = 5000;
        _firstChargeDone = false;
    }

    void Reset() override
    {
        Initialize();
    }

    static bool ManageTimer(uint32 diff, uint32& timer)
    {
        if (timer < diff)
            return true;

        timer -= diff;
        return false;
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_instance)
            _instance->SetGuidData(TYPE_ROOM_EVENT, me->GetGUID());
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (ManageTimer(diff, _chargeBouclierTimer))
        {
            Unit* target1;
            if (_firstChargeDone)
                target1 = SelectTarget(SelectTargetMethod::Random, 0);
            else
            {
                target1 = me->GetVictim();
                _firstChargeDone = true;
            }
            if (target1)
            {
                if (DoCast(target1, SPELL_CHARGE_BOUCLIER) == SPELL_CAST_OK)
                    _chargeBouclierTimer = urand(8000, 14000);
            }
        }
        if (ManageTimer(diff, _coupBouclierTimer))
        {
            if (Unit* target2 = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (target2->IsNonMeleeSpellCast(false, false, true))
                {
                    if (!urand(0, 3)) // because otherwise they all decast at the same, which is stupid.
                    {
                        if (DoCast(target2, SPELL_COUP_BOUCLIER) == SPELL_CAST_OK)
                            _coupBouclierTimer = 10000;
                    }
                }
            }
        }
        if (ManageTimer(diff, _frappeTimer))
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(target, SPELL_FRAPPE) == SPELL_CAST_OK)
                    _frappeTimer = 6000;
            }
        }
    }

private:
    InstanceScript* _instance;
    uint32 _chargeBouclierTimer;
    uint32 _coupBouclierTimer;
    uint32 _frappeTimer;
    bool _firstChargeDone;
};

void AddSC_classic_ubrs_trash()
{
    RegisterCreatureAI(classic_npc_blackhand_veteran);
}
