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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/desolace/maraudon/maraudon.h (Nostalrius / VMaNGOS, GPL-2)

#ifndef CLASSIC_MARAUDON_H
#define CLASSIC_MARAUDON_H

#include "Define.h"

#define ClassicMaraudonScriptName "classic_instance_maraudon"

// Numeric values are the VMaNGOS ones (DB scripts / SmartAI use them through SET_INST_DATA)
enum ClassicMaraudonData : uint32
{
    CLASSIC_MARA_TYPE_LARVA_SPEWER      = 0,
    CLASSIC_MARA_TYPE_CELEBRAS          = 1,
    CLASSIC_MARA_MAX_ENCOUNTER          = 2,

    NPC_MARA_SPEWED_LARVA               = 13533,
    NPC_MARA_CELEBRAS_REDEEMED          = 13716,

    GO_MARA_HEALED_CELEBRIAN_VINE       = 178904,
    GO_MARA_VYLESTEM_VINE               = 178905,

    GO_MARA_LARVA_SPEWER                = 178559
};

#endif
