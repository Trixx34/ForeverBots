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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/boss_high_inquisitor_fairbanks.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_high_inquisitor_fairbanks (4542)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

namespace
{
enum ClassicFairbanks
{
    SPELL_FAIRBANKS_CURSEOFBLOOD        = 8282,
    SPELL_FAIRBANKS_DISPELMAGIC         = 15090,
    SPELL_FAIRBANKS_FEAR                = 12096,
    SPELL_FAIRBANKS_HEAL                = 12039,
    SPELL_FAIRBANKS_POWERWORDSHIELD     = 11647,
    SPELL_FAIRBANKS_SLEEP               = 8399,

    SPELL_FAIRBANKS_AB_EFFECT_000       = 28441,
    SPELL_FAIRBANKS_TRANSFORM           = 28443
};
}

struct classic_boss_high_inquisitor_fairbanks : public ScriptedAI
{
    classic_boss_high_inquisitor_fairbanks(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _curseOfBloodTimer = 10000;
        _dispelMagicTimer = 30000;
        _fearTimer = 40000;
        _healTimer = 30000;
        _sleepTimer = 30000;
        _dispelTimer = 20000;
        _powerWordShield = false;
        _ashbringer = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_FAIRBANKS_AB_EFFECT_000 && !_ashbringer && caster && me->IsWithinLOSInMap(caster))
        {
            me->SetSheath(SHEATH_STATE_UNARMED);
            me->CastSpell(me, SPELL_FAIRBANKS_TRANSFORM, true);
            me->SetFacingToObject(caster);
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            _ashbringer = true;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // If we are <25% hp cast Heal
        if (me->GetHealthPct() <= 25.0f && !me->IsNonMeleeSpellCast(false) && _healTimer < diff)
        {
            DoCastSelf(SPELL_FAIRBANKS_HEAL);
            _healTimer = 30000;
        }
        else
            _healTimer -= diff; // VMaNGOS behaviour (may wrap below 0 like the original)

        if (_fearTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
                DoCast(target, SPELL_FAIRBANKS_FEAR);

            _fearTimer = 40000;
        }
        else
            _fearTimer -= diff;

        if (_sleepTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0))
                DoCast(target, SPELL_FAIRBANKS_SLEEP);

            _sleepTimer = 30000;
        }
        else
            _sleepTimer -= diff;

        if (!_powerWordShield && me->GetHealthPct() <= 25.0f)
        {
            DoCastSelf(SPELL_FAIRBANKS_POWERWORDSHIELD);
            _powerWordShield = true;
        }

        // VMaNGOS checks Dispel_Timer but resets/decrements DispelMagic_Timer (kept as-is)
        if (_dispelTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_FAIRBANKS_DISPELMAGIC);

            _dispelMagicTimer = 30000;
        }
        else
            _dispelMagicTimer -= diff;

        if (_curseOfBloodTimer < diff)
        {
            DoCastVictim(SPELL_FAIRBANKS_CURSEOFBLOOD);
            _curseOfBloodTimer = 25000;
        }
        else
            _curseOfBloodTimer -= diff;
    }

private:
    uint32 _curseOfBloodTimer;
    uint32 _dispelMagicTimer;
    uint32 _fearTimer;
    uint32 _healTimer;
    uint32 _sleepTimer;
    uint32 _dispelTimer;
    bool _powerWordShield;
    bool _ashbringer;
};

void AddSC_classic_boss_high_inquisitor_fairbanks()
{
    RegisterCreatureAI(classic_boss_high_inquisitor_fairbanks);
}
