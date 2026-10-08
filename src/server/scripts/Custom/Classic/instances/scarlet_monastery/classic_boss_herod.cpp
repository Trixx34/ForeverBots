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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/boss_herod.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: boss_herod (3975), mob_scarlet_trainee (6575), go_herod_lever (101855)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include <list>

namespace
{
enum ClassicHerod
{
    SAY_HEROD_AGGRO             = 6194, // Ah - I've been waiting for a real challenge!
    SAY_HEROD_WHIRLWIND         = 6534, // Blades of light!
    SAY_HEROD_ENRAGE            = 6195, // Light, give me strength!
    SAY_HEROD_KILL              = 6196, // Is that all?
    EMOTE_HEROD_ENRAGE          = 7798, // %s becomes enraged!

    SAY_HEROD_TRAINEE_SPAWN     = 2842, // The master has fallen!  Avenge him, my brethren!

    SPELL_HEROD_RUSHINGCHARGE   = 8260,
    SPELL_HEROD_CLEAVE          = 15496,
    SPELL_HEROD_WHIRLWIND       = 8989,
    SPELL_HEROD_FRENZY          = 8269,

    NPC_HEROD_SCARLET_MYRMIDON  = 4295,
    NPC_HEROD_SCARLET_TRAINEE   = 6575,

    GO_HEROD_DOOR               = 101854
};

Position const HerodMyrmidonSpawn = { 1926.03f, -370.61f, 18.0f, 0.05f };
Position const HerodRoomCenter    = { 1965.09f, -431.61f, 6.79f, 0.0f };
}

struct classic_boss_herod : public ScriptedAI
{
    classic_boss_herod(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _enrage = false;
        _traineeSay = false;
        _whirlwind = false;
        _nbTrainee = 0;
        _rushingChargeTimer = 1500;
        _cleaveTimer = 12000;
        _whirlwindTimer = urand(10000, 20000);
        _rootTimer = 0;
        _roomCheck = 500;
        _myrmidonsSpawned = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        SpawnMyrmidons();
        ClassicScriptText(SAY_HEROD_AGGRO, me);
        DoCastSelf(SPELL_HEROD_RUSHINGCHARGE);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        ClassicScriptText(SAY_HEROD_KILL, me);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_HEROD_SCARLET_TRAINEE)
        {
            if (!_traineeSay)
            {
                ClassicScriptText(SAY_HEROD_TRAINEE_SPAWN, summoned);
                _traineeSay = true;
            }

            if (_nbTrainee < 10)
                summoned->GetMotionMaster()->MovePoint(0, 1940.257080f, -434.454315f, 17.094456f);
            else
                summoned->GetMotionMaster()->MovePoint(100, 1940.508301f, -428.826080f, 17.095098f);

            ++_nbTrainee;
            return;
        }

        summoned->SetCanGiveExperience(false); // VMaNGOS SetNoXP()
        _myrmidonGuids.push_back(summoned->GetGUID());
    }

    void EngageMyrmidons(Unit* victim)
    {
        if (!victim)
            return;

        for (ObjectGuid const& guid : _myrmidonGuids)
        {
            if (Creature* myrmidon = me->GetMap()->GetCreature(guid))
            {
                if (!myrmidon->IsAlive() || myrmidon->GetVictim())
                    continue;
                if (victim->IsAlive())
                    myrmidon->SetInCombatWith(victim);
            }
        }
    }

    void SpawnMyrmidons()
    {
        _myrmidonsSpawned = true;
        for (uint8 i = 0; i < 4; ++i)
        {
            me->SummonCreature(NPC_HEROD_SCARLET_MYRMIDON,
                HerodMyrmidonSpawn.GetPositionX() + frand(-3.0f, 3.0f),
                HerodMyrmidonSpawn.GetPositionY() + frand(-3.0f, 3.0f),
                HerodMyrmidonSpawn.GetPositionZ(),
                HerodMyrmidonSpawn.GetOrientation(), TEMPSUMMON_DEAD_DESPAWN, 20s);
        }
    }

    void DespawnMyrmidons()
    {
        for (ObjectGuid const& guid : _myrmidonGuids)
        {
            if (Creature* myrmidon = me->GetMap()->GetCreature(guid))
            {
                if (myrmidon->IsAlive() && !myrmidon->GetVictim())
                    myrmidon->DespawnOrUnsummon();
            }
        }

        _myrmidonsSpawned = false;
        _myrmidonGuids.clear();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        me->ClearUnitState(UNIT_STATE_ROOT);
        DespawnMyrmidons();
        ScriptedAI::EnterEvadeMode(why);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DespawnMyrmidons();
        for (uint8 i = 0; i < 20; ++i)
            me->SummonCreature(NPC_HEROD_SCARLET_TRAINEE, 1939.18f, -431.58f, 17.09f, 6.22f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 180s);

        if (GameObject* door = me->FindNearestGameObject(GO_HEROD_DOOR, 100.0f))
        {
            if (door->GetGoState() != GO_STATE_ACTIVE)
                door->UseDoorOrButton();
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // check if the target is still inside the room
        if (_myrmidonsSpawned && _roomCheck <= diff)
        {
            if (!me->IsWithinDist2d(HerodRoomCenter.GetPositionX(), HerodRoomCenter.GetPositionY(), 32.0f))
                EngageMyrmidons(me->GetVictim());
            _roomCheck = 500;
        }
        else
            _roomCheck -= diff;

        if (_whirlwind)
        {
            if (_rootTimer < diff)
            {
                me->ClearUnitState(UNIT_STATE_ROOT);
                _whirlwind = false;
            }
            else
            {
                _rootTimer -= diff;
                return;
            }
        }

        // If we are <50% hp goes Enraged
        if (!_enrage && me->GetHealthPct() <= 50.0f && !me->IsNonMeleeSpellCast(false))
        {
            if (DoCastSelf(SPELL_HEROD_FRENZY) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_HEROD_ENRAGE, me);
                ClassicScriptText(SAY_HEROD_ENRAGE, me);
                _enrage = true;
            }
        }

        // Rushing Charge
        if (_rushingChargeTimer < diff)
        {
            Unit* victim = me->GetVictim();
            if (victim && !victim->IsInRange(me, 0.0f, MELEE_RANGE + 10.0f))
            {
                if (DoCastSelf(SPELL_HEROD_RUSHINGCHARGE) == SPELL_CAST_OK)
                    _rushingChargeTimer = 4500;
            }
        }
        else
            _rushingChargeTimer -= diff;

        // Cleave
        if (_cleaveTimer < diff)
        {
            if (DoCastVictim(SPELL_HEROD_CLEAVE) == SPELL_CAST_OK)
                _cleaveTimer = 12000;
        }
        else
            _cleaveTimer -= diff;

        // Whirlwind
        if (_whirlwindTimer < diff)
        {
            if (DoCastVictim(SPELL_HEROD_WHIRLWIND) == SPELL_CAST_OK)
            {
                me->AddUnitState(UNIT_STATE_ROOT);
                _whirlwind = true;
                _rootTimer = 11000;
                ClassicScriptText(SAY_HEROD_WHIRLWIND, me);
                _whirlwindTimer = urand(20000, 30000);
            }
        }
        else
            _whirlwindTimer -= diff;
    }

