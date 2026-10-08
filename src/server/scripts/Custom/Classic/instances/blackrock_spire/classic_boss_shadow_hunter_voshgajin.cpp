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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_shadow_hunter_voshgajin.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"

namespace
{
    enum ClassicVoshgajinSpells : uint32
    {
        SPELL_CURSEOFBLOOD = 24673,
        SPELL_HEX          = 16708,
        SPELL_CLEAVE       = 20691
    };
}

struct classic_boss_shadow_hunter_voshgajin : public ScriptedAI
{
    classic_boss_shadow_hunter_voshgajin(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _curseOfBloodTimer = 2000;
        _hexTimer          = 8000;
        _cleaveTimer       = 14000;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_curseOfBloodTimer < diff)
        {
            DoCastSelf(SPELL_CURSEOFBLOOD);
            _curseOfBloodTimer = 45000;
        }
        else
            _curseOfBloodTimer -= diff;

        if (_hexTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                DoCast(target, SPELL_HEX);
                _hexTimer += 15000;     // VMaNGOS adds to the (expired) timer
            }
        }
        else
            _hexTimer -= diff;

        if (_cleaveTimer < diff)
        {
            DoCastVictim(SPELL_CLEAVE);
            _cleaveTimer = 7000;
        }
        else
            _cleaveTimer -= diff;
    }

private:
    uint32 _curseOfBloodTimer;
    uint32 _hexTimer;
    uint32 _cleaveTimer;
};

void AddSC_classic_boss_shadow_hunter_voshgajin()
{
    RegisterCreatureAI(classic_boss_shadow_hunter_voshgajin);
}
