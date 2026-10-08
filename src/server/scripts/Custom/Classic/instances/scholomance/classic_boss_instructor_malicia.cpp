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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_instructor_malicia.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicMaliciaSpells : uint32
    {
        SPELL_CALLOFGRAVES         = 17831,
        SPELL_CORRUPTION           = 11672,
        SPELL_FLASHHEAL            = 10917,
        SPELL_RENEW                = 10929,
        SPELL_HEALINGTOUCH         = 9889
    };
}

struct classic_boss_instructor_malicia : public ScriptedAI
{
    classic_boss_instructor_malicia(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _callOfGravesTimer = 4000;
        _corruptionTimer = 8000;
        _flashHealTimer = 22000;
        _renewTimer = 15000;
        _healingTouchTimer = 25000;
        _flashCounter = 0;
        _touchCounter = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_MALICIA, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_callOfGravesTimer < diff)
        {
            DoCastVictim(SPELL_CALLOFGRAVES);
            _callOfGravesTimer = 65000;
        }
        else
            _callOfGravesTimer -= diff;

        if (_corruptionTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_CORRUPTION);

            _corruptionTimer = 24000;
        }
        else
            _corruptionTimer -= diff;

        if (_renewTimer < diff)
        {
            DoCastSelf(SPELL_RENEW);
            _renewTimer = 10000;
        }
        else
            _renewTimer -= diff;

        if (_flashHealTimer < diff)
        {
            DoCastSelf(SPELL_FLASHHEAL);

            // 5 Flashheals will be casted
            if (_flashCounter < 2)
            {
                _flashHealTimer = 5000;
                ++_flashCounter;
            }
            else
            {
                _flashCounter = 0;
                _flashHealTimer = 30000;
            }
        }
        else
            _flashHealTimer -= diff;

        if (_healingTouchTimer < diff)
        {
            DoCastSelf(SPELL_HEALINGTOUCH);

            // 3 Healingtouchs will be casted
            // (VMaNGOS compares the timer instead of TouchCounter here; kept as is)
            if (_healingTouchTimer < 2)
            {
                _healingTouchTimer = 5500;
                ++_touchCounter;
            }
            else
            {
                _touchCounter = 0;
                _healingTouchTimer = 30000;
            }
        }
        else
            _healingTouchTimer -= diff;
    }

private:
    uint32 _callOfGravesTimer;
    uint32 _corruptionTimer;
    uint32 _flashHealTimer;
    uint32 _renewTimer;
    uint32 _healingTouchTimer;
    uint32 _flashCounter;
    uint32 _touchCounter;
};

void AddSC_classic_boss_instructor_malicia()
{
    RegisterCreatureAI(classic_boss_instructor_malicia);
}
