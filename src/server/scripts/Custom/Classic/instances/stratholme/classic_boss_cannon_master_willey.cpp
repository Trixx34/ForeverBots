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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_cannon_master_willey.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Player.h"
#include "QuaternionData.h"
#include "ScriptedCreature.h"
#include <list>

namespace
{
enum ClassicCannonMasterWilley : uint32
{
    SPELL_WILLEY_KNOCK_AWAY = 10101,
    SPELL_WILLEY_PUMMEL     = 15615,
    SPELL_WILLEY_SHOOT      = 20463,
  //SPELL_SUMMON_CRIMSON    = 17279,            // Summons three Crimson Rifleman

    NPC_CRIMSON_RIFLEMAN    = 11054,

    GO_WILLEY_GATE          = 175969,

    GO_CANNONBALL           = 176211,
    SPELL_CANNON_FIRE       = 17278
};

// Rifleman spawn points: front left, front right, mid left, mid right, back left, back mid, back right, behind left, behind right
Position const WilleyAddPos[9] =
{
    { 3537.2725f,   -2958.18f,     125.001015f, 0.592007f },
    { 3542.206299f, -2965.929932f, 125.001015f, 0.592007f },
    { 3539.417480f, -2959.667236f, 125.001015f, 0.592007f },
    { 3540.651855f, -2964.519043f, 125.001015f, 0.592007f },
    { 3531.927246f, -2962.977295f, 125.001015f, 0.592007f },
    { 3538.094697f, -2963.123291f, 125.001015f, 0.592007f },
    { 3535.727539f, -2969.776123f, 125.001015f, 0.592007f },
    { 3532.156250f, -2966.162354f, 125.001015f, 0.592007f },
    { 3533.202148f, -2969.437744f, 125.001015f, 0.592007f }
};

// per urand(0, 8) case: which three ADD_n points (0-based) get a rifleman
uint8 const WilleyAddGroups[9][3] =
{
    { 0, 1, 3 },
    { 1, 2, 4 },
    { 2, 3, 5 },
    { 3, 4, 6 },
    { 4, 5, 7 },
    { 5, 6, 8 },
    { 6, 7, 0 },
    { 7, 8, 1 },
    { 8, 0, 2 }
};
}

/*######
## boss_cannon_master_willey
######*/

struct classic_boss_cannon_master_willey : public ScriptedAI
{
    classic_boss_cannon_master_willey(Creature* creature) : ScriptedAI(creature) { }

    bool m_bInMelee = true;
    uint32 m_uiKnockAwayTimer = 0;
    uint32 m_uiPummelTimer = 0;
    uint32 m_uiShootTimer = 0;
    uint32 m_uiSummonRiflemanTimer = 0;

    void Reset() override
    {
        ToggleGate(true);

        SetCombatMovement(true);
        m_bInMelee                  = true;

        m_uiKnockAwayTimer          = urand(15000, 20000);
        m_uiPummelTimer             = urand(5000, 10000);
        m_uiShootTimer              = 1000;
        m_uiSummonRiflemanTimer     = 5000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ToggleGate(false);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ToggleGate(true);
    }

    void ToggleGate(bool bOpen)
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return;

        if (GameObject* pGo = me->FindNearestGameObject(GO_WILLEY_GATE, 200.0f))
        {
            if (bOpen && pGo->GetGoState() == GO_STATE_READY)
                instance->DoUseDoorOrButton(pGo->GetGUID());
            if (!bOpen && pGo->GetGoState() == GO_STATE_ACTIVE)
                instance->DoUseDoorOrButton(pGo->GetGUID());
        }
    }

    void JustSummoned(Creature* summon) override
    {
        CreatureAI::DoZoneInCombat(summon);     // VMaNGOS SetInCombatWithZone
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        std::list<Creature*> RiflemanList;
        me->GetCreatureListWithEntryInGrid(RiflemanList, NPC_CRIMSON_RIFLEMAN, 200.0f);
        for (Creature* rifleman : RiflemanList)
            rifleman->DespawnOrUnsummon();

        ScriptedAI::EnterEvadeMode(why);
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        // Pummel
        if (m_uiPummelTimer < diff)
        {
            if (DoCastVictim(SPELL_WILLEY_PUMMEL) == SPELL_CAST_OK)
                m_uiPummelTimer = 12000;
        }
        else
            m_uiPummelTimer -= diff;

        // Knock Away
        if (m_uiKnockAwayTimer < diff)
        {
            if (DoCastVictim(SPELL_WILLEY_KNOCK_AWAY) == SPELL_CAST_OK)
                m_uiKnockAwayTimer = urand(15000, 20000);
        }
        else
            m_uiKnockAwayTimer -= diff;

        // Summon Rifleman
        if (m_uiSummonRiflemanTimer < diff)
        {
            uint8 const* group = WilleyAddGroups[urand(0, 8)];
            for (uint8 i = 0; i < 3; ++i)
                me->SummonCreature(NPC_CRIMSON_RIFLEMAN, WilleyAddPos[group[i]], TEMPSUMMON_TIMED_DESPAWN, 240000ms);
            m_uiSummonRiflemanTimer = 10000;
        }
        else
            m_uiSummonRiflemanTimer -= diff;

        // Shoot
        if (m_uiShootTimer < diff)
        {
            if (DoCastVictim(SPELL_WILLEY_SHOOT) == SPELL_CAST_OK)
                m_uiShootTimer = urand(2500, 3500);
        }
        else
            m_uiShootTimer -= diff;

        Unit* victim = me->GetVictim();
        if (!victim)
            return;

        if (!IsCombatMovementAllowed())
        { //Melee
            if (!m_bInMelee && (me->GetDistance2d(victim) < 8.0f || me->GetDistance2d(victim) > 27.0f || !me->IsWithinLOSInMap(victim)))
            {
                SetCombatMovement(true);
                DoStartMovement(victim);
                m_bInMelee = true;
                return;
            }
        }
        else
        { //Range
            if (m_bInMelee && me->GetDistance2d(victim) >= 8.0f && me->GetDistance2d(victim) <= 27.0f && me->IsWithinLOSInMap(victim))
            {
                SetCombatMovement(false);
                m_bInMelee = false;
                DoStartNoMovement(victim);
                return;
            }
        }
    }
};

/*######
## go_scarlet_cannon
######*/

struct classic_go_scarlet_cannon : public GameObjectAI
{
    classic_go_scarlet_cannon(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (GameObject* pCannonBall = player->SummonGameObject(GO_CANNONBALL, 3534.3f, -2966.74f, 125.001f, 0.279252f, QuaternionData(0.0f, 0.0f, 0.139173f, 0.990268f), 1s))
            pCannonBall->Use(player);
        return false;
    }
};

void AddSC_classic_boss_cannon_master_willey()
{
    RegisterCreatureAI(classic_boss_cannon_master_willey);
    RegisterGameObjectAI(classic_go_scarlet_cannon);
}
