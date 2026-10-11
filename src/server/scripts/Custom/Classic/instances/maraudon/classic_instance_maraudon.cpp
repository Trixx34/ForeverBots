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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/desolace/maraudon/instance_maraudon.cpp (Nostalrius / VMaNGOS, GPL-2)
// Ported: instance_maraudon (map 349)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "ScriptedCreature.h"
#include "classic_dungeon_encounter_tracker.h"
#include "classic_maraudon.h"

namespace
{
// Classic 1.60: TC boss states for the DungeonEncounter.db2 encounters of Maraudon (map 349), driven by
// ClassicDungeonEncounterTracker (boss kill credit, encounter start / end, instance lock)
enum ClassicMaraudonEncounterBosses : uint32
{
    CMARA_ENC_NOXXION = 0,
    CMARA_ENC_RAZORLASH = 1,
    CMARA_ENC_GIZLOCK = 2,
    CMARA_ENC_VYLETONGUE = 3,
    CMARA_ENC_CELEBRAS = 4,
    CMARA_ENC_LANDSLIDE = 5,
    CMARA_ENC_ROTGRIP = 6,
    CMARA_ENC_THERADRAS = 7,
    CMARA_ENC_MAX
};

constexpr ClassicDungeonBossEntry ClassicMaraudonEncounterBossEntries[] =
{
    { CMARA_ENC_NOXXION, 13282 }, // Noxxion
    { CMARA_ENC_RAZORLASH, 12258 }, // Razorlash
    { CMARA_ENC_GIZLOCK, 13601 }, // Tinkerer Gizlock
    { CMARA_ENC_VYLETONGUE, 12236 }, // Lord Vyletongue
    { CMARA_ENC_CELEBRAS, 12225 }, // Celebras the Cursed
    { CMARA_ENC_LANDSLIDE, 12203 }, // Landslide
    { CMARA_ENC_ROTGRIP, 13596 }, // Rotgrip
    { CMARA_ENC_THERADRAS, 12201 } // Princess Theradras
};
}

class classic_instance_maraudon : public InstanceMapScript
{
public:
    classic_instance_maraudon() : InstanceMapScript(ClassicMaraudonScriptName, 349) { }

    struct classic_instance_maraudon_InstanceScript : public InstanceScript
    {
        classic_instance_maraudon_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _savedLarvaSpewer(*this, "LarvaSpewer", 0u), _savedCelebras(*this, "Celebras", 0u),
            _respawnSpewedLarva(false), _spewedLarvaTimer(4000)
        {
            SetHeaders("MARA");
            SetBossNumber(CMARA_ENC_MAX);
            static constexpr DungeonEncounterData encounters[] =
            {
                { CMARA_ENC_NOXXION, {{ 422 }} },
                { CMARA_ENC_RAZORLASH, {{ 423 }} },
                { CMARA_ENC_GIZLOCK, {{ 427 }} },
                { CMARA_ENC_VYLETONGUE, {{ 424 }} },
                { CMARA_ENC_CELEBRAS, {{ 425 }} },
                { CMARA_ENC_LANDSLIDE, {{ 426 }} },
                { CMARA_ENC_ROTGRIP, {{ 428 }} },
                { CMARA_ENC_THERADRAS, {{ 429 }} }
            };
            LoadDungeonEncounterData(encounters);
            _encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] = 0;
            _encounter[CLASSIC_MARA_TYPE_CELEBRAS] = 0;
        }

        ClassicDungeonEncounterTracker _encounterTracker{ *this, ClassicMaraudonEncounterBossEntries };

        void OnCreatureRemove(Creature* creature) override
        {
            InstanceScript::OnCreatureRemove(creature);
            _encounterTracker.OnCreatureRemove(creature);
        }

        void OnUnitDeath(Unit* unit) override
        {
            _encounterTracker.OnUnitDeath(unit);
        }

