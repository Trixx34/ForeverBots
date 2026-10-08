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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/ashenvale/ashenvale.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_torek (quest 6544), npc_feero_ironhand (quest 976), go_foulweald_totem_mound (quest 6641)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

namespace
{
struct ClassicEscortPoint
{
    float X, Y, Z, O;
    uint32 WaitMs;
};

// VMaNGOS npc_escortAI reads its path from world.script_waypoint (by creature entry). TC EscortAI needs the path
// supplied by the script, so the VMaNGOS rows are embedded here (node ids are 0-based and contiguous, as TC requires).
template <std::size_t N, typename RunPredicate>
void LoadClassicEscortPath(EscortAI* ai, ClassicEscortPoint const (&points)[N], RunPredicate isRunning)
{
    ai->ResetPath();
    for (uint32 i = 0; i < N; ++i)
    {
        Optional<Milliseconds> waitTime;
        if (points[i].WaitMs)
            waitTime = Milliseconds(points[i].WaitMs);
        ai->AddWaypoint(i, points[i].X, points[i].Y, points[i].Z, points[i].O, waitTime, isRunning(i));
    }
}

// VMaNGOS script_waypoint entry 12858 (pointid 0..22, stored 0-based here)
ClassicEscortPoint const EscortPath12858[] =
{
    { 1782.63f, -2241.11f, 109.73f, 0.1493f, 5000 },  // 0
    { 1788.88f, -2240.17f, 111.71f, 0.1493f, 0 },  // 1
    { 1797.49f, -2238.11f, 112.31f, 0.2348f, 0 },  // 2
    { 1803.83f, -2232.77f, 111.22f, 0.7f, 0 },  // 3
    { 1806.65f, -2217.83f, 107.36f, 1.3842f, 0 },  // 4
    { 1811.81f, -2208.01f, 107.45f, 1.087f, 0 },  // 5
    { 1820.85f, -2190.82f, 100.49f, 1.0867f, 0 },  // 6
    { 1829.6f, -2177.49f, 96.44f, 0.9899f, 0 },  // 7
    { 1837.98f, -2164.19f, 96.71f, 1.0086f, 0 },  // 8: prepare
    { 1839.99f, -2149.29f, 96.78f, 1.4367f, 0 },  // 9
    { 1835.14f, -2134.98f, 96.8f, 1.8976f, 0 },  // 10
    { 1823.57f, -2118.27f, 97.43f, 2.1764f, 0 },  // 11
    { 1814.99f, -2110.35f, 98.38f, 2.3962f, 0 },  // 12
    { 1806.6f, -2103.09f, 99.19f, 2.4283f, 0 },  // 13
    { 1798.27f, -2095.77f, 100.04f, 2.4206f, 0 },  // 14
    { 1783.59f, -2079.92f, 100.81f, 2.3179f, 0 },  // 15
    { 1776.79f, -2069.48f, 101.77f, 2.1481f, 0 },  // 16
    { 1776.82f, -2054.59f, 109.82f, 1.5688f, 0 },  // 17
    { 1776.88f, -2047.56f, 109.83f, 1.5623f, 0 },  // 18
    { 1776.86f, -2036.55f, 109.83f, 1.5726f, 0 },  // 19
    { 1776.9f, -2024.56f, 109.83f, 1.5675f, 0 },  // 20: win
    { 1776.87f, -2028.31f, 109.83f, 4.7044f, 60000 },  // 21: stay
    { 1776.9f, -2028.3f, 109.83f, 0.3218f, 0 },  // 22
};
// VMaNGOS script_waypoint entry 4484 (pointid 0..32, stored 0-based here)
ClassicEscortPoint const EscortPath4484[] =
{
    { 3178.57f, 188.52f, 4.27f, 0.7286f, 0 },  // 0: SAY_QUEST_START
    { 3189.82f, 198.56f, 5.62f, 0.7286f, 0 },  // 1
    { 3215.21f, 185.78f, 6.43f, 5.8169f, 0 },  // 2
    { 3224.05f, 183.08f, 6.74f, 5.9868f, 0 },  // 3
    { 3228.11f, 194.97f, 7.51f, 1.2417f, 0 },  // 4
    { 3225.33f, 201.78f, 7.25f, 1.9584f, 0 },  // 5
    { 3233.33f, 226.88f, 10.18f, 1.2623f, 0 },  // 6
    { 3274.12f, 225.83f, 10.72f, 6.2574f, 0 },  // 7
    { 3321.63f, 209.82f, 12.36f, 5.9582f, 0 },  // 8
    { 3369.66f, 226.21f, 11.69f, 0.3289f, 0 },  // 9
    { 3402.35f, 227.2f, 9.48f, 0.0303f, 0 },  // 10
    { 3441.92f, 224.75f, 10.85f, 6.2213f, 0 },  // 11
    { 3453.87f, 220.31f, 12.52f, 5.9274f, 0 },  // 12
    { 3472.51f, 213.68f, 13.26f, 5.9415f, 0 },  // 13
    { 3515.49f, 212.96f, 9.76f, 6.2664f, 5000 },  // 14: SAY_FIRST_AMBUSH_START
    { 3516.21f, 212.84f, 9.52f, 6.118f, 20000 },  // 15: SAY_FIRST_AMBUSH_END
    { 3548.22f, 217.12f, 7.34f, 0.1329f, 0 },  // 16
    { 3567.57f, 219.43f, 5.22f, 0.1188f, 0 },  // 17
    { 3659.85f, 209.68f, 2.27f, 6.1779f, 0 },  // 18
    { 3734.9f, 177.64f, 6.75f, 5.8797f, 0 },  // 19
    { 3760.24f, 162.51f, 7.49f, 5.7449f, 5000 },  // 20: SAY_SECOND_AMBUSH_START
    { 3761.58f, 161.14f, 7.37f, 5.4867f, 20000 },  // 21: SAY_SECOND_AMBUSH_END
    { 3801.17f, 129.87f, 9.38f, 5.6147f, 0 },  // 22
    { 3815.53f, 118.53f, 10.14f, 5.6148f, 0 },  // 23
    { 3894.58f, 44.88f, 15.49f, 5.5331f, 0 },  // 24
    { 3972.83f, 0.42f, 17.34f, 5.7665f, 0 },  // 25
    { 4026.41f, -7.63f, 16.77f, 6.1341f, 0 },  // 26
    { 4086.24f, 12.32f, 16.12f, 0.3219f, 0 },  // 27
    { 4158.79f, 50.67f, 25.86f, 0.4863f, 0 },  // 28
    { 4223.48f, 99.52f, 35.47f, 0.6468f, 5000 },  // 29: SAY_FINAL_AMBUSH_START
    { 4224.28f, 100.02f, 35.49f, 0.5586f, 10000 },  // 30: SAY_QUEST_END
    { 4243.45f, 117.44f, 38.83f, 0.7376f, 0 },  // 31
    { 4264.18f, 134.22f, 42.96f, 0.6805f, 0 },  // 32
};
}

