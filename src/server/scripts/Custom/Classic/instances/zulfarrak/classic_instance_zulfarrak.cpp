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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/tanaris/zulfarrak/instance_zulfarrak.cpp (TrinityCore/ScriptDev2 lineage, GPL-2)
// Ported: instance_zulfarrak (map 209) incl. the pyramid (Sergeant Bly & crew) wave event

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"
#include "classic_zulfarrak.h"
#include <list>

namespace
{
enum ClassicZFInstanceMisc
{
    NPC_ZF_GAHZRILLA = 7273
};

struct ClassicZFPyramidSpawn
{
    uint32 Wave;
    uint32 Entry;
    float X;
    float Y;
};

/* list of wave spawns: wave ID, creature id, x, y - no z coordinate b/c they're all the same */
ClassicZFPyramidSpawn const ClassicZFPyramidSpawns[] =
{
    {1, 7789, 1894.64f, 1206.29f},
    {1, 7787, 1890.08f, 1218.68f},
    {1, 8876, 1883.76f, 1222.3f},
    {1, 7789, 1874.18f, 1221.24f},
    {1, 7787, 1892.28f, 1225.49f},
    {1, 7788, 1889.94f, 1212.21f},
    {1, 7787, 1879.02f, 1223.06f},
    {1, 7789, 1874.45f, 1204.44f},
    {1, 8876, 1898.23f, 1217.97f},
    {1, 7787, 1882.07f, 1225.7f},
    {1, 8877, 1896.46f, 1205.62f},
    {1, 7787, 1886.97f, 1225.86f},
    {1, 7787, 1894.72f, 1221.91f},
    {1, 7787, 1883.5f, 1218.25f},
    {1, 7787, 1886.93f, 1221.4f},
    {1, 8876, 1889.82f, 1222.51f},
    {1, 7788, 1893.07f, 1215.26f},
    {1, 7788, 1878.57f, 1214.16f},
    {1, 7788, 1883.74f, 1212.35f},
    {1, 8877, 1877.0f, 1207.27f},
    {1, 8877, 1873.63f, 1204.65f},
    {1, 8876, 1877.4f, 1216.41f},
    {1, 8877, 1899.63f, 1202.52f},
    {2, 7789, 1902.83f, 1223.41f},
    {2, 8876, 1889.82f, 1222.51f},
    {2, 7787, 1883.5f, 1218.25f},
    {2, 7788, 1883.74f, 1212.35f},
    {2, 8877, 1877.0f, 1207.27f},
    {2, 7787, 1890.08f, 1218.68f},
    {2, 7789, 1894.64f, 1206.29f},
    {2, 8876, 1877.4f, 1216.41f},
    {2, 7787, 1892.28f, 1225.49f},
    {2, 7788, 1893.07f, 1215.26f},
    {2, 8877, 1896.46f, 1205.62f},
    {2, 7789, 1874.45f, 1204.44f},
    {2, 7789, 1874.18f, 1221.24f},
    {2, 7787, 1879.02f, 1223.06f},
    {2, 8876, 1898.23f, 1217.97f},
    {2, 7787, 1882.07f, 1225.7f},
    {2, 8877, 1873.63f, 1204.65f},
    {2, 7787, 1886.97f, 1225.86f},
    {2, 7788, 1878.57f, 1214.16f},
    {2, 7787, 1894.72f, 1221.91f},
    {2, 7787, 1886.93f, 1221.4f},
    {2, 8876, 1883.76f, 1222.3f},
    {2, 7788, 1889.94f, 1212.21f},
    {2, 8877, 1899.63f, 1202.52f},
    {3, 7788, 1878.57f, 1214.16f},
    {3, 7787, 1894.72f, 1221.91f},
    {3, 7787, 1886.93f, 1221.4f},
    {3, 8876, 1883.76f, 1222.3f},
    {3, 7788, 1889.94f, 1212.21f},
    {3, 7275, 1889.23f, 1207.72f},
    {3, 7796, 1879.77f, 1207.96f}
};

float const ClassicZFPyramidSpawnZ = 8.87f;
}

class classic_instance_zulfarrak : public InstanceMapScript
{
public:
    classic_instance_zulfarrak() : InstanceMapScript(ClassicZFScriptName, 209) { }

    struct classic_instance_zulfarrak_InstanceScript : public InstanceScript
    {
        classic_instance_zulfarrak_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _savedEndDoor(*this, "EndDoor", uint32(NOT_STARTED)),
            _gahzRillaEncounter(NOT_STARTED), _endDoorEncounter(NOT_STARTED), _zumrahEncounter(NOT_STARTED), _antusulEncounter(NOT_STARTED),
            _pyramidPhase(PYRAMID_ZF_NOT_STARTED), _majorWaveTimer(0), _minorWaveTimer(0), _addGroupSize(0)
        {
            SetHeaders("ZF");
        }

