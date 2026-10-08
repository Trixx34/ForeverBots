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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/desolace/maraudon/boss_noxxion.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_noxxion (13282)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

namespace
{
enum ClassicNoxxion
{
    SPELL_NOXXION_TOXICVOLLEY   = 21687,
    SPELL_NOXXION_UPPERCUT      = 22916,

    NPC_NOXXION_SPAWN           = 13456,    // Noxxion's Spawns

    FACTION_NOXXION_HOSTILE     = 14,
    FACTION_NOXXION_FRIENDLY    = 35,

    MODEL_NOXXION               = 11172,
    MODEL_NOXXION_INVISIBLE     = 11686
};
}

struct classic_boss_noxxion : public ScriptedAI
{
    classic_boss_noxxion(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _toxicVolleyTimer = 7000;
        _uppercutTimer = 16000;
        _addsTimer = 19000;
        _invisibleTimer = 15000; // Too much too low?
        _invisible = false;
    }

    void Reset() override
    {
        Initialize();
        me->SetCanMelee(true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DeMorph();
        me->RestoreFaction(); // VMaNGOS SetFactionTemplateId(GetCreatureInfo()->faction)
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
    }

    void SummonAdds(Unit* victim)
    {
        Position const pos = me->GetRandomPoint(*me, 8.0f); // VMaNGOS DoSpawnCreature(13456, 8.0f, ...) (random offset)
        if (TempSummon* summoned = me->SummonCreature(NPC_NOXXION_SPAWN, pos, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 90s))
            if (summoned->AI())
                summoned->AI()->AttackStart(victim);
    }

    void UpdateAI(uint32 diff) override
    {
        if (_invisible && _invisibleTimer < diff)
        {
            // Become visible again
            me->SetFaction(FACTION_NOXXION_HOSTILE);
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            // Noxxion model
            me->SetDisplayId(MODEL_NOXXION);
            me->SetCanMelee(true);
            _invisible = false;
        }
        else if (_invisible)
        {
            _invisibleTimer -= diff;
            // Do nothing while invisible
            return;
        }

        // Return since we have no target
        if (!UpdateVictim())
            return;

        if (_toxicVolleyTimer < diff)
        {
            if (DoCastVictim(SPELL_NOXXION_TOXICVOLLEY) == SPELL_CAST_OK)
                _toxicVolleyTimer = 9000;
        }
        else
            _toxicVolleyTimer -= diff;

        if (_uppercutTimer < diff)
        {
            if (DoCastVictim(SPELL_NOXXION_UPPERCUT) == SPELL_CAST_OK)
                _uppercutTimer = 12000;
        }
        else
            _uppercutTimer -= diff;

        if (!_invisible && _addsTimer < diff)
        {
            // Interrupt any spell casting
            me->InterruptNonMeleeSpells(false);
            me->SetFaction(FACTION_NOXXION_FRIENDLY);
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            // Invisible Model
            me->SetDisplayId(MODEL_NOXXION_INVISIBLE);
            for (int i = 0; i < 5; ++i)
                SummonAdds(me->GetVictim());
            _invisible = true;
            _invisibleTimer = 15000;
            // VMaNGOS simply skipped DoMeleeAttackIfReady() while invisible; TC melee is automatic
            me->SetCanMelee(false);

            _addsTimer = 40000;
        }
        else
            _addsTimer -= diff;
    }

private:
    uint32 _toxicVolleyTimer;
    uint32 _uppercutTimer;
    uint32 _addsTimer;
    uint32 _invisibleTimer;
    bool _invisible;
};

void AddSC_classic_boss_noxxion()
{
    RegisterCreatureAI(classic_boss_noxxion);
}
