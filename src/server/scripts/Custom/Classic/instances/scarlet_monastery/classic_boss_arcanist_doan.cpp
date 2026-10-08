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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/boss_arcanist_doan.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_arcanist_doan (6487)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"

namespace
{
enum ClassicArcanistDoan
{
    SAY_DOAN_AGGRO              = 6199, // You will not defile these mysteries!
    SAY_DOAN_BURN_IN_FIRE       = 6200, // Burn in righteous fire!

    SPELL_DOAN_POLYMORPH        = 13323,
    SPELL_DOAN_AOESILENCE       = 8988,
    SPELL_DOAN_ARCANEEXPLOSION  = 9433,
    SPELL_DOAN_FIREAOE          = 9435,
    SPELL_DOAN_ARCANEBUBBLE     = 9438
};
}

struct classic_boss_arcanist_doan : public ScriptedAI
{
    classic_boss_arcanist_doan(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _polymorphTimer = 20000;
        _aoeSilenceTimer = 15000;
        _arcaneExplosionTimer = 3000;
        _canDetonate = false;
        _shielded = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_DOAN_AGGRO, me);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_shielded && _canDetonate)
        {
            ClassicScriptText(SAY_DOAN_BURN_IN_FIRE, me);
            DoCastSelf(SPELL_DOAN_FIREAOE);
            _canDetonate = false;
        }

        if (me->HasAura(SPELL_DOAN_ARCANEBUBBLE))
            return;

        // If we are <50% hp cast Arcane Bubble
        if (!_shielded && me->GetHealthPct() <= 50.0f)
        {
            // wait if we already casting
            if (me->IsNonMeleeSpellCast(false))
                return;

            DoCastSelf(SPELL_DOAN_ARCANEBUBBLE);

            _canDetonate = true;
            _shielded = true;
        }

        if (_polymorphTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
                DoCast(target, SPELL_DOAN_POLYMORPH);

            _polymorphTimer = 20000;
        }
        else
            _polymorphTimer -= diff;

        if (_aoeSilenceTimer < diff)
        {
            DoCastVictim(SPELL_DOAN_AOESILENCE);
            _aoeSilenceTimer = urand(15000, 20000);
        }
        else
            _aoeSilenceTimer -= diff;

        if (_arcaneExplosionTimer < diff)
        {
            DoCastVictim(SPELL_DOAN_ARCANEEXPLOSION);
            _arcaneExplosionTimer = 8000;
        }
        else
            _arcaneExplosionTimer -= diff;
    }

private:
    uint32 _polymorphTimer;
    uint32 _aoeSilenceTimer;
    uint32 _arcaneExplosionTimer;
    bool _canDetonate;
    bool _shielded;
};

void AddSC_classic_boss_arcanist_doan()
{
    RegisterCreatureAI(classic_boss_arcanist_doan);
}
