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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/hinterlands/hinterlands.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 2742 (Rin'ji is Trapped!), 7840 (Lard's picnic basket)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

/*######
## npc_rinji
######*/

enum RinjiData
{
    SAY_RIN_FREE            = 3787,
    SAY_RIN_BY_OUTRUNNER    = 3827,
    SAY_RIN_HELP_1          = 3862,
    SAY_RIN_HELP_2          = 3861,
    SAY_RIN_COMPLETE        = 3790,
    SAY_RIN_PROGRESS_1      = 3817,
    SAY_RIN_PROGRESS_2      = 3818,

    QUEST_RINJI_TRAPPED     = 2742,
    NPC_RANGER              = 2694,
    NPC_OUTRUNNER           = 2691,
    GO_RINJI_CAGE           = 142036,
    FACTION_ESCORTEE        = 33
};

Position const RinjiAmbushSpawn[] =
{
    { 191.29620f, -2839.329346f, 107.388f },
    { 70.972466f, -2848.674805f, 109.459f }
};

Position const RinjiAmbushMoveTo[] =
{
    { 166.63038f, -2824.780273f, 108.153f },
    { 70.886589f, -2874.335449f, 116.675f }
};

// VMaNGOS script_waypoint entry 7780 (pointid, x, y, z, waittime ms)
namespace
{
struct ClassicEscortPoint
{
    uint32 Id;
    float X, Y, Z;
    uint32 WaitTime;
};
}

ClassicEscortPoint const RinjiPath[] =
{
    {  0, 261.059f,  -2757.88f, 122.553f,    0 },
    {  1, 259.812f,  -2758.25f, 122.555f,    0 }, // SAY_RIN_FREE
    {  2, 253.823f,  -2758.62f, 122.562f,    0 },
    {  3, 241.395f,  -2769.75f, 123.309f,    0 },
    {  4, 218.916f,  -2783.4f,  123.355f,    0 },
    {  5, 209.088f,  -2789.68f, 122.001f,    0 },
    {  6, 204.454f,  -2792.21f, 120.62f,     0 },
    {  7, 182.013f,  -2810.0f,  113.887f,    0 }, // summon
    {  8, 164.412f,  -2825.16f, 107.779f,    0 },
    {  9, 149.728f,  -2833.7f,  106.224f,    0 },
    { 10, 142.448f,  -2838.81f, 109.665f,    0 },
    { 11, 133.275f,  -2845.14f, 112.606f,    0 },
    { 12, 111.247f,  -2861.07f, 116.305f,    0 },
    { 13, 96.1041f,  -2874.89f, 114.397f,    0 }, // summon
    { 14, 73.3699f,  -2881.18f, 117.666f,    0 },
    { 15, 58.5792f,  -2889.15f, 116.253f,    0 },
    { 16, 33.2142f,  -2906.34f, 115.083f,    0 },
    { 17, 19.5865f,  -2908.71f, 117.276f, 7500 }, // SAY_RIN_COMPLETE
    { 18, 10.2825f,  -2911.61f, 118.394f,    0 },
    { 19, -37.5804f, -2942.73f, 117.145f,    0 },
    { 20, -68.5994f, -2953.69f, 116.685f,    0 },
    { 21, -102.054f, -2956.97f, 116.677f,    0 },
    { 22, -135.994f, -2955.74f, 115.788f,    0 },
    { 23, -171.562f, -2951.42f, 115.451f,    0 }
};

struct classic_npc_rinji : public EscortAI
{
    classic_npc_rinji(Creature* creature) : EscortAI(creature), _isByOutrunner(false), _postEventCount(0), _postEventTimer(3000), _spawnId(0) { }

