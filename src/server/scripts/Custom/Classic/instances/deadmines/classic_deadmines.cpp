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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/westfall/deadmines/deadmines.cpp (ScriptDev2 lineage, GPL-2)
// Ported: go_door_lever_dm, go_defias_cannon, go_defias_gunpowder

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "classic_deadmines.h"

/*######
## go_door_lever_dm
######*/

struct classic_go_door_lever_dm : public GameObjectAI
{
    classic_go_door_lever_dm(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        GameObject* door = me->GetMap()->GetGameObject(instance->GetGuidData(CDM_DATA_DEFIAS_DOOR));

        // VMaNGOS: return !(door && door->GetGoState() == 1) -> lever only usable while the iron clad door is closed (GO_STATE_READY)
        return !(door && door->GetGoState() == GO_STATE_READY);
    }
};

/*######
## go_defias_cannon
######*/

struct classic_go_defias_cannon : public GameObjectAI
{
    classic_go_defias_cannon(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        if (instance->GetData(CDM_TYPE_DEFIAS_ENDDOOR) == DONE || instance->GetData(CDM_TYPE_DEFIAS_ENDDOOR) == IN_PROGRESS)
            return false;

        instance->SetData(CDM_TYPE_DEFIAS_ENDDOOR, IN_PROGRESS);
        return false;
    }
};

/*######
## go_defias_gunpowder
######*/

enum ClassicDefiasGunpowder
{
    CDM_NPC_DEFIAS_OVERSEER     = 634
};

struct classic_go_defias_gunpowder : public GameObjectAI
{
    classic_go_defias_gunpowder(GameObject* go) : GameObjectAI(go), _moveStep(0), _checkTimer(0) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        if (instance->GetData(CDM_GUN_POWDER_EVENT) == 0)
        {
            if (TempSummon* pirate3 = me->SummonCreature(CDM_NPC_DEFIAS_OVERSEER, -131.290833f, -591.243103f, 18.077190f, 4.792192f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 310000ms))
            {
                pirate3->GetMotionMaster()->MovePoint(0, -128.925980f, -616.494629f, 13.532340f, true, 6.269623f);
                pirate3->SetRespawnDelay(350);
                _overseerGUID = pirate3->GetGUID();
                _moveStep = 0;
                _checkTimer = 500;
            }
            instance->SetData(CDM_GUN_POWDER_EVENT, 1);
        }

        // TODO(classic): VMaNGOS returns true here (script handled the use). In TC the chest's open-lock spell also goes
        // through GameObject::Use -> OnGossipHello, so returning true would block the loot; return false to keep it lootable.
        return false;
    }

    // VMaNGOS SummonedMovementInform (GameObjectAI hook that does not exist in TC): poll the summon's arrival instead
    void UpdateAI(uint32 diff) override
    {
        if (_overseerGUID.IsEmpty())
            return;

        if (_checkTimer > diff)
        {
            _checkTimer -= diff;
            return;
        }
        _checkTimer = 500;

        Creature* summoned = me->GetMap()->GetCreature(_overseerGUID);
        if (!summoned || !summoned->IsAlive())
        {
            _overseerGUID.Clear();
            return;
        }

        if (summoned->IsInCombat() || summoned->isMoving())
            return;

        if (_moveStep == 0 && summoned->GetExactDist(-128.925980f, -616.494629f, 13.532340f) < 2.0f)
        {
            summoned->GetMotionMaster()->MovePoint(1, -115.263672f, -617.396118f, 13.579387f, true, 6.182347f);
            _moveStep = 1;
        }
        else if (_moveStep == 1 && summoned->GetExactDist(-115.263672f, -617.396118f, 13.579387f) < 2.0f)
        {
            summoned->SetHomePosition(-115.263672f, -617.396118f, 13.579387f, 6.182347f);
            _overseerGUID.Clear();
        }
    }

private:
    ObjectGuid _overseerGUID;
    uint8 _moveStep;
    uint32 _checkTimer;
};

void AddSC_classic_deadmines()
{
    RegisterGameObjectAI(classic_go_door_lever_dm);
    RegisterGameObjectAI(classic_go_defias_cannon);
    RegisterGameObjectAI(classic_go_defias_gunpowder);
}