/*####
# npc_torek
####*/

enum TorekData
{
    SAY_READY                   = 8284,
    SAY_MOVE                    = 8278,
    SAY_PREPARE                 = 8282,
    SAY_WIN                     = 8280,
    SAY_END                     = 8281,

    SPELL_REND                  = 11977,
    SPELL_THUNDERCLAP           = 8078,

    FACTION_ORGRIMMAR           = 1174,

    QUEST_TOREK_ASSULT          = 6544,

    NPC_SPLINTERTREE_RAIDER     = 12859,
    NPC_DURIEL                  = 12860,
    NPC_SILVERWING_SENTINEL     = 12896,
    NPC_SILVERWING_WARRIOR      = 12897
};

struct classic_npc_torek : public EscortAI
{
    classic_npc_torek(Creature* creature) : EscortAI(creature), _rendTimer(5000), _thunderclapTimer(8000) { }

    void Reset() override
    {
        _rendTimer = 5000;
        _thunderclapTimer = 8000;
    }

    void JustAppeared() override
    {
        // VMaNGOS SetFactionTemporary(..., TEMPFACTION_RESTORE_RESPAWN)
        me->RestoreFaction();
        EscortAI::JustAppeared();
    }

    void DespawnRaiders()
    {
        std::list<Creature*> raiders;
        me->GetCreatureListWithEntryInGrid(raiders, NPC_SPLINTERTREE_RAIDER, 40.0f);
        for (Creature* raider : raiders)
            raider->DisappearAndDie();
    }

    void JustDied(Unit* killer) override
    {
        DespawnRaiders();
        EscortAI::JustDied(killer);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_TOREK_ASSULT)
            return;

        // TODO(classic): VMaNGOS comment - find companions, make them follow Torek at any time
        ClassicScriptText(SAY_READY, me, player);

