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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/instance_scholomance.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: instance_scholomance, go_brazier_herald, go_viewing_room_door

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "classic_dungeon_encounter_tracker.h"
#include "classic_scholomance.h"
#include <array>
#include <memory>

using namespace ClassicScholomance;

namespace
{
    // Save keys for the VMaNGOS m_auiEncounter[] array (VMaNGOS saves the whole array as "a b c ...").
    char const* const ClassicSchEncounterSaveKeys[INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER] =
    {
        "enc0", "enc1", "enc2", "enc3", "enc4", "enc5", "enc6", "enc7",
        "enc8", "enc9", "enc10", "enc11", "enc12", "enc13", "enc14", "enc15"
    };
}

namespace
{
// Classic 1.60: TC boss states for the DungeonEncounter.db2 encounters of Scholomance (map 289), driven by
// ClassicDungeonEncounterTracker (boss kill credit, encounter start / end, instance lock)
enum ClassicScholomanceEncounterBosses : uint32
{
    CSCHOLO_ENC_KIRTONOS = 0,
    CSCHOLO_ENC_JANDICE = 1,
    CSCHOLO_ENC_RATTLEGORE = 2,
    CSCHOLO_ENC_MARDUK = 3,
    CSCHOLO_ENC_VECTUS = 4,
    CSCHOLO_ENC_KORMOK = 5,
    CSCHOLO_ENC_RAS = 6,
    CSCHOLO_ENC_MALICIA = 7,
    CSCHOLO_ENC_KRASTINOV = 8,
    CSCHOLO_ENC_POLKELT = 9,
    CSCHOLO_ENC_RAVENIAN = 10,
    CSCHOLO_ENC_ALEXEI = 11,
    CSCHOLO_ENC_ILLUCIA = 12,
    CSCHOLO_ENC_GANDLING = 13,
    CSCHOLO_ENC_MAX
};

constexpr ClassicDungeonBossEntry ClassicScholomanceEncounterBossEntries[] =
{
    { CSCHOLO_ENC_KIRTONOS, 10506 }, // Kirtonos the Herald
    { CSCHOLO_ENC_JANDICE, 10503 }, // Jandice Barov
    { CSCHOLO_ENC_RATTLEGORE, 11622 }, // Rattlegore
    { CSCHOLO_ENC_MARDUK, 10433 }, // Marduk Blackpool
    { CSCHOLO_ENC_VECTUS, 10432 }, // Vectus
    { CSCHOLO_ENC_KORMOK, 16118 }, // Kormok
    { CSCHOLO_ENC_RAS, 10508 }, // Ras Frostwhisper
    { CSCHOLO_ENC_MALICIA, 10505 }, // Instructor Malicia
    { CSCHOLO_ENC_KRASTINOV, 11261 }, // Doctor Theolen Krastinov
    { CSCHOLO_ENC_POLKELT, 10901 }, // Lorekeeper Polkelt
    { CSCHOLO_ENC_RAVENIAN, 10507 }, // The Ravenian
    { CSCHOLO_ENC_ALEXEI, 10504 }, // Lord Alexei Barov
    { CSCHOLO_ENC_ILLUCIA, 10502 }, // Lady Illucia Barov
    { CSCHOLO_ENC_GANDLING, 1853 } // Darkmaster Gandling
};
}

class classic_instance_scholomance : public InstanceMapScript
{
public:
    classic_instance_scholomance() : InstanceMapScript(InstanceScriptName, MapId) { }

    struct classic_instance_scholomance_InstanceScript : public InstanceScript
    {
        classic_instance_scholomance_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(CSCHOLO_ENC_MAX);
            static constexpr DungeonEncounterData encounters[] =
            {
                { CSCHOLO_ENC_KIRTONOS, {{ 2805 }} },
                { CSCHOLO_ENC_JANDICE, {{ 2804 }} },
                { CSCHOLO_ENC_RATTLEGORE, {{ 2811 }} },
                { CSCHOLO_ENC_MARDUK, {{ 2809 }} },
                { CSCHOLO_ENC_VECTUS, {{ 2813 }} },
                { CSCHOLO_ENC_KORMOK, {{ 3055 }} },
                { CSCHOLO_ENC_RAS, {{ 2810 }} },
                { CSCHOLO_ENC_MALICIA, {{ 2803 }} },
                { CSCHOLO_ENC_KRASTINOV, {{ 2802 }} },
                { CSCHOLO_ENC_POLKELT, {{ 2808 }} },
                { CSCHOLO_ENC_RAVENIAN, {{ 2812 }} },
                { CSCHOLO_ENC_ALEXEI, {{ 2807 }} },
                { CSCHOLO_ENC_ILLUCIA, {{ 2806 }} },
                { CSCHOLO_ENC_GANDLING, {{ 2801 }} }
            };
            LoadDungeonEncounterData(encounters);

