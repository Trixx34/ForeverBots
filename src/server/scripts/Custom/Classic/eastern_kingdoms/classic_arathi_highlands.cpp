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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/arathi_highlands/arathi_highlands.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 660 (Hints of a New Plague?), 665 (Sunken Treasure), 667 (Death From Below)
// Escort waypoints are the VMaNGOS script_waypoint rows, added inline (TC master has no script_waypoint table).

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "Loot.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include <iterator>
#include <set>

namespace
{
struct ClassicEscortPoint
{
    float x, y, z;
    uint32 waitMs;
};

// VMaNGOS npc_escortAI::JustDied / GroupEventFailHappens
void ClassicFailEscortQuest(Player* player, uint32 questId)
{
    if (!player)
        return;

    if (Group* group = player->GetGroup())
    {
        for (GroupReference const& groupRef : group->GetMembers())
            if (groupRef.GetSource()->IsInMap(player))
                groupRef.GetSource()->FailQuest(questId);
    }
    else
        player->FailQuest(questId);
}
}

/*######
## npc_professor_phizzlethorpe
######*/

enum ProfessorPhizzlethorpe
{
    SAY_PROGRESS_1          = 845,
    SAY_PROGRESS_2          = 846,
    SAY_PROGRESS_3          = 847,
    EMOTE_PROGRESS_4        = 848,
    SAY_AGGRO               = 859,
    SAY_PROGRESS_5          = 849,
    SAY_PROGRESS_6          = 850,
    SAY_PROGRESS_7          = 851,
    EMOTE_PROGRESS_8        = 889,
    SAY_PROGRESS_9          = 890,

    QUEST_SUNKEN_TREASURE   = 665,
    ENTRY_VENGEFUL_SURGE    = 2776
};

// VMaNGOS script_waypoint entry 2768 (SetRun() at point 11 -> points 12+ run)
ClassicEscortPoint const PhizzlethorpePath[] =
{
    { -2077.73f, -2091.17f,  9.49f,     0 }, // 0
    { -2077.99f, -2105.33f, 13.24f,     0 }, // 1
    { -2074.60f, -2109.67f, 14.24f,     0 }, // 2
    { -2076.60f, -2117.46f, 16.67f,     0 }, // 3
    { -2073.51f, -2123.46f, 18.42f,  2000 }, // 4
    { -2073.51f, -2123.46f, 18.42f,  4000 }, // 5
    { -2066.60f, -2131.85f, 21.56f,     0 }, // 6
    { -2053.85f, -2143.19f, 20.31f,     0 }, // 7
    { -2043.49f, -2153.73f, 20.20f, 10000 }, // 8
    { -2043.49f, -2153.73f, 20.20f, 20000 }, // 9
    { -2043.49f, -2153.73f, 20.20f, 10000 }, // 10
    { -2043.49f, -2153.73f, 20.20f,  2000 }, // 11
    { -2053.85f, -2143.19f, 20.31f,     0 }, // 12
    { -2066.60f, -2131.85f, 21.56f,     0 }, // 13
    { -2073.51f, -2123.46f, 18.42f,     0 }, // 14
    { -2076.60f, -2117.46f, 16.67f,     0 }, // 15
    { -2074.60f, -2109.67f, 14.24f,     0 }, // 16
    { -2077.99f, -2105.33f, 13.24f,     0 }, // 17
    { -2077.73f, -2091.17f,  9.49f,     0 }, // 18
    { -2066.41f, -2086.21f,  8.97f,  6000 }, // 19
    { -2066.41f, -2086.21f,  8.97f,  2000 }, // 20
};

struct classic_npc_professor_phizzlethorpe : public EscortAI
{
    classic_npc_professor_phizzlethorpe(Creature* creature) : EscortAI(creature)
    {
        for (uint32 i = 0; i < std::size(PhizzlethorpePath); ++i)
        {
            ClassicEscortPoint const& p = PhizzlethorpePath[i];
            AddWaypoint(i, p.x, p.y, p.z, 0.0f, p.waitMs ? Optional<Milliseconds>(Milliseconds(p.waitMs)) : Optional<Milliseconds>(), i > 11);
        }
    }

