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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/westfall/deadmines/deadmines.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_DEADMINES_H
#define CLASSIC_DEADMINES_H

#include "Define.h"

#define ClassicDeadminesScriptName "classic_instance_deadmines"

enum ClassicDeadminesData : uint32
{
    CDM_MAX_ENCOUNTER           = 1,

    CDM_TYPE_DEFIAS_ENDDOOR     = 1,
    CDM_DATA_DEFIAS_DOOR        = 2,

    CDM_INST_SAY_ALARM1         = 1148,
    CDM_INST_SAY_ALARM2         = 1149,

    /** Doors which need to be opened automatically once the boss before died */
    CDM_GO_DOOR1                = 13965,
    CDM_GO_DOOR2                = 16400,
    CDM_GO_DOOR3                = 16399,

    CDM_GUN_POWDER_EVENT        = 5000,

    CDM_GO_DOOR_LEVER           = 101833,
    CDM_GO_IRON_CLAD            = 16397,
    CDM_GO_DEFIAS_CANNON        = 16398,
    CDM_GO_DMF_CHEST            = 180024,
    CDM_NPC_MR_SMITE            = 646,
    CDM_NPC_PIRATE              = 657,
    CDM_NPC_SNEED               = 643,
    CDM_NPC_RHAHKZOR            = 644,
    CDM_NPC_GILDNID             = 1763,

    CDM_QUEST_FORTUNE_AWAITS    = 7938
};

#endif
