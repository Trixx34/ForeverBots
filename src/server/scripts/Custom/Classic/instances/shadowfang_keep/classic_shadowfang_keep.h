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

// Classic 1.60 port of the data enum of VMaNGOS src/scripts/eastern_kingdoms/silverpine_forest/shadowfang_keep/
// instance_shadowfang_keep.cpp (ScriptDev2 lineage, GPL-2). VMaNGOS has no separate header for this dungeon.

#ifndef CLASSIC_SHADOWFANG_KEEP_H
#define CLASSIC_SHADOWFANG_KEEP_H

#include "Define.h"

#define ClassicShadowfangKeepScriptName "classic_instance_shadowfang_keep"

enum ClassicShadowfangKeepData : uint32
{
    // SetData / GetData types (set by creature_ai_scripts / creature_movement_scripts in the world DB)
    CSFK_TYPE_FREE_NPC           = 1,
    CSFK_TYPE_RETHILGORE         = 2,
    CSFK_TYPE_FENRUS             = 3,
    CSFK_TYPE_NANDOS             = 4,
    CSFK_TYPE_INTRO              = 5,
    CSFK_TYPE_VOIDWALKER         = 6,
    CSFK_MAX_ENCOUNTER           = 6,

    CSFK_NPC_BARON_SILVERLAINE   = 3887,
    CSFK_NPC_CMD_SPRINGVALE      = 4278,
    CSFK_NPC_ASH                 = 3850,
    CSFK_NPC_ADA                 = 3849,
    CSFK_NPC_ARUGAL              = 10000,                   // "Arugal" says intro text
    CSFK_NPC_ARCHMAGE_ARUGAL     = 4275,                    // "Archmage Arugal" does Fenrus event
    CSFK_NPC_FENRUS              = 4274,                    // used to summon Arugal in Fenrus event
    CSFK_NPC_VINCENT             = 4444,                    // Vincent should be "dead" is Arugal is done the intro already
    CSFK_NPC_NANDOS              = 3927,
    CSFK_NPC_WOLF_GUARD          = 3854,                    // Baron Silverlaine and Commander Springvale patrol

    CSFK_GO_COURTYARD_DOOR       = 18895,                   // door to open when talking to NPC's
    CSFK_GO_SORCERER_DOOR        = 18972,                   // door to open when Fenrus the Devourer dies
    CSFK_GO_ARUGAL_DOOR          = 18971,                   // door to open when Wolf Master Nandos dies
    CSFK_GO_ARUGAL_FOCUS         = 18973,                   // this generates the lightning visual in the Fenrus event

    CSFK_SOUND_FENRUS_AGGRO      = 6017,                    // Fenrus howls on aggro A_FenrusAggro in sound entries
};

#endif
