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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/razorfen_downs/razorfen_downs.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 3525 (Extinguishing the Idol)
// Ported: npc_belnistrasz, go_gong

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "TemporarySummon.h"
#include "classic_razorfen_downs.h"
#include "classic_script_text.h"

/*###
# npc_belnistrasz
####*/

enum Belnistrasz
{
    QUEST_EXTINGUISHING_THE_IDOL = 3525,

    SAY_BELNISTRASZ_READY     = 4493,
    SAY_BELNISTRASZ_START_RIT = 4501,
    SAY_BELNISTRASZ_AGGRO_1   = 9008,
    SAY_BELNISTRASZ_AGGRO_2   = 9007,
    SAY_BELNISTRASZ_3_MIN     = 4504,
    SAY_BELNISTRASZ_2_MIN     = 4505,
    SAY_BELNISTRASZ_1_MIN     = 4506,
    SAY_BELNISTRASZ_FINISH    = 4507,

    NPC_IDOL_ROOM_SPAWNER = 8611,
    NPC_WITHERED_BATTLE_BOAR = 7333,
    NPC_WITHERED_QUILGUARD = 7329,
    NPC_DEATHS_HEAD_GEOMANCER = 7335,
    NPC_PLAGUEMAW_THE_ROTTING = 7356,

    GO_BELNISTRASZ_BRAZIER = 152097,
    GO_IDOL_OVEN_FIRE = 151951,
    GO_IDOL_MOUTH_FIRE = 151973,

    SPELL_ARCANE_INTELLECT = 13326, // use this somewhere (he has it as default)
    SPELL_FIREBALL = 9053,
    SPELL_FROST_NOVA = 11831,
    SPELL_IDOL_SHUTDOWN = 12774,

    // summon spells only exist in 1.x
    //SPELL_SUMMON_1 = 12694, // NPC_WITHERED_BATTLE_BOAR
    //SPELL_SUMMON_2 = 14802, // NPC_DEATHS_HEAD_GEOMANCER
    //SPELL_SUMMON_3 = 14801, // NPC_WITHERED_QUILGUARD
};

namespace
{
float const RFDSpawnerCoord[3][4] =
{
    {2582.79f, 954.392f, 52.4821f, 3.78736f},
    {2569.42f, 956.380f, 52.2732f, 5.42797f},
    {2570.62f, 942.393f, 53.7433f, 0.71558f}
};

// VMaNGOS script_waypoint entry 8516 (pointid, x, y, z); no wait times
struct ClassicRFDEscortPoint
{
    uint32 Id;
    float X, Y, Z;
};

ClassicRFDEscortPoint const BelnistraszPath[] =
{
    {  1, 2603.18f, 725.259f, 54.6927f },
    {  2, 2587.13f, 734.392f, 55.231f  },
    {  3, 2570.69f, 753.572f, 54.5855f },
    {  4, 2558.51f, 747.66f,  54.4482f },
    {  5, 2544.23f, 772.924f, 47.9255f },
    {  6, 2530.08f, 797.475f, 45.97f   },
    {  7, 2521.83f, 799.127f, 44.3061f },
    {  8, 2502.61f, 789.222f, 39.5074f },
    {  9, 2495.25f, 789.406f, 39.499f  },
    { 10, 2488.07f, 802.455f, 42.9834f },
    { 11, 2486.64f, 826.649f, 43.6363f },
    { 12, 2492.64f, 835.166f, 45.1427f },
    { 13, 2505.02f, 847.564f, 47.6487f },
    { 14, 2538.96f, 877.362f, 47.6781f },
    { 15, 2546.07f, 885.672f, 47.6789f },
    { 16, 2548.02f, 897.584f, 47.7277f },
    { 17, 2544.29f, 909.116f, 46.2506f },
    { 18, 2523.6f,  920.306f, 45.8717f },
    { 19, 2522.69f, 933.546f, 47.5769f },
    { 20, 2531.63f, 959.893f, 49.4111f },
    { 21, 2540.23f, 973.338f, 50.1241f },
    { 22, 2547.21f, 977.489f, 49.9759f },
    { 23, 2558.75f, 969.243f, 50.7353f },
    { 24, 2575.6f,  950.138f, 52.846f  },
    { 25, 2575.6f,  950.138f, 52.846f  }
};
}

