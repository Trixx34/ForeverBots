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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_jandice_barov.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_jandice_barov, mob_illusionofjandicebarov

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SharedDefines.h"
#include "TemporarySummon.h"
#include <vector>

namespace
{
    enum ClassicJandiceData : uint32
    {
        SPELL_CURSEOFBLOOD          = 16098,
        SPELL_BANISH                = 8994,
        SPELL_ILLUSION              = 17773,
        SPELL_DROP_JOURNAL          = 26096,
        SPELL_PHASING               = 16122,
        SPELL_CLEAVE                = 15584,

        NPC_ILLUSION_OF_JANDICE     = 11439,

        JANDICE_FACTION_HOSTILE     = 14,
        JANDICE_FACTION_FRIENDLY    = 35
    };
}

struct classic_boss_jandice_barov : public ScriptedAI
{
    classic_boss_jandice_barov(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _curseOfBloodTimer = 10000;
        _illusionTimer = 15000;
        _invisibleTimer = 3000;                             // Too much too low?
        _invisible = false;
        _damageTaken = 0;
        _checkForDamage = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void SummonIllusions(Unit* victim)
    {
        Position pos = me->GetRandomPoint(*me, 10.0f);
        if (Creature* summoned = me->SummonCreature(NPC_ILLUSION_OF_JANDICE, pos, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 60s))
        {
            if (summoned->AI())
            {
                if (victim)
                    summoned->AI()->AttackStart(victim);
                _illusionGUIDs.push_back(summoned->GetGUID());
            }
        }
    }

    void UnsummonIllusions()
    {
        for (ObjectGuid const& guid : _illusionGUIDs)
            if (Creature* illusion = ObjectAccessor::GetCreature(*me, guid))
                illusion->DespawnOrUnsummon();
        _illusionGUIDs.clear();
    }

    void JustDied(Unit* /*killer*/) override
    {
        UnsummonIllusions();
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        me->SetVisible(true);

        // VMaNGOS: only from patch 1.9 on - always true for the Classic 1.60 client
        DoCastSelf(SPELL_DROP_JOURNAL, true);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (_checkForDamage)
        {
            _damageTaken += damage;
            if (_damageTaken > 500)
            {
                UnsummonIllusions();
                _checkForDamage = false;
                _damageTaken = 0;
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (_invisible && _invisibleTimer < diff)
        {
            // Become visible again
            me->SetFaction(JANDICE_FACTION_HOSTILE);
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            me->SetVisible(true);
            _invisible = false;
            _damageTaken = 0;
            _checkForDamage = true;
        }
        else if (_invisible)
        {
            _invisibleTimer -= diff;
            // Do nothing while invisible
            return;
        }

        if (!UpdateVictim())
            return;

        if (_curseOfBloodTimer < diff)
        {
            DoCastVictim(SPELL_CURSEOFBLOOD);
            _curseOfBloodTimer = 30000;
        }
        else
            _curseOfBloodTimer -= diff;

        if (!_invisible && _illusionTimer < diff)
        {
            // Interrupt any spell casting
            me->InterruptNonMeleeSpells(false);
            me->SetFaction(JANDICE_FACTION_FRIENDLY);
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            if (Unit* victim = me->GetVictim())
                ModifyThreatByPercent(victim, -99);
            me->SetVisible(false);

            _damageTaken = 0;
            _invisible = true;
            _invisibleTimer = 3000;

            // 25 seconds until we should cast this again
            _illusionTimer = 25000;

            // Summon 10 Illusions attacking random gamers
            for (int i = 0; i < 10; ++i)
                SummonIllusions(SelectTarget(SelectTargetMethod::Random, 0));
        }
        else
            _illusionTimer -= diff;
    }

private:
    uint32 _curseOfBloodTimer;
    uint32 _illusionTimer;
    uint32 _damageTaken;
    std::vector<ObjectGuid> _illusionGUIDs;

    uint32 _invisibleTimer;
    bool _invisible;
    bool _checkForDamage;
};

// Illusion of Jandice Barov Script

struct classic_mob_illusionofjandicebarov : public ScriptedAI
{
    classic_mob_illusionofjandicebarov(Creature* creature) : ScriptedAI(creature)
    {
        _cleaveTimer = urand(2000, 8000);
    }

    void Reset() override
    {
        _cleaveTimer = urand(2000, 8000);
        me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_MAGIC, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_cleaveTimer < diff)
        {
            if (DoCastVictim(SPELL_CLEAVE) == SPELL_CAST_OK)
                _cleaveTimer = urand(5000, 15000);
        }
        else
            _cleaveTimer -= diff;
    }

private:
    uint32 _cleaveTimer;
};

void AddSC_classic_boss_jandice_barov()
{
    RegisterCreatureAI(classic_boss_jandice_barov);
    RegisterCreatureAI(classic_mob_illusionofjandicebarov);
}