        // VMaNGOS Load(): IN_PROGRESS -> NOT_STARTED
        void AfterDataLoad() override
        {
            _encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] = _savedLarvaSpewer;
            _encounter[CLASSIC_MARA_TYPE_CELEBRAS] = _savedCelebras;
            for (uint32& i : _encounter)
                if (i == IN_PROGRESS)
                    i = NOT_STARTED;
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);
            _encounterTracker.OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_MARA_CELEBRAS_REDEEMED:
                    _celebrasGUID = creature->GetGUID();

                    if (_encounter[CLASSIC_MARA_TYPE_CELEBRAS] != DONE)
                        creature->SetVisible(false);
                    break;
                case NPC_MARA_SPEWED_LARVA:
                    _spewedLarvaGUID = creature->GetGUID();
                    // No functional spewer no larva
                    if (_encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] == DONE)
                        creature->DisappearAndDie();
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
                case GO_MARA_HEALED_CELEBRIAN_VINE:
                    // The healed vine is summoned by the corrupted one
                    _vineGUID = go->GetGUID();
                    break;
                case GO_MARA_LARVA_SPEWER:
                    _larvaSpewerGUID = go->GetGUID();
                    // Alternative state = destroyed
                    if (_encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] == DONE)
                        go->SetGoState(GO_STATE_DESTROYED);
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case CLASSIC_MARA_TYPE_LARVA_SPEWER:
                    return _encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER];
                case CLASSIC_MARA_TYPE_CELEBRAS:
                    return _encounter[CLASSIC_MARA_TYPE_CELEBRAS];
                default:
                    return 0;
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case CLASSIC_MARA_TYPE_LARVA_SPEWER:
                    if (data == IN_PROGRESS)
                    {
                        // First kill our larva
                        if (Creature* spewedLarva = instance->GetCreature(_spewedLarvaGUID))
                            spewedLarva->DisappearAndDie();
                        SpewLarva();
                    }
                    else
                        _encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] = data;
                    break;
                case CLASSIC_MARA_TYPE_CELEBRAS:
                    if (data == DONE)
                        if (Creature* celebras = instance->GetCreature(_celebrasGUID))
                            celebras->SetVisible(true);

                    _encounter[CLASSIC_MARA_TYPE_CELEBRAS] = data;
                    break;
                default:
                    break;
            }

            if (data == DONE)
            {
                // VMaNGOS SaveToDB()
                _savedLarvaSpewer = _encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER];
                _savedCelebras = _encounter[CLASSIC_MARA_TYPE_CELEBRAS];
            }
        }

        // VMaNGOS GetData64
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case NPC_MARA_CELEBRAS_REDEEMED:
                    return _celebrasGUID;
                default:
                    break;
            }

            return ObjectGuid::Empty;
        }

        void SpewLarva()
        {
            if (GameObject* larvaSpewer = instance->GetGameObject(_larvaSpewerGUID))
            {
                if (larvaSpewer->GetGoState() == GO_STATE_READY)
                {
                    // The custom animation is the spewing
                    larvaSpewer->SendCustomAnim(0);
                    _respawnSpewedLarva = true;
                    _spewedLarvaTimer = 4000;
                }
            }
        }

        void Update(uint32 diff) override
        {
            _encounterTracker.Update(diff);

            // Remove a corrupted vine when a healed one was summoned over
            if (!_vineGUID.IsEmpty())
            {
                if (GameObject* healedVine = instance->GetGameObject(_vineGUID))
                    if (GameObject* corruptedVine = GetClosestGameObjectWithEntry(healedVine, GO_MARA_VYLESTEM_VINE, INTERACTION_DISTANCE))
                        corruptedVine->Delete();
                _vineGUID.Clear();
            }

            // VMaNGOS OnCreatureRespawn: no functional spewer no larva (TC has no instance respawn hook, so poll it)
            if (_encounter[CLASSIC_MARA_TYPE_LARVA_SPEWER] == DONE)
                if (Creature* spewedLarva = instance->GetCreature(_spewedLarvaGUID))
                    if (spewedLarva->IsAlive())
                        spewedLarva->DisappearAndDie();

            // Respawn a larva 4 seconds after the spewer animation start
            if (_respawnSpewedLarva)
            {
                if (_spewedLarvaTimer <= diff)
                {
                    if (Creature* spewedLarva = instance->GetCreature(_spewedLarvaGUID))
                        spewedLarva->Respawn();
                    _respawnSpewedLarva = false;
                }
                else
                    _spewedLarvaTimer -= diff;
            }
        }

    private:
        uint32 _encounter[CLASSIC_MARA_MAX_ENCOUNTER];
        PersistentInstanceScriptValue<uint32> _savedLarvaSpewer;
        PersistentInstanceScriptValue<uint32> _savedCelebras;

        ObjectGuid _celebrasGUID;
        ObjectGuid _spewedLarvaGUID;
        ObjectGuid _vineGUID;
        ObjectGuid _larvaSpewerGUID;
        bool _respawnSpewedLarva;
        uint32 _spewedLarvaTimer;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_maraudon_InstanceScript(map);
    }
};

void AddSC_classic_instance_maraudon()
{
    new classic_instance_maraudon();
}
