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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/dun_morogh/gnomeregan/instance_gnomeregan.cpp (ScriptDev2 lineage, GPL-2)
// Support for Grubbis and Thermaplugg Encounters

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "classic_gnomeregan.h"
#include <list>

classic_instance_gnomeregan_InstanceScript::classic_instance_gnomeregan_InstanceScript(InstanceMap* map) : InstanceScript(map),
    _savedGrubbis(*this, "Grubbis", NOT_STARTED), _savedThermaplugg(*this, "Thermaplugg", NOT_STARTED)
{
    SetHeaders(ClassicGnomereganDataHeader);
    for (uint32& encounter : _encounter)
        encounter = NOT_STARTED;
}

// VMaNGOS Load(): saved states, IN_PROGRESS -> NOT_STARTED
void classic_instance_gnomeregan_InstanceScript::AfterDataLoad()
{
    _encounter[TYPE_GRUBBIS] = _savedGrubbis;
    _encounter[TYPE_THERMAPLUGG] = _savedThermaplugg;

    for (uint32& encounter : _encounter)
        if (encounter == IN_PROGRESS)
            encounter = NOT_STARTED;
}

void classic_instance_gnomeregan_InstanceScript::OnCreatureCreate(Creature* creature)
{
    InstanceScript::OnCreatureCreate(creature);

    switch (creature->GetEntry())
    {
        case NPC_BLASTMASTER_SHORTFUSE:
            _blastmasterShortfuseGUID = creature->GetGUID();
            break;
        case NPC_ALARM_A_BOMB_2600:
            _alarmABomb2600GUID = creature->GetGUID();
            break;
        default:
            break;
    }
}

void classic_instance_gnomeregan_InstanceScript::OnGameObjectCreate(GameObject* go)
{
    InstanceScript::OnGameObjectCreate(go);

    switch (go->GetEntry())
    {
        case GO_RED_ROCKET:
            _redRocketGUIDs.push_back(go->GetGUID());
            break;
        case GO_CAVE_IN_NORTH:
            _caveInNorthGUID = go->GetGUID();
            break;
        case GO_CAVE_IN_SOUTH:
            _caveInSouthGUID = go->GetGUID();
            break;
        case GO_EXPLOSIVE_CHARGE:
            _explosiveCharges.push_back(go->GetGUID());
            break;
        case GO_THE_FINAL_CHAMBER:
            _doorFinalChamberGUID = go->GetGUID();
            break;

        case GO_GNOME_FACE_1:
            _bombFaces[0].GnomeFaceGUID = go->GetGUID();
            break;
        case GO_GNOME_FACE_2:
            _bombFaces[1].GnomeFaceGUID = go->GetGUID();
            break;
        case GO_GNOME_FACE_3:
            _bombFaces[2].GnomeFaceGUID = go->GetGUID();
            break;
        case GO_GNOME_FACE_4:
            _bombFaces[3].GnomeFaceGUID = go->GetGUID();
            break;
        case GO_GNOME_FACE_5:
            _bombFaces[4].GnomeFaceGUID = go->GetGUID();
            break;
        case GO_GNOME_FACE_6:
            _bombFaces[5].GnomeFaceGUID = go->GetGUID();
            break;
        default:
            break;
    }
}

