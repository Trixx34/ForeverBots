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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_overlord_wyrmthalak.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

namespace
{
    enum ClassicWyrmthalakData : uint32
    {
        SPELL_BLASTWAVE            = 11130,
        SPELL_SHOUT                = 23511,
        SPELL_CLEAVE               = 20691,
        SPELL_KNOCKAWAY            = 20686,

        NPC_SPIRESTONE_WARLORD     = 9216,
        NPC_SMOLDERTHORN_BERSERKER = 9268
    };

    Position const ClassicWyrmthalakAddLocations[2] =
    {
        { -39.355381f, -513.456482f, 88.472046f, 4.679872f },
        { -49.875881f, -511.896942f, 88.195160f, 4.613114f }
    };
}

struct classic_boss_overlord_wyrmthalak : public ScriptedAI
{
    classic_boss_overlord_wyrmthalak(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _blastWaveTimer = 20000;
        _shoutTimer     = 2000;
        _cleaveTimer    = 6000;
        _knockawayTimer = 12000;
        _summoned = false;
        _pulledByPet = false;

        _leashCheckTimer = 5000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() != NPC_SPIRESTONE_WARLORD && summoned->GetEntry() != NPC_SMOLDERTHORN_BERSERKER)
            return;

        if (Unit* victim = me->GetVictim())
        {
            Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
            if (summoned->AI())
                summoned->AI()->AttackStart(target ? target : victim);
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        // Prevent exploit where pet can run through the wall and pull the boss.
        if (Unit* owner = who->GetOwner())
            if (!owner->IsWithinLOSInMap(me))
                _pulledByPet = true;
    }

    void LeashIfOutOfCombatArea(uint32 diff)
    {
        if (_leashCheckTimer < diff)
            _leashCheckTimer = 3500;
        else
        {
            _leashCheckTimer -= diff;
            return;
        }

        if (_pulledByPet || (me->GetPositionZ() > 100.0f))
            EnterEvadeMode(EvadeReason::Other);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // Prevent players from pulling Wyrmthalak into UBRS
        LeashIfOutOfCombatArea(diff);

        if (_blastWaveTimer < diff)
        {
            DoCastSelf(SPELL_BLASTWAVE);
            _blastWaveTimer = 20000;
        }
        else
            _blastWaveTimer -= diff;

        if (_shoutTimer < diff)
        {
            DoCastSelf(SPELL_SHOUT);
            _shoutTimer = 10000;
        }
        else
            _shoutTimer -= diff;

        if (_cleaveTimer < diff)
        {
            DoCastVictim(SPELL_CLEAVE);
            _cleaveTimer = 7000;
        }
        else
            _cleaveTimer -= diff;

        if (_knockawayTimer < diff)
        {
            DoCastSelf(SPELL_KNOCKAWAY);
            _knockawayTimer = 14000;
        }
        else
            _knockawayTimer -= diff;

        // Summon two Beserks
        if (!_summoned && me->GetHealthPct() < 51.0f)
        {
            me->SummonCreature(NPC_SPIRESTONE_WARLORD, ClassicWyrmthalakAddLocations[0], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s);
            me->SummonCreature(NPC_SMOLDERTHORN_BERSERKER, ClassicWyrmthalakAddLocations[1], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s);

            _summoned = true;
        }
    }

private:
    uint32 _blastWaveTimer;
    uint32 _shoutTimer;
    uint32 _cleaveTimer;
    uint32 _knockawayTimer;
    bool _summoned;
    bool _pulledByPet;
    uint32 _leashCheckTimer;
};

void AddSC_classic_boss_overlord_wyrmthalak()
{
    RegisterCreatureAI(classic_boss_overlord_wyrmthalak);
}
