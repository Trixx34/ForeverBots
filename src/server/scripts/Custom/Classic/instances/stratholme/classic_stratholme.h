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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/stratholme.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_STRATHOLME_H
#define CLASSIC_STRATHOLME_H

#include "Define.h"

#define ClassicStratholmeScriptName "classic_instance_stratholme"

uint32 const CLASSIC_STRATHOLME_MAP_ID = 329;

// Instance data ids (VMaNGOS values). States use EncounterState (NOT_STARTED/IN_PROGRESS/FAIL/DONE/SPECIAL),
// which have the same numeric values as the VMaNGOS ones.
enum ClassicStratholmeData : uint32
{
    TYPE_BARON_RUN          = 0,
    TYPE_BARONESS,
    TYPE_NERUB,
    TYPE_PALLID,
    TYPE_RAMSTEIN,
    TYPE_BARON,
    TYPE_CRISTAL_ALL_DIE,
    TYPE_EVENT_AURIUS,
    TYPE_RAMSTEIN_EVENT,
    TYPE_POSTMASTER,
    TYPE_UNFORGIVEN,
    STRAT_MAX_ENCOUNTER,
    TYPE_CRISTAL_DIE,

    DATA_BARON              = 10,                   // GetGuidData
    DATA_YSIDA_TRIGGER      = 11,                   // GetGuidData

    TYPE_SH_QUEST           = 20,
    TYPE_SH_CATHELA         = 21,
    TYPE_SH_GREGOR          = 22,
    TYPE_SH_NEMAS           = 23,
    TYPE_SH_VICAR           = 24,
    TYPE_SH_AELMAR          = 25
};

enum ClassicStratholmeMisc : uint32
{
    QUEST_DEAD_MAN_PLEA                 = 8945,
    SPELL_BARON_ULTIMATUM_45MIN         = 27861,
    SPELL_BARON_ULTIMATUM_10MIN         = 27863,
    SPELL_BARON_ULTIMATUM_5MIN          = 27864,
    SPELL_BARON_ULTIMATUM_1MIN          = 27865,
    SPELL_SUMMON_POSTMASTER             = 24627,

    NPC_UNDEAD_POSTMAN                  = 11142,
    NPC_STRAT_DATHROHAN                 = 10812         // also used as GetGuidData id (VMaNGOS GetData64(NPC_DATHROHAN))
};

#endif
