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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_halycon.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

namespace
{
    enum ClassicHalyconData : uint32
    {
        SPELL_CROWDPUMMEL       = 10887,
        SPELL_MIGHTYBLOW        = 14099,

        NPC_GIZRUL_THE_SLAVENER = 10268
    };
}

struct classic_boss_halycon : public ScriptedAI
{
    classic_boss_halycon(Creature* creature) : ScriptedAI(creature)
    {
        _summoned = false;
        Initialize();
    }

    void Initialize()
    {
        _crowdPummelTimer = 8000;
        _mightyBlowTimer = 14000;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_crowdPummelTimer < diff)
        {
            DoCastVictim(SPELL_CROWDPUMMEL);
            _crowdPummelTimer = 14000;
        }
        else
            _crowdPummelTimer -= diff;

        if (_mightyBlowTimer < diff)
        {
            DoCastVictim(SPELL_MIGHTYBLOW);
            _mightyBlowTimer = 10000;
        }
        else
            _mightyBlowTimer -= diff;
    }

    void JustDied(Unit* /*killer*/) override
    {
        // Summon Gizrul
        if (!_summoned)
        {
            me->TextEmote("Halycon lets loose a gutteral growl as her body collapses. A horrifying howl can be heard echoing through the halls of Blackrock Spire. Something is very, very angry.");
            if (Creature* gizrul = me->SummonCreature(NPC_GIZRUL_THE_SLAVENER, -167.58f, -382.41f, 64.401f, 1.563f, TEMPSUMMON_DEAD_DESPAWN, 0s))
            {
                gizrul->SetHomePosition(-172.633f, -324.253f, 64.401f, 4.74f);
                CreatureAI::DoZoneInCombat(gizrul);
            }
            _summoned = true;
        }
    }

private:
    uint32 _crowdPummelTimer;
    uint32 _mightyBlowTimer;
    bool _summoned;
};

void AddSC_classic_boss_halycon()
{
    RegisterCreatureAI(classic_boss_halycon);
}
