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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/desolace/maraudon/boss_landslide.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_landslide (12203)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

namespace
{
enum ClassicLandslide
{
    SPELL_LANDSLIDE_KNOCKAWAY   = 18670,
    SPELL_LANDSLIDE_TRAMPLE     = 5568,
    SPELL_LANDSLIDE_LANDSLIDE   = 21808
};
}

struct classic_boss_landslide : public ScriptedAI
{
    classic_boss_landslide(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _knockAwayTimer = 8000;
        _trampleTimer = 2000;
        _landslideTimer = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_knockAwayTimer < diff)
        {
            DoCastVictim(SPELL_LANDSLIDE_KNOCKAWAY);
            _knockAwayTimer = 15000;
        }
        else
            _knockAwayTimer -= diff;

        if (_trampleTimer < diff)
        {
            DoCastSelf(SPELL_LANDSLIDE_TRAMPLE);
            _trampleTimer = 8000;
        }
        else
            _trampleTimer -= diff;

        // Landslide
        if (me->GetHealthPct() < 50.0f)
        {
            if (_landslideTimer < diff)
            {
                me->InterruptNonMeleeSpells(false);
                DoCastSelf(SPELL_LANDSLIDE_LANDSLIDE);
                _landslideTimer = 60000;
            }
            else
                _landslideTimer -= diff;
        }
    }

private:
    uint32 _knockAwayTimer;
    uint32 _trampleTimer;
    uint32 _landslideTimer;
};

void AddSC_classic_boss_landslide()
{
    RegisterCreatureAI(classic_boss_landslide);
}