        // VMaNGOS Load(): only the end door state is saved
        void AfterDataLoad() override
        {
            _endDoorEncounter = _savedEndDoor;
            if (_endDoorEncounter != DONE)
                _endDoorEncounter = NOT_STARTED;
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case ENTRY_ZF_ZUMRAH:
                    _zumrahGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_BLY:
                    _blyGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_RAVEN:
                    _ravenGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_ORO:
                    _oroGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_WEEGLI:
                    _weegliGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_MURTA:
                    _murtaGUID = creature->GetGUID();
                    break;
                case ENTRY_ZF_UKORZ:
                    _ukorzGUID = creature->GetGUID();
                    break;
                case NPC_ZF_GAHZRILLA:
                    if (_gahzRillaEncounter >= IN_PROGRESS)
                        creature->DespawnOrUnsummon(); // VMaNGOS DisappearAndDie() (Gahz'rilla is a summon)
                    else
                        _gahzRillaEncounter = IN_PROGRESS;
                    break;
                default:
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_ZF_END_DOOR:
                    _endDoorGUID = go->GetGUID();
                    if (_endDoorEncounter == DONE)
                        go->UseDoorOrButton(0, true);
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case EVENT_ZF_PYRAMID:
                    return _pyramidPhase;
                case EVENT_ZF_ZUMRAH:
                    return _zumrahEncounter;
                case EVENT_ZF_ANTUSUL:
                    return _antusulEncounter;
                default:
                    break;
            }
            return 0;
        }