    void Reset() override { }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!player)
            return;

        switch (waypointId)
        {
            case 4:
                ClassicScriptText(SAY_PROGRESS_2, me, player);
                break;
            case 5:
                ClassicScriptText(SAY_PROGRESS_3, me, player);
                break;
            case 8:
                ClassicScriptText(EMOTE_PROGRESS_4, me);
                break;
            case 9:
                me->SummonCreature(ENTRY_VENGEFUL_SURGE, -2056.41f, -2144.01f, 20.59f, 5.70f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 600000ms);
                me->SummonCreature(ENTRY_VENGEFUL_SURGE, -2050.17f, -2140.02f, 19.54f, 5.17f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 600000ms);
                break;
            case 10:
                ClassicScriptText(SAY_PROGRESS_5, me, player);
                break;
            case 11:
                ClassicScriptText(SAY_PROGRESS_6, me, player);
                me->SetWalk(false); // SetRun(); following waypoints are run waypoints
                break;
            case 19:
                ClassicScriptText(SAY_PROGRESS_7, me, player);
                break;
            case 20:
                ClassicScriptText(EMOTE_PROGRESS_8, me);
                ClassicScriptText(SAY_PROGRESS_9, me, player);
                player->GroupEventHappens(QUEST_SUNKEN_TREASURE, me);
                break;
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_AGGRO, me);
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(me);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_SUNKEN_TREASURE)
        {
            me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_PASSIVE);
            ClassicScriptText(SAY_PROGRESS_1, me, player);
            Start(true, player->GetGUID(), quest, true);
        }
    }
};

/*####
# npc_shakes_o_breen
####*/

enum ShakesOBreen
{
    QUEST_DEATH_FROM_BELOW     = 667,

    NPC_DAGGERSPINE_RAIDER     = 2595,
    NPC_DAGGERSPINE_SORCERESS  = 2596,

    BREEN_YELL_1               = 6372,
    NAGA_YELL_1                = 854,
    BREEN_SAY_2                = 863,
};

float const NagaCoord[4][4] =
{
    { -2154.049f, -1969.738f, 15.371f, 5.54f },
    { -2157.606f, -1972.530f, 15.552f, 5.54f },
    { -2157.533f, -1968.904f, 15.410f, 5.54f },
    { -2109.839f, -2017.029f, 6.0080f, 5.54f },
};

// VMaNGOS uses npc_escortAI only to track the event player (single waypoint, escort paused for the whole event).
// Ported as ScriptedAI with the same player tracking: script range check (150 yd, every 3 s) plus the escort base
// range check (100 yd out of combat, fails the quest) and quest failure on death.
struct classic_npc_shakes_o_breen : public ScriptedAI
{
    classic_npc_shakes_o_breen(Creature* creature) : ScriptedAI(creature),
        _waveId(0), _nagaAlive(0), _eventTimer(20000), _playerCheckTimer(3000), _escortCheckTimer(1000), _nagaCheckTimer(500), _eventActive(false) { }

    void Reset() override
    {
        _eventTimer = 20000;

        if (!_eventActive)
        {
            _waveId = 0;
            _nagaAlive = 0;
            me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_DEATH_FROM_BELOW || _eventActive || me->IsEngaged())
            return;

        me->Yell(BREEN_YELL_1);

        // npc_escortAI::Start + SetEscortPaused(true)
        _playerGUID = player->GetGUID();
        _eventActive = true;
        _playerCheckTimer = 3000;
        _escortCheckTimer = 1000;
        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
        me->GetMotionMaster()->MoveIdle();
        me->SetFacingTo(2.67f);
    }

    void DoSummon(uint32 entry, uint8 index)
    {
        me->SummonCreature(entry, NagaCoord[index][0], NagaCoord[index][1], NagaCoord[index][2], NagaCoord[index][3], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60000ms);
    }

    void DoWaveSummon()
    {
        ++_waveId;

        if (_waveId == 3)
            me->Say(BREEN_SAY_2);

        switch (_waveId)
        {
            case 1:
            case 3:
                DoSummon(NPC_DAGGERSPINE_RAIDER, 0);
                DoSummon(NPC_DAGGERSPINE_RAIDER, 1);
                DoSummon(NPC_DAGGERSPINE_SORCERESS, 2);
                break;
            case 2:
                DoSummon(NPC_DAGGERSPINE_RAIDER, 0);
                DoSummon(NPC_DAGGERSPINE_RAIDER, 1);
                break;
        }
    }