    void Reset() override
    {
        _postEventCount = 0;
        _postEventTimer = 3000;
    }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        _isByOutrunner = false;
        _spawnId = 0;
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    void JustEngagedWith(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))
        {
            if (who->GetEntry() == NPC_OUTRUNNER && !_isByOutrunner)
            {
                ClassicScriptText(SAY_RIN_BY_OUTRUNNER, who);
                _isByOutrunner = true;
            }

            if (urand(0, 3))
                return;

            // only if attacked and escorter is not in combat?
            ClassicScriptText(urand(0, 1) ? SAY_RIN_HELP_1 : SAY_RIN_HELP_2, me);
        }
    }

    void DoSpawnAmbush(bool first)
    {
        if (!first)
            _spawnId = 1;

        Position const& pos = RinjiAmbushSpawn[_spawnId];
        me->SummonCreature(NPC_RANGER, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 60s);

        for (int i = 0; i < 2; ++i)
            me->SummonCreature(NPC_OUTRUNNER, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 60s);
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->SetWalk(false);
        Position const& pos = RinjiAmbushMoveTo[_spawnId];
        summoned->GetMotionMaster()->MovePoint(0, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
    }

    void LoadRinjiPath()
    {
        ResetPath();
        for (ClassicEscortPoint const& point : RinjiPath)
        {
            Optional<Milliseconds> wait;
            if (point.WaitTime)
                wait = Milliseconds(point.WaitTime);
            // VMaNGOS calls SetRun() at point 17: the segments after it are run
            AddWaypoint(point.Id, point.X, point.Y, point.Z, 0.0f, wait, point.Id > 17);
        }
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!player)
            return;

        switch (waypointId)
        {
            case 1:
                ClassicScriptText(SAY_RIN_FREE, me, player);
                break;
            case 7:
                DoSpawnAmbush(true);
                break;
            case 13:
                DoSpawnAmbush(false);
                break;
            case 17:
                ClassicScriptText(SAY_RIN_COMPLETE, me, player);
                player->GroupEventHappens(QUEST_RINJI_TRAPPED, me);
                me->SetWalk(false);
                _postEventCount = 1;
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        // Check if we have a current target
        if (!UpdateVictim())
        {
            if (HasEscortState(STATE_ESCORT_ESCORTING) && _postEventCount)
            {
                if (_postEventTimer < diff)
                {
                    _postEventTimer = 3000;

                    if (Player* player = GetPlayerForEscort())
                    {
                        switch (_postEventCount)
                        {
                            case 1:
                                ClassicScriptText(SAY_RIN_PROGRESS_1, me, player);
                                ++_postEventCount;
                                break;
                            case 2:
                                ClassicScriptText(SAY_RIN_PROGRESS_2, me, player);
                                _postEventCount = 0;
                                break;
                        }
                    }
                    else
                    {
                        me->DespawnOrUnsummon();
                        return;
                    }
                }
                else
                    _postEventTimer -= diff;
            }

            return;
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_RINJI_TRAPPED)
        {
            if (GameObject* go = GetClosestGameObjectWithEntry(me, GO_RINJI_CAGE, INTERACTION_DISTANCE))
                go->UseDoorOrButton();

            me->SetFaction(FACTION_ESCORTEE); // VMaNGOS TEMPFACTION_RESTORE_RESPAWN: TC restores the template faction on respawn
            me->SetImmuneToNPC(false);

            LoadRinjiPath();
            Start(false, player->GetGUID(), quest);
        }
    }

private:
    bool _isByOutrunner;
    uint32 _postEventCount;
    uint32 _postEventTimer;
    int _spawnId;
};

/*######
## go_lards_picnic_basket
######*/

enum LardsPicnicBasketData
{
    NPC_KIDNAPPEUR_VILEBRANCH     = 14748
};

struct classic_go_lards_picnic_basket : public GameObjectAI
{
    classic_go_lards_picnic_basket(GameObject* go) : GameObjectAI(go), _timer(0), _state(false) { }

    void UpdateAI(uint32 diff) override
    {
        if (_state)
        {
            if (_timer < diff)
            {
                _state = false;
                me->SetGoState(GO_STATE_READY);
                me->RemoveFlag(GO_FLAG_IN_USE);
            }
            else
                _timer -= diff;
        }
    }

    bool CheckCanStartEvent() const
    {
        return !_state;
    }

    void SetInUse()
    {
        me->SetGoState(GO_STATE_ACTIVE);
        me->SetFlag(GO_FLAG_IN_USE);
        _state = true;
        _timer = 300000;
    }

    bool OnGossipHello(Player* player) override
    {
        if (me->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            if (CheckCanStartEvent())
            {
                SetInUse();
                for (int i = 0; i < 3; ++i)
                    player->SummonCreature(NPC_KIDNAPPEUR_VILEBRANCH, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30s);
            }
        }
        return false;
    }

private:
    uint32 _timer;
    bool _state; // false = usual, can launch; true = in use, cannot launch
};

void AddSC_classic_hinterlands()
{
    RegisterCreatureAI(classic_npc_rinji);
    RegisterGameObjectAI(classic_go_lards_picnic_basket);
}
