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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_lord_alexei_barov.cpp
// (ScriptDev2 lineage, GPL-2). Aura applied/defined in database.

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicAlexeiSpells : uint32
    {
        SPELL_IMMOLATE             = 15570,
        SPELL_VEILOFSHADOW         = 17820
    };
}

struct classic_boss_lord_alexei_barov : public ScriptedAI
{
    classic_boss_lord_alexei_barov(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _immolateTimer = 7000;
        _veilOfShadowTimer = 15000;
    }

    void Reset() override
    {
        Initialize();
        me->LoadCreaturesAddon();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_ALEXEIBAROV, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_immolateTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_IMMOLATE);

            _immolateTimer = 12000;
        }
        else
            _immolateTimer -= diff;

        if (_veilOfShadowTimer < diff)
        {
            DoCastVictim(SPELL_VEILOFSHADOW);
            _veilOfShadowTimer = 20000;
        }
        else
            _veilOfShadowTimer -= diff;
    }

private:
    uint32 _immolateTimer;
    uint32 _veilOfShadowTimer;
};

void AddSC_classic_boss_lord_alexei_barov()
{
    RegisterCreatureAI(classic_boss_lord_alexei_barov);
}
