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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_doctor_theolen_krastinov.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "classic_scholomance.h"
#include "classic_script_text.h"

using namespace ClassicScholomance;

namespace
{
    enum ClassicTheolenData : uint32
    {
        EMOTE_GENERIC_FRENZY_KILL   = 7797,

        SPELL_REND                  = 16509,
        SPELL_BACKHAND              = 18103,
        SPELL_FRENZY                = 8269
    };
}

struct classic_boss_doctor_theolen_krastinov : public ScriptedAI
{
    classic_boss_doctor_theolen_krastinov(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _rendTimer = 8000;
        _backhandTimer = 9000;
        _frenzyTimer = 1000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_THEOLEN, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_rendTimer < diff)
        {
            DoCastVictim(SPELL_REND);
            _rendTimer = 10000;
        }
        else
            _rendTimer -= diff;

        if (_backhandTimer < diff)
        {
            DoCastVictim(SPELL_BACKHAND);
            if (Unit* victim = me->GetVictim())
                ModifyThreatByPercent(victim, -100);
            _backhandTimer = 10000;
        }
        else
            _backhandTimer -= diff;

        if (me->GetHealthPct() < 26.0f)
        {
            if (_frenzyTimer < diff)
            {
                if (DoCastSelf(SPELL_FRENZY) == SPELL_CAST_OK)
                {
                    ClassicScriptText(EMOTE_GENERIC_FRENZY_KILL, me);
                    _frenzyTimer = 120000;
                }
            }
            else
                _frenzyTimer -= diff;
        }
    }

private:
    uint32 _rendTimer;
    uint32 _backhandTimer;
    uint32 _frenzyTimer;
};

void AddSC_classic_boss_doctor_theolen_krastinov()
{
    RegisterCreatureAI(classic_boss_doctor_theolen_krastinov);
}