            _encounter.fill(NOT_STARTED);
            for (uint32 i = 0; i < INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER; ++i)
                _saved[i] = std::make_unique<PersistentInstanceScriptValue<uint32>>(*this, ClassicSchEncounterSaveKeys[i], uint32(NOT_STARTED));

            _summonGandlingPending = false;
        }

        ClassicDungeonEncounterTracker _encounterTracker{ *this, ClassicScholomanceEncounterBossEntries };

        void OnCreatureRemove(Creature* creature) override
        {
            InstanceScript::OnCreatureRemove(creature);
            _encounterTracker.OnCreatureRemove(creature);
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);
            _encounterTracker.OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_VECTUS:
                    _vectusGUID = creature->GetGUID();
                    break;
                case NPC_MARDUKE:
                    _mardukeGUID = creature->GetGUID();
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
                case GO_GATE_KIRTONOS:
                    _gateKirtonosGUID = go->GetGUID();
                    break;
                case GO_GATE_GANDLING:
                    _gateGandlingGUID = go->GetGUID();
                    break;
                case GO_GATE_MALICIA:
                    _gateMiliciaGUID = go->GetGUID();
                    break;
                case GO_GATE_THEOLEN:
                    _gateTheolenGUID = go->GetGUID();
                    break;
                case GO_GATE_POLKELT:
                    _gatePolkeltGUID = go->GetGUID();
                    break;
                case GO_GATE_RAVENIAN:
                    _gateRavenianGUID = go->GetGUID();
                    break;
                case GO_GATE_BAROV:
                    _gateBarovGUID = go->GetGUID();
                    break;
                case GO_GATE_ILLUCIA:
                    _gateIlluciaGUID = go->GetGUID();
                    break;
                case GO_BRAZIER_KIRTONOS:
                    _brazierKirtonosGUID = go->GetGUID();
                    break;
                case GO_VIEWING_ROOM_DOOR:
                    if (GetData(TYPE_VIEWING_ROOM_DOOR) == DONE)
                        go->UseDoorOrButton();
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            if (type == TYPE_GANDLING)
            {
                // VMaNGOS: GetData() itself switches Gandling to SPECIAL once all six bosses are dead
                if (_encounter[TYPE_GANDLING] == NOT_STARTED && _encounter[TYPE_ALEXEIBAROV] == DONE && _encounter[TYPE_THEOLEN] == DONE
                    && _encounter[TYPE_RAVENIAN] == DONE && _encounter[TYPE_POLKELT] == DONE && _encounter[TYPE_MALICIA] == DONE
                    && _encounter[TYPE_ILLUCIABAROV] == DONE)
                    _encounter[TYPE_GANDLING] = SPECIAL;
                return _encounter[TYPE_GANDLING];
            }

            if (type < INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER)
                return _encounter[type];

