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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/razorfen_downs/razorfen_downs.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_RAZORFEN_DOWNS_H
#define CLASSIC_RAZORFEN_DOWNS_H

#include "Define.h"

#define ClassicRFDScriptName "classic_instance_razorfen_downs"
#define ClassicRFDDataHeader "CRFD"

uint32 const CLASSIC_RFD_MAP_ID = 129;

enum ClassicRFDData
{
    BOSS_TUTEN_KASH,
    DATA_GONG_WAVES,
    EXTINGUISH_FIRES
};

enum ClassicRFDData64
{
    DATA_GONG
};

enum ClassicRFDGameObjects
{
    GO_GONG                                     = 148917,
    GO_IDOL_CUP_FIRE                            = 151952
};

enum ClassicRFDCreatures
{
    CREATURE_TOMB_FIEND                         = 7349,
    CREATURE_TOMB_REAVER                        = 7351,
    CREATURE_TUTEN_KASH                         = 7355
};

#endif
