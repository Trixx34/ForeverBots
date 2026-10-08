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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_spire/boss_warmaster_voone.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

namespace
{
    enum ClassicVooneData : uint32
    {
        SPELL_SNAPKICK          = 15618,
        SPELL_CLEAVE            = 15284,
        SPELL_UPPERCUT          = 10966,
        SPELL_MORTALSTRIKE      = 15708,
        SPELL_PUMMEL            = 15615,
        SPELL_THROWAXE          = 16075,
        SPELL_UNARMED_PASSIVE   = 16076,

        ITEM_VOONE_MAINHAND_AFTER_THROW = 12348,

        VOONE_VIRTUAL_SLOT_MAINHAND = 0,    // VMaNGOS BASE_ATTACK
        VOONE_VIRTUAL_SLOT_OFFHAND  = 1     // VMaNGOS OFF_ATTACK
    };
}

struct classic_boss_warmaster_voone : public ScriptedAI
{
    classic_boss_warmaster_voone(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _snapKickTimer = 8000;
        _cleaveTimer = 14000;
        _uppercutTimer = 20000;
        _mortalStrikeTimer = 12000;
        _pummelTimer = 32000;
        _throwAxeTimer = 1000;
        _axesThrownCount = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_THROWAXE)
        {
            switch (_axesThrownCount)
            {
                case 0:
                    me->SetVirtualItem(VOONE_VIRTUAL_SLOT_MAINHAND, ITEM_VOONE_MAINHAND_AFTER_THROW);
                    me->SetVirtualItem(VOONE_VIRTUAL_SLOT_OFFHAND, 0);
                    break;
                case 1:
                    me->SetVirtualItem(VOONE_VIRTUAL_SLOT_MAINHAND, 0);
                    me->SetVirtualItem(VOONE_VIRTUAL_SLOT_OFFHAND, 0);
                    DoCastSelf(SPELL_UNARMED_PASSIVE, true);
                    break;
                default:
                    break;
            }
            ++_axesThrownCount;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_snapKickTimer < diff)
        {
            DoCastVictim(SPELL_SNAPKICK);
            _snapKickTimer = 6000;
        }
        else
            _snapKickTimer -= diff;

        if (_cleaveTimer < diff)
        {
            DoCastVictim(SPELL_CLEAVE);
            _cleaveTimer = 12000;
        }
        else
            _cleaveTimer -= diff;

        if (_uppercutTimer < diff)
        {
            DoCastVictim(SPELL_UPPERCUT);
            _uppercutTimer = 14000;
        }
        else
            _uppercutTimer -= diff;

        if (_mortalStrikeTimer < diff)
        {
            DoCastVictim(SPELL_MORTALSTRIKE);
            _mortalStrikeTimer = 10000;
        }
        else
            _mortalStrikeTimer -= diff;

        if (_pummelTimer < diff)
        {
            DoCastVictim(SPELL_PUMMEL);
            _pummelTimer = 16000;
        }
        else
            _pummelTimer -= diff;

        if (!me->HasAura(SPELL_UNARMED_PASSIVE))
        {
            if (_throwAxeTimer < diff)
            {
                DoCastVictim(SPELL_THROWAXE);
                _throwAxeTimer = 8000;
            }
            else
                _throwAxeTimer -= diff;
        }
    }

private:
    uint32 _snapKickTimer;
    uint32 _cleaveTimer;
    uint32 _uppercutTimer;
    uint32 _mortalStrikeTimer;
    uint32 _pummelTimer;
    uint32 _throwAxeTimer;
    uint32 _axesThrownCount;
};

void AddSC_classic_boss_warmaster_voone()
{
    RegisterCreatureAI(classic_boss_warmaster_voone);
}