        // Faction changes during escort.
        me->SetFaction(FACTION_ORGRIMMAR);
        std::list<Creature*> raiders;
        me->GetCreatureListWithEntryInGrid(raiders, NPC_SPLINTERTREE_RAIDER, 40.0f);
        for (Creature* raider : raiders)
            raider->SetFaction(FACTION_ORGRIMMAR); // TODO(classic): VMaNGOS restores the raiders' faction on their respawn (TEMPFACTION_RESTORE_RESPAWN)

        LoadClassicEscortPath(this, EscortPath12858, [](uint32) { return true; }); // VMaNGOS Start(bRun = true)
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!player)
            return;

        switch (waypointId)
        {
            case 1:
                ClassicScriptText(SAY_MOVE, me, player);
                break;
            case 8:
                ClassicScriptText(SAY_PREPARE, me, player);
                break;
            case 19:
                // TODO: verify location and creatures amount. (VMaNGOS comment)
                me->SummonCreature(NPC_DURIEL, 1776.73f, -2049.06f, 109.83f, 1.54f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 25s);
                me->SummonCreature(NPC_SILVERWING_SENTINEL, 1774.64f, -2049.41f, 109.83f, 1.40f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 25s);
                me->SummonCreature(NPC_SILVERWING_WARRIOR, 1778.73f, -2049.50f, 109.83f, 1.67f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 25s);
                break;
            case 20:
                ClassicScriptText(SAY_WIN, me, player);
                player->GroupEventHappens(QUEST_TOREK_ASSULT, me);
                break;
            case 21:
                ClassicScriptText(SAY_END, me, player);
                break;
            case 22:
                DespawnRaiders();
                break;
            default:
                break;
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(me);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_rendTimer < diff)
        {
            DoCastVictim(SPELL_REND);
            _rendTimer = 20000;
        }
        else
            _rendTimer -= diff;

        if (_thunderclapTimer < diff)
        {
            DoCastSelf(SPELL_THUNDERCLAP);
            _thunderclapTimer = 30000;
        }
        else
            _thunderclapTimer -= diff;
    }

private:
    uint32 _rendTimer;
    uint32 _thunderclapTimer;
};

/*####
# npc_feero_ironhand
####*/

enum FeeroIronhandData
{
    SAY_QUEST_START             = 1292,
    SAY_FIRST_AMBUSH_START      = 1372,
    SAY_FIRST_AMBUSH_END        = 1294,
    SAY_SECOND_AMBUSH_START     = 1373,
    SAY_SCOUT_SECOND_AMBUSH     = 1309,
    SAY_SECOND_AMBUSH_END       = 1310,
    SAY_FINAL_AMBUSH_START      = 1374,
    SAY_BALIZAR_FINAL_AMBUSH    = 1313,
    SAY_FINAL_AMBUSH_ATTACK     = 1499,
    SAY_QUEST_END               = 1315,

    QUEST_SUPPLIES_TO_AUBERDINE = 976,

    NPC_DARK_STRAND_ASSASSIN    = 3879,
    NPC_FORSAKEN_SCOUT          = 3893,
    NPC_ALIGAR_THE_TORMENTOR    = 3898,
    NPC_BALIZAR_THE_UMBRAGE     = 3899,
    NPC_CAEDAKAR_THE_VICIOUS    = 3900,

    FACTION_ESCORT_A_NEUTRAL_PASSIVE = 10
};

// Distance, Angle or Offset
static float const FeeroSummonPositions[2][2] =
{
    { 30.0f, 1.25f },
    { 15.0f, 0.95f } // 30.0f is in the tree and gets stuck with pathfinding
};

// Hardcoded positions for the last 3 mobs
static float const FeeroEliteSummonPositions[3][4] =
{
    { 4243.12f, 108.22f, 38.12f, 3.62f },
    { 4240.95f, 114.04f, 38.35f, 3.56f },
    { 4235.78f, 118.09f, 38.08f, 4.12f }
};

