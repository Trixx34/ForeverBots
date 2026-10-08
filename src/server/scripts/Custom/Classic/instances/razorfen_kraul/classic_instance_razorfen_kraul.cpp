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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/razorfen_kraul/instance_razorfen_kraul.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "classic_razorfen_kraul.h"

class classic_instance_razorfen_kraul : public InstanceMapScript
{
public:
    classic_instance_razorfen_kraul() : InstanceMapScript(ClassicRFKScriptName, CLASSIC_RFK_MAP_ID) { }

    struct classic_instance_razorfen_kraul_InstanceScript : public InstanceScript
    {
        classic_instance_razorfen_kraul_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _wardKeepersRemaining(0), _savedAgathelos(*this, "Agathelos", NOT_STARTED)
        {
            SetHeaders(ClassicRFKDataHeader);
            for (uint32& encounter : _encounter)
                encounter = NOT_STARTED;
        }

        // VMaNGOS Load(): saved states, IN_PROGRESS -> NOT_STARTED
        void AfterDataLoad() override
        {
            _encounter[0] = _savedAgathelos;
            for (uint32& encounter : _encounter)
                if (encounter == IN_PROGRESS)
                    encounter = NOT_STARTED;
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_AGATHELOS_WARD:
                    _agathelosWardGUID = go->GetGUID();
                    if (_encounter[0] == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                default:
                    break;
            }
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_WARD_KEEPER:
                    ++_wardKeepersRemaining;
                    break;
                case NPC_AGATHELOS:
                    _agathelosGUID = creature->GetGUID();
                    break;
                default:
                    break;
            }
        }

        // TODO(classic): TYPE_AGATHELOS is set (data DONE) by the VMaNGOS EventAI of the Ward Keepers on death
        // (creature_ai_scripts 462501, command 37) - that DB script must be ported for the ward to open.
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case TYPE_AGATHELOS:
                    if (_wardKeepersRemaining)
                        --_wardKeepersRemaining;
                    if (!_wardKeepersRemaining)
                    {
                        _encounter[0] = data;
                        DoUseDoorOrButton(_agathelosWardGUID);
                        if (Creature* agathelos = instance->GetCreature(_agathelosGUID))
                        {
                            agathelos->SetWalk(false);
                            agathelos->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
                            // TODO(classic): VMaNGOS MoveWaypoint() uses the creature_movement path of Agathelos' spawn;
                            // in TC the default waypoint movement needs creature_addon.PathId for that spawn.
                            agathelos->GetMotionMaster()->Initialize();
                        }
                    }
                    break;
                default:
                    break;
            }

            if (data == DONE)
                _savedAgathelos = _encounter[0];
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case TYPE_AGATHELOS:
                    return _encounter[0];
                default:
                    break;
            }
            return 0;
        }

    private:
        uint32 _encounter[RFK_MAX_ENCOUNTER];
        uint8 _wardKeepersRemaining;
        ObjectGuid _agathelosWardGUID;
        ObjectGuid _agathelosGUID;
        PersistentInstanceScriptValue<uint32> _savedAgathelos;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_razorfen_kraul_InstanceScript(map);
    }
};

void AddSC_classic_instance_razorfen_kraul()
{
    new classic_instance_razorfen_kraul();
}
