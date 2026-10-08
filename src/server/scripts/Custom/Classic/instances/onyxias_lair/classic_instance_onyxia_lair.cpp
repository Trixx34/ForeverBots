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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/dustwallow_marsh/onyxias_lair/instance_onyxia_lair.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "classic_onyxias_lair.h"
#include <vector>

namespace
{
// DungeonEncounter.db2 id used by TC for Onyxia; only registered when present in the client data
constexpr uint32 CLASSIC_OL_DUNGEON_ENCOUNTER_ONYXIA = 1084;
}

class classic_instance_onyxia_lair : public InstanceMapScript
{
public:
    classic_instance_onyxia_lair() : InstanceMapScript(ClassicOnyxiaLairScriptName, CLASSIC_OL_MAP_ID) { }

    struct classic_instance_onyxia_lair_InstanceScript : public InstanceScript
    {
        classic_instance_onyxia_lair_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(ClassicOnyxiaLairDataHeader);
            SetBossNumber(CLASSIC_OL_MAX_ENCOUNTER);

            if (sDungeonEncounterStore.LookupEntry(CLASSIC_OL_DUNGEON_ENCOUNTER_ONYXIA))
            {
                DungeonEncounterData const encounters[] =
                {
                    { CLASSIC_OL_DATA_ONYXIA_EVENT, {{ CLASSIC_OL_DUNGEON_ENCOUNTER_ONYXIA }} }
                };
                LoadDungeonEncounterData(encounters);
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case CLASSIC_OL_DATA_ONYXIA_EVENT:
                    return GetBossState(CLASSIC_OL_DATA_ONYXIA_EVENT);
                default:
                    break;
            }
            return 0;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case CLASSIC_OL_DATA_ONYXIA_EVENT:
                    // TC blocks DONE -> other state transitions (VMaNGOS would overwrite them in memory only)
                    if (GetBossState(CLASSIC_OL_DATA_ONYXIA_EVENT) == DONE && data != DONE)
                        return;
                    SetBossState(CLASSIC_OL_DATA_ONYXIA_EVENT, EncounterState(data));
                    break;
                default:
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case CLASSIC_OL_GO_WHELP_SPAWNER:
                    // VMaNGOS casts directly in OnObjectCreate; defer to the next instance update so the GO is fully in world
                    _pendingWhelpSpawners.push_back(go->GetGUID());
                    break;
                default:
                    break;
            }
        }

        void Update(uint32 /*diff*/) override
        {
            if (_pendingWhelpSpawners.empty())
                return;

            std::vector<ObjectGuid> spawners;
            spawners.swap(_pendingWhelpSpawners);
            for (ObjectGuid const& guid : spawners)
                if (GameObject* go = instance->GetGameObject(guid))
                    go->CastSpell(go->GetPosition(), CLASSIC_OL_SPELL_SUMMON_WHELP, true);
        }

    private:
        std::vector<ObjectGuid> _pendingWhelpSpawners;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_onyxia_lair_InstanceScript(map);
    }
};

void AddSC_classic_instance_onyxia_lair()
{
    new classic_instance_onyxia_lair();
}