            return 0;
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_VECTUS:
                    return _vectusGUID;
                case DATA_MARDUKE:
                    return _mardukeGUID;
                default:
                    break;
            }

            return ObjectGuid::Empty;
        }

        void OnUnitDeath(Unit* unit) override
        {
            _encounterTracker.OnUnitDeath(unit);

            switch (unit->GetEntry())
            {
                case NPC_KIRTONOS:
                    OpenDoorIfClosed(_gateKirtonosGUID);
                    break;
                default:
                    break;
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case TYPE_GANDLING:
                    _encounter[TYPE_GANDLING] = data;
                    if (data == FAIL || data == DONE)
                    {
                        OpenDoorIfClosed(_gateMiliciaGUID);
                        OpenDoorIfClosed(_gateTheolenGUID);
                        OpenDoorIfClosed(_gatePolkeltGUID);
                        OpenDoorIfClosed(_gateRavenianGUID);
                        OpenDoorIfClosed(_gateBarovGUID);
                        OpenDoorIfClosed(_gateIlluciaGUID);
                    }
                    break;
                case TYPE_KIRTONOS:
                    _encounter[TYPE_KIRTONOS] = data;
                    if (data == IN_PROGRESS)
                    {
                        if (GameObject* go = instance->GetGameObject(_gateKirtonosGUID))
                            if (go->GetGoState() == GO_STATE_ACTIVE)
                                DoUseDoorOrButton(_gateKirtonosGUID);
                    }
                    else if (data == FAIL)
                    {
                        OpenDoorIfClosed(_gateKirtonosGUID);
                        if (GameObject* go = instance->GetGameObject(_brazierKirtonosGUID))
                            go->ResetDoorOrButton();
                    }
                    break;
                case TYPE_ALEXEIBAROV:
                case TYPE_THEOLEN:
                case TYPE_RAVENIAN:
                case TYPE_POLKELT:
                case TYPE_MALICIA:
                case TYPE_ILLUCIABAROV:
                case TYPE_VIEWING_ROOM_DOOR:
                case TYPE_DARKREAVER:
                    _encounter[type] = data;
                    break;
                default:
                    break;
            }

            if (data == DONE)
                SaveEncounterData();

            SummonGandlingIfPossible();
        }

        void AfterDataLoad() override
        {
            for (uint32 i = 0; i < INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER; ++i)
            {
                _encounter[i] = uint32(*_saved[i]);
                if (_encounter[i] == IN_PROGRESS)
                    _encounter[i] = NOT_STARTED;
            }

            // VMaNGOS summons Gandling directly from Load(); done on the first map update here (grids are not loaded yet)
            _summonGandlingPending = true;
        }

        void Update(uint32 diff) override
        {
            _encounterTracker.Update(diff);

            if (_summonGandlingPending)
            {
                _summonGandlingPending = false;
                SummonGandlingIfPossible();
            }
        }

    private:
        void OpenDoorIfClosed(ObjectGuid guid)
        {
            if (GameObject* go = instance->GetGameObject(guid))
                if (go->GetGoState() != GO_STATE_ACTIVE)
                    DoUseDoorOrButton(guid);
        }

        void SaveEncounterData()
        {
            for (uint32 i = 0; i < INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER; ++i)
                if (uint32(*_saved[i]) != _encounter[i])
                    *_saved[i] = _encounter[i];
        }

        void SummonGandlingIfPossible()
        {
            if (GetData(TYPE_GANDLING) == SPECIAL)
            {
                if (TempSummon* gandling = instance->SummonCreature(NPC_GANDLING, Position(180.771f, -5.4286f, 75.5702f, 1.29154f)))
                    gandling->SetTempSummonType(TEMPSUMMON_DEAD_DESPAWN);
                SetData(TYPE_GANDLING, IN_PROGRESS);
            }
        }

        mutable std::array<uint32, INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER> _encounter;
        std::array<std::unique_ptr<PersistentInstanceScriptValue<uint32>>, INSTANCE_SCHOLOMANCE_MAX_ENCOUNTER> _saved;
        bool _summonGandlingPending;

        ObjectGuid _vectusGUID;
        ObjectGuid _mardukeGUID;

        ObjectGuid _gateKirtonosGUID;
        ObjectGuid _gateGandlingGUID;
        ObjectGuid _gateMiliciaGUID;
        ObjectGuid _gateTheolenGUID;
        ObjectGuid _gatePolkeltGUID;
        ObjectGuid _gateRavenianGUID;
        ObjectGuid _gateBarovGUID;
        ObjectGuid _gateIlluciaGUID;
        ObjectGuid _brazierKirtonosGUID;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_scholomance_InstanceScript(map);
    }
};

/*######
## go_brazier_herald
######*/

// VMaNGOS pGOOpen hook -> TC GameObjectAI::OnGossipHello (called from GameObject::Use); return values kept.
struct classic_go_brazier_herald : public GameObjectAI
{
    classic_go_brazier_herald(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            switch (instance->GetData(TYPE_KIRTONOS))
            {
                case IN_PROGRESS:
                case DONE:
                    return false;
                default:
                    break;
            }

            instance->SetData(TYPE_KIRTONOS, IN_PROGRESS);
            me->PlayDirectSound(SOUND_SCREECH);

            player->SummonCreature(NPC_KIRTONOS, 315.028f, 70.53845f, 102.1496f, 0.3859715f, TEMPSUMMON_DEAD_DESPAWN, 900000ms);
        }

        return true;
    }
};

/*######
## go_viewing_room_door
######*/

struct classic_go_viewing_room_door : public GameObjectAI
{
    classic_go_viewing_room_door(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        // Save door state to database
        if (InstanceScript* instance = player->GetInstanceScript())
            instance->SetData(TYPE_VIEWING_ROOM_DOOR, DONE);
        return false;
    }
};

void AddSC_classic_instance_scholomance()
{
    new classic_instance_scholomance();
    RegisterGameObjectAI(classic_go_brazier_herald);
    RegisterGameObjectAI(classic_go_viewing_room_door);
}
