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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_ras_frostwhisper.cpp
// (ScriptDev2 lineage, GPL-2)
// NOTE: VMaNGOS registers this script as "boss_boss_ras_frostwhisper" (double "boss_"); the name is kept as is.

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"

namespace
{
    enum ClassicRasSpells : uint32
    {
        SPELL_FROSTBOLT         = 21369,
        SPELL_ICEARMOR          = 18100,                    // This is actually a buff he gives himself
        SPELL_FREEZE            = 18763,
        SPELL_FEAR              = 26070,
        SPELL_CHILLNOVA         = 18099,
        SPELL_FROSTVOLLEY       = 8398
    };
}

struct classic_boss_boss_ras_frostwhisper : public ScriptedAI
{
    classic_boss_boss_ras_frostwhisper(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _iceArmorTimer = 2000;
        _frostboltTimer = 8000;
        _chillNovaTimer = 12000;
        _freezeTimer = 18000;
        _frostVolleyTimer = 24000;
        _fearTimer = 45000;
    }

    void Reset() override
    {
        Initialize();
        DoCastSelf(SPELL_ICEARMOR, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_iceArmorTimer < diff)
        {
            DoCastSelf(SPELL_ICEARMOR);
            _iceArmorTimer = 180000;
        }
        else
            _iceArmorTimer -= diff;

        if (_frostboltTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_FROSTBOLT);

            _frostboltTimer = 8000;
        }
        else
            _frostboltTimer -= diff;

        if (_freezeTimer < diff)
        {
            DoCastVictim(SPELL_FREEZE);
            _freezeTimer = 24000;
        }
        else
            _freezeTimer -= diff;

        if (_fearTimer < diff)
        {
            DoCastVictim(SPELL_FEAR);
            _fearTimer = 30000;
        }
        else
            _fearTimer -= diff;

        if (_chillNovaTimer < diff)
        {
            DoCastVictim(SPELL_CHILLNOVA);
            _chillNovaTimer = 14000;
        }
        else
            _chillNovaTimer -= diff;

        if (_frostVolleyTimer < diff)
        {
            DoCastVictim(SPELL_FROSTVOLLEY);
            _frostVolleyTimer = 15000;
        }
        else
            _frostVolleyTimer -= diff;
    }

private:
    uint32 _iceArmorTimer;
    uint32 _frostboltTimer;
    uint32 _freezeTimer;
    uint32 _fearTimer;
    uint32 _chillNovaTimer;
    uint32 _frostVolleyTimer;
};

void AddSC_classic_boss_ras_frostwhisper()
{
    RegisterCreatureAI(classic_boss_boss_ras_frostwhisper);
}