void classic_instance_gnomeregan_InstanceScript::SetData(uint32 type, uint32 data)
{
    switch (type)
    {
        case TYPE_GRUBBIS:
            _encounter[TYPE_GRUBBIS] = data;
            if (data == IN_PROGRESS)
            {
                // Sort the explosive charges if needed
                if (!_explosiveCharges.empty())
                {
                    std::list<GameObject*> charges;
                    for (ObjectGuid const& guid : _explosiveCharges)
                        if (GameObject* go = instance->GetGameObject(guid))
                            charges.push_back(go);

                    // Sort from east to west
                    charges.sort([](GameObject const* first, GameObject const* second)
                    {
                        return first->GetPositionY() < second->GetPositionY();
                    });

                    // Sort to south and north
                    uint8 counterSouth = 0;
                    uint8 counterNorth = 0;
                    GameObject* caveInSouth = instance->GetGameObject(_caveInSouthGUID);
                    GameObject* caveInNorth = instance->GetGameObject(_caveInNorthGUID);
                    if (caveInSouth && caveInNorth)
                    {
                        for (GameObject* go : charges)
                        {
                            if (go->GetDistanceOrder(caveInSouth, caveInNorth) && counterSouth < MAX_EXPLOSIVES_PER_SIDE)
                            {
                                _explosiveSortedGUIDs[0][counterSouth] = go->GetGUID();
                                counterSouth++;
                            }
                            else if (counterNorth < MAX_EXPLOSIVES_PER_SIDE)
                            {
                                _explosiveSortedGUIDs[1][counterNorth] = go->GetGUID();
                                counterNorth++;
                            }
                        }
                        _explosiveCharges.clear();
                    }
                }
            }
            if (data == FAIL)
            {
                // Despawn possible spawned explosive charges
                SetData(TYPE_EXPLOSIVE_CHARGE, DATA_EXPLOSIVE_CHARGE_USE);
            }
            if (data == DONE)
            {
                for (ObjectGuid const& guid : _redRocketGUIDs)
                    DoRespawnGameObject(guid, 1h);
            }
            break;
        case TYPE_EXPLOSIVE_CHARGE:
            switch (data)
            {
                case DATA_EXPLOSIVE_CHARGE_1:
                    DoRespawnGameObject(_explosiveSortedGUIDs[0][0], 1h);
                    _spawnedExplosiveChargeGUIDs.push_back(_explosiveSortedGUIDs[0][0]);
                    break;
                case DATA_EXPLOSIVE_CHARGE_2:
                    DoRespawnGameObject(_explosiveSortedGUIDs[0][1], 1h);
                    _spawnedExplosiveChargeGUIDs.push_back(_explosiveSortedGUIDs[0][1]);
                    break;
                case DATA_EXPLOSIVE_CHARGE_3:
                    DoRespawnGameObject(_explosiveSortedGUIDs[1][0], 1h);
                    _spawnedExplosiveChargeGUIDs.push_back(_explosiveSortedGUIDs[1][0]);
                    break;
                case DATA_EXPLOSIVE_CHARGE_4:
                    DoRespawnGameObject(_explosiveSortedGUIDs[1][1], 1h);
                    _spawnedExplosiveChargeGUIDs.push_back(_explosiveSortedGUIDs[1][1]);
                    break;
                case DATA_EXPLOSIVE_CHARGE_USE:
                {
                    Creature* blastmaster = instance->GetCreature(_blastmasterShortfuseGUID);
                    if (!blastmaster)
                        break;
                    for (ObjectGuid const& guid : _spawnedExplosiveChargeGUIDs)
                    {
                        if (GameObject* explosive = instance->GetGameObject(guid))
                            explosive->Use(blastmaster);
                    }
                    _spawnedExplosiveChargeGUIDs.clear();
                    break;
                }
                default:
                    break;
            }
            return;
        case TYPE_THERMAPLUGG:
            _encounter[TYPE_THERMAPLUGG] = data;
            if (data == IN_PROGRESS)
            {
                // Make Door locked
                if (GameObject* door = instance->GetGameObject(_doorFinalChamberGUID))
                {
                    if (door->getLootState() == GO_ACTIVATED)
                        door->ResetDoorOrButton();

                    // Doesn't work here, because the flags are to be reseted on next tick in GO::Update
                    door->SetFlag(GO_FLAG_LOCKED);
                }

                // Always directly activates this bomb-face
                DoActivateBombFace(2);
            }
            else if (data == DONE || data == FAIL)
            {
                // Make Door unlocked again
                if (GameObject* door = instance->GetGameObject(_doorFinalChamberGUID))
                {
                    if (door->getLootState() == GO_READY)
                        door->UseDoorOrButton();
                    door->RemoveFlag(GO_FLAG_LOCKED);
                }

                // Deactivate all remaining BombFaces
                for (uint8 i = 0; i < MAX_GNOME_FACES; i++)
                    DoDeactivateBombFace(i);
            }
            break;
        default:
            break;
    }

    if (data == DONE)
    {
        _savedGrubbis = _encounter[TYPE_GRUBBIS];
        _savedThermaplugg = _encounter[TYPE_THERMAPLUGG];
    }
}

uint32 classic_instance_gnomeregan_InstanceScript::GetData(uint32 type) const
{
    switch (type)
    {
        case TYPE_GRUBBIS:
            return _encounter[TYPE_GRUBBIS];
        case TYPE_THERMAPLUGG:
            return _encounter[TYPE_THERMAPLUGG];
        default:
            return 0;
    }
}

ObjectGuid classic_instance_gnomeregan_InstanceScript::GetGuidData(uint32 type) const
{
    switch (type)
    {
        case GO_CAVE_IN_NORTH:
            return _caveInNorthGUID;
        case GO_CAVE_IN_SOUTH:
            return _caveInSouthGUID;
        case NPC_ALARM_A_BOMB_2600:
            return _alarmABomb2600GUID;
        default:
            return ObjectGuid::Empty;
    }
}

ClassicGnomereganBombFace* classic_instance_gnomeregan_InstanceScript::GetBombFaces()
{
    return _bombFaces;
}

void classic_instance_gnomeregan_InstanceScript::DoActivateBombFace(uint8 index)
{
    if (index >= MAX_GNOME_FACES)
        return;

    if (!_bombFaces[index].Activated)
    {
        DoUseDoorOrButton(_bombFaces[index].GnomeFaceGUID);
        _bombFaces[index].Activated = true;
        _bombFaces[index].BombTimer = 3000;
    }
}

void classic_instance_gnomeregan_InstanceScript::DoDeactivateBombFace(uint8 index)
{
    if (index >= MAX_GNOME_FACES)
        return;

    if (_bombFaces[index].Activated)
    {
        DoUseDoorOrButton(_bombFaces[index].GnomeFaceGUID);
        _bombFaces[index].Activated = false;
        _bombFaces[index].BombTimer = 0;
    }
}

class classic_instance_gnomeregan : public InstanceMapScript
{
public:
    classic_instance_gnomeregan() : InstanceMapScript(ClassicGnomereganScriptName, CLASSIC_GNOMEREGAN_MAP_ID) { }

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_gnomeregan_InstanceScript(map);
    }
};

void AddSC_classic_instance_gnomeregan()
{
    new classic_instance_gnomeregan();
}