        // VMaNGOS GetData64(entry)
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case ENTRY_ZF_ZUMRAH:
                    return _zumrahGUID;
                case ENTRY_ZF_BLY:
                    return _blyGUID;
                case ENTRY_ZF_RAVEN:
                    return _ravenGUID;
                case ENTRY_ZF_ORO:
                    return _oroGUID;
                case ENTRY_ZF_WEEGLI:
                    return _weegliGUID;
                case ENTRY_ZF_MURTA:
                    return _murtaGUID;
                case ENTRY_ZF_UKORZ:
                    return _ukorzGUID;
                case GO_ZF_END_DOOR:
                    return _endDoorGUID;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case EVENT_ZF_PYRAMID:
                    _pyramidPhase = data;
                    break;
                case EVENT_ZF_END_DOOR:
                    _endDoorEncounter = data;
                    break;
                case EVENT_ZF_ZUMRAH:
                    _zumrahEncounter = data;
                    break;
                case EVENT_ZF_ANTUSUL:
                    _antusulEncounter = data;
                    break;
                default:
                    break;
            }

            if (type == EVENT_ZF_END_DOOR && data == DONE)
                _savedEndDoor = _endDoorEncounter;
        }

        void Update(uint32 diff) override
        {
            switch (_pyramidPhase)
            {
                case PYRAMID_ZF_NOT_STARTED:
                case PYRAMID_ZF_KILLED_ALL_TROLLS:
                    break;
                case PYRAMID_ZF_ARRIVED_AT_STAIR:
                    SpawnPyramidWave(1);
                    SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_WAVE_1);
                    _majorWaveTimer = 120000;
                    _minorWaveTimer = 0;
                    _addGroupSize = 2;
                    break;
                case PYRAMID_ZF_WAVE_1:
                    if (IsWaveAllDead())
                    {
                        SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_PRE_WAVE_2);
                        _majorWaveTimer = 10000; // give players a few seconds before wave 2 starts to rebuff
                    }
                    else if (_minorWaveTimer < diff)
                    {
                        SendAddsUpStairs(_addGroupSize++);
                        _minorWaveTimer = 10000;
                    }
                    else
                        _minorWaveTimer -= diff;
                    break;
                case PYRAMID_ZF_PRE_WAVE_2:
                    if (_majorWaveTimer < diff)
                    {
                        // beginning 2nd wave!
                        SpawnPyramidWave(2);
                        SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_WAVE_2);
                        _minorWaveTimer = 0;
                        _addGroupSize = 2;
                    }
                    else
                        _majorWaveTimer -= diff;
                    break;
                case PYRAMID_ZF_WAVE_2:
                    if (IsWaveAllDead())
                    {
                        SpawnPyramidWave(3);
                        SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_PRE_WAVE_3);
                        _majorWaveTimer = 5000; // give NPCs time to return to their home spots
                    }
                    else if (_minorWaveTimer < diff)
                    {
                        SendAddsUpStairs(_addGroupSize++);
                        _minorWaveTimer = 10000;
                    }
                    else
                        _minorWaveTimer -= diff;
                    break;
                case PYRAMID_ZF_PRE_WAVE_3:
                    if (_majorWaveTimer < diff)
                    {
                        // move NPCs to bottom of stair
                        MoveNPCIfAlive(ENTRY_ZF_BLY, 1887.92f, 1228.179f, 9.98f);
                        MoveNPCIfAlive(ENTRY_ZF_MURTA, 1891.57f, 1228.68f, 9.69f);
                        MoveNPCIfAlive(ENTRY_ZF_ORO, 1897.23f, 1228.34f, 9.43f);
                        MoveNPCIfAlive(ENTRY_ZF_RAVEN, 1883.68f, 1227.95f, 9.543f);
                        MoveNPCIfAlive(ENTRY_ZF_WEEGLI, 1878.02f, 1227.65f, 9.485f);
                        SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_WAVE_3);
                    }
                    else
                        _majorWaveTimer -= diff;
                    break;
                case PYRAMID_ZF_WAVE_3:
                    if (IsWaveAllDead()) // move NPCS to their final positions
                    {
                        SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_KILLED_ALL_TROLLS);
                        MoveNPCIfAlive(ENTRY_ZF_BLY, 1883.82f, 1200.83f, 8.87f);
                        MoveNPCIfAlive(ENTRY_ZF_MURTA, 1891.83f, 1201.45f, 8.87f);
                        MoveNPCIfAlive(ENTRY_ZF_ORO, 1894.50f, 1204.40f, 8.87f);
                        MoveNPCIfAlive(ENTRY_ZF_RAVEN, 1874.11f, 1206.17f, 8.87f);
                        MoveNPCIfAlive(ENTRY_ZF_WEEGLI, 1877.52f, 1199.63f, 8.87f);
                    }
                    break;
                default:
                    break;
            }
        }

        // (VMaNGOS passes an orientation too but does not use it: the home orientation stays the current one)
        void MoveNPCIfAlive(uint32 entry, float x, float y, float z)
        {
            if (Creature* npc = instance->GetCreature(GetGuidData(entry)))
            {
                if (npc->IsAlive())
                {
                    npc->GetMotionMaster()->MovePoint(1, x, y, z, true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceWalk);
                    // (VMaNGOS SetCombatStartPosition(x, y, z): no TC equivalent, the home position covers it)
                    npc->SetHomePosition(x, y, z, npc->GetOrientation());
                }
            }
        }

        void SpawnPyramidWave(uint32 wave)
        {
            for (ClassicZFPyramidSpawn const& spawn : ClassicZFPyramidSpawns)
            {
                if (spawn.Wave != wave)
                    continue;

                if (TempSummon* summon = instance->SummonCreature(spawn.Entry, Position(spawn.X, spawn.Y, ClassicZFPyramidSpawnZ, 0.0f)))
                    _addsAtBase.push_back(summon->GetGUID());
            }
        }

        bool IsWaveAllDead()
        {
            for (ObjectGuid const& guid : _addsAtBase)
                if (Creature* add = instance->GetCreature(guid))
                    if (add->IsAlive())
                        return false;

            for (ObjectGuid const& guid : _movedAdds)
                if (Creature* add = instance->GetCreature(guid))
                    if (add->IsAlive())
                        return false;

            return true;
        }

        void SendAddsUpStairs(uint32 count)
        {
            // pop a add from list, send him up the stairs...
            for (uint32 addCount = 0; addCount < count && !_addsAtBase.empty(); ++addCount)
            {
                if (Creature* add = instance->GetCreature(_addsAtBase.front()))
                {
                    if (add->IsAlive())
                    {
                        add->GetMotionMaster()->MovePoint(0, float(1880 + urand(0, 10)), 1274.0f, 42.0f, true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceRun);
                        add->SetWalk(false);
                    }
                    _movedAdds.push_back(add->GetGUID());
                }
                _addsAtBase.pop_front();
            }
        }

    private:
        PersistentInstanceScriptValue<uint32> _savedEndDoor;

        uint32 _gahzRillaEncounter;
        uint32 _endDoorEncounter;
        uint32 _zumrahEncounter;
        uint32 _antusulEncounter;

        ObjectGuid _ukorzGUID;
        ObjectGuid _zumrahGUID;
        ObjectGuid _blyGUID;
        ObjectGuid _weegliGUID;
        ObjectGuid _oroGUID;
        ObjectGuid _ravenGUID;
        ObjectGuid _murtaGUID;
        ObjectGuid _endDoorGUID;
        uint32 _pyramidPhase;
        uint32 _majorWaveTimer;
        uint32 _minorWaveTimer;
        uint32 _addGroupSize;

        std::list<ObjectGuid> _addsAtBase;
        std::list<ObjectGuid> _movedAdds;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_zulfarrak_InstanceScript(map);
    }
};

void AddSC_classic_instance_zulfarrak()
{
    new classic_instance_zulfarrak();
}
