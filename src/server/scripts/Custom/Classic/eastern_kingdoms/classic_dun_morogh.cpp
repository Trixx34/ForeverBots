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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/dun_morogh/dun_morogh.cpp (GPL-2)
// Quest support: 1783 (Narm Faulk wakes up when healed with Symbol of Life)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "classic_script_text.h"

/*######
## npc_narm_faulk
######*/

enum NarmFaulk
{
    SAY_NARM_HEAL           = 2281,
    SPELL_SYMBOL_OF_LIFE    = 8593
};

struct classic_npc_narm_faulk : public ScriptedAI
{
    classic_npc_narm_faulk(Creature* creature) : ScriptedAI(creature), _lifeTimer(120000), _spellHit(false) { }

    void Reset() override
    {
        _lifeTimer = 120000;
        me->SetStandState(UNIT_STAND_STATE_DEAD);   // lay down (UNIT_DYNFLAG_DEAD no longer exists in TC master)
        _spellHit = false;
    }

    void MoveInLineOfSight(Unit* /*who*/) override { }

    void UpdateAI(uint32 diff) override
    {
        if (me->IsStandState())
        {
            if (_lifeTimer < diff)
                EnterEvadeMode();
            else
                _lifeTimer -= diff;
        }
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_SYMBOL_OF_LIFE && !_spellHit)
        {
            me->SetStandState(UNIT_STAND_STATE_STAND);
            ClassicScriptText(SAY_NARM_HEAL, me, caster ? caster->ToUnit() : nullptr);
            _spellHit = true;
        }
    }

private:
    uint32 _lifeTimer;
    bool _spellHit;
};

void AddSC_classic_dun_morogh()
{
    RegisterCreatureAI(classic_npc_narm_faulk);
}
