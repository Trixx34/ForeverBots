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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/wailing_caverns/def_wailing_caverns.h (ScriptDev2 lineage, GPL-2)

#ifndef CLASSIC_WAILING_CAVERNS_H
#define CLASSIC_WAILING_CAVERNS_H

#include "Define.h"

#define ClassicWailingCavernsScriptName "classic_instance_wailing_caverns"

enum ClassicWailingCavernsData : uint32
{
    // SetData / GetData types (bosses set them from creature_ai_scripts in the world DB)
    CWC_TYPE_ANACONDRA                  = 0,
    CWC_TYPE_COBRAHN                    = 1,
    CWC_TYPE_PYTHAS                     = 2,
    CWC_TYPE_SERPENTIS                  = 3,
    CWC_TYPE_DISCIPLE                   = 4,
    CWC_TYPE_MUTANUS                    = 5,
    CWC_MAX_ENCOUNTER                   = 6,

    // GetGuidData (VMaNGOS GetData64)
    CWC_DATA_NARALEX                    = 6,

    CWC_YELL_AFTER_GOSSIP               = 2101,
    CWC_SERPENTIS_YELL                  = 2102,

    CWC_GO_DMF_CHEST                    = 180055,

    CWC_GOSSIP_DISCIPLE_SPECIAL         = 202,

    CWC_QUEST_FORTUNE_AWAITS            = 7944,

    CWC_NPC_KRESH                       = 3653,
    CWC_NPC_LADY_ANACONDRA              = 3671,
    CWC_NPC_LORD_SERPENTIS              = 3673,
    CWC_NPC_DISCIPLE_OF_NARALEX         = 3678,
    CWC_NPC_NARALEX                     = 3679,
    CWC_NPC_DRUID_OF_THE_FANG           = 3840,
};

#endif
