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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/temple_of_ahnqiraj.h (GPL-2)

#ifndef CLASSIC_TEMPLE_OF_AHNQIRAJ_H
#define CLASSIC_TEMPLE_OF_AHNQIRAJ_H

#include "Define.h"
#include "InstanceScript.h"
#include "ObjectGuid.h"

class Creature;
class GameObject;
class Player;
class Unit;
class WorldObject;
struct AreaTriggerEntry;

#define ClassicTempleOfAhnQirajScriptName "classic_instance_temple_of_ahnqiraj"
#define ClassicTempleOfAhnQirajDataHeader "CAQ40"

// Data ids keep the VMaNGOS values. TYPE_* 0..8 are also the InstanceScript boss ids
// (SetData/GetData with these ids work exactly like VMaNGOS: NOT_STARTED/IN_PROGRESS/FAIL/DONE/SPECIAL).
enum
{
    TYPE_SKERAM                 = 0,
    TYPE_SARTURA                = 1,
    TYPE_FANKRISS               = 2,
    TYPE_HUHURAN                = 3,
    TYPE_TWINS                  = 4,
    TYPE_CTHUN                  = 5,
    TYPE_BUG_TRIO               = 6,
    TYPE_VISCIDUS               = 7,
    TYPE_OURO                   = 8,

    MAX_ENCOUNTER               = 10,

    CLASSIC_AQ40_MAP_ID         = 531
};

enum
{
    NPC_VEKNISS_SOLDIER         = 15229,
    NPC_VEKNISS_WARRIOR         = 15230,
    NPC_VEKNISS_GUARDIAN        = 15233,
    NPC_VEKNISS_STINGER         = 15235,
    NPC_VEKNISS_WASP            = 15236,
    NPC_VEKNISS_HIVE_CRAWLER    = 15240,
    NPC_QIRAJI_MINDSLAYER       = 15246,
    NPC_QIRAJI_BRAINWASHER      = 15247,
    NPC_QIRAJI_LASHER           = 15249,
    NPC_QIRAJI_SLAYER           = 15250,
    NPC_QIRAJI_CHAMPION         = 15252,
    NPC_OBSIDIAN_ERADICATOR     = 15262,
    NPC_SKERAM                  = 15263,
    NPC_ANUBISATH_SENTINEL      = 15264,
    NPC_VEKNILASH               = 15275,
    NPC_VEKLOR                  = 15276,
    NPC_ANUBISATH_DEFENDER      = 15277,
    NPC_VISCIDUS                = 15299,
    NPC_VEKNISS_DRONE           = 15300,
    NPC_ANUBISATH_WARDER        = 15311,
    NPC_OBSIDIAN_NULLIFIER      = 15312,
    NPC_QIRAJI_SCARAB           = 15316,
    NPC_QIRAJI_SCORPION         = 15317,
    NPC_MERITHRA_OF_THE_DREAM   = 15378,
    NPC_CAELESTRASZ             = 15379,
    NPC_ARYGOS                  = 15380,
    NPC_ANDORGOS                = 15502,
    NPC_KANDROSTRASZ            = 15503,
    NPC_VETHSERA                = 15504,
    NPC_PRINCESS_HUHURAN        = 15509,
    NPC_FANKRISS_THE_UNYIELDING = 15510,
    NPC_KRI                     = 15511,
    NPC_BATTLEGUARD_SARTURA     = 15516,
    NPC_OURO                    = 15517,
    NPC_ANUBISATH_WARRIOR       = 15537,
    NPC_ANUBISATH_SWARMGUARD    = 15538,
    NPC_PRINCESS_YAUJ           = 15543,
    NPC_VEM                     = 15544,
    NPC_EYE_OF_C_THUN           = 15589,
    NPC_YAUJ_BROOD              = 15621,
    NPC_VEKNISS_BORER           = 15622,
    NPC_SPAWN_OF_FANKRISS       = 15630,
    NPC_OURO_SCARAB             = 15718,
    NPC_OURO_SPAWNER            = 15957,
    NPC_CTHUN                   = 15727,
    NPC_CTHUN_PORTAL            = 15896,
    NPC_VEKNISS_HATCHLING       = 15962,
    NPC_MASTERS_EYE             = 15963,
    NPC_SARTURA_S_ROYAL_GUARD   = 15984,

