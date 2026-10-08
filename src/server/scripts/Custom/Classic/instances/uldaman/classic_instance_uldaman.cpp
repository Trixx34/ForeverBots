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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/uldaman/instance_uldaman.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "QuaternionData.h"
#include "classic_uldaman.h"
#include <algorithm>
#include <vector>

class classic_instance_uldaman : public InstanceMapScript
{
public:
    classic_instance_uldaman() : InstanceMapScript(ClassicUldamanScriptName, CLASSIC_ULDAMAN_MAP_ID) { }

    struct classic_instance_uldaman_InstanceScript : public InstanceScript
    {
        classic_instance_uldaman_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _ironayaSealDoorTimer(27000), // animation time
            _keystoneCheck(false),
            _savedIronayaDoor(*this, "IronayaDoor", NOT_STARTED),
            _savedStoneKeepers(*this, "StoneKeepers", NOT_STARTED),
            _savedArchaedas(*this, "Archaedas", NOT_STARTED)
        {
            SetHeaders(ClassicUldamanDataHeader);
            for (uint32& encounter : _encounter)
                encounter = NOT_STARTED;
            _vaultWarder.reserve(2);
            _vaultWarderFurniture.reserve(2);
            _earthenGuardian.reserve(6);
        }

        // VMaNGOS Load(): everything that is not DONE -> NOT_STARTED
        void AfterDataLoad() override
        {
            _encounter[0] = _savedIronayaDoor;
            _encounter[1] = _savedStoneKeepers;
            _encounter[2] = _savedArchaedas;
            for (uint32& encounter : _encounter)
                if (encounter != DONE)
                    encounter = NOT_STARTED;
        }

        bool IsEncounterInProgress() const override
        {
            for (uint32 encounter : _encounter)
                if (encounter == IN_PROGRESS)
                    return true;

            return false;
        }

        static void AddUniqueGuid(std::vector<ObjectGuid>& container, ObjectGuid const& guid)
        {
            if (std::find(container.begin(), container.end(), guid) == container.end())
                container.push_back(guid);
        }

        // VMaNGOS HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC)
        static bool HasImmuneFlags(Creature const* creature)
        {
            return creature->IsImmuneToPC() || creature->IsImmuneToNPC();
        }

