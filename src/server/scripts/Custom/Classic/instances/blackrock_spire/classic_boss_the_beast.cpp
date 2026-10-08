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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_the_beast.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

namespace
{
    enum ClassicTheBeastSpells : uint32
    {
        SPELL_FLAMEBREAK       = 16785,
        AURA_IMMOLATE          = 15506,
        SPELL_TERRIFYINGROAR   = 14100,
        SPELL_BERSERKER_CHARGE = 16636,
        SPELL_FIREBALL         = 16788,
        SPELL_FIREBLAST        = 14144,
        SPELL_SUMMON_FINKLE    = 16710
    };
}

struct classic_boss_the_beast : public ScriptedAI
{
    classic_boss_the_beast(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _flamebreakTimer     = urand(8000, 12000);
        _terrifyingRoarTimer = 13000;
        _berserkerChargeTimer = 0;
        _fireballTimer       = 10000;
        _fireBlastTimer      = urand(8000, 11000);
    }

    void Reset() override
    {
        Initialize();
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        // VMaNGOS checks Effect[0] == SPELL_EFFECT_SKINNING: skinning The Beast summons Finkle Einhorn
        if (caster && spellInfo->HasEffect(SPELL_EFFECT_SKINNING))
            caster->CastSpell(caster, SPELL_SUMMON_FINKLE, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->HasAura(AURA_IMMOLATE))
            DoCastSelf(AURA_IMMOLATE, true);

        if (!UpdateVictim())
            return;

        if (_flamebreakTimer < diff)
        {
            if (DoCastSelf(SPELL_FLAMEBREAK) == SPELL_CAST_OK)
                _flamebreakTimer = urand(14000, 20000);
        }
        else
            _flamebreakTimer -= diff;

        if (_terrifyingRoarTimer < diff)
        {
            if (DoCastSelf(SPELL_TERRIFYINGROAR) == SPELL_CAST_OK)
                _terrifyingRoarTimer = urand(16000, 18000);
        }
        else
            _terrifyingRoarTimer -= diff;

        if (_berserkerChargeTimer <= diff)
        {
            Unit* target = SelectTarget(SelectTargetMethod::Random, 1);
            if (_berserkerChargeTimer == 0)
                target = me->GetVictim();

            if (target && DoCast(target, SPELL_BERSERKER_CHARGE) == SPELL_CAST_OK)
                _berserkerChargeTimer = urand(15000, 20000);
        }
        else
            _berserkerChargeTimer -= diff;

        if (_fireballTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
            {
                if (DoCast(target, SPELL_FIREBALL) == SPELL_CAST_OK)
                    _fireballTimer = urand(10000, 12000);
            }
        }
        else
            _fireballTimer -= diff;

        if (_fireBlastTimer < diff)
        {
            if (Unit* target = me->GetVictim())
            {
                if (DoCast(target, SPELL_FIREBLAST) == SPELL_CAST_OK)
                    _fireBlastTimer = urand(14000, 20000);
            }
        }
        else
            _fireBlastTimer -= diff;
    }

private:
    uint32 _flamebreakTimer;
    uint32 _terrifyingRoarTimer;
    uint32 _berserkerChargeTimer;
    uint32 _fireballTimer;
    uint32 _fireBlastTimer;
};

void AddSC_classic_boss_the_beast()
{
    RegisterCreatureAI(classic_boss_the_beast);
}
