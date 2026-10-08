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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_illucia_barov.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicIlluciaSpells : uint32
    {
        SPELL_CURSEOFAGONY      = 18671,
        SPELL_SHADOWSHOCK       = 20603,
        SPELL_SILENCE           = 15487,
        SPELL_FEAR              = 6215
    };
}

struct classic_boss_illucia_barov : public ScriptedAI
{
    classic_boss_illucia_barov(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _curseOfAgonyTimer = 18000;
        _shadowShockTimer = 9000;
        _silenceTimer = 5000;
        _fearTimer = 30000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_ILLUCIABAROV, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_curseOfAgonyTimer < diff)
        {
            DoCastVictim(SPELL_CURSEOFAGONY);
            _curseOfAgonyTimer = 30000;
        }
        else
            _curseOfAgonyTimer -= diff;

        if (_shadowShockTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_SHADOWSHOCK);

            _shadowShockTimer = 12000;
        }
        else
            _shadowShockTimer -= diff;

        if (_silenceTimer < diff)
        {
            DoCastVictim(SPELL_SILENCE);
            _silenceTimer = 14000;
        }
        else
            _silenceTimer -= diff;

        if (_fearTimer < diff)
        {
            DoCastVictim(SPELL_FEAR);
            _fearTimer = 30000;
        }
        else
            _fearTimer -= diff;
    }

private:
    uint32 _curseOfAgonyTimer;
    uint32 _shadowShockTimer;
    uint32 _silenceTimer;
    uint32 _fearTimer;
};

void AddSC_classic_boss_illucia_barov()
{
    RegisterCreatureAI(classic_boss_illucia_barov);
}
