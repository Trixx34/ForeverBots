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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_lorekeeper_polkelt.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicPolkeltSpells : uint32
    {
        SPELL_VOLATILEINFECTION      = 24928,
        SPELL_DARKPLAGUE_AURA        = 12038,
        SPELL_CORROSIVEACID          = 8245,
        SPELL_NOXIOUSCATALYST        = 18151
    };
}

struct classic_boss_lorekeeper_polkelt : public ScriptedAI
{
    classic_boss_lorekeeper_polkelt(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _volatileInfectionTimer = 38000;
        _corrosiveAcidTimer = 45000;
        _noxiousCatalystTimer = 35000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // VMaNGOS CF_TRIGGERED | CF_AURA_NOT_PRESENT
        if (!me->HasAura(SPELL_DARKPLAGUE_AURA))
            DoCastSelf(SPELL_DARKPLAGUE_AURA, true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_POLKELT, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_volatileInfectionTimer < diff)
        {
            DoCastVictim(SPELL_VOLATILEINFECTION);
            _volatileInfectionTimer = 32000;
        }
        else
            _volatileInfectionTimer -= diff;

        if (_corrosiveAcidTimer < diff)
        {
            DoCastVictim(SPELL_CORROSIVEACID);
            _corrosiveAcidTimer = 25000;
        }
        else
            _corrosiveAcidTimer -= diff;

        if (_noxiousCatalystTimer < diff)
        {
            DoCastVictim(SPELL_NOXIOUSCATALYST);
            _noxiousCatalystTimer = 38000;
        }
        else
            _noxiousCatalystTimer -= diff;
    }

private:
    uint32 _volatileInfectionTimer;
    uint32 _corrosiveAcidTimer;
    uint32 _noxiousCatalystTimer;
};

void AddSC_classic_boss_lorekeeper_polkelt()
{
    RegisterCreatureAI(classic_boss_lorekeeper_polkelt);
}
