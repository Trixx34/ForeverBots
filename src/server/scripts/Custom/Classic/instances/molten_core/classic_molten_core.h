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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/molten_core.h (GPL-2)

#ifndef CLASSIC_MOLTEN_CORE_H
#define CLASSIC_MOLTEN_CORE_H

#include "Define.h"

#define ClassicMoltenCoreScriptName "classic_instance_molten_core"
#define ClassicMoltenCoreDataHeader "CMC"

// Data ids keep the VMaNGOS values. TYPE_* 0..9 are also the InstanceScript boss ids.
enum ClassicMoltenCoreData : uint32
{
    CLASSIC_MC_TYPE_SULFURON             = 0,
    CLASSIC_MC_TYPE_GEDDON               = 1,
    CLASSIC_MC_TYPE_SHAZZRAH             = 2,
    CLASSIC_MC_TYPE_GOLEMAGG             = 3,
    CLASSIC_MC_TYPE_GARR                 = 4,
    CLASSIC_MC_TYPE_MAGMADAR             = 5,
    CLASSIC_MC_TYPE_GEHENNAS             = 6,
    CLASSIC_MC_TYPE_LUCIFRON             = 7,
    CLASSIC_MC_TYPE_MAJORDOMO            = 8,
    CLASSIC_MC_TYPE_RAGNAROS             = 9,
    CLASSIC_MC_MAX_ENCOUNTER             = 10,

    // GetGuidData (VMaNGOS GetData64)
    CLASSIC_MC_DATA_SULFURON             = 11,
    CLASSIC_MC_DATA_GOLEMAGG             = 12,
    CLASSIC_MC_DATA_GARR                 = 13,
    CLASSIC_MC_DATA_MAJORDOMO            = 14,

    CLASSIC_MC_DATA_RUNE_ACTIVE_0        = 16,
    CLASSIC_MC_DATA_RUNE_ACTIVE_1        = 17,
    CLASSIC_MC_DATA_RUNE_ACTIVE_2        = 18,
    CLASSIC_MC_DATA_RUNE_ACTIVE_3        = 19,
    CLASSIC_MC_DATA_RUNE_ACTIVE_4        = 20,
    CLASSIC_MC_DATA_RUNE_ACTIVE_5        = 21,
    CLASSIC_MC_DATA_RUNE_ACTIVE_6        = 22,

    CLASSIC_MC_DATA_DOMO_SPAWNED         = 23,

    CLASSIC_MC_MAP_ID                    = 409
};

enum ClassicMoltenCoreEntries : uint32
{
    // Npcs
    CLASSIC_MC_NPC_LUCIFRON              = 12118,
    CLASSIC_MC_NPC_MAGMADAR              = 11982,
    CLASSIC_MC_NPC_GEHENNAS              = 12259,
    CLASSIC_MC_NPC_GARR                  = 12057,
    CLASSIC_MC_NPC_GEDDON                = 12056,
    CLASSIC_MC_NPC_SHAZZRAH              = 12264,
    CLASSIC_MC_NPC_GOLEMAGG              = 11988,
    CLASSIC_MC_NPC_SULFURON              = 12098,
    CLASSIC_MC_NPC_MAJORDOMO             = 12018,
    CLASSIC_MC_NPC_RAGNAROS              = 11502,
    CLASSIC_MC_NPC_LAVA_SURGER           = 12101,
    CLASSIC_MC_NPC_LAVA_ANNIHILATOR      = 11665,
    CLASSIC_MC_NPC_FIRELORD              = 11668,
    CLASSIC_MC_NPC_LAVA_SPAWN            = 12265,
    CLASSIC_MC_NPC_CORE_HOUND            = 11671,
    CLASSIC_MC_NPC_ANCIENT_CORE_HOUND    = 11673,
    CLASSIC_MC_NPC_FLAMEWAKER            = 11661,
    CLASSIC_MC_NPC_FLAMEWAKER_PRIEST     = 11662,
    CLASSIC_MC_NPC_FLAMEWAKER_PROTECTOR  = 12119,
    CLASSIC_MC_NPC_FIRESWORN             = 12099,
    CLASSIC_MC_NPC_CORE_RAGER            = 11672,
    CLASSIC_MC_NPC_FLAMEWAKER_HEALER     = 11663,
    CLASSIC_MC_NPC_FLAMEWAKER_ELITE      = 11664,

    // Objects
    CLASSIC_MC_RUNE_MAGMADAR             = 176956,
    CLASSIC_MC_RUNE_GEHENNAS             = 176957,
    CLASSIC_MC_RUNE_GEDDON               = 176952,
    CLASSIC_MC_RUNE_GARR                 = 176955,
    CLASSIC_MC_RUNE_SHAZZRAH             = 176953,
    CLASSIC_MC_RUNE_GOLEMAGG             = 176954,
    CLASSIC_MC_RUNE_SULFURON             = 176951,
    CLASSIC_MC_RUNE_MAJORDOMO            = 179703,   // Cache of the Firelord

    CLASSIC_MC_GO_HOT_COALS              = 177000,

    // Majordomo
    CLASSIC_MC_SAY_RUNES_DESTROYED       = 7566,
    CLASSIC_MC_FACTION_DOMO_FRIENDLY     = 1080,

    // Misc
    CLASSIC_MC_MAX_LAVA_SPAWNS           = 18
};

#endif // CLASSIC_MOLTEN_CORE_H
