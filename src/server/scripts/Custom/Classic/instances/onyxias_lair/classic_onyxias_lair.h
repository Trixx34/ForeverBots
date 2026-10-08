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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/dustwallow_marsh/onyxias_lair/instance_onyxia_lair.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_ONYXIAS_LAIR_H
#define CLASSIC_ONYXIAS_LAIR_H

#include "Define.h"

#define ClassicOnyxiaLairScriptName "classic_instance_onyxia_lair"
#define ClassicOnyxiaLairDataHeader "COL"

enum ClassicOnyxiaLairData : uint32
{
    // boss ids (InstanceScript boss states)
    CLASSIC_OL_DATA_ONYXIA_EVENT    = 0,   // VMaNGOS DATA_ONYXIA_EVENT
    CLASSIC_OL_MAX_ENCOUNTER        = 1,

    CLASSIC_OL_MAP_ID               = 249
};

enum ClassicOnyxiaLairEntries : uint32
{
    CLASSIC_OL_NPC_ONYXIA           = 10184,
    CLASSIC_OL_NPC_ONYXIAN_WHELP    = 11262,
    CLASSIC_OL_NPC_ONYXIAN_WARDER   = 12129,

    CLASSIC_OL_GO_WHELP_SPAWNER     = 176510,
    CLASSIC_OL_SPELL_SUMMON_WHELP   = 17646
};

#endif // CLASSIC_ONYXIAS_LAIR_H
