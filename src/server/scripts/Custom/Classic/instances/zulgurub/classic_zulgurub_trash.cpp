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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/zulgurub_trash.cpp (GPL-2)
// Scripts: npc_gurubashi_berserker, npc_hakkari_doctor, npc_esprit_vaudou, npc_jinxed_voodoo_pile
// (npc_gurubashi_axethrower and npc_fils_hakkar are not registered in VMaNGOS and are not ported)

#include "ScriptMgr.h"
#include "Creature.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_zulgurub.h"

namespace
{
enum ClassicZgBerserker : uint32
{
    SPELL_ZG_BERSERKER_THUNDERCLAP  = 15588,
    SPELL_ZG_BERSERKER_FEAR         = 16508,
    SPELL_ZG_BERSERKER_ENRAGE       = 8269,
    SPELL_ZG_BERSERKER_KNOCKBACK    = 11130
};

enum ClassicZgWitchDoctor : uint32
{
    SPELL_ZG_DOCTOR_HEX             = 24053,    // Malefice
    SPELL_ZG_DOCTOR_SHADOW_SHOCK    = 17289,    // Horion de l'ombre
    SPELL_ZG_TOAD_EXPLODE           = 24065,
    SPELL_ZG_VOODOO_SPIRIT_BURST    = 24050,

    NPC_ZG_DOCTOR_TOAD              = 15010,
    NPC_ZG_VOODOO_SPIRIT            = 15009
};

enum ClassicZgVoodooPile : uint32
{
    SPELL_ZG_WILL_OF_HAKKAR         = 24178
};

// VMaNGOS SummonCreatureAndAttack(entry, target)
// TODO(classic): the VMaNGOS core is not available; summon type/duration assumed (summoner position, 15s out of combat)
Creature* ClassicZgSummonCreatureAndAttack(Creature* me, uint32 entry, Unit* target)
{
    Creature* summon = me->SummonCreature(entry, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s);
    if (summon && target && summon->AI())
        summon->AI()->AttackStart(target);
    return summon;
}
}

/*######
## npc_gurubashi_berserker
######*/

struct classic_npc_gurubashi_berserker : public ScriptedAI
{
    classic_npc_gurubashi_berserker(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiKnockBack_Timer = 0;
    uint32 m_uiThunderClap_Timer = 0;
    uint32 m_uiFear_Timer = 0;
    bool   m_bEnrage = false;

    void Reset() override
    {
        m_uiKnockBack_Timer   = 10000;
        m_uiThunderClap_Timer = 5000;
        m_uiFear_Timer        = 15000;
        m_bEnrage             = false;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiKnockBack_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_ZG_BERSERKER_KNOCKBACK) == SPELL_CAST_OK)
            {
                ResetThreatList();
                m_uiKnockBack_Timer = 10000;
            }
        }
        else
            m_uiKnockBack_Timer -= uiDiff;

        if (m_uiThunderClap_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_ZG_BERSERKER_THUNDERCLAP) == SPELL_CAST_OK)
                m_uiThunderClap_Timer = urand(14000, 16000);
        }
        else
            m_uiThunderClap_Timer -= uiDiff;

        if (m_uiFear_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_ZG_BERSERKER_FEAR) == SPELL_CAST_OK)
            {
                m_uiFear_Timer = urand(25000, 30000);
                ResetThreatList();
            }
        }
        else
            m_uiFear_Timer -= uiDiff;

        if (me->GetHealthPct() < 50.0f && !m_bEnrage)
        {
            me->CastSpell(me, SPELL_ZG_BERSERKER_ENRAGE, false);
            m_bEnrage = true;
        }

        // melee: TC master auto-melee
    }
};

/*######
## npc_hakkari_doctor (11831)
## When the witch doctor dies, a Voodoo Spirit (15009) appears. It slowly moves to a random target and explodes
## (Spirit Burst 24050) when it reaches it; otherwise it disappears after a few seconds.
######*/

