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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/swamp_of_sorrows/sunken_temple/instance_sunken_temple.cpp
// (ScriptDev2 / ScriptDev0 lineage, GPL-2)
// Ported: instance_sunken_temple (map 109)
// The Atal'ai statue events (VMaNGOS event_scripts 3094-3100, command SetData64(statue entry)) are handled here in ProcessEvent.

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"
#include "classic_sunken_temple.h"
#include <list>

namespace
{
// This is also the needed order for activation
uint32 const ClassicSTAtalaiStatueEntries[CLASSIC_ST_MAX_STATUES] =
{
    GO_ST_ATALAI_STATUE_1, // S
    GO_ST_ATALAI_STATUE_2, // N
    GO_ST_ATALAI_STATUE_3, // SW
    GO_ST_ATALAI_STATUE_4, // SE
    GO_ST_ATALAI_STATUE_5, // NW
    GO_ST_ATALAI_STATUE_6  // NE
};

// VMaNGOS event_scripts: goober event of each Atal'ai statue -> SetData64(statue entry)
struct ClassicSTStatueEvent
{
    uint32 EventId;
    uint32 StatueEntry;
};

ClassicSTStatueEvent const ClassicSTStatueEvents[CLASSIC_ST_MAX_STATUES] =
{
    { 3094, GO_ST_ATALAI_STATUE_1 },
    { 3095, GO_ST_ATALAI_STATUE_2 },
    { 3097, GO_ST_ATALAI_STATUE_3 },
    { 3098, GO_ST_ATALAI_STATUE_4 },
    { 3099, GO_ST_ATALAI_STATUE_5 },
    { 3100, GO_ST_ATALAI_STATUE_6 }
};

Seconds const ClassicSTRespawnForever = Seconds(HOUR * IN_MILLISECONDS); // VMaNGOS DoRespawnGameObject(guid, HOUR * IN_MILLISECONDS)
}

class classic_instance_sunken_temple : public InstanceMapScript
{
public:
    classic_instance_sunken_temple() : InstanceMapScript(ClassicSTScriptName, 109) { }

    struct classic_instance_sunken_temple_InstanceScript : public InstanceScript
    {
        classic_instance_sunken_temple_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _saved0(*this, "SecretCircle", 0u), _saved1(*this, "Protectors", 0u), _saved2(*this, "Jammalan", 0u),
            _saved3(*this, "Malfurion", 0u), _saved4(*this, "Avatar", 0u), _saved5(*this, "Eranikus", 0u),
            _statueCounter(0), _flameCounter(0), _restoreCircleState(true),
            _dreamscythInCombat(false), _atalarionInCombat(false), _eranikusInCombat(false)
        {
            SetHeaders("ST");
            for (uint32& i : _encounter)
                i = 0;
        }

        // VMaNGOS Load(): 6 encounter values, IN_PROGRESS -> NOT_STARTED
        void AfterDataLoad() override
        {
            _encounter[0] = _saved0;
            _encounter[1] = _saved1;
            _encounter[2] = _saved2;
            _encounter[3] = _saved3;
            _encounter[4] = _saved4;
            _encounter[5] = _saved5;
            for (uint32& i : _encounter)
                if (i == IN_PROGRESS)
                    i = NOT_STARTED;
        }

        // VMaNGOS SaveToDB()
        void SaveClassicData()
        {
            _saved0 = _encounter[0];
            _saved1 = _encounter[1];
            _saved2 = _encounter[2];
            _saved3 = _encounter[3];
            _saved4 = _encounter[4];
            _saved5 = _encounter[5];
        }

        void DoSpawnAtalarionIfCan()
        {
            Creature* atalarion = instance->GetCreature(_atalarionGUID);
            if (!atalarion)
                return;

            ClassicScriptText(SAY_ST_ATALALARION_SPAWN, atalarion);
            atalarion->SetVisible(true);
            atalarion->SetImmuneToPC(false);
        }

        void HandleStatueEventDone()
        {
            // Spawn the idol of Hakkar
            DoRespawnGameObject(_idolHakkarGUID, ClassicSTRespawnForever);

            // Disable interacting with circle stones
            for (ObjectGuid const& guid : _atalaiStatueGUIDs)
                if (GameObject* go = instance->GetGameObject(guid))
                    go->SetFlag(GO_FLAG_NOT_SELECTABLE); // VMaNGOS GO_FLAG_NO_INTERACT

            // Spawn the big green lights
            for (ObjectGuid const& guid : _bigLightGUIDs)
                DoRespawnGameObject(guid, ClassicSTRespawnForever);
        }

