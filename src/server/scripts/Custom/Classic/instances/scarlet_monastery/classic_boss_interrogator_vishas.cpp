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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/boss_interrogator_vishas.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_interrogator_vishas (3983)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "classic_scarlet_monastery.h"
#include "classic_script_text.h"

namespace
{
enum ClassicInterrogatorVishas
{
    SAY_VISHAS_AGGRO            = 6204, // Tell me... tell me everything!
    SAY_VISHAS_HEALTH1          = 6206, // Naughty secrets!
    SAY_VISHAS_HEALTH2          = 6207, // I'll rip the secrets from your flesh!
    SAY_VISHAS_KILL             = 6205, // Purged by pain!
    SAY_VISHAS_TRIGGER_VORREL   = 1376, // Finally. The bastard got what he deserved.

    SPELL_VISHAS_SHADOWWORDPAIN = 2767
};
}

struct classic_boss_interrogator_vishas : public ScriptedAI
{
    classic_boss_interrogator_vishas(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
        Initialize();
    }

    void Initialize()
    {
        _yell30 = false;
        _yell60 = false;
        _shadowWordPainTimer = 5000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_VISHAS_AGGRO, me);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        ClassicScriptText(SAY_VISHAS_KILL, me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!_instance)
            return;

        if (Creature* vorrel = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(CLASSIC_SM_DATA_VORREL)))
            ClassicScriptText(SAY_VISHAS_TRIGGER_VORREL, vorrel);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (!_yell60 && me->GetHealthPct() <= 60.0f)
        {
            ClassicScriptText(SAY_VISHAS_HEALTH1, me);
            _yell60 = true;
        }

        if (!_yell30 && me->GetHealthPct() <= 30.0f)
        {
            ClassicScriptText(SAY_VISHAS_HEALTH2, me);
            _yell30 = true;
        }

        if (_shadowWordPainTimer < diff)
        {
            DoCastVictim(SPELL_VISHAS_SHADOWWORDPAIN);
            _shadowWordPainTimer = urand(5000, 15000);
        }
        else
            _shadowWordPainTimer -= diff;
    }

private:
    InstanceScript* _instance;
    bool _yell30;
    bool _yell60;
    uint32 _shadowWordPainTimer;
};

void AddSC_classic_boss_interrogator_vishas()
{
    RegisterCreatureAI(classic_boss_interrogator_vishas);
}
