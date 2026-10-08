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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/tanaris/zulfarrak/zulfarrak.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_ZULFARRAK_H
#define CLASSIC_ZULFARRAK_H

#include "Define.h"

#define ClassicZFScriptName "classic_instance_zulfarrak"

// Numeric values are the VMaNGOS ones (DB scripts / SmartAI use them through SET_INST_DATA)
enum ClassicZFEntries : uint32
{
    ENTRY_ZF_ZUMRAH     = 7271,
    ENTRY_ZF_BLY        = 7604,
    ENTRY_ZF_RAVEN      = 7605,
    ENTRY_ZF_ORO        = 7606,
    ENTRY_ZF_WEEGLI     = 7607,
    ENTRY_ZF_MURTA      = 7608,
    ENTRY_ZF_UKORZ      = 7267,

    GO_ZF_END_DOOR      = 146084,

    // SetData / GetData types
    EVENT_ZF_PYRAMID    = 1,
    EVENT_ZF_GAHZRILLA  = 2,
    EVENT_ZF_END_DOOR   = 3,
    EVENT_ZF_ZUMRAH     = 4,
    EVENT_ZF_ANTUSUL    = 5
};

enum ClassicZFPyramidPhases : uint32
{
    PYRAMID_ZF_NOT_STARTED,         // default
    PYRAMID_ZF_CAGES_OPEN,          // happens in GO hello for cages
    PYRAMID_ZF_ARRIVED_AT_STAIR,    // happens in Weegli's movementinform
    PYRAMID_ZF_WAVE_1,
    PYRAMID_ZF_PRE_WAVE_2,
    PYRAMID_ZF_WAVE_2,
    PYRAMID_ZF_PRE_WAVE_3,
    PYRAMID_ZF_WAVE_3,
    PYRAMID_ZF_KILLED_ALL_TROLLS
};

#endif