    GO_SKERAM_GATE              = 180636,
    GO_TWINS_ENTER_DOOR         = 180634,
    GO_TWINS_EXIT_DOOR          = 180635,
    GO_SANDWORM_BASE            = 180795,
    GO_GRASP_OF_CTHUN           = 180745,

    AREATRIGGER_TWIN_EMPERORS   = 4047,
    AREATRIGGER_SARTURA         = 4052,
    AREATRIGGER_STOMACH_GROUND  = 4033,
    AREATRIGGER_STOMACH_AIR     = 4034,
    AREATRIGGER_CTHUN_KNOCKBACK = 4036,

    // Whispered on players around the map - NO SNIFF DATA EXISTS IN BROADCAST_TEXTS FOR THESE!!
    // (VMaNGOS script_texts ids, not broadcast text ids)
    SAY_CTHUN_WHISPER_1         = -1531033,
    SAY_CTHUN_WHISPER_2         = -1531034,
    SAY_CTHUN_WHISPER_3         = -1531035,
    SAY_CTHUN_WHISPER_4         = -1531036,
    SAY_CTHUN_WHISPER_5         = -1531037,
    SAY_CTHUN_WHISPER_6         = -1531038,
    SAY_CTHUN_WHISPER_7         = -1531039,
    SAY_CTHUN_WHISPER_8         = -1531040,

    SPELL_SUMMON_PLAYER         = 20477,

    // Cast periodically on players around the instance
    SPELL_WHISPERINGS_CTHUN_1   = 26195,
    SPELL_WHISPERINGS_CTHUN_2   = 26197,
    SPELL_WHISPERINGS_CTHUN_3   = 26198,
    SPELL_WHISPERINGS_CTHUN_4   = 26258,
    SPELL_WHISPERINGS_CTHUN_5   = 26259,
};

// Public interface of the VMaNGOS instance_temple_of_ahnqiraj class (implemented in classic_instance_temple_of_ahnqiraj.cpp).
// Boss scripts get it with GetClassicTempleOfAhnQirajInstance(me) and must null-check the result.
// VMaNGOS -> here:
//   m_pInstance->GetSingleCreatureFromStorage(entry)   -> same
//   m_pInstance->GetSingleGameObjectFromStorage(entry) -> same
//   m_pInstance->GetCreature(guid) / GetGameObject(guid) -> GetCreatureByGuid(guid) / GetGameObjectByGuid(guid)
//   m_pInstance->GetMap()->GetPlayer(guid)             -> GetPlayerByGuid(guid)
//   m_pInstance->GetPlayerInMap(onlyAlive, canBeGM)    -> same
//   m_pInstance->DoResetDoor(guid)                     -> DoCloseDoorOrButton(guid) (TC InstanceScript)
//   everything else keeps its VMaNGOS name.
class ClassicTempleOfAhnQirajInstanceScript : public InstanceScript
{
public:
    explicit ClassicTempleOfAhnQirajInstanceScript(InstanceMap* map) : InstanceScript(map) { }

    // ScriptedInstance storage helpers
    virtual Creature* GetSingleCreatureFromStorage(uint32 entry) const = 0;
    virtual GameObject* GetSingleGameObjectFromStorage(uint32 entry) const = 0;
    virtual Creature* GetCreatureByGuid(ObjectGuid guid) const = 0;
    virtual GameObject* GetGameObjectByGuid(ObjectGuid guid) const = 0;
    virtual Player* GetPlayerByGuid(ObjectGuid guid) const = 0;
    virtual Player* GetPlayerInMap(bool onlyAlive = false, bool canBeGamemaster = true) const = 0;

    // instance_temple_of_ahnqiraj
    virtual void GetRoyalGuardGUIDList(GuidList& lList) const = 0;
    virtual bool TwinsDialogueStartedOrDone() const = 0;

    // C'Thun stomach
    virtual void DoHandleTempleAreaTrigger(uint32 uiTriggerId) = 0;
    virtual void HandleStomachTriggers(Player* pPlayer, AreaTriggerEntry const* pAt) = 0;
    virtual void AddPlayerToStomach(Unit* p) = 0;
    virtual bool PlayerInStomach(Unit* p) = 0;
    virtual bool KillPlayersInStomach() = 0;
};

// Returns the AQ40 instance script of obj's map (nullptr outside AQ40 / when another instance script is bound).
ClassicTempleOfAhnQirajInstanceScript* GetClassicTempleOfAhnQirajInstance(WorldObject const* obj);

#endif // CLASSIC_TEMPLE_OF_AHNQIRAJ_H
