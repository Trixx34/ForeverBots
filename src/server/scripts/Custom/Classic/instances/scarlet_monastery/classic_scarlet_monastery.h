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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/tirisfal_glades/scarlet_monastery/scarlet_monastery.h
// (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_SCARLET_MONASTERY_H
#define CLASSIC_SCARLET_MONASTERY_H

#include "Define.h"

#define ClassicSMScriptName "classic_instance_scarlet_monastery"

// Numeric values are the VMaNGOS ones (DB scripts / SmartAI use them through SET_INST_DATA)
enum ClassicSMData : uint32
{
    CLASSIC_SM_MAX_ENCOUNTER            = 2,

    CLASSIC_SM_TYPE_MOGRAINE_AND_WHITE  = 1,    // TYPE_MOGRAINE_AND_WHITE_EVENT
    CLASSIC_SM_TYPE_ASHBRINGER          = 2,    // TYPE_ASHBRINGER_EVENT

    // GetGuidData (VMaNGOS GetData64)
    CLASSIC_SM_DATA_MOGRAINE            = 2,
    CLASSIC_SM_DATA_WHITEMANE           = 3,
    CLASSIC_SM_DATA_DOOR_WHITEMANE      = 4,
    CLASSIC_SM_DATA_VORREL              = 5,
    CLASSIC_SM_DATA_DOOR_CHAPEL         = 6,

    // SetGuidData: no VMaNGOS equivalent (VMaNGOS used the OnCreatureSpellHit instance hook, which TC does not have).
    // Value = the player that hit Commander Mograine with spell 28441 (AB Effect 000).
    CLASSIC_SM_DATA_ASHBRINGER_MOGRAINE_HIT = 100
};

#endif
