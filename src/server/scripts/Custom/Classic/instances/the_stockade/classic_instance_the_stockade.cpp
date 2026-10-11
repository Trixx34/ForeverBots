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

// Classic 1.60: vanilla Stormwind Stockade (map 34). Replaces TC's instance_the_stockade (Cataclysm encounters
// 1144-1146 that the Classic client does not have) with the five vanilla bosses; all of them run SmartAI, so the boss
// states come from ClassicDungeonEncounterTracker.

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "classic_dungeon_encounter_tracker.h"

namespace
{
constexpr uint32 CLASSIC_STOCKADE_MAP_ID = 34;

// Classic 1.60: TC boss states for the DungeonEncounter.db2 encounters of The Stockade (map 34), driven by
// ClassicDungeonEncounterTracker (boss kill credit, encounter start / end, instance lock)
enum ClassicTheStockadeEncounterBosses : uint32
{
    CSTK_ENC_TARGORR = 0,
    CSTK_ENC_KAM_DEEPFURY = 1,
    CSTK_ENC_HAMHOCK = 2,
    CSTK_ENC_DEXTREN_WARD = 3,
    CSTK_ENC_BAZIL_THREDD = 4,
    CSTK_ENC_MAX
};

constexpr ClassicDungeonBossEntry ClassicTheStockadeEncounterBossEntries[] =
{
    { CSTK_ENC_TARGORR, 1696 }, // Targorr the Dread
    { CSTK_ENC_KAM_DEEPFURY, 1666 }, // Kam Deepfury
    { CSTK_ENC_HAMHOCK, 1717 }, // Hamhock
    { CSTK_ENC_DEXTREN_WARD, 1663 }, // Dextren Ward
    { CSTK_ENC_BAZIL_THREDD, 1716 } // Bazil Thredd
};
}

class classic_instance_the_stockade : public InstanceMapScript
{
public:
    classic_instance_the_stockade() : InstanceMapScript("classic_instance_the_stockade", CLASSIC_STOCKADE_MAP_ID) { }

    struct classic_instance_the_stockade_InstanceScript : public InstanceScript
    {
        classic_instance_the_stockade_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders("CSTK");
            SetBossNumber(CSTK_ENC_MAX);
            static constexpr DungeonEncounterData encounters[] =
            {
                { CSTK_ENC_TARGORR, {{ 2756 }} },
                { CSTK_ENC_KAM_DEEPFURY, {{ 2757 }} },
                { CSTK_ENC_HAMHOCK, {{ 2758 }} },
                { CSTK_ENC_DEXTREN_WARD, {{ 2759 }} },
                { CSTK_ENC_BAZIL_THREDD, {{ 2760 }} }
            };
            LoadDungeonEncounterData(encounters);
        }

        ClassicDungeonEncounterTracker _encounterTracker{ *this, ClassicTheStockadeEncounterBossEntries };

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);
            _encounterTracker.OnCreatureCreate(creature);
        }

        void OnCreatureRemove(Creature* creature) override
        {
            InstanceScript::OnCreatureRemove(creature);
            _encounterTracker.OnCreatureRemove(creature);
        }

        void OnUnitDeath(Unit* unit) override
        {
            _encounterTracker.OnUnitDeath(unit);
        }

        void Update(uint32 diff) override
        {
            _encounterTracker.Update(diff);
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_the_stockade_InstanceScript(map);
    }
};

void AddSC_classic_instance_the_stockade()
{
    new classic_instance_the_stockade();
}