private:
    bool _enrage;
    bool _traineeSay;
    bool _whirlwind;
    bool _myrmidonsSpawned;
    uint8 _nbTrainee;
    uint32 _rushingChargeTimer;
    uint32 _cleaveTimer;
    uint32 _whirlwindTimer;
    uint32 _rootTimer;
    uint32 _roomCheck;
    std::list<ObjectGuid> _myrmidonGuids;
};

/*######
## mob_scarlet_trainee
######*/

struct classic_mob_scarlet_trainee : public ScriptedAI
{
    classic_mob_scarlet_trainee(Creature* creature) : ScriptedAI(creature), _hasFled(false), _startTimer(urand(1000, 6000)),
        _group1(false), _group2(false) { }

    void Reset() override
    {
        _hasFled = false;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_startTimer)
        {
            if (_startTimer <= diff)
            {
                me->SetSpeedRate(MOVE_WALK, 2.20f);

                if (_group1)
                    me->GetMotionMaster()->MovePoint(1, 1946.433594f, -435.955109f, 16.367277f);
                else if (_group2)
                    me->GetMotionMaster()->MovePoint(101, 1940.257080f, -434.454315f, 17.094456f);

                _startTimer = 0;
            }
            else
                _startTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (!_hasFled && me->GetHealthPct() < 15.0f)
        {
            _hasFled = true;
            me->DoFleeToGetAssistance(); // VMaNGOS DoFlee()
            return;
        }
    }

    void MovementInform(uint32 movementType, uint32 id) override
    {
        if (movementType != POINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 0:
                _group1 = true;
                break;
            case 100:
                _group2 = true;
                break;
            case 1:
                me->GetMotionMaster()->MovePoint(2, 1952.834717f, -447.514130f, 13.804327f);
                break;
            case 101:
                me->GetMotionMaster()->MovePoint(102, 1953.056763f, -416.109863f, 13.861217f);
                break;
            case 2:
                me->GetMotionMaster()->MovePoint(3, 1965.592041f, -451.153778f, 11.272284f);
                break;
            case 102:
                me->GetMotionMaster()->MovePoint(103, 1965.369629f, -412.147949f, 11.272387f);
                break;
            case 3:
                me->GetMotionMaster()->MovePoint(4, 1982.692749f, -441.514343f, 11.272284f);
                break;
            case 103:
                me->GetMotionMaster()->MovePoint(104, 1980.908081f, -421.008026f, 11.272387f);
                break;
            case 4:
                me->GetMotionMaster()->MovePoint(5, 1978.061890f, -428.549500f, 11.272232f);
                break;
            case 104:
                me->GetMotionMaster()->MovePoint(105, 1979.139038f, -434.856934f, 11.272370f);
                break;
            case 5:
                me->GetMotionMaster()->MovePoint(6, 1971.447144f, -419.629272f, 8.087179f);
                break;
            case 105:
                me->GetMotionMaster()->MovePoint(106, 1972.044800f, -442.568573f, 8.434578f);
                break;
            case 6:
                me->GetMotionMaster()->MovePoint(7, 1964.354004f, -418.632904f, 6.177466f);
                break;
            case 106:
                me->GetMotionMaster()->MovePoint(107, 1964.691162f, -444.223022f, 6.177622f);
                break;
            case 7:
            case 107:
                me->GetMotionMaster()->MovePoint(116, 1965.039795f, -431.733856f, 6.177539f);
                break;
            default:
                break;
        }
    }

private:
    bool _hasFled;
    uint32 _startTimer;
    bool _group1;
    bool _group2;
};

/*######
## go_herod_lever
######*/

struct classic_go_herod_lever : public GameObjectAI
{
    classic_go_herod_lever(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        if (GameObject* door = me->FindNearestGameObject(GO_HEROD_DOOR, 40.0f))
        {
            if (door->getLootState() == GO_READY || door->getLootState() == GO_JUST_DEACTIVATED)
                door->UseDoorOrButton();
            else
                door->ResetDoorOrButton();
        }

        return true;
    }
};

void AddSC_classic_boss_herod()
{
    RegisterCreatureAI(classic_boss_herod);
    RegisterCreatureAI(classic_mob_scarlet_trainee);
    RegisterGameObjectAI(classic_go_herod_lever);
}
