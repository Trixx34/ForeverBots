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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/ashenvale/blackfathom_deeps/blackfathom_deeps.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_BLACKFATHOM_DEEPS_H
#define CLASSIC_BLACKFATHOM_DEEPS_H

#include "Define.h"

#define ClassicBlackfathomDeepsScriptName "classic_instance_blackfathom_deeps"

enum ClassicBlackfathomDeepsData : uint32
{
    // GetGuidData (VMaNGOS GetData64) ids
    CBFD_DATA_SHRINE1                = 1,
    CBFD_DATA_SHRINE2                = 2,
    CBFD_DATA_SHRINE3                = 3,
    CBFD_DATA_SHRINE4                = 4,
    CBFD_DATA_TWILIGHT_LORD_KELRIS   = 5,
    CBFD_DATA_SHRINE_OF_GELIHAST     = 6,
    CBFD_DATA_ALTAR_OF_THE_DEEPS     = 7,
    CBFD_DATA_MAINDOOR               = 8,

    // SetData / GetData types (TYPE_KELRIS is set by Kelris' creature_ai_scripts, TYPE_AQUANIS by the Fathom Core GO script)
    CBFD_TYPE_KELRIS                 = 10,
    CBFD_TYPE_SHRINE                 = 11,
    CBFD_TYPE_AQUANIS                = 12,

    CBFD_NPC_TWILIGHT_LORD_KELRIS    = 4832,
    CBFD_NPC_BARON_AQUANIS           = 12876,
    CBFD_GO_FATHOM_STONE             = 177964,
    CBFD_GO_SHRINE_OF_GELIHAST       = 103015,
    CBFD_GO_ALTAR_OF_THE_DEEPS       = 103016,

    // Shrine event
    CBFD_NPC_AKUMAI_SERVANT          = 4978,
    CBFD_NPC_AKUMAI_SNAPJAW          = 4825,
    CBFD_NPC_MURKSHALLOW_SNAPCLAW    = 4815,
    CBFD_NPC_MURKSHALLOW_SOFTSHELL   = 4977,

    CBFD_GO_PORTAL_DOOR              = 21117,
    CBFD_GO_SHRINE_1                 = 21118,
    CBFD_GO_SHRINE_2                 = 21119,
    CBFD_GO_SHRINE_3                 = 21120,
    CBFD_GO_SHRINE_4                 = 21121,

    CBFD_ENCOUNTER_KELRIS            = 0,
    CBFD_ENCOUNTER_SHRINE            = 1,
    CBFD_ENCOUNTER_AQUANIS           = 2,
    CBFD_MAX_ENCOUNTER               = 3,
};

#endif
