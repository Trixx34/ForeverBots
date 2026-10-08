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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/swamp_of_sorrows/sunken_temple/sunken_temple.h
// (ScriptDev2 / ScriptDev0 lineage, GPL-2)

#ifndef CLASSIC_SUNKEN_TEMPLE_H
#define CLASSIC_SUNKEN_TEMPLE_H

#include "Define.h"

#define ClassicSTScriptName "classic_instance_sunken_temple"

// Numeric values are the VMaNGOS ones (DB scripts / SmartAI use them through SET_INST_DATA)
enum ClassicSTData : uint32
{
    CLASSIC_ST_MAX_ENCOUNTER        = 6,
    CLASSIC_ST_MAX_STATUES          = 6,
    CLASSIC_ST_MAX_FLAMES           = 4,

    // Don't change types 1,2 and 3 (handled in ACID)
    CLASSIC_ST_TYPE_ATALARION_OBSOLET = 1,
    CLASSIC_ST_TYPE_PROTECTORS_OBS  = 2,
    CLASSIC_ST_TYPE_JAMMALAN_OBS    = 3,

    CLASSIC_ST_TYPE_SECRET_CIRCLE   = 4,
    CLASSIC_ST_TYPE_PROTECTORS      = 5,
    CLASSIC_ST_TYPE_JAMMALAN        = 6,
    CLASSIC_ST_TYPE_MALFURION       = 7,
    CLASSIC_ST_TYPE_AVATAR          = 8,
    CLASSIC_ST_TYPE_ERANIKUS        = 9,
    CLASSIC_ST_TYPE_ETERNAL_FLAME   = 10
};

enum ClassicSTEntries : uint32
{
    NPC_ST_ATALARION                = 8580,
    NPC_ST_DREAMSCYTH               = 5721,
    NPC_ST_WEAVER                   = 5720,
    NPC_ST_JAMMALAN                 = 5710,
    NPC_ST_AVATAR_OF_HAKKAR         = 8443,
    NPC_ST_SHADE_OF_ERANIKUS        = 5709,
    NPC_ST_OGOM                     = 5711,

    // Jammal'an mini-bosses
    NPC_ST_ZOLO                     = 5712,
    NPC_ST_GASHER                   = 5713,
    NPC_ST_LORO                     = 5714,
    NPC_ST_HUKKU                    = 5715,
    NPC_ST_ZULLOR                   = 5716,
    NPC_ST_MIJAN                    = 5717,

    // Avatar of Hakkar mobs
    NPC_ST_SHADE_OF_HAKKAR          = 8440,  // Shade of Hakkar appears when the event starts; will despawn when avatar of hakkar is summoned
    NPC_ST_BLOODKEEPER              = 8438,  // Spawned rarely and contains the hakkari blood -> used to extinguish the flames
    NPC_ST_HAKKARI_MINION           = 8437,  // Npc randomly spawned during the event = trash
    NPC_ST_SUPPRESSOR               = 8497,  // Npc summoned at one of the two doors and moves to the boss

    NPC_ST_MALFURION                = 15362,
    AREATRIGGER_ST_MALFURION        = 4016,

    GO_ST_IDOL_OF_HAKKAR            = 148838, // Appears when atalarion is summoned; this was removed in 4.0.1

    GO_ST_ATALAI_STATUE_1           = 148830,
    GO_ST_ATALAI_STATUE_2           = 148831,
    GO_ST_ATALAI_STATUE_3           = 148832,
    GO_ST_ATALAI_STATUE_4           = 148833,
    GO_ST_ATALAI_STATUE_5           = 148834,
    GO_ST_ATALAI_STATUE_6           = 148835,

    GO_ST_ATALAI_LIGHT              = 148883, // Green light, activates when the correct statue is chosen
    GO_ST_ATALAI_LIGHT_BIG          = 148937, // Big light, used at the altar event

    GO_ST_ATALAI_TRAP_1             = 177484, // Traps triggered if the wrong statue is activated
    GO_ST_ATALAI_TRAP_2             = 177485, // The traps are spawned in DB randomly around the statues
    GO_ST_ATALAI_TRAP_3             = 148837,

    GO_ST_JAMMALAN_BARRIER          = 149431,

    SAY_ST_JAMMALAN_INTRO           = 4490,
    SAY_ST_DREAMSCYTHE_INTRO        = 4364,
    SAY_ST_DREAMSCYTHE_AGGRO        = 6220,
    SAY_ST_ATALALARION_AGGRO        = 6216,
    SAY_ST_ATALALARION_SPAWN        = 4485
};

#endif
