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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackwing_lair/blackwing_lair.h (GPL-2)

#ifndef CLASSIC_BLACKWING_LAIR_H
#define CLASSIC_BLACKWING_LAIR_H

#include "Define.h"

#define ClassicBlackwingLairScriptName "classic_instance_blackwing_lair"
#define ClassicBlackwingLairDataHeader "CBWL"

// VMaNGOS uses two overlapping id spaces: GetData/SetData(TYPE_*) and GetData64(DATA_*).
// Port: TYPE_* 0..7 are the InstanceScript boss ids (boss states), TYPE_VAEL_EVENT / TYPE_SCEPTER_RUN are persistent values.
// VMaNGOS GetData64(DATA_*) guids -> GetGuidData(DATA_*); VMaNGOS GetData64(DATA_*) numbers -> GetData(DATA_*).
enum ClassicBlackwingLairType : uint32
{
    CLASSIC_BWL_TYPE_RAZORGORE              = 0,
    CLASSIC_BWL_TYPE_VAELASTRASZ            = 1,
    CLASSIC_BWL_TYPE_LASHLAYER              = 2,
    CLASSIC_BWL_TYPE_FIREMAW                = 3,
    CLASSIC_BWL_TYPE_EBONROC                = 4,
    CLASSIC_BWL_TYPE_FLAMEGOR               = 5,
    CLASSIC_BWL_TYPE_CHROMAGGUS             = 6,
    CLASSIC_BWL_TYPE_NEFARIAN               = 7,
    CLASSIC_BWL_MAX_BOSS_ENCOUNTER          = 8,        // boss states 0..7
    CLASSIC_BWL_TYPE_VAEL_EVENT             = 8,
    CLASSIC_BWL_TYPE_SCEPTER_RUN            = 9,
    CLASSIC_BWL_MAX_ENCOUNTER               = 10
};

enum ClassicBlackwingLairData : uint32
{
    // GetGuidData
    CLASSIC_BWL_DATA_RAZORGORE_GUID         = 0,
    CLASSIC_BWL_DATA_VAELASTRASZ_GUID       = 1,
    CLASSIC_BWL_DATA_LASHLAYER_GUID         = 2,
    CLASSIC_BWL_DATA_FIREMAW_GUID           = 3,
    CLASSIC_BWL_DATA_EBONROC_GUID           = 4,
    CLASSIC_BWL_DATA_FLAMEGOR_GUID          = 5,
    CLASSIC_BWL_DATA_CHROMAGGUS_GUID        = 6,
    CLASSIC_BWL_DATA_NEFARIUS_GUID          = 7,
    CLASSIC_BWL_DATA_NEFARIAN_GUID          = 8,
    CLASSIC_BWL_DATA_GRETOK_GUID            = 9,
    CLASSIC_BWL_DATA_TRIGGER_GUID           = 10,
    CLASSIC_BWL_DATA_ORB_DOMINATION_GUID    = 11,
    // GetData / SetData (numbers)
    CLASSIC_BWL_DATA_EGG                    = 12,
    CLASSIC_BWL_DATA_HOW_EGG                = 13,
    CLASSIC_BWL_DATA_CHROM_BREATH           = 14,
    CLASSIC_BWL_DATA_NEF_COLOR              = 15,
    // GetGuidData (doors)
    CLASSIC_BWL_DATA_DOOR_RAZORGORE_ENTER   = 16,
    CLASSIC_BWL_DATA_DOOR_RAZORGORE_EXIT    = 17,
    CLASSIC_BWL_DATA_DOOR_VAELASTRASZ       = 18,
    CLASSIC_BWL_DATA_DOOR_LASHLAYER         = 19,
    CLASSIC_BWL_DATA_DOOR_CHROMAGGUS_ENTER  = 20,
    CLASSIC_BWL_DATA_DOOR_CHROMAGGUS_EXIT   = 21,
    CLASSIC_BWL_DATA_DOOR_CHROMAGGUS_SIDE   = 22,
    CLASSIC_BWL_DATA_DOOR_NEFARIAN          = 23,
    // GetGuidData / SetGuidData (player guid)
    CLASSIC_BWL_DATA_SCEPTER_CHAMPION       = 24,
    // GetData / SetData (milliseconds)
    CLASSIC_BWL_DATA_SCEPTER_RUN_TIME       = 25,
    CLASSIC_BWL_MAX_DATAS                   = 26,

    // VMaNGOS instance SetData(GOSSIP_OPTION_NEFARIUS) (gossip_scripts 604500) -> starts Nefarius' event
    CLASSIC_BWL_GOSSIP_OPTION_NEFARIUS      = 6045,