struct classic_npc_hakkari_doctor : public ScriptedAI
{
    classic_npc_hakkari_doctor(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiMaleficeTimer = 0;
    uint32 m_uiCrapaudsTimer = 0;
    uint32 m_uiCrapaudsLiberationTimer = 0;
    uint32 m_uiOrionOmbreTimer = 0;

    void Reset() override
    {
        m_uiMaleficeTimer = 5000;
        m_uiCrapaudsTimer = 15000;
        m_uiCrapaudsLiberationTimer = 15000;
        m_uiOrionOmbreTimer = 4000;
    }

    // VMaNGOS GetFarthestVictimInRange(10.0f, 20.0f)
    Unit* GetFarthestVictimInRange(float minRange, float maxRange)
    {
        return SelectTarget(SelectTargetMethod::MaxDistance, 0, [this, minRange, maxRange](Unit* unit)
        {
            float dist = me->GetDistance(unit);
            return dist >= minRange && dist <= maxRange;
        });
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiMaleficeTimer < uiDiff)
        {
            if (Unit* pTarget = GetFarthestVictimInRange(10.0f, 20.0f))
            {
                DoCast(pTarget, SPELL_ZG_DOCTOR_HEX);
                m_uiMaleficeTimer = urand(20000, 40000);
            }
        }
        else
            m_uiMaleficeTimer -= uiDiff;

        if (m_uiCrapaudsTimer < uiDiff)
        {
            if (Unit* pTarget = GetFarthestVictimInRange(10.0f, 20.0f))
            {
                for (uint8 i = 0; i < 4; ++i)
                    ClassicZgSummonCreatureAndAttack(me, NPC_ZG_DOCTOR_TOAD, pTarget);
                m_uiCrapaudsTimer = urand(10, 40) * 1000;
            }
        }
        else
            m_uiCrapaudsTimer -= uiDiff;

        if (m_uiCrapaudsLiberationTimer < uiDiff)
        {
            if (Creature* pCrapaud = me->FindNearestCreature(NPC_ZG_DOCTOR_TOAD, 40.0f))
            {
                pCrapaud->CastSpell(pCrapaud, SPELL_ZG_TOAD_EXPLODE, true);
                pCrapaud->DisappearAndDie();
            }
            m_uiCrapaudsLiberationTimer = 1000;
        }
        else
            m_uiCrapaudsLiberationTimer -= uiDiff;

        if (m_uiOrionOmbreTimer < uiDiff)
        {
            if (Unit* pTarget = GetFarthestVictimInRange(10.0f, 20.0f))
            {
                me->CastSpell(pTarget, SPELL_ZG_DOCTOR_SHADOW_SHOCK, true);
                m_uiOrionOmbreTimer = urand(5000, 20000);
            }
        }
        else
            m_uiOrionOmbreTimer -= uiDiff;

        // melee: TC master auto-melee
    }

    void JustDied(Unit* killer) override
    {
        ClassicZgSummonCreatureAndAttack(me, NPC_ZG_VOODOO_SPIRIT, killer);
    }
};

/*######
## npc_esprit_vaudou (15009)
######*/

struct classic_npc_esprit_vaudou : public ScriptedAI
{
    classic_npc_esprit_vaudou(Creature* creature) : ScriptedAI(creature)
    {
        // Unkillable! (VMaNGOS UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE)
        me->SetUnitFlag(UnitFlags(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE));
    }

    void Reset() override { }

    void UpdateAI(uint32 /*uiDiff*/) override
    {
        if (Unit* pVictim = me->GetVictim())
        {
            if (pVictim->GetDistance(me) < 5.0f)
            {
                pVictim->CastSpell(pVictim, SPELL_ZG_VOODOO_SPIRIT_BURST, true);
                me->DespawnOrUnsummon();
                return;
            }
        }
    }
};

/*######
## npc_jinxed_voodoo_pile (15047)
######*/

struct classic_npc_jinxed_voodoo_pile : public ScriptedAI
{
    classic_npc_jinxed_voodoo_pile(Creature* creature) : ScriptedAI(creature) { }

    bool m_castedMindControl = false;

    void Reset() override
    {
        m_castedMindControl = false;
    }

    void UpdateAI(uint32 /*uiDiff*/) override
    {
        if (me->GetCharmedGUID().IsEmpty())
        {
            if (m_castedMindControl)
            {
                me->KillSelf();
                return;
            }

            if (Player* pPlayer = me->SelectNearestPlayer(5.0f))
            {
                if (me->CastSpell(pPlayer, SPELL_ZG_WILL_OF_HAKKAR, false) == SPELL_CAST_OK)
                {
                    m_castedMindControl = true;
                    DoZoneInCombat();
                    return;
                }
            }
        }
    }
};

void AddSC_classic_zulgurub_trash()
{
    RegisterCreatureAI(classic_npc_gurubashi_berserker);
    RegisterCreatureAI(classic_npc_hakkari_doctor);
    RegisterCreatureAI(classic_npc_esprit_vaudou);
    RegisterCreatureAI(classic_npc_jinxed_voodoo_pile);
}