struct classic_npc_feero_ironhand : public EscortAI
{
    classic_npc_feero_ironhand(Creature* creature) : EscortAI(creature), _creaturesCount(0), _isAttacked(false) { }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            _creaturesCount = 0;
            _isAttacked = false;
        }
    }

    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        me->RestoreFaction();
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_SUPPLIES_TO_AUBERDINE)
            return;

        ClassicScriptText(SAY_QUEST_START, me, player);
        me->SetFaction(FACTION_ESCORT_A_NEUTRAL_PASSIVE);
        me->SetImmuneToNPC(false);

        LoadClassicEscortPath(this, EscortPath4484, [](uint32) { return true; }); // VMaNGOS Start(bRun = true)
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 14:
                // Prepare the first ambush
                ClassicScriptText(SAY_FIRST_AMBUSH_START, me);
                for (uint8 i = 0; i < 4; ++i)
                    DoSpawnMob(NPC_DARK_STRAND_ASSASSIN, FeeroSummonPositions[0][0], FeeroSummonPositions[0][1] - float(M_PI) / 4 * i);
                break;
            case 20:
                // Prepare the second ambush
                ClassicScriptText(SAY_SECOND_AMBUSH_START, me);
                for (uint8 i = 0; i < 3; ++i)
                    DoSpawnMob(NPC_FORSAKEN_SCOUT, FeeroSummonPositions[1][0], FeeroSummonPositions[1][1] - float(M_PI) / 3 * i);
                break;
            case 29:
                // Final ambush
                ClassicScriptText(SAY_FINAL_AMBUSH_START, me);
                me->SummonCreature(NPC_BALIZAR_THE_UMBRAGE, FeeroEliteSummonPositions[0][0], FeeroEliteSummonPositions[0][1], FeeroEliteSummonPositions[0][2], FeeroEliteSummonPositions[0][3], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
                me->SummonCreature(NPC_ALIGAR_THE_TORMENTOR, FeeroEliteSummonPositions[1][0], FeeroEliteSummonPositions[1][1], FeeroEliteSummonPositions[1][2], FeeroEliteSummonPositions[1][3], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
                me->SummonCreature(NPC_CAEDAKAR_THE_VICIOUS, FeeroEliteSummonPositions[2][0], FeeroEliteSummonPositions[2][1], FeeroEliteSummonPositions[2][2], FeeroEliteSummonPositions[2][3], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
                break;
            case 30:
                // Complete the quest
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_SUPPLIES_TO_AUBERDINE, me);
                break;
            default:
                break;
        }
    }

    // VMaNGOS AttackedBy(): TC has no AttackedBy hook for creatures, the first damage taken from Balizar is used instead.
    void DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        // Yell only at the first attack
        if (!_isAttacked && attacker && attacker->ToCreature() && attacker->GetEntry() == NPC_BALIZAR_THE_UMBRAGE)
        {
            ClassicScriptText(SAY_FINAL_AMBUSH_ATTACK, me);
            _isAttacked = true;
        }
    }

    // Summon mobs at calculated points
    void DoSpawnMob(uint32 entry, float distance, float angle)
    {
        float x, y, z;
        me->GetNearPoint(me, x, y, z, distance, angle);
        me->SummonCreature(entry, x, y, z, 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
    }

    void SummonedCreatureDies(Creature* summoned, Unit* /*killer*/) override
    {
        --_creaturesCount;

        if (!_creaturesCount)
        {
            switch (summoned->GetEntry())
            {
                case NPC_DARK_STRAND_ASSASSIN:
                    ClassicScriptText(SAY_FIRST_AMBUSH_END, me);
                    break;
                case NPC_FORSAKEN_SCOUT:
                    ClassicScriptText(SAY_SECOND_AMBUSH_END, me);
                    break;
                case NPC_ALIGAR_THE_TORMENTOR:
                case NPC_BALIZAR_THE_UMBRAGE:
                case NPC_CAEDAKAR_THE_VICIOUS:
                    ClassicScriptText(SAY_QUEST_END, me);
                    break;
                default:
                    break;
            }
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_FORSAKEN_SCOUT)
        {
            // Only one of the scouts yells
            if (_creaturesCount == 1)
                ClassicScriptText(SAY_SCOUT_SECOND_AMBUSH, summoned, me);
        }
        else if (summoned->GetEntry() == NPC_BALIZAR_THE_UMBRAGE)
            ClassicScriptText(SAY_BALIZAR_FINAL_AMBUSH, summoned);

        ++_creaturesCount;
        if (Player* player = GetPlayerForEscort())
            summoned->AI()->AttackStart(player);
    }

private:
    uint8 _creaturesCount;
    bool _isAttacked;
};

/*####
# go_foulweald_totem_mound (Alita - King of the Foulweald, quest 6641)
####*/

enum FoulwealdTotemMoundData
{
    NPC_ENRAGED_FOULWEALD           = 12921,
    NPC_CHIEF_MURGUT                = 12918,

    GO_KARANG_S_BANNER              = 178205,
    GO_KARANG_LIGHT                 = 178207,

    EVENT_KING_OF_THE_FOULWEALD     = 6721  // VMaNGOS scripted_event_id "event_king_of_the_foulweald"
};

