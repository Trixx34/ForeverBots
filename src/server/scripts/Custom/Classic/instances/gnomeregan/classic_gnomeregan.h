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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/dun_morogh/gnomeregan/gnomeregan.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_GNOMEREGAN_H
#define CLASSIC_GNOMEREGAN_H

#include "InstanceScript.h"
#include "ObjectGuid.h"
#include <list>

#define ClassicGnomereganScriptName "classic_instance_gnomeregan"
#define ClassicGnomereganDataHeader "CGNO"

uint32 const CLASSIC_GNOMEREGAN_MAP_ID = 90;

enum ClassicGnomereganData
{
    MAX_GNOME_FACES             = 6,
    MAX_EXPLOSIVES_PER_SIDE     = 2,

    TYPE_GRUBBIS                = 0,
    TYPE_THERMAPLUGG            = 1,
    TYPE_EXPLOSIVE_CHARGE       = 2,
    INSTANCE_GNOMEREGAN_MAX_ENCOUNTER = 2,                        // Only Grubbis and Thermaplugg need treatment

    DATA_EXPLOSIVE_CHARGE_1     = 1,
    DATA_EXPLOSIVE_CHARGE_2     = 2,
    DATA_EXPLOSIVE_CHARGE_3     = 3,
    DATA_EXPLOSIVE_CHARGE_4     = 4,
    DATA_EXPLOSIVE_CHARGE_USE   = 5
};

// GetGuidData() ids are the entries (as in VMaNGOS GetData64)
enum ClassicGnomereganEntries
{
    NPC_BLASTMASTER_SHORTFUSE   = 7998,
    NPC_ALARM_A_BOMB_2600       = 7897,

    GO_RED_ROCKET               = 103820,
    GO_CAVE_IN_NORTH            = 146085,
    GO_CAVE_IN_SOUTH            = 146086,
    GO_EXPLOSIVE_CHARGE         = 144065,
    GO_THE_FINAL_CHAMBER        = 142207,

    GO_GNOME_FACE_1             = 142211,
    GO_GNOME_FACE_2             = 142210,
    GO_GNOME_FACE_3             = 142209,
    GO_GNOME_FACE_4             = 142208,
    GO_GNOME_FACE_5             = 142213,
    GO_GNOME_FACE_6             = 142212,

    GO_BUTTON_1                 = 142214,
    GO_BUTTON_2                 = 142215,
    GO_BUTTON_3                 = 142216,
    GO_BUTTON_4                 = 142217,
    GO_BUTTON_5                 = 142218,
    GO_BUTTON_6                 = 142219
};

struct ClassicGnomereganBombFace
{
    ObjectGuid GnomeFaceGUID;
    bool Activated = false;
    uint32 BombTimer = 0;
};

// VMaNGOS instance_gnomeregan. Declared here because boss_thermaplugg / go_gnomeface_button use the bomb-face API.
// Defined in classic_instance_gnomeregan.cpp.
class classic_instance_gnomeregan_InstanceScript : public InstanceScript
{
public:
    explicit classic_instance_gnomeregan_InstanceScript(InstanceMap* map);

    void AfterDataLoad() override;

    void OnCreatureCreate(Creature* creature) override;
    void OnGameObjectCreate(GameObject* go) override;

    void SetData(uint32 type, uint32 data) override;
    uint32 GetData(uint32 type) const override;
    ObjectGuid GetGuidData(uint32 type) const override;

    ClassicGnomereganBombFace* GetBombFaces();
    void DoActivateBombFace(uint8 index);
    void DoDeactivateBombFace(uint8 index);

private:
    uint32 _encounter[INSTANCE_GNOMEREGAN_MAX_ENCOUNTER];

    ClassicGnomereganBombFace _bombFaces[MAX_GNOME_FACES];
    ObjectGuid _explosiveSortedGUIDs[2][MAX_EXPLOSIVES_PER_SIDE];

    ObjectGuid _blastmasterShortfuseGUID;
    ObjectGuid _alarmABomb2600GUID;
    ObjectGuid _caveInNorthGUID;
    ObjectGuid _caveInSouthGUID;
    ObjectGuid _doorFinalChamberGUID;

    std::list<ObjectGuid> _explosiveCharges;              // VMaNGOS stored GameObject*, guids are safer
    std::list<ObjectGuid> _spawnedExplosiveChargeGUIDs;
    std::list<ObjectGuid> _redRocketGUIDs;

    PersistentInstanceScriptValue<uint32> _savedGrubbis;
    PersistentInstanceScriptValue<uint32> _savedThermaplugg;
};

#endif
