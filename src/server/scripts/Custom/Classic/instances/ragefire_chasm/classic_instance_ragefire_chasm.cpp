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

// Classic 1.60: vanilla Ragefire Chasm (map 389). Replaces TC's instance_ragefire_chasm (Mists of Pandaria encounters
// 1443-1446 that the Classic client does not have) with the four vanilla bosses; all of them run SmartAI, so the boss
// states come from ClassicDungeonEncounterTracker.

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "classic_dungeon_encounter_tracker.h"

namespace
{
constexpr uint32 CLASSIC_RFC_MAP_ID = 389;

// Classic 1.60: TC boss states for the DungeonEncounter.db2 encounters of Ragefire Chasm (map 389), driven by
// ClassicDungeonEncounterTracker (boss kill credit, encounter start / end, instance lock)
enum ClassicRagefireChasmEncounterBosses : uint32
{
    CRFC_ENC_OGGLEFLINT = 0,
    CRFC_ENC_TARAGAMAN = 1,
    CRFC_ENC_JERGOSH = 2,
    CRFC_ENC_BAZZALAN = 3,
    CRFC_ENC_MAX
};

constexpr ClassicDungeonBossEntry ClassicRagefireChasmEncounterBossEntries[] =
{
    { CRFC_ENC_OGGLEFLINT, 11517 }, // Oggleflint
    { CRFC_ENC_TARAGAMAN, 11520 }, // Taragaman the Hungerer
    { CRFC_ENC_JERGOSH, 11518 }, // Jergosh the Invoker
    { CRFC_ENC_BAZZALAN, 11519 } // Bazzalan
};
}

class classic_instance_ragefire_chasm : public InstanceMapScript
{
public:
    classic_instance_ragefire_chasm() : InstanceMapScript("classic_instance_ragefire_chasm", CLASSIC_RFC_MAP_ID) { }

    struct classic_instance_ragefire_chasm_InstanceScript : public InstanceScript
    {
        classic_instance_ragefire_chasm_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders("CRFC");
            SetBossNumber(CRFC_ENC_MAX);
            static constexpr DungeonEncounterData encounters[] =
            {
                { CRFC_ENC_OGGLEFLINT, {{ 2732 }} },
                { CRFC_ENC_TARAGAMAN, {{ 2733 }} },
                { CRFC_ENC_JERGOSH, {{ 2734 }} },
                { CRFC_ENC_BAZZALAN, {{ 2735 }} }
            };
            LoadDungeonEncounterData(encounters);
        }

        ClassicDungeonEncounterTracker _encounterTracker{ *this, ClassicRagefireChasmEncounterBossEntries };

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
        return new classic_instance_ragefire_chasm_InstanceScript(map);
    }
};

void AddSC_classic_instance_ragefire_chasm()
{
    new classic_instance_ragefire_chasm();
}
