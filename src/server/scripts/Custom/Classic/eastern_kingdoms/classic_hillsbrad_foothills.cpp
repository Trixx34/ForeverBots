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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/hillsbrad_foothills/hillsbrad_foothills.cpp (Nostalrius, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "QuaternionData.h"
#include "TemporarySummon.h"
#include <list>

/*######
## go_helcular_s_grave
######*/

enum HelcularsGrave
{
    NPC_HELCULAR = 2433
};

struct classic_go_helcular_s_grave : public GameObjectAI
{
    classic_go_helcular_s_grave(GameObject* go) : GameObjectAI(go) { }

    bool CheckHelcularSpawned() const
    {
        return ObjectAccessor::GetCreature(*me, _helcularGuid) != nullptr;
    }

    void OnQuestReward(Player* /*player*/, Quest const* /*quest*/, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (!CheckHelcularSpawned())
            if (Creature* helcular = me->SummonCreature(NPC_HELCULAR, -741.982f, -621.186f, 18.3853f, 2.05043f, TEMPSUMMON_DEAD_DESPAWN, 0s))
                _helcularGuid = helcular->GetGUID();
    }

private:
    ObjectGuid _helcularGuid;
};

/*######
## go_dusty_rug
######*/

enum DustyRug
{
    NPC_CAPTURED_FARMER     = 2284,

    GO_TAINTED_KEG          = 1729,
    GO_TAINTED_KEG_SMOKE    = 1730
};

struct classic_go_dusty_rug : public GameObjectAI
{
    classic_go_dusty_rug(GameObject* go) : GameObjectAI(go), _timer(0), _step(0) { }

    void UpdateAI(uint32 diff) override
    {
        if (!_step)
            return;

        if (_timer < diff)
        {
            switch (_step)
            {
                case 1:
                    if (GameObject* keg = me->FindNearestGameObject(GO_TAINTED_KEG, 10.0f))
                    {
                        std::list<Creature*> farmers;
                        me->GetCreatureListWithEntryInGrid(farmers, NPC_CAPTURED_FARMER, 30.0f);
                        for (Creature* farmer : farmers)
                        {
                            if (farmer->IsAlive())
                            {
                                float x, y, z;
                                _farmers.push_back(farmer->GetGUID());
                                keg->GetContactPoint(farmer, x, y, z, CONTACT_DISTANCE);
                                farmer->GetMotionMaster()->MovePoint(1, x, y, z);
                            }
                        }
                    }
                    _timer = 4500;
                    _step++;
                    break;
                case 2:
                    if (!_farmers.empty())
                        if (Creature* farmer = ObjectAccessor::GetCreature(*me, _farmers.front()))
                            farmer->SetStandState(UNIT_STAND_STATE_KNEEL);
                    _timer = 2000;
                    _step++;
                    break;
                case 3:
                    if (GameObject* keg = me->FindNearestGameObject(GO_TAINTED_KEG, 10.0f))
                        me->SummonGameObject(GO_TAINTED_KEG_SMOKE, keg->GetPositionX(), keg->GetPositionY(), keg->GetPositionZ() + 1, 0.0f,
                            QuaternionData::fromEulerAnglesZYX(0.0f, 0.0f, 0.0f), 120s);
                    if (!_farmers.empty())
                        if (Creature* farmer = ObjectAccessor::GetCreature(*me, _farmers.front()))
                            farmer->SetStandState(UNIT_STAND_STATE_STAND);
                    while (!_farmers.empty())
                    {
                        if (Creature* farmer = ObjectAccessor::GetCreature(*me, _farmers.front()))
                            if (farmer->IsAlive())
                                farmer->KillSelf();
                        _farmers.pop_front();
                    }

                    _timer = 20000;
                    _step++;
                    break;
                case 4:
                    _timer = 0;
                    _step = 0;
                    break;
            }
        }
        else
            _timer -= diff;
    }

    void StartEvent()
    {
        if (_step)
            return;

        _step = 1;
        _timer = 2000;
        if (GameObject* keg = me->FindNearestGameObject(GO_TAINTED_KEG, 10.0f))
        {
            keg->SetRespawnTime(120);
            keg->Refresh();
        }
    }

    void OnQuestReward(Player* /*player*/, Quest const* /*quest*/, LootItemType /*type*/, uint32 /*opt*/) override
    {
        StartEvent();
    }

private:
    uint32 _timer;
    GuidList _farmers;
    uint8 _step; // 0 = usual, nothing going on // 1+ event going on
};

void AddSC_classic_hillsbrad_foothills()
{
    RegisterGameObjectAI(classic_go_helcular_s_grave);
    RegisterGameObjectAI(classic_go_dusty_rug);
}