struct classic_npc_belnistrasz : public EscortAI
{
    classic_npc_belnistrasz(Creature* creature) : EscortAI(creature)
    {
        _instance = creature->GetInstanceScript();
        _ritualPhase = 0;
        _ritualTimer = 1000;
        _aggro = false;
        _fireballTimer = 1000;
        _frostNovaTimer = 6000;
    }

    void Reset() override
    {
        _fireballTimer = 1000;
        _frostNovaTimer = 6000;
    }

    void JustDied(Unit* killer) override
    {
        _ritualPhase = 0;
        _ritualTimer = 1000;
        _aggro = false;
        EscortAI::JustDied(killer);
    }

    // VMaNGOS AttackedBy(): aggro text once while the ritual pauses the escort
    void JustEngagedWith(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_PAUSED))
        {
            if (!_aggro)
            {
                ClassicScriptText(urand(0, 1) ? SAY_BELNISTRASZ_AGGRO_1 : SAY_BELNISTRASZ_AGGRO_2, me, who);
                _aggro = true;
            }
        }
    }

    void AttackStart(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_PAUSED) && (_ritualPhase > 0))
            return;

        EscortAI::AttackStart(who);
    }

    void SpawnerSummon(Creature* summoner)
    {
        if (_ritualPhase > 7)
        {
            summoner->SummonCreature(NPC_PLAGUEMAW_THE_ROTTING, summoner->GetPositionX(), summoner->GetPositionY(), summoner->GetPositionZ(), summoner->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60s);
            return;
        }

        for (int i = 0; i < 4; ++i)
        {
            uint32 entry = 0;
            // ref TARGET_RANDOM_CIRCUMFERENCE_POINT
            float angle = 2.0f * float(M_PI) * float(rand_norm());
            float x, y, z;
            summoner->GetClosePoint(x, y, z, 0.0f, 2.0f, angle);
            switch (i)
            {
                case 0:
                case 1:
                    entry = NPC_WITHERED_BATTLE_BOAR;
                    break;
                case 2:
                    entry = NPC_WITHERED_QUILGUARD;
                    break;
                case 3:
                    entry = NPC_DEATHS_HEAD_GEOMANCER;
                    break;
            }
            summoner->SummonCreature(entry, x, y, z, 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60s);
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        SpawnerSummon(summoned);
    }

    void DoSummonRandom()
    {
        uint32 type = urand(0, 2);
        // VMaNGOS TEMPSUMMON_TIMED_COMBAT_OR_DEAD_DESPAWN (not in TC); the spawner never fights
        me->SummonCreature(NPC_IDOL_ROOM_SPAWNER, RFDSpawnerCoord[type][0], RFDSpawnerCoord[type][1], RFDSpawnerCoord[type][2], RFDSpawnerCoord[type][3], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_EXTINGUISHING_THE_IDOL)
            return;

        // VMaNGOS Start(bRun = false, ...)
        ResetPath();
        for (ClassicRFDEscortPoint const& point : BelnistraszPath)
            AddWaypoint(point.Id, point.X, point.Y, point.Z, false);

        Start(true, player->GetGUID(), quest);
        ClassicScriptText(SAY_BELNISTRASZ_READY, me, player);
        me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_ACTIVE);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (waypointId == 24)
        {
            ClassicScriptText(SAY_BELNISTRASZ_START_RIT, me);
            SetEscortPaused(true);
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (HasEscortState(STATE_ESCORT_PAUSED))
        {
            if (_ritualTimer < diff)
            {
                switch (_ritualPhase)
                {
                    case 0:
                        SetCombatMovement(false);
                        DoCastSelf(SPELL_IDOL_SHUTDOWN);
                        _ritualTimer = 1000;
                        break;
                    case 1:
                        DoSummonRandom();
                        _ritualTimer = 39000;
                        break;
                    case 2:
                        DoSummonRandom();
                        _ritualTimer = 20000;
                        break;
                    case 3:
                        ClassicScriptText(SAY_BELNISTRASZ_3_MIN, me, me);
                        _ritualTimer = 20000;
                        break;
                    case 4:
                        DoSummonRandom();
                        _ritualTimer = 40000;
                        break;
                    case 5:
                        DoSummonRandom();
                        ClassicScriptText(SAY_BELNISTRASZ_2_MIN, me, me);
                        _ritualTimer = 40000;
                        break;
                    case 6:
                        DoSummonRandom();
                        _ritualTimer = 20000;
                        break;
                    case 7:
                        ClassicScriptText(SAY_BELNISTRASZ_1_MIN, me, me);
                        _ritualTimer = 40000;
                        break;
                    case 8:
                        DoSummonRandom();
                        _ritualTimer = 20000;
                        break;
                    case 9:
                        ClassicScriptText(SAY_BELNISTRASZ_FINISH, me, me);
                        _ritualTimer = 3000;
                        break;
                    case 10:
                    {
                        if (Player* player = GetPlayerForEscort())
                            player->GroupEventHappens(QUEST_EXTINGUISHING_THE_IDOL, me);
                        me->RemoveAurasDueToSpell(SPELL_IDOL_SHUTDOWN);
                        me->SummonGameObject(GO_BELNISTRASZ_BRAZIER, 2577.196f, 947.0781f, 53.16757f, 2.356195f, QuaternionData(0.0f, 0.0f, 0.9238796f, 0.3826832f), 3600s, GO_SUMMON_TIMED_DESPAWN);
                        if (GameObject* ovenFire = me->FindNearestGameObject(GO_IDOL_OVEN_FIRE, 50.0f))
                            ovenFire->SetLootState(GO_JUST_DEACTIVATED);
                        if (GameObject* mouthFire = me->FindNearestGameObject(GO_IDOL_MOUTH_FIRE, 50.0f))
                            mouthFire->SetLootState(GO_JUST_DEACTIVATED);
                        if (_instance)
                            _instance->SetData(EXTINGUISH_FIRES, 0);
                        SetEscortPaused(false);
                        break;
                    }
                    default:
                        break;
                }
                ++_ritualPhase;
            }
            else
                _ritualTimer -= diff;
            return;
        }

        if (!UpdateVictim())
            return;

        if (_fireballTimer < diff)
        {
            DoCastVictim(SPELL_FIREBALL);
            _fireballTimer = urand(2000, 3000);
        }
        else
            _fireballTimer -= diff;

        if (_frostNovaTimer < diff)
        {
            DoCastVictim(SPELL_FROST_NOVA);
            _frostNovaTimer = urand(10000, 15000);
        }
        else
            _frostNovaTimer -= diff;
    }

private:
    InstanceScript* _instance;
    uint8 _ritualPhase;
    uint32 _ritualTimer;
    bool _aggro;
    uint32 _fireballTimer;
    uint32 _frostNovaTimer;
};

/*######
## go_gong
######*/

struct classic_go_gong : public GameObjectAI
{
    classic_go_gong(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        //basic support, not blizzlike data is missing...
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            instance->SetData(DATA_GONG_WAVES, instance->GetData(DATA_GONG_WAVES) + 1);
            return true;
        }

        return false;
    }
};

void AddSC_classic_razorfen_downs()
{
    RegisterCreatureAI(classic_npc_belnistrasz);
    RegisterGameObjectAI(classic_go_gong);
}