    CLASSIC_BWL_MAP_ID                      = 469
};

enum ClassicBlackwingLairActions : int32
{
    CLASSIC_BWL_ACTION_NEFARIUS_START       = 1
};

enum ClassicBlackwingLairEntries : uint32
{
    CLASSIC_BWL_GO_DRAKONID_BONES           = 179804,

    CLASSIC_BWL_SPELL_POSSESS               = 19832,
    CLASSIC_BWL_SPELL_POSSESS_VISUAL        = 23014,

    CLASSIC_BWL_NPC_RAZORGORE               = 12435,
    CLASSIC_BWL_NPC_VAELASTRASZ             = 13020,
    CLASSIC_BWL_NPC_LASHLAYER               = 12017,
    CLASSIC_BWL_NPC_FIREMAW                 = 11983,
    CLASSIC_BWL_NPC_EBONROC                 = 14601,
    CLASSIC_BWL_NPC_FLAMEGOR                = 11981,
    CLASSIC_BWL_NPC_CHROMAGGUS              = 14020,
    CLASSIC_BWL_NPC_NEFARIAN                = 11583,
    CLASSIC_BWL_NPC_LORD_NEFARIAN           = 10162,    // Lord Victor Nefarius
    CLASSIC_BWL_NPC_ORB_OF_DOMINATION       = 14453,
    CLASSIC_BWL_NPC_GRETHOK_THE_CONTROLLER  = 12557,
    CLASSIC_BWL_NPC_BLACKWING_GUARDSMAN     = 14456,
    CLASSIC_BWL_NPC_BLACKWING_LEGGIONAIRE   = 12416,
    CLASSIC_BWL_NPC_BLACKWING_MAGE          = 12420,
    CLASSIC_BWL_NPC_DEATH_TALON_DRAGONSPAWN = 12422,
    CLASSIC_BWL_NPC_DEATH_TALON_CAPTAIN     = 12467,
    CLASSIC_BWL_NPC_DEATH_TALON_SEETHER     = 12464,
    CLASSIC_BWL_NPC_DEATH_TALON_WYRMKIN     = 12465,
    CLASSIC_BWL_NPC_DEATH_TALON_FLAMESCALE  = 12463,
    CLASSIC_BWL_NPC_DEATH_TALON_HATCHER     = 12468,
    CLASSIC_BWL_NPC_BLACKWING_TASKMASTER    = 12458,
    CLASSIC_BWL_NPC_BLACKWING_TECHNICIAN    = 13996,
    CLASSIC_BWL_NPC_CORRUPTED_GREEN_WHELP   = 14023,
    CLASSIC_BWL_NPC_CORRUPTED_RED_WHELP     = 14022,
    CLASSIC_BWL_NPC_CORRUPTED_BLUE_WHELP    = 14024,
    CLASSIC_BWL_NPC_CORRUPTED_BRONZE_WHELP  = 14025,
    CLASSIC_BWL_NPC_BLACKWING_WARLOCK       = 12459,
    CLASSIC_BWL_NPC_DEATH_TALON_OVERSEER    = 12461,
    CLASSIC_BWL_NPC_BLACKWING_SPELLBINDER   = 12457,
    CLASSIC_BWL_NPC_DEATH_TALON_WYRMGUARD   = 12460,
    CLASSIC_BWL_NPC_BRONZE_DRAKANOID        = 14263,
    CLASSIC_BWL_NPC_BLUE_DRAKANOID          = 14261,
    CLASSIC_BWL_NPC_RED_DRAKANOID           = 14264,
    CLASSIC_BWL_NPC_GREEN_DRAKANOID         = 14262,
    CLASSIC_BWL_NPC_BLACK_DRAKANOID         = 14265,
    CLASSIC_BWL_NPC_CHROMATIC_DRAKANOID     = 14302,
    CLASSIC_BWL_NPC_BONE_CONSTRUCT          = 14605,

    CLASSIC_BWL_QUEST_NEFARIUS_CORRUPTION   = 8730,

    // VMaNGOS FACTION_MONSTER / FACTION_FRIENDLY (prefixed: TC headers define FACTION_*)
    CLASSIC_BWL_FACTION_MONSTER             = 14,
    CLASSIC_BWL_FACTION_FRIENDLY            = 35,

    CLASSIC_BWL_RAZORGORE_MAX_HEALTH_DURING_POSESSION = 450000
};

#endif // CLASSIC_BLACKWING_LAIR_H
