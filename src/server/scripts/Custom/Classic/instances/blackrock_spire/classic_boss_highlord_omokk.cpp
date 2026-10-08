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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_highlord_omokk.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"

namespace
{
    enum ClassicOmokkSpells : uint32
    {
        SPELL_WARSTOMP    = 24375,
        SPELL_STRIKE      = 18368,
        SPELL_REND        = 18106,
        SPELL_SUNDERARMOR = 24317,
        SPELL_KNOCKAWAY   = 20686,
        SPELL_SLOW        = 22356
    };
}

struct classic_boss_highlord_omokk : public ScriptedAI
{
    classic_boss_highlord_omokk(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _warStompTimer    = 15000;
        _strikeTimer      = 10000;
        _rendTimer        = 14000;
        _sunderArmorTimer = 2000;
        _knockAwayTimer   = 18000;
        _slowTimer        = 24000;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_warStompTimer < diff)
        {
            DoCastSelf(SPELL_WARSTOMP);
            _warStompTimer = 14000;
        }
        else
            _warStompTimer -= diff;

        if (_strikeTimer < diff)
        {
            DoCastVictim(SPELL_STRIKE);
            _strikeTimer = 10000;
        }
        else
            _strikeTimer -= diff;

        if (_rendTimer < diff)
        {
            DoCastVictim(SPELL_REND);
            _rendTimer = 18000;
        }
        else
            _rendTimer -= diff;

        if (_sunderArmorTimer < diff)
        {
            DoCastVictim(SPELL_SUNDERARMOR);
            _sunderArmorTimer = 25000;
        }
        else
            _sunderArmorTimer -= diff;

        if (_knockAwayTimer < diff)
        {
            DoCastSelf(SPELL_KNOCKAWAY);
            _knockAwayTimer = 12000;
        }
        else
            _knockAwayTimer -= diff;

        if (_slowTimer < diff)
        {
            DoCastSelf(SPELL_SLOW);
            _slowTimer = 18000;
        }
        else
            _slowTimer -= diff;
    }

private:
    uint32 _warStompTimer;
    uint32 _strikeTimer;
    uint32 _rendTimer;
    uint32 _sunderArmorTimer;
    uint32 _knockAwayTimer;
    uint32 _slowTimer;
};

void AddSC_classic_boss_highlord_omokk()
{
    RegisterCreatureAI(classic_boss_highlord_omokk);
}