    void FinishEvent(bool success)
    {
        if (success)
        {
            if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                player->GroupEventHappens(QUEST_DEATH_FROM_BELOW, me);
        }
        else
        {
            me->DespawnOrUnsummon();
            return;
        }

        _eventActive = false;
        _playerGUID.Clear();
        _pendingNagas.clear();
        Reset();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (_waveId == 1 && summoned->GetEntry() == NPC_DAGGERSPINE_RAIDER && !_nagaAlive)
            summoned->Yell(NAGA_YELL_1);

        ++_nagaAlive;

        summoned->SetCanGiveExperience(false);
        summoned->GetMotionMaster()->Clear();
        summoned->SetWalk(false);
        summoned->GetMotionMaster()->MovePoint(0, NagaCoord[3][0], NagaCoord[3][1], NagaCoord[3][2]);
        _pendingNagas.insert(summoned->GetGUID());
    }

    void SummonedCreatureDies(Creature* summoned, Unit* /*killer*/) override
    {
        if (_nagaAlive)
            --_nagaAlive;
        _pendingNagas.erase(summoned->GetGUID());
        // pSummoned->loot.clear()
        summoned->m_loot.reset();
        summoned->m_personalLoot.clear();
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        _pendingNagas.erase(summoned->GetGUID());
    }

    // VMaNGOS SummonedMovementInform(point 0): TC has no summoned movement hook, the arrival is polled instead
    void CheckNagaArrival()
    {
        for (auto itr = _pendingNagas.begin(); itr != _pendingNagas.end();)
        {
            Creature* naga = ObjectAccessor::GetCreature(*me, *itr);
            if (!naga || !naga->IsAlive())
            {
                itr = _pendingNagas.erase(itr);
                continue;
            }

            if (naga->IsWithinDist3d(NagaCoord[3][0], NagaCoord[3][1], NagaCoord[3][2], 2.0f))
            {
                me->GetThreatManager().AddThreat(naga, 10.0f);
                naga->GetThreatManager().AddThreat(me, 10.0f);
                itr = _pendingNagas.erase(itr);
                continue;
            }
            ++itr;
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_eventActive)
            ClassicFailEscortQuest(ObjectAccessor::GetPlayer(*me, _playerGUID), QUEST_DEATH_FROM_BELOW);
    }

    void UpdateAI(uint32 diff) override
    {
        if (_eventActive)
        {
            // npc_escortAI base check: player/group out of range (100 yd) while out of combat -> quest failed, despawn
            if (!me->IsEngaged())
            {
                if (_escortCheckTimer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID);
                    if (!player || !IsPlayerOrGroupInRange(player, 100.0f))
                    {
                        ClassicFailEscortQuest(player, QUEST_DEATH_FROM_BELOW);
                        FinishEvent(false);
                        return;
                    }
                    _escortCheckTimer = 1000;
                }
                else
                    _escortCheckTimer -= diff;
            }

            if (_playerCheckTimer < diff)
            {
                Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID);
                if (!player || !player->IsWithinDist(me, 150.0f))
                {
                    FinishEvent(false);
                    return;
                }
                else
                    _playerCheckTimer = 3000;
            }
            else
                _playerCheckTimer -= diff;

            if (_eventTimer < diff)
            {
                if (_waveId < 3)
                {
                    DoWaveSummon();
                    _eventTimer = 20000;
                }
                else
                {
                    if (_nagaAlive)
                        _eventTimer = 1000;
                    else
                        FinishEvent(true);
                }
            }
            else
                _eventTimer -= diff;
        }

        if (!_pendingNagas.empty())
        {
            if (_nagaCheckTimer <= diff)
            {
                CheckNagaArrival();
                _nagaCheckTimer = 500;
            }
            else
                _nagaCheckTimer -= diff;
        }

        UpdateVictim();
    }

