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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/razorfen_downs/instance_razorfen_downs.cpp (GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Random.h"
#include "TemporarySummon.h"
#include "classic_razorfen_downs.h"

namespace
{
uint32 const CLASSIC_RFD_MAX_ENCOUNTER = 1;
}

class classic_instance_razorfen_downs : public InstanceMapScript
{
public:
    classic_instance_razorfen_downs() : InstanceMapScript(ClassicRFDScriptName, CLASSIC_RFD_MAP_ID) { }

    struct classic_instance_razorfen_downs_InstanceScript : public InstanceScript
    {
        classic_instance_razorfen_downs_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _gongWaves(0), _savedTutenKash(*this, "TutenKash", NOT_STARTED)
        {
            SetHeaders(ClassicRFDDataHeader);
            for (uint32& encounter : _encounter)
                encounter = NOT_STARTED;
        }

        // VMaNGOS Load(): saved states, IN_PROGRESS -> NOT_STARTED
        void AfterDataLoad() override
        {
            _encounter[0] = _savedTutenKash;
            for (uint32& encounter : _encounter)
                if (encounter == IN_PROGRESS)
                    encounter = NOT_STARTED;
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_GONG:
                    _gongGUID = go->GetGUID();
                    if (_encounter[0] == DONE)
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);    // VMaNGOS GO_FLAG_NO_INTERACT (0x10)
                    break;
                case GO_IDOL_CUP_FIRE:
                    if (!_cupFire1GUID.IsEmpty())
                        _cupFire2GUID = go->GetGUID();
                    else
                        _cupFire1GUID = go->GetGUID();
                    break;
                default:
                    break;
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            if (type == DATA_GONG_WAVES)
            {
                _gongWaves = data;
                switch (_gongWaves)
                {
                    case 9:
                    case 14:
                        if (GameObject* go = instance->GetGameObject(_gongGUID))
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case 1:
                    case 10:
                    case 15:
                    {
                        GameObject* go = instance->GetGameObject(_gongGUID);

                        if (!go)
                            return;

                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);

                        uint32 creatureEntry = 0;
                        uint8 summonTimes = 0;

                        switch (_gongWaves)
                        {
                            case 1:
                                creatureEntry = CREATURE_TOMB_FIEND;
                                summonTimes = 7;
                                break;
                            case 10:
                                creatureEntry = CREATURE_TOMB_REAVER;
                                summonTimes = 3;
                                break;
                            case 15:
                                creatureEntry = CREATURE_TUTEN_KASH;
                                break;
                            default:
                                break;
                        }

                        if (Creature* creature = go->SummonCreature(creatureEntry, 2502.635f, 844.140f, 46.896f, 0.633f, TEMPSUMMON_DEAD_DESPAWN))
                        {
                            if (_gongWaves == 10 || _gongWaves == 1)
                            {
                                float x, y, z;
                                for (uint8 i = 0; i < summonTimes; ++i)
                                {
                                    if (i % 2 == 1)
                                    {
                                        x = 2502.635f;
                                        y = 844.140f;
                                        z = 46.896f;
                                    }
                                    else
                                    {
                                        x = 2546.33f;
                                        y = 887.455f;
                                        z = 47.69f;
                                    }
                                    if (Creature* summon = go->SummonCreature(creatureEntry, x + float(irand(-5, 5)), y + float(irand(-5, 5)), z, 0.633f, TEMPSUMMON_DEAD_DESPAWN))
                                    {
                                        summon->SetWalk(false);
                                        summon->GetMotionMaster()->MovePoint(0, 2533.479f + float(irand(-5, 5)), 870.020f + float(irand(-5, 5)), 47.678f);
                                    }
                                }
                            }
                            creature->SetWalk(false);
                            creature->GetMotionMaster()->MovePoint(0, 2533.479f + float(irand(-5, 5)), 870.020f + float(irand(-5, 5)), 47.678f);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            if (type == BOSS_TUTEN_KASH)
                _encounter[0] = data;

            if (type == EXTINGUISH_FIRES)
            {
                if (GameObject* cupFire1 = instance->GetGameObject(_cupFire1GUID))
                    cupFire1->SetLootState(GO_JUST_DEACTIVATED);
                if (GameObject* cupFire2 = instance->GetGameObject(_cupFire2GUID))
                    cupFire2->SetLootState(GO_JUST_DEACTIVATED);
            }

            // VMaNGOS saves whenever data == DONE (also for gong wave 3)
            if (data == DONE)
                _savedTutenKash = _encounter[0];
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_GONG_WAVES:
                    return _gongWaves;
                default:
                    break;
            }

            return 0;
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_GONG:
                    return _gongGUID;
                default:
                    break;
            }

            return ObjectGuid::Empty;
        }

    private:
        ObjectGuid _gongGUID;
        ObjectGuid _cupFire1GUID;
        ObjectGuid _cupFire2GUID;
        uint32 _encounter[CLASSIC_RFD_MAX_ENCOUNTER];
        uint8 _gongWaves;
        PersistentInstanceScriptValue<uint32> _savedTutenKash;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_razorfen_downs_InstanceScript(map);
    }
};

void AddSC_classic_instance_razorfen_downs()
{
    new classic_instance_razorfen_downs();
}