        void ProcessStatueUsed(uint32 statueEntry)
        {
            if (GetData(CLASSIC_ST_TYPE_SECRET_CIRCLE) == DONE)
                return;

            if (GetData(CLASSIC_ST_TYPE_SECRET_CIRCLE) != IN_PROGRESS)
                SetData(CLASSIC_ST_TYPE_SECRET_CIRCLE, IN_PROGRESS);

            ObjectGuid statueGuid;
            for (uint8 i = 0; i < CLASSIC_ST_MAX_STATUES; ++i)
                if (ClassicSTAtalaiStatueEntries[i] == statueEntry)
                    statueGuid = _atalaiStatueGUIDs[i];

            if (statueGuid.IsEmpty())
                return;

            GameObject* statue = instance->GetGameObject(statueGuid);
            if (!statue)
                return;

            bool activationSuccess = false;

            // Check if the statues are activated correctly
            // Increase the counter when the correct statue is activated
            for (uint8 i = 0; i < CLASSIC_ST_MAX_STATUES; ++i)
            {
                if (statueEntry == ClassicSTAtalaiStatueEntries[i] && _statueCounter == i)
                {
                    // Correct statue activated
                    ++_statueCounter;
                    activationSuccess = true;
                    statue->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    // Show green light
                    if (GameObject* light = GetClosestGameObjectWithEntry(statue, GO_ST_ATALAI_LIGHT, INTERACTION_DISTANCE, false))
                        DoRespawnGameObject(light->GetGUID(), ClassicSTRespawnForever);
                    break;
                }
            }

            // Check if all statues are activated
            if (_statueCounter == CLASSIC_ST_MAX_STATUES)
            {
                SetData(CLASSIC_ST_TYPE_SECRET_CIRCLE, DONE);
                return;
            }

            if (activationSuccess)
                return;

            // If the wrong statue was activated, then trigger trap
            // We don't know actually which trap goes to which statue so we need to search for each
            Creature* atalarion = instance->GetCreature(_atalarionGUID);
            if (!atalarion)
                return;

            uint32 const traps[] = { GO_ST_ATALAI_TRAP_1, GO_ST_ATALAI_TRAP_2, GO_ST_ATALAI_TRAP_3 };
            if (GameObject* trap = GetClosestGameObjectWithEntry(statue, traps[urand(0, 2)], INTERACTION_DISTANCE))
                trap->Use(atalarion);
        }