private:
    bool IsPlayerOrGroupInRange(Player* player, float range) const
    {
        if (Group* group = player->GetGroup())
        {
            for (GroupReference const& groupRef : group->GetMembers())
                if (me->IsWithinDistInMap(groupRef.GetSource(), range))
                    return true;
            return false;
        }
        return me->IsWithinDistInMap(player, range);
    }

    ObjectGuid _playerGUID;
    std::set<ObjectGuid> _pendingNagas;
    uint8 _waveId;
    uint8 _nagaAlive;
    uint32 _eventTimer;
    uint32 _playerCheckTimer;
    uint32 _escortCheckTimer;
    uint32 _nagaCheckTimer;
    bool _eventActive;
};

/*######
## npc_kinelory
######*/

enum Kinelory
{
    SAY_START               = 816,
    SAY_REACH_BOTTOM        = 817,
    SAY_AGGRO_KINELORY      = 897,
    SAY_AGGRO_JORELL        = 896,
    SAY_WATCH_BACK          = 818,
    EMOTE_BELONGINGS        = 819,
    SAY_DATA_FOUND          = 821,
    SAY_ESCAPE              = 822,
    SAY_FINISH              = 892,
    EMOTE_HAND_PACK         = 891,

    SPELL_REJUVENATION      = 3627,
    SPELL_BEAR_FORM         = 4948,

    NPC_JORELL              = 2733,
    NPC_QUAE                = 2712,

    QUEST_HINTS_NEW_PLAGUE  = 660
};

// VMaNGOS script_waypoint entry 2713 (SetRun() at point 18 -> points 19+ run)
ClassicEscortPoint const KineloryPath[] =
{
    { -1416.91f, -3044.12f, 36.21f,    0 }, // 0
    { -1408.43f, -3051.35f, 37.79f,    0 }, // 1
    { -1399.45f, -3069.20f, 31.25f,    0 }, // 2
    { -1400.28f, -3083.14f, 27.06f,    0 }, // 3
    { -1405.30f, -3096.72f, 26.36f,    0 }, // 4
    { -1406.12f, -3105.95f, 24.82f,    0 }, // 5
    { -1417.41f, -3106.80f, 16.61f,    0 }, // 6
    { -1433.06f, -3101.55f, 12.56f,    0 }, // 7
    { -1439.86f, -3086.36f, 12.29f,    0 }, // 8
    { -1450.48f, -3065.16f, 12.58f, 5000 }, // 9  SAY_REACH_BOTTOM
    { -1456.15f, -3055.53f, 12.54f,    0 }, // 10
    { -1459.41f, -3035.16f, 12.11f,    0 }, // 11
    { -1472.47f, -3034.18f, 12.44f,    0 }, // 12
    { -1495.57f, -3034.48f, 12.55f,    0 }, // 13
    { -1524.91f, -3035.47f, 13.15f,    0 }, // 14
    { -1549.05f, -3037.77f, 12.98f,    0 }, // 15
    { -1555.69f, -3028.02f, 13.64f, 3000 }, // 16 SAY_WATCH_BACK
    { -1555.69f, -3028.02f, 13.64f, 5000 }, // 17 SAY_DATA_FOUND
    { -1555.69f, -3028.02f, 13.64f, 2000 }, // 18 SAY_ESCAPE
    { -1551.19f, -3037.78f, 12.96f,    0 }, // 19
    { -1584.60f, -3048.77f, 13.67f,    0 }, // 20
    { -1602.14f, -3042.82f, 15.12f,    0 }, // 21
    { -1610.68f, -3027.42f, 17.22f,    0 }, // 22
    { -1601.65f, -3007.97f, 24.65f,    0 }, // 23
    { -1581.05f, -2992.32f, 30.85f,    0 }, // 24
    { -1559.95f, -2979.51f, 34.30f,    0 }, // 25
    { -1536.51f, -2969.78f, 32.64f,    0 }, // 26
    { -1511.81f, -2961.09f, 29.12f,    0 }, // 27
    { -1484.83f, -2960.87f, 32.54f,    0 }, // 28
    { -1458.23f, -2966.80f, 40.52f,    0 }, // 29
    { -1440.20f, -2971.20f, 43.15f,    0 }, // 30
    { -1427.85f, -2989.15f, 38.09f,    0 }, // 31
    { -1420.27f, -3008.91f, 35.01f,    0 }, // 32
    { -1427.58f, -3032.53f, 32.31f, 5000 }, // 33 SAY_FINISH
    { -1427.40f, -3035.17f, 32.26f,    0 }, // 34
};