static float const FoulwealdSpawnCoords[4][3] =
{
    { 2237.48f, -1524.45f, 89.7827f },
    { 2202.16f, -1544.48f, 87.796f },
    { 2235.44f, -1578.43f, 86.4944f },
    { 2260.9f,  -1547.91f, 89.1733f }
};

// TODO(classic): the enraged foulwealds' banner attack (npc_enraged_foulweald, SPELL_DESTROY_KARANG_S_BANNER_1/2) is a separate
// VMaNGOS script that is not part of this port; the mound only ends early when that script calls EventEnded(). The event still
// ends on its timer (phase 4). The foulweald death callback is handled here through GameObjectAI::SummonedCreatureDies.
struct classic_go_foulweald_totem_mound : public GameObjectAI
{
    classic_go_foulweald_totem_mound(GameObject* go) : GameObjectAI(go)
    {
        ResetEvent();
    }

    void ResetEvent()
    {
        _eventPhase = 0;
        _phaseTimer = 170000;
    }

    // VMaNGOS ProcessEventId_event_king_of_the_foulweald (event target is the mound)
    void EventInform(uint32 eventId) override
    {
        if (eventId == EVENT_KING_OF_THE_FOULWEALD)
            EventStart();
    }

    bool EventStart()
    {
        if (_eventPhase != 0)
            return false;

        _eventPhase = 1;
        for (uint32 i = 0; i < 2; ++i)
            SummonEnragedFoulweald(i, i);

        return true;
    }

    void SummonEnragedFoulweald(uint32 slot, uint32 posIndex)
    {
        if (TempSummon* foulweald = me->SummonCreature(NPC_ENRAGED_FOULWEALD, FoulwealdSpawnCoords[posIndex][0], FoulwealdSpawnCoords[posIndex][1], FoulwealdSpawnCoords[posIndex][2], 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 420s))
        {
            _currentEnragedFoulweald[slot] = foulweald->GetGUID();
            foulweald->GetMotionMaster()->MovePoint(1, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());
            foulweald->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f);
            foulweald->SetRespawnDelay(425);
        }
    }

    void EventEnded()
    {
        if (GameObject* banner = me->FindNearestGameObject(GO_KARANG_S_BANNER, 10.0f))
            banner->DespawnOrUnsummon();
        ResetEvent();
    }

    void EnragedFoulwealdJustDied(ObjectGuid const& creatureGUID)
    {
        if (_eventPhase != 1)
            return;

        for (uint32 slot = 0; slot < 2; ++slot)
            if (_currentEnragedFoulweald[slot] == creatureGUID)
                SummonEnragedFoulweald(slot, urand(0, 3));
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_ENRAGED_FOULWEALD)
            EnragedFoulwealdJustDied(summon->GetGUID());
    }

    void UpdateAI(uint32 diff) override
    {
        if (_eventPhase == 0 || _eventPhase > 4)
            return;

        if (_phaseTimer < diff)
        {
            ++_eventPhase;
            switch (_eventPhase)
            {
                case 2:
                    _phaseTimer = 10000;
                    break;
                case 3:
                    if (TempSummon* murgut = me->SummonCreature(NPC_CHIEF_MURGUT, FoulwealdSpawnCoords[3][0], FoulwealdSpawnCoords[3][1], FoulwealdSpawnCoords[3][2], 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 120s))
                    {
                        murgut->GetMotionMaster()->MovePoint(1, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());
                        murgut->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f);
                        murgut->SetRespawnDelay(125);
                        if (GameObject* banner = me->FindNearestGameObject(GO_KARANG_S_BANNER, 10.0f))
                            me->SummonGameObject(GO_KARANG_LIGHT, banner->GetPositionX(), banner->GetPositionY(), banner->GetPositionZ(), 0.0f, QuaternionData::fromEulerAnglesZYX(0.0f, 0.0f, 0.0f), 120s);
                    }
                    _phaseTimer = 120000;
                    break;
                case 4:
                    EventEnded();
                    break;
                default:
                    break;
            }
        }
        else
            _phaseTimer -= diff;
    }

private:
    ObjectGuid _currentEnragedFoulweald[2];
    uint8 _eventPhase;   // 0 nothing, 1 repoping enraged foulwealds, 2 wait, 3 chief_murgut, 4 done
    uint32 _phaseTimer;
};

void AddSC_classic_ashenvale()
{
    RegisterCreatureAI(classic_npc_torek);
    RegisterCreatureAI(classic_npc_feero_ironhand);
    RegisterGameObjectAI(classic_go_foulweald_totem_mound);
}
