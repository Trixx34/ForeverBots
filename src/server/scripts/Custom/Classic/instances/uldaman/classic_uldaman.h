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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/uldaman/uldaman.h (ScriptDev2 / ScriptDev0 lineage, GPL-2)

#ifndef CLASSIC_ULDAMAN_H
#define CLASSIC_ULDAMAN_H

#include "Define.h"

#define ClassicUldamanScriptName "classic_instance_uldaman"
#define ClassicUldamanDataHeader "CULD"

uint32 const CLASSIC_ULDAMAN_MAP_ID = 70;

enum ClassicUldamanData
{
    // Encounters
    ULDAMAN_ENCOUNTER_IRONAYA_DOOR  = 0,
    ULDAMAN_ENCOUNTER_STONE_KEEPERS = 1,
    ULDAMAN_ENCOUNTER_ARCHAEDAS     = 2,
    ULDAMAN_MAX_ENCOUNTER           = 3,

    // Data
    DATA_KEEPERS_ALTAR   = 10,
    DATA_ANCIENT_DOOR    = 11,
    DATA_ARCHAEDAS_ALTAR = 12
};

// VMaNGOS SetData64() types -> SetGuidData()
enum ClassicUldamanSetGuidData
{
    SET_DATA64_IRONAYA_WAKER        = 0,    // Ironaya's waker
    SET_DATA64_UNFREEZE             = 1,    // Unfreeze this creature
    SET_DATA64_FREEZE               = 2     // Freeze this creature
};

// VMaNGOS GetData64() types -> GetGuidData()
enum ClassicUldamanGetGuidData
{
    DATA64_IRONAYA_WAKER            = 0,
    DATA64_VAULT_WARDER_1           = 1,
    DATA64_VAULT_WARDER_2           = 2,
    DATA64_EARTHEN_GUARDIAN_1       = 5,
    DATA64_EARTHEN_GUARDIAN_2       = 6,
    DATA64_EARTHEN_GUARDIAN_3       = 7,
    DATA64_EARTHEN_GUARDIAN_4       = 8,
    DATA64_EARTHEN_GUARDIAN_5       = 9,
    DATA64_EARTHEN_GUARDIAN_6       = 10,
    DATA64_ARCHAEDAS                = 11,
    DATA64_VAULT_FURNITURE_1        = 12,
    DATA64_VAULT_FURNITURE_2        = 13
};

enum ClassicUldamanGameObjects
{
    GO_ALTAR_ARCHAEDAS                 = 133234,
    GO_ALTAR_KEEPERS                   = 130511,
    GO_IRONAYA_SEAL_DOOR               = 124372,
    GO_ARCHAEDAS_TEMPLE_DOOR           = 141869,
    GO_ALTAR_OF_THE_KEEPER_TEMPLE_DOOR = 124367,
    GO_ANCIENT_VAULT_DOOR              = 124369,
    GO_KEYSTONE                        = 124371,
    GO_ANCIENT_TREASURE                = 141979
};

enum ClassicUldamanCreatures
{
    NPC_ARCHAEDAS          = 2748,
    NPC_STONE_KEEPER       = 4857,
    NPC_EARTHEN_HALLSHAPER = 7077,
    NPC_EARTHEN_GUARDIAN   = 7076,
    NPC_IRONAYA            = 7228,
    NPC_EARTHEN_CUSTODIAN  = 7309,
    NPC_VAULT_WARDER       = 10120
};

enum ClassicUldamanSpells
{
    // Archaedas
    SPELL_GROUND_TREMOR           = 6524,
    // Visuals for stone npcs
    SPELL_STONE_DWARF_AWAKEN      = 10254,
    SPELL_STONED                  = 10255,
    SPELL_AWAKEN_EARTHEN_DWARF    = 10259,
    SPELL_ARCHAEDAS_AWAKEN        = 10347,
    SPELL_AWAKEN_EARTHEN_GUARDIAN = 10252,
    SPELL_AWAKEN_VAULT_WARDER     = 10258,
    /* spells cast from summoning ritual altars, start event scripts
    SPELL_ULDMAN_SUB_BOSS_AGGRO  = 11568, event 2228
    SPELL_ULDMAN_BOSS_AGGRO      = 10340, event 2268*/
    // Earthen Custodians
    SPELL_RECONSTRUCT             = 10260,
    // Ironaya
    SPELL_ARCINGSMASH             = 8374,
    SPELL_KNOCKAWAY               = 10101,
    SPELL_WSTOMP                  = 11876,
    // Jadespine Basilisk
    SPELL_CRYSTALLINE_SLUMBER     = 3636,
    // Vault Warder
    SPELL_TRAMPLE                 = 5568,
    // Others
    SPELL_SELF_DESTRUCT           = 9874,
    SPELL_ALTAR_SUMMONING_VISUAL  = 11206

    /* For information, used in db scripts
    NPC_OBSIDIAN_SENTINEL         = 7023,
    SPELL_SPELL_REFLECTION        = 9941,
    SPELL_SUMMON_OBSIDIAN_SHARD   = 10061,
    SPELL_SPLINTERED_OBSIDIAN     = 10072,*/
};

// VMaNGOS DB event_scripts 2228 / 2268 (command 37 SET_INST_DATA), handled in the instance script's ProcessEvent
enum ClassicUldamanEvents
{
    EVENT_ULDAMAN_ALTAR_OF_THE_KEEPERS = 2228,
    EVENT_ULDAMAN_ALTAR_OF_ARCHAEDAS   = 2268
};

#endif