struct classic_npc_kinelory : public EscortAI
{
    classic_npc_kinelory(Creature* creature) : EscortAI(creature), _bearFormTimer(0), _healTimer(0)
    {
        for (uint32 i = 0; i < std::size(KineloryPath); ++i)
        {
            ClassicEscortPoint const& p = KineloryPath[i];
            AddWaypoint(i, p.x, p.y, p.z, 0.0f, p.waitMs ? Optional<Milliseconds>(Milliseconds(p.waitMs)) : Optional<Milliseconds>(), i > 18);
        }
    }

    void Reset() override
    {
        _bearFormTimer = urand(5000, 7000);
        _healTimer     = urand(2000, 5000);
    }

    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 9:
                ClassicScriptText(SAY_REACH_BOTTOM, me);
                break;
            case 16:
                ClassicScriptText(SAY_WATCH_BACK, me);
                ClassicScriptText(EMOTE_BELONGINGS, me);
                break;
            case 17:
                ClassicScriptText(SAY_DATA_FOUND, me);
                break;
            case 18:
                ClassicScriptText(SAY_ESCAPE, me);
                if (Player* player = GetPlayerForEscort())
                    me->SetFacingToObject(player);
                me->SetWalk(false); // SetRun(); following waypoints are run waypoints
                break;
            case 33:
                ClassicScriptText(SAY_FINISH, me);
                if (Creature* quae = me->FindNearestCreature(NPC_QUAE, 10.0f))
                {
                    ClassicScriptText(EMOTE_HAND_PACK, me, quae);
                    me->SetFacingToObject(quae);
                }
                break;
            case 34:
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_HINTS_NEW_PLAGUE, me);
                break;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        if (who->GetEntry() == NPC_JORELL)
            ClassicScriptText(SAY_AGGRO_JORELL, who, me);
        else if (roll_chance(10))
            ClassicScriptText(SAY_AGGRO_KINELORY, me);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_HINTS_NEW_PLAGUE)
        {
            ClassicScriptText(SAY_START, me);
            me->SetImmuneToNPC(false);
            Start(true, player->GetGUID(), quest);
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_bearFormTimer < diff)
        {
            if (DoCastSelf(SPELL_BEAR_FORM) == SPELL_CAST_OK)
                _bearFormTimer = urand(25000, 30000);
        }
        else
            _bearFormTimer -= diff;

        if (_healTimer < diff)
        {
            if (me->HealthBelowPct(80))
            {
                if (DoCastSelf(SPELL_REJUVENATION) == SPELL_CAST_OK)
                    _healTimer = urand(15000, 25000);
            }
        }
        else
            _healTimer -= diff;
    }

private:
    uint32 _bearFormTimer;
    uint32 _healTimer;
};

/*######
## go_arathi_cannon_fire
######*/

// VMaNGOS: GameObjectAI::OnUse returns true -> the trap never fires on its own (the cannon fire spell is driven by the
// Death From Below event and filtered by the spell script spell_death_from_below_cannon_fire).
// TODO(classic): TC master's GameObjectAI has no hook to veto a trap trigger (GameObject::Update casts goInfo->trap.spell
// directly). To get the VMaNGOS behaviour, set gameobject_template.Data2 (trap radius) of 113529 to 0 in the world DB;
// the spell target filter (VMaNGOS spell script spell_death_from_below_cannon_fire) is not ported here either.
struct classic_go_arathi_cannon_fire : public GameObjectAI
{
    classic_go_arathi_cannon_fire(GameObject* go) : GameObjectAI(go) { }
};

void AddSC_classic_arathi_highlands()
{
    RegisterCreatureAI(classic_npc_professor_phizzlethorpe);
    RegisterCreatureAI(classic_npc_shakes_o_breen);
    RegisterCreatureAI(classic_npc_kinelory);
    RegisterGameObjectAI(classic_go_arathi_cannon_fire);
}
