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

// Classic 1.60 (Forever): The Hall of Thanes (map 3065), from the official dungeon sniff of 2026-10-12. The bosses run SmartAI, so
// the boss states come from ClassicDungeonEncounterTracker. Encounter to creature (ENCOUNTER_START / END next to the boss threat and
// kill packets): 3493 Faldrim Anvilmar, 3495 "Infurnus" is Magmatus, 3494 Plunder, 3496 Durgen Dirgehammer.
// Doors as the sniff showed them (GO state updates):
//   - Faldrim's death opens the Portcullises 642024 and 642025 (out of Anvilmar's Rest)
//   - Plunder's death opens the Iron Door 645389 (his hallway)
//   - the Ancient Switch 673473 (Durgen's room) opens the Iron Doors 645391 and 645392 back to the entrance and the whole instance
//     gets "The sound of heavy metal grinding upon stone echoes in the distance." (BG system message, no sender). Kept as a boss
//     state without encounter so the doors stay open for doors loaded later.
// No door closed during a boss fight. The Vault Doors around Durgen's room are opened by players (no lock), not scripted.

#include "ScriptMgr.h"
#include "ChatPackets.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "classic_dungeon_encounter_tracker.h"

namespace
{
constexpr uint32 CLASSIC_HOT_MAP_ID = 3065;
constexpr char const* CLASSIC_HOT_SWITCH_TEXT = "The sound of heavy metal grinding upon stone echoes in the distance.";

enum ClassicHallOfThanesBosses : uint32
{
    CHOT_ENC_FALDRIM = 0,
    CHOT_ENC_MAGMATUS = 1,
    CHOT_ENC_PLUNDER = 2,
    CHOT_ENC_DURGEN = 3,
    CHOT_ANCIENT_SWITCH = 4,        // not an encounter: the switch that opens the way back
    CHOT_MAX
};

enum ClassicHallOfThanesObjects : uint32
{
    GO_HOT_PORTCULLIS_REST_1 = 642024,
    GO_HOT_PORTCULLIS_REST_2 = 642025,
    GO_HOT_IRON_DOOR_PLUNDER = 645389,
    GO_HOT_IRON_DOOR_BACK_1 = 645391,
    GO_HOT_IRON_DOOR_BACK_2 = 645392
};

constexpr ClassicDungeonBossEntry ClassicHallOfThanesBossEntries[] =
{
    { CHOT_ENC_FALDRIM, 261306 },   // Faldrim Anvilmar
    { CHOT_ENC_MAGMATUS, 261316 },  // Magmatus (encounter "Infurnus")
    { CHOT_ENC_PLUNDER, 261311 },   // Plunder
    { CHOT_ENC_DURGEN, 261319 }     // Durgen Dirgehammer
};

constexpr DoorData ClassicHallOfThanesDoors[] =
{
    { GO_HOT_PORTCULLIS_REST_1, CHOT_ENC_FALDRIM, EncounterDoorBehavior::OpenWhenDone },
    { GO_HOT_PORTCULLIS_REST_2, CHOT_ENC_FALDRIM, EncounterDoorBehavior::OpenWhenDone },
    { GO_HOT_IRON_DOOR_PLUNDER, CHOT_ENC_PLUNDER, EncounterDoorBehavior::OpenWhenDone },
    { GO_HOT_IRON_DOOR_BACK_1, CHOT_ANCIENT_SWITCH, EncounterDoorBehavior::OpenWhenDone },
    { GO_HOT_IRON_DOOR_BACK_2, CHOT_ANCIENT_SWITCH, EncounterDoorBehavior::OpenWhenDone }
};
}

class classic_instance_hall_of_thanes : public InstanceMapScript
{
public:
    classic_instance_hall_of_thanes() : InstanceMapScript("classic_instance_hall_of_thanes", CLASSIC_HOT_MAP_ID) { }

    struct classic_instance_hall_of_thanes_InstanceScript : public InstanceScript
    {
        classic_instance_hall_of_thanes_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders("CHOT");
            SetBossNumber(CHOT_MAX);
            static constexpr DungeonEncounterData encounters[] =
            {
                { CHOT_ENC_FALDRIM, {{ 3493 }} },
                { CHOT_ENC_MAGMATUS, {{ 3495 }} },
                { CHOT_ENC_PLUNDER, {{ 3494 }} },
                { CHOT_ENC_DURGEN, {{ 3496 }} }
            };
            LoadDungeonEncounterData(encounters);
            LoadDoorData(ClassicHallOfThanesDoors);
        }

        ClassicDungeonEncounterTracker _encounterTracker{ *this, ClassicHallOfThanesBossEntries };

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);
            _encounterTracker.OnCreatureCreate(creature);
        }

        void OnCreatureRemove(Creature* creature) override
        {
            InstanceScript::OnCreatureRemove(creature);
            _encounterTracker.OnCreatureRemove(creature);
        }

        void OnUnitDeath(Unit* unit) override
        {
            _encounterTracker.OnUnitDeath(unit);
        }

        void Update(uint32 diff) override
        {
            _encounterTracker.Update(diff);
        }

        void SetData(uint32 type, uint32 /*data*/) override
        {
            if (type != CHOT_ANCIENT_SWITCH || GetBossState(CHOT_ANCIENT_SWITCH) == DONE)
                return;

            if (GetBossState(CHOT_ANCIENT_SWITCH) == TO_BE_DECIDED)
                SetBossState(CHOT_ANCIENT_SWITCH, NOT_STARTED);
            SetBossState(CHOT_ANCIENT_SWITCH, DONE);

            WorldPackets::Chat::Chat packet;
            packet.Initialize(CHAT_MSG_BG_SYSTEM_NEUTRAL, LANG_UNIVERSAL, nullptr, nullptr, CLASSIC_HOT_SWITCH_TEXT);
            instance->SendToPlayers(packet.Write());
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_hall_of_thanes_InstanceScript(map);
    }
};

// Ancient Switch (673473): sniff 2026-10-12, used once after Durgen died; the doors opened in the same second
class go_hall_of_thanes_ancient_switch : public GameObjectScript
{
public:
    go_hall_of_thanes_ancient_switch() : GameObjectScript("go_hall_of_thanes_ancient_switch") { }

    struct go_hall_of_thanes_ancient_switchAI : public GameObjectAI
    {
        go_hall_of_thanes_ancient_switchAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* /*player*/) override
        {
            if (InstanceScript* instance = me->GetInstanceScript())
                instance->SetData(CHOT_ANCIENT_SWITCH, DONE);
            return false;               // the button itself works as usual
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        if (go->GetMapId() != CLASSIC_HOT_MAP_ID)
            return nullptr;
        return new go_hall_of_thanes_ancient_switchAI(go);
    }
};

void AddSC_classic_instance_hall_of_thanes()
{
    new classic_instance_hall_of_thanes();
    new go_hall_of_thanes_ancient_switch();
}
