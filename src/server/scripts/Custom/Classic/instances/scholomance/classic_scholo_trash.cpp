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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/scholo_trash.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: npc_unstable_corpse, npc_reanimated_corpse, npc_spectral_projection
// TODO(classic): VMaNGOS notes a DB fix for Dark Plague (spell_proc_event 12038 ppmRate 3, spell_mod 18270 debuff stacking
// between mobs); not applied here.

#include "ScriptMgr.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

namespace
{
    enum ClassicScholoTrashData : uint32
    {
        SPELL_DARK_PLAGUE_AURA  = 12038,    // procs 18270
        SPELL_FULL_HEAL         = 17683,
        SPELL_EXPLOSION         = 17689,

        NPC_SPECTRAL_PROJECTION = 11263,

        SPELL_MANA_BURN         = 17630,
        SPELL_SILENCE           = 12528,
        SPELL_IMAGE_PROJECTION  = 17651,
        SPELL_PROJECTION_LEECH  = 17652,
        SPELL_SUMMON_PROJECTION = 17653
    };
}

/*######
## npc_unstable_corpse
######*/

struct classic_npc_unstable_corpse : public ScriptedAI
{
    classic_npc_unstable_corpse(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override { }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // VMaNGOS CF_TRIGGERED | CF_AURA_NOT_PRESENT
        if (!me->HasAura(SPELL_DARK_PLAGUE_AURA))
            DoCastSelf(SPELL_DARK_PLAGUE_AURA, true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoCastSelf(SPELL_EXPLOSION, true);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        UpdateVictim();
    }
};

/*######
## npc_reanimated_corpse
######*/

struct classic_npc_reanimated_corpse : public ScriptedAI
{
    classic_npc_reanimated_corpse(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _healTimer = 0;
        _hasRessed = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // VMaNGOS CF_TRIGGERED | CF_AURA_NOT_PRESENT
        if (!me->HasAura(SPELL_DARK_PLAGUE_AURA))
            DoCastSelf(SPELL_DARK_PLAGUE_AURA, true);
    }

    void Resurrect()
    {
        DoCastSelf(SPELL_FULL_HEAL, true);
        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->AttackStop();
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        // VMaNGOS SetInvincibilityHpThreshold(1) until the corpse has resurrected once
        if (_hasRessed)
            return;

        if (damage < me->GetHealth())
            return;

        damage = me->GetHealth() > 1 ? uint32(me->GetHealth() - 1) : 0;

        if (!_healTimer)
        {
            me->SetHealth(1);
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveIdle();
            me->SetStandState(UNIT_STAND_STATE_DEAD);   // UNIT_DYNFLAG_DEAD does not exist in TC master
            _healTimer = 10000;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (_healTimer)
        {
            if (_healTimer <= diff)
            {
                Resurrect();
                _hasRessed = true;
                _healTimer = 0;
            }
            else
            {
                _healTimer -= diff;
                return;
            }
        }

        UpdateVictim();
    }

private:
    uint32 _healTimer;
    bool _hasRessed;
};

/*######
## npc_spectral_projection
######*/

struct classic_npc_spectral_projection : public ScriptedAI
{
    classic_npc_spectral_projection(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override { }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_PROJECTION_LEECH)
        {
            if (Unit* unit = caster ? caster->ToUnit() : nullptr)
            {
                // hack life leech effect
                unit->ModifyHealth(1000);
                // remove from world, or projections will respawn
                me->DespawnOrUnsummon();
            }
        }
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        UpdateVictim();
    }
};

void AddSC_classic_scholo_trash()
{
    RegisterCreatureAI(classic_npc_unstable_corpse);
    RegisterCreatureAI(classic_npc_reanimated_corpse);
    RegisterCreatureAI(classic_npc_spectral_projection);
}