        void ProcessEvent(WorldObject* obj, uint32 eventId, WorldObject* /*invoker*/) override
        {
            // Prefer the statue that sent the event, fall back on the VMaNGOS event id
            if (GameObject* go = obj ? obj->ToGameObject() : nullptr)
            {
                for (uint32 entry : ClassicSTAtalaiStatueEntries)
                {
                    if (go->GetEntry() == entry)
                    {
                        ProcessStatueUsed(entry);
                        return;
                    }
                }
            }

            for (ClassicSTStatueEvent const& statueEvent : ClassicSTStatueEvents)
            {
                if (statueEvent.EventId == eventId)
                {
                    ProcessStatueUsed(statueEvent.StatueEntry);
                    return;
                }
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_ST_JAMMALAN_BARRIER:
                    _jammalanBarrierGUID = go->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_PROTECTORS) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                case GO_ST_IDOL_OF_HAKKAR:
                    _idolHakkarGUID = go->GetGUID();
                    go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                case GO_ST_ATALAI_STATUE_1:
                    _atalaiStatueGUIDs[0] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_STATUE_2:
                    _atalaiStatueGUIDs[1] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_STATUE_3:
                    _atalaiStatueGUIDs[2] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_STATUE_4:
                    _atalaiStatueGUIDs[3] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_STATUE_5:
                    _atalaiStatueGUIDs[4] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_STATUE_6:
                    _atalaiStatueGUIDs[5] = go->GetGUID();
                    break;
                case GO_ST_ATALAI_LIGHT_BIG:
                {
                    uint32 countLight = 0;
                    for (ObjectGuid const& guid : _bigLightGUIDs)
                        if (!guid.IsEmpty() && guid != go->GetGUID())
                            ++countLight;
                    if (countLight < CLASSIC_ST_MAX_STATUES)
                        _bigLightGUIDs[countLight] = go->GetGUID();
                    break;
                }
                default:
                    break;
            }
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_ST_ZOLO:
                    _protectorGUIDs[0] = creature->GetGUID();
                    break;
                case NPC_ST_GASHER:
                    _protectorGUIDs[1] = creature->GetGUID();
                    break;
                case NPC_ST_LORO:
                    _protectorGUIDs[2] = creature->GetGUID();
                    break;
                case NPC_ST_HUKKU:
                    _protectorGUIDs[3] = creature->GetGUID();
                    break;
                case NPC_ST_ZULLOR:
                    _protectorGUIDs[4] = creature->GetGUID();
                    break;
                case NPC_ST_MIJAN:
                    _protectorGUIDs[5] = creature->GetGUID();
                    break;
                case NPC_ST_JAMMALAN:
                    _jammalanGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_PROTECTORS) == DONE)
                        creature->SetImmuneToPC(false);
                    break;
                case NPC_ST_OGOM:
                    _ogomGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_PROTECTORS) == DONE)
                        creature->SetImmuneToPC(false);
                    break;
                case NPC_ST_ATALARION:
                    _atalarionGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_SECRET_CIRCLE) != SPECIAL && GetData(CLASSIC_ST_TYPE_SECRET_CIRCLE) != DONE)
                    {
                        creature->SetVisible(false);
                        creature->SetImmuneToPC(true);
                    }
                    else
                        creature->SetImmuneToPC(false);
                    break;
                case NPC_ST_SHADE_OF_ERANIKUS:
                    _shadeEranikusGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_JAMMALAN) != DONE)
                    {
                        creature->SetImmuneToPC(true);
                        creature->SetStandState(UNIT_STAND_STATE_SLEEP);
                    }
                    else
                        creature->SetImmuneToPC(false);
                    break;
                case NPC_ST_DREAMSCYTH:
                    _dreamscythGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_JAMMALAN) != DONE)
                    {
                        creature->SetVisible(false);
                        creature->SetImmuneToPC(true);
                        creature->GetMotionMaster()->MoveIdle();
                    }
                    else
                        creature->SetImmuneToPC(false);
                    break;
                case NPC_ST_WEAVER:
                    _weaverGUID = creature->GetGUID();
                    if (GetData(CLASSIC_ST_TYPE_JAMMALAN) != DONE)
                    {
                        creature->SetVisible(false);
                        creature->SetImmuneToPC(true);
                        creature->GetMotionMaster()->MoveIdle();
                    }
                    else
                        creature->SetImmuneToPC(false);
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS OnCreatureEnterCombat hook: TC has no instance hook for this, so combat entry is polled in Update()
        void OnClassicCreatureEnterCombat(Creature* creature)
        {
            switch (creature->GetEntry())
            {
                case NPC_ST_DREAMSCYTH:
                    ClassicScriptText(SAY_ST_DREAMSCYTHE_AGGRO, creature);
                    break;
                case NPC_ST_ATALARION:
                    if (creature->IsVisible())
                        ClassicScriptText(SAY_ST_ATALALARION_AGGRO, creature);
                    else
                    {
                        creature->AI()->EnterEvadeMode();
                        creature->SetImmuneToPC(true);
                    }
                    break;
                case NPC_ST_SHADE_OF_ERANIKUS:
                    creature->SetStandState(UNIT_STAND_STATE_STAND);
                    break;
                default:
                    break;
            }
        }

        void PollCombatEntry(ObjectGuid const& guid, bool& wasInCombat)
        {
            Creature* creature = instance->GetCreature(guid);
            bool inCombat = creature && creature->IsAlive() && creature->IsInCombat();
            if (inCombat && !wasInCombat)
                OnClassicCreatureEnterCombat(creature);
            wasInCombat = inCombat;
        }

        // VMaNGOS OnCreatureDeath
        void OnUnitDeath(Unit* unit) override
        {
            Creature* creature = unit->ToCreature();
            if (!creature)
                return;

            switch (creature->GetEntry())
            {
                case NPC_ST_ATALARION:
                    if (GameObject* idol = instance->GetGameObject(_idolHakkarGUID))
                        idol->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS: SetInCombatWithZone() on every DB-spawned living creature of the given entries around source
        void ZoneInCombatDbSpawns(Creature* source, std::initializer_list<uint32> entries, float range)
        {
            for (uint32 entry : entries)
            {
                std::list<Creature*> mobs;
                GetCreatureListWithEntryInGrid(mobs, source, entry, range);
                for (Creature* mob : mobs)
                {
                    // Summoned creature
                    if (!mob->GetSpawnId())
                        continue;

                    if (mob->IsAlive())
                        CreatureAI::DoZoneInCombat(mob);
                }
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case CLASSIC_ST_TYPE_SECRET_CIRCLE:
                    _encounter[0] = data;
                    if (data == DONE)
                    {
                        HandleStatueEventDone();
                        DoSpawnAtalarionIfCan();
                    }
                    break;
                case CLASSIC_ST_TYPE_PROTECTORS:
                    _encounter[1] = data;
                    if (data == DONE)
                    {
                        bool allDead = true;
                        for (ObjectGuid const& guid : _protectorGUIDs)
                        {
                            if (Creature* protector = instance->GetCreature(guid))
                            {
                                if (protector->IsAlive())
                                {
                                    allDead = false;
                                    break;
                                }
                            }
                        }

                        if (allDead)
                        {
                            if (GameObject* barrier = instance->GetGameObject(_jammalanBarrierGUID))
                                if (barrier->GetGoState() != GO_STATE_ACTIVE) // Closed
                                    DoUseDoorOrButton(_jammalanBarrierGUID);

                            // Intro yell
                            if (Creature* jammalan = instance->GetCreature(_jammalanGUID))
                            {
                                jammalan->SetImmuneToPC(false);
                                ClassicScriptText(SAY_ST_JAMMALAN_INTRO, jammalan);
                            }
                            if (Creature* ogom = instance->GetCreature(_ogomGUID))
                                ogom->SetImmuneToPC(false);
                        }
                    }
                    break;
                case CLASSIC_ST_TYPE_JAMMALAN:
                    _encounter[2] = data;
                    if (data == DONE)
                    {
                        if (Creature* eranikus = instance->GetCreature(_shadeEranikusGUID))
                            eranikus->SetImmuneToPC(false);
                        if (Creature* dreamscyth = instance->GetCreature(_dreamscythGUID))
                        {
                            dreamscyth->SetVisible(true);
                            dreamscyth->SetImmuneToPC(false);
                            StartDbWaypoints(dreamscyth);
                            ClassicScriptText(SAY_ST_DREAMSCYTHE_INTRO, dreamscyth);
                        }
                        if (Creature* weaver = instance->GetCreature(_weaverGUID))
                        {
                            weaver->SetVisible(true);
                            weaver->SetImmuneToPC(false);
                            StartDbWaypoints(weaver);
                        }
                    }
                    else if (data == IN_PROGRESS)
                    {
                        if (Creature* jammalan = instance->GetCreature(_jammalanGUID))
                            ZoneInCombatDbSpawns(jammalan, {
                                5263,   // Mummified Atal'ai
                                5271,   // Atal'ai Deathwalker
                                5273    // Atal'ai High Priest
                            }, 150.0f);
                    }
                    break;
                case CLASSIC_ST_TYPE_MALFURION:
                    _encounter[3] = data;
                    break;
                case CLASSIC_ST_TYPE_AVATAR:
                    _encounter[4] = data;
                    break;
                case CLASSIC_ST_TYPE_ERANIKUS:
                    _encounter[5] = data;
                    if (data == IN_PROGRESS)
                    {
                        if (Creature* eranikus = instance->GetCreature(_shadeEranikusGUID))
                            ZoneInCombatDbSpawns(eranikus, {
                                5277,   // Nightmare Scalebane
                                5280,   // Nightmare Wyrmkin
                                8319,   // Nightmare Whelp
                                5283    // Nightmare Wanderer
                            }, 300.0f);
                    }
                    break;
                case CLASSIC_ST_TYPE_ETERNAL_FLAME:
                    _flameCounter = data;
                    break;
                default:
                    break;
            }

            if (data == DONE)
                SaveClassicData();
        }

        // VMaNGOS MoveWaypoint(): start the creature's DB waypoint path
        static void StartDbWaypoints(Creature* creature)
        {
            if (uint32 pathId = creature->GetWaypointPathId())
                creature->GetMotionMaster()->MovePath(pathId, true);
            else
                creature->GetMotionMaster()->InitializeDefault();
        }

        // VMaNGOS SetData64
        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            switch (type)
            {
                case NPC_ST_SHADE_OF_HAKKAR:
                    _shadeHakkarGUID = data;
                    break;
                case NPC_ST_ATALARION:
                    _atalarionGUID = data;
                    break;
                case NPC_ST_AVATAR_OF_HAKKAR:
                    _avatarHakkarGUID = data;
                    break;
                case GO_ST_ATALAI_STATUE_1:
                case GO_ST_ATALAI_STATUE_2:
                case GO_ST_ATALAI_STATUE_3:
                case GO_ST_ATALAI_STATUE_4:
                case GO_ST_ATALAI_STATUE_5:
                case GO_ST_ATALAI_STATUE_6:
                    ProcessStatueUsed(type);
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case CLASSIC_ST_TYPE_SECRET_CIRCLE:
                    return _encounter[0];
                case CLASSIC_ST_TYPE_PROTECTORS:
                    return _encounter[1];
                case CLASSIC_ST_TYPE_JAMMALAN:
                    return _encounter[2];
                case CLASSIC_ST_TYPE_MALFURION:
                    return _encounter[3];
                case CLASSIC_ST_TYPE_AVATAR:
                    return _encounter[4];
                case CLASSIC_ST_TYPE_ERANIKUS:
                    return _encounter[5];
                case CLASSIC_ST_TYPE_ETERNAL_FLAME:
                    return _flameCounter;
                default:
                    return 0;
            }
        }

        // VMaNGOS GetData64
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case NPC_ST_SHADE_OF_HAKKAR:
                    return _shadeHakkarGUID;
                case NPC_ST_ATALARION:
                    return _atalarionGUID;
                case NPC_ST_AVATAR_OF_HAKKAR:
                    return _avatarHakkarGUID;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        void Update(uint32 /*diff*/) override
        {
            // (VMaNGOS also flushed the map remove list every 5s here; TC does that itself)

            // Check if need to restore state after server crash
            if (_restoreCircleState)
            {
                _restoreCircleState = false;
                if (GetData(CLASSIC_ST_TYPE_SECRET_CIRCLE) == DONE)
                {
                    HandleStatueEventDone();
                    if (Creature* atalarion = instance->GetCreature(_atalarionGUID))
                    {
                        // Idol of Hakkar should be interactable when Atal'alarion was killed
                        if (!atalarion->IsAlive())
                            if (GameObject* idol = instance->GetGameObject(_idolHakkarGUID))
                                idol->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                    }
                }
            }

            PollCombatEntry(_dreamscythGUID, _dreamscythInCombat);
            PollCombatEntry(_atalarionGUID, _atalarionInCombat);
            PollCombatEntry(_shadeEranikusGUID, _eranikusInCombat);
        }

    private:
        uint32 _encounter[CLASSIC_ST_MAX_ENCOUNTER];
        PersistentInstanceScriptValue<uint32> _saved0;
        PersistentInstanceScriptValue<uint32> _saved1;
        PersistentInstanceScriptValue<uint32> _saved2;
        PersistentInstanceScriptValue<uint32> _saved3;
        PersistentInstanceScriptValue<uint32> _saved4;
        PersistentInstanceScriptValue<uint32> _saved5;

        ObjectGuid _protectorGUIDs[6];  // Jammal'an door handling
        uint8 _statueCounter;           // Atal'alarion statue event
        uint32 _flameCounter;           // Avatar of Hakkar event
        ObjectGuid _shadeHakkarGUID;
        ObjectGuid _atalarionGUID;
        ObjectGuid _jammalanBarrierGUID;
        ObjectGuid _idolHakkarGUID;
        ObjectGuid _shadeEranikusGUID;
        ObjectGuid _jammalanGUID;
        ObjectGuid _ogomGUID;
        ObjectGuid _dreamscythGUID;
        ObjectGuid _weaverGUID;
        ObjectGuid _avatarHakkarGUID;

        ObjectGuid _atalaiStatueGUIDs[CLASSIC_ST_MAX_STATUES];
        ObjectGuid _bigLightGUIDs[CLASSIC_ST_MAX_STATUES];

        bool _restoreCircleState;
        bool _dreamscythInCombat;
        bool _atalarionInCombat;
        bool _eranikusInCombat;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_sunken_temple_InstanceScript(map);
    }
};

void AddSC_classic_instance_sunken_temple()
{
    new classic_instance_sunken_temple();
}
