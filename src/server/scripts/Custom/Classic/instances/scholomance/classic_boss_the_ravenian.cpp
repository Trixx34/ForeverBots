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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_the_ravenian.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicRavenianSpells : uint32
    {
        SPELL_TRAMPLE           = 15550,
        SPELL_CLEAVE            = 20691,
        SPELL_SUNDERINCLEAVE    = 25174,
        SPELL_KNOCKAWAY         = 10101
    };
}

struct classic_boss_the_ravenian : public ScriptedAI
{
    classic_boss_the_ravenian(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _trampleTimer = 24000;
        _cleaveTimer = 15000;
        _sunderingCleaveTimer = 40000;
        _knockAwayTimer = 32000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_RAVENIAN, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_trampleTimer < diff)
        {
            DoCastVictim(SPELL_TRAMPLE);
            _trampleTimer = 10000;
        }
        else
            _trampleTimer -= diff;

        if (_cleaveTimer < diff)
        {
            DoCastVictim(SPELL_CLEAVE);
            _cleaveTimer = 7000;
        }
        else
            _cleaveTimer -= diff;

        if (_sunderingCleaveTimer < diff)
        {
            DoCastVictim(SPELL_SUNDERINCLEAVE);
            _sunderingCleaveTimer = 20000;
        }
        else
            _sunderingCleaveTimer -= diff;

        if (_knockAwayTimer < diff)
        {
            DoCastVictim(SPELL_KNOCKAWAY);
            _knockAwayTimer = 12000;
        }
        else
            _knockAwayTimer -= diff;
    }

private:
    uint32 _trampleTimer;
    uint32 _cleaveTimer;
    uint32 _sunderingCleaveTimer;
    uint32 _knockAwayTimer;
};

void AddSC_classic_boss_the_ravenian()
{
    RegisterCreatureAI(classic_boss_the_ravenian);
}