        // VMaNGOS RespawnMinion() on a freshly created creature: it is alive at its spawn point, only the flags are set.
        // (VMaNGOS kills/respawns it in place, which is not safe inside OnCreatureCreate in TC.)
        static void FreezeFreshMinion(Creature* creature)
        {
            if (creature->IsAlive() && !HasImmuneFlags(creature))
            {
                creature->SetImmuneToPC(true);
                creature->SetImmuneToNPC(true);
            }
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_STONE_KEEPER:
                    AddUniqueGuid(_stoneKeeper, creature->GetGUID());
                    if (_encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] != DONE)
                        FreezeFreshMinion(creature);
                    break;
                case NPC_EARTHEN_CUSTODIAN:
                case NPC_EARTHEN_HALLSHAPER:
                    AddUniqueGuid(_archaedasWallMinions, creature->GetGUID());
                    if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] != DONE)
                        FreezeFreshMinion(creature);
                    break;
                case NPC_EARTHEN_GUARDIAN:
                    AddUniqueGuid(_earthenGuardian, creature->GetGUID());
                    if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] != DONE)
                        FreezeFreshMinion(creature);
                    break;
                case NPC_IRONAYA:
                    _ironayaGUID = creature->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR] != DONE)
                    {
                        // VMaNGOS SetFrozenState() here; the Stoned aura is cast on the next instance update
                        creature->SetImmuneToPC(true);
                        creature->SetImmuneToNPC(true);
                        creature->SetUninteractible(true);
                        _pendingFreeze.push_back(creature->GetGUID());
                    }
                    break;
                case NPC_VAULT_WARDER:
                    // Only take the ones inside Archaedas room
                    if (creature->IsWithinDist2d(104.0f, 272.0f, 35.0f))
                    {
                        AddUniqueGuid(_vaultWarder, creature->GetGUID());
                        if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] != DONE)
                            FreezeFreshMinion(creature);
                    }
                    else
                        AddUniqueGuid(_vaultWarderFurniture, creature->GetGUID());
                    break;
                case NPC_ARCHAEDAS:
                    _archaedasGUID = creature->GetGUID();
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
                case GO_ALTAR_ARCHAEDAS:
                    _altarOfArchaedasGUID = go->GetGUID();
                    break;
                case GO_ALTAR_KEEPERS:
                    _altarOfTheKeeperGUID = go->GetGUID();
                    break;
                case GO_ALTAR_OF_THE_KEEPER_TEMPLE_DOOR:
                    _altarOfTheKeeperTempleDoorGUID = go->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] == DONE)
                        go->UseDoorOrButton(0, false);
                    break;
                case GO_ARCHAEDAS_TEMPLE_DOOR:
                    _archaedasTempleDoorGUID = go->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] == DONE)
                        go->UseDoorOrButton(0, false);
                    break;
                case GO_ANCIENT_VAULT_DOOR:
                    go->SetGoState(GO_STATE_READY);
                    go->ReplaceAllFlags(GameObjectFlags(33));
                    _ancientVaultDoorGUID = go->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] == DONE)
                        go->UseDoorOrButton(0, false);
                    break;
                case GO_IRONAYA_SEAL_DOOR:
                    _ironayaSealDoorGUID = go->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR] == DONE)
                        go->UseDoorOrButton(0, false);
                    break;
                case GO_KEYSTONE:
                    _keystoneGUID = go->GetGUID();
                    if (_encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR] == DONE)
                        go->ReplaceAllFlags(GO_FLAG_INTERACT_COND);
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS DB event_scripts 2228 / 2268 (altar ritual spells 11568 / 10340 -> SET_INST_DATA IN_PROGRESS)
        void ProcessEvent(WorldObject* /*obj*/, uint32 eventId, WorldObject* /*invoker*/) override
        {
            switch (eventId)
            {
                case EVENT_ULDAMAN_ALTAR_OF_THE_KEEPERS:
                    SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, IN_PROGRESS);
                    break;
                case EVENT_ULDAMAN_ALTAR_OF_ARCHAEDAS:
                    SetData(ULDAMAN_ENCOUNTER_ARCHAEDAS, IN_PROGRESS);
                    break;
                default:
                    break;
            }
        }

        void SetFrozenState(Creature* creature)
        {
            creature->SetImmuneToPC(true);
            creature->SetImmuneToNPC(true);
            creature->SetUninteractible(true);
            creature->RemoveAllAuras();
            if (!creature->HasAura(SPELL_STONED))
                creature->CastSpell(creature, SPELL_STONED, false);
        }

        void SetUnFrozenState(Creature* creature)
        {
            creature->SetImmuneToPC(false);
            creature->SetImmuneToNPC(false);
            creature->SetUninteractible(false);
            if (creature->HasAura(SPELL_STONED))
                creature->RemoveAurasDueToSpell(SPELL_STONED);
        }

        // VMaNGOS: SetDeathState(JUST_DIED); RemoveCorpse(); Respawn(); -> TC Respawn(true) (kills first when alive).
        // TODO(classic): Respawn in place (same GUID) needs the spawn in a compatibility-mode spawn group; in dynamic
        // spawn mode the creature is re-created and only gets the immune flags from OnCreatureCreate.
        Creature* RespawnInPlace(ObjectGuid const& guid)
        {
            Creature* target = instance->GetCreature(guid);
            if (!target)
                return nullptr;

            target->Respawn(true);

            target = instance->GetCreature(guid);
            if (target && target->IsAlive())
            {
                target->RestoreFaction();                   // VMaNGOS TEMPFACTION_RESTORE_RESPAWN
                return target;
            }
            return nullptr;
        }

        void RespawnMinion(ObjectGuid const& guid)
        {
            Creature* target = instance->GetCreature(guid);
            if (!target || (target->IsAlive() && HasImmuneFlags(target)))
                return;

            if (Creature* respawned = RespawnInPlace(guid))
            {
                respawned->SetImmuneToPC(true);
                respawned->SetImmuneToNPC(true);
            }
        }

        void DespawnMinion(ObjectGuid const& guid)
        {
            Creature* target = instance->GetCreature(guid);
            if (!target || target->isDead())
                return;
            target->DespawnOrUnsummon();                    // VMaNGOS SetDeathState(JUST_DIED); RemoveCorpse();
        }

        void RespawnFurniture()
        {
            for (ObjectGuid const& guid : _vaultWarderFurniture)
            {
                Creature* target = instance->GetCreature(guid);
                if (target && target->getDeathState() == DEAD)  // VMaNGOS IsDespawned()
                    target->Respawn();
            }
        }

        void DoOpenDoor(ObjectGuid const& guid)
        {
            if (GameObject* go = instance->GetGameObject(guid))
                if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR || go->GetGoType() == GAMEOBJECT_TYPE_BUTTON)
                    if (go->getLootState() == GO_READY)
                        go->UseDoorOrButton(0, false);
        }

        void DoResetDoor(ObjectGuid const& guid)
        {
            if (GameObject* go = instance->GetGameObject(guid))
                if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR || go->GetGoType() == GAMEOBJECT_TYPE_BUTTON)
                    go->ResetDoorOrButton();
        }

        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            switch (type)
            {
                case SET_DATA64_IRONAYA_WAKER:
                    _whoWokeIronayaGUID = data;
                    break;
                case SET_DATA64_UNFREEZE:
                    if (Creature* target = instance->GetCreature(data))
                        SetUnFrozenState(target);
                    break;
                case SET_DATA64_FREEZE:
                    if (Creature* target = instance->GetCreature(data))
                        SetFrozenState(target);
                    break;
                default:
                    break;
            }
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            auto at = [](std::vector<ObjectGuid> const& container, size_t index)
            {
                return index < container.size() ? container[index] : ObjectGuid::Empty;
            };

            switch (type)
            {
                case DATA64_IRONAYA_WAKER:      return _whoWokeIronayaGUID;
                case DATA64_VAULT_WARDER_1:     return at(_vaultWarder, 0);
                case DATA64_VAULT_WARDER_2:     return at(_vaultWarder, 1);
                case DATA64_EARTHEN_GUARDIAN_1: return at(_earthenGuardian, 0);
                case DATA64_EARTHEN_GUARDIAN_2: return at(_earthenGuardian, 1);
                case DATA64_EARTHEN_GUARDIAN_3: return at(_earthenGuardian, 2);
                case DATA64_EARTHEN_GUARDIAN_4: return at(_earthenGuardian, 3);
                case DATA64_EARTHEN_GUARDIAN_5: return at(_earthenGuardian, 4);
                case DATA64_EARTHEN_GUARDIAN_6: return at(_earthenGuardian, 5);
                case DATA64_ARCHAEDAS:          return _archaedasGUID;
                case DATA64_VAULT_FURNITURE_1:  return at(_vaultWarderFurniture, 0);
                case DATA64_VAULT_FURNITURE_2:  return at(_vaultWarderFurniture, 1);
                default:
                    break;
            }

            return ObjectGuid::Empty;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case ULDAMAN_ENCOUNTER_IRONAYA_DOOR:
                    if (data == DONE)
                    {
                        _keystoneCheck = true;
                        _encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR] = data;
                    }
                    break;
                case DATA_KEEPERS_ALTAR:
                    if (data == IN_PROGRESS)
                    {
                        if (GameObject* go = instance->GetGameObject(_altarOfTheKeeperGUID))
                            go->SetGoState(GO_STATE_ACTIVE);
                    }
                    else if (data == NOT_STARTED)
                    {
                        if (GameObject* go = instance->GetGameObject(_altarOfTheKeeperGUID))
                            go->SetGoState(GO_STATE_READY);
                    }
                    break;
                case ULDAMAN_ENCOUNTER_STONE_KEEPERS:
                    switch (data)
                    {
                        case DONE:
                            _encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] = data;
                            DoOpenDoor(_altarOfTheKeeperTempleDoorGUID);
                            break;
                        case IN_PROGRESS:
                        {
                            _encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] = data;
                            /** Check the list of Stone Keeper created at instance creation */
                            Creature* target = nullptr;
                            bool encounterDone = true;
                            for (ObjectGuid const& guid : _stoneKeeper)
                            {
                                Creature* current = instance->GetCreature(guid);

                                /* Do nothing if one is already alive and awaken */
                                if (current && current->IsAlive() && !HasImmuneFlags(current))
                                {
                                    target = nullptr;
                                    encounterDone = false;
                                    break;
                                }
                                /* Save a creature that can be awaken for later */
                                if (!target && current && current->IsAlive() && HasImmuneFlags(current))
                                    target = current;
                            }
                            if (target)
                            {
                                encounterDone = false;
                                /** Creature become alive */
                                SetUnFrozenState(target);
                                target->SetFaction(470);    // VMaNGOS SetFactionTemporary(470, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_RESTORE_COMBAT_STOP)
                                if (Unit* victim = target->SelectNearestTarget(80.0f))
                                {
                                    if (target->AI())
                                        target->AI()->AttackStart(victim);
                                }
                                else
                                    SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, FAIL);
                            }
                            if (encounterDone)
                                SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, DONE); //Open the doors
                            break;
                        }
                        case FAIL:
                            _encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] = data;
                            for (ObjectGuid const& guid : _stoneKeeper)
                            {
                                Creature* target = instance->GetCreature(guid);
                                if (!target)
                                    continue;
                                if (target->isDead() || !HasImmuneFlags(target))
                                    if (Creature* respawned = RespawnInPlace(guid))
                                        SetFrozenState(respawned);
                            }
                            break;
                        default:
                            _encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS] = data;
                            break;
                    }
                    break;
                case DATA_ANCIENT_DOOR:
                    if (data == DONE) //archeadas defeat
                    {
                        DoOpenDoor(_archaedasTempleDoorGUID); //re open entrance
                        DoOpenDoor(_ancientVaultDoorGUID);
                    }
                    else if (data == FAIL)
                    {
                        DoOpenDoor(_archaedasTempleDoorGUID);
                        if (GameObject* go = instance->GetGameObject(_archaedasTempleDoorGUID))
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE); // VMaNGOS GO_FLAG_NO_INTERACT (0x10)
                    }
                    else if (data == IN_PROGRESS)
                    {
                        DoResetDoor(_archaedasTempleDoorGUID); //reset the door
                        if (GameObject* go = instance->GetGameObject(_archaedasTempleDoorGUID))
                            go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    }
                    break;
                case DATA_ARCHAEDAS_ALTAR:
                    if (data == IN_PROGRESS)
                    {
                        if (GameObject* go = instance->GetGameObject(_altarOfArchaedasGUID))
                            go->SetGoState(GO_STATE_ACTIVE);
                    }
                    else if (data == NOT_STARTED)
                    {
                        if (GameObject* go = instance->GetGameObject(_altarOfArchaedasGUID))
                            go->SetGoState(GO_STATE_READY);
                    }
                    break;
                case ULDAMAN_ENCOUNTER_ARCHAEDAS:
                {
                    Creature* archaedas = instance->GetCreature(_archaedasGUID);
                    switch (data)
                    {
                        case IN_PROGRESS: // Event is started
                        {
                            if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] != IN_PROGRESS)
                            {
                                if (_encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] != DONE)
                                    SetData(DATA_ANCIENT_DOOR, IN_PROGRESS);
                                if (archaedas)
                                {
                                    if (archaedas->IsAlive() && HasImmuneFlags(archaedas))
                                    {
                                        archaedas->CastSpell(archaedas, SPELL_ARCHAEDAS_AWAKEN, false);
                                        SetUnFrozenState(archaedas);
                                    }
                                }
                            }
                            else if (archaedas) // Wake a wall minion
                            {
                                for (ObjectGuid const& guid : _archaedasWallMinions)
                                {
                                    Creature* target = instance->GetCreature(guid);
                                    if (!target || !target->IsAlive() || !HasImmuneFlags(target))
                                        continue;
                                    archaedas->CastSpell(target, SPELL_AWAKEN_EARTHEN_DWARF, false);
                                    target->SetImmuneToPC(false);
                                    target->SetImmuneToNPC(false);
                                    target->SetUninteractible(false);
                                    break; // only want the first one we find
                                }
                            }
                            _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] = data;
                            break;
                        }
                        case NOT_STARTED: // Archaedas reaches his spawn point
                            // respawn any aggroed wall minions
                            for (ObjectGuid const& guid : _archaedasWallMinions)
                                RespawnMinion(guid);
                            // Vault Warders
                            for (ObjectGuid const& guid : _vaultWarder)
                                RespawnMinion(guid);
                            // Earthen Guardians
                            for (ObjectGuid const& guid : _earthenGuardian)
                                RespawnMinion(guid);
                            // Furniture
                            RespawnFurniture();
                            if (archaedas)
                                SetFrozenState(archaedas);
                            _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] = data;
                            break;
                        case FAIL: // Archaedas resets and moves towards his spawn point
                            _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] = data;
                            SetData(DATA_ANCIENT_DOOR, FAIL); // open the temple door
                            break;
                        case DONE: // Archaedas is dead
                            _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] = data;
                            // remove anything that isn't dead
                            // Wall minions
                            for (ObjectGuid const& guid : _archaedasWallMinions)
                                DespawnMinion(guid);
                            // Vault Warders
                            for (ObjectGuid const& guid : _vaultWarder)
                                DespawnMinion(guid);
                            // Earthen Guardians
                            for (ObjectGuid const& guid : _earthenGuardian)
                                DespawnMinion(guid);
                            // Furniture
                            RespawnFurniture();
                            // Open the Vault door
                            SetData(DATA_ANCIENT_DOOR, DONE);
                            // Summon Ancient Treasure
                            // VMaNGOS Map::SummonGameObject (no summoner); TC needs a summoner - use Archaedas (just died, still in world)
                            if (archaedas)
                                archaedas->SummonGameObject(GO_ANCIENT_TREASURE, 153.39f, 289.091f, -52.2262f, 2.68781f,
                                    QuaternionData(0.0f, 0.0f, 0.97437f, 0.224951f), 0s, GO_SUMMON_TIMED_DESPAWN);
                            break;
                        default:
                            _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS] = data;
                            break;
                    }
                    break;
                }
                default:
                    break;
            }

            if (type < ULDAMAN_MAX_ENCOUNTER && data == DONE)
            {
                _savedIronayaDoor = _encounter[0];
                _savedStoneKeepers = _encounter[1];
                _savedArchaedas = _encounter[2];
            }
        }

        uint32 GetData(uint32 type) const override
        {
            if (type == ULDAMAN_ENCOUNTER_IRONAYA_DOOR) return _encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR];
            if (type == ULDAMAN_ENCOUNTER_STONE_KEEPERS) return _encounter[ULDAMAN_ENCOUNTER_STONE_KEEPERS];
            if (type == ULDAMAN_ENCOUNTER_ARCHAEDAS) return _encounter[ULDAMAN_ENCOUNTER_ARCHAEDAS];

            return 0;
        }

        void Update(uint32 diff) override
        {
            // deferred part of SetFrozenState() for creatures frozen in OnCreatureCreate
            if (!_pendingFreeze.empty())
            {
                for (ObjectGuid const& guid : _pendingFreeze)
                    if (Creature* creature = instance->GetCreature(guid))
                        if (creature->IsAlive() && _encounter[ULDAMAN_ENCOUNTER_IRONAYA_DOOR] != DONE)
                            SetFrozenState(creature);
                _pendingFreeze.clear();
            }

            if (!_keystoneCheck)
                return;

            if (_ironayaSealDoorTimer <= diff)
            {
                Creature* ironaya = instance->GetCreature(_ironayaGUID);
                if (!ironaya)
                    return;
                SetUnFrozenState(ironaya);
                ironaya->SetFaction(415);
                DoOpenDoor(_ironayaSealDoorGUID);
                _keystoneCheck = false;
            }
            else
                _ironayaSealDoorTimer -= diff;
        }

    private:
        uint32 _encounter[ULDAMAN_MAX_ENCOUNTER];

        ObjectGuid _archaedasGUID;
        ObjectGuid _altarOfArchaedasGUID;
        ObjectGuid _ironayaGUID;
        ObjectGuid _whoWokeIronayaGUID;
        ObjectGuid _altarOfTheKeeperGUID;
        ObjectGuid _altarOfTheKeeperTempleDoorGUID;
        ObjectGuid _ancientVaultDoorGUID;
        ObjectGuid _ironayaSealDoorGUID;
        ObjectGuid _keystoneGUID;
        ObjectGuid _archaedasTempleDoorGUID;
        uint32 _ironayaSealDoorTimer;
        bool _keystoneCheck;

        std::vector<ObjectGuid> _stoneKeeper;
        std::vector<ObjectGuid> _vaultWarder;
        std::vector<ObjectGuid> _vaultWarderFurniture;
        std::vector<ObjectGuid> _earthenGuardian;
        std::vector<ObjectGuid> _archaedasWallMinions; //Minions lined up around the wall
        std::vector<ObjectGuid> _pendingFreeze;

        PersistentInstanceScriptValue<uint32> _savedIronayaDoor;
        PersistentInstanceScriptValue<uint32> _savedStoneKeepers;
        PersistentInstanceScriptValue<uint32> _savedArchaedas;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_uldaman_InstanceScript(map);
    }
};

void AddSC_classic_instance_uldaman()
{
    new classic_instance_uldaman();
}
