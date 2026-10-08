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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/blackrock_depths.cpp
// (ScriptDev2 lineage, GPL-2)
// Quest support: 4001, 4342, 7604, 4322 (Jail Break!), 4201 (Love Potion), 4295 (Rocknot's Ale). Vendor Lokhtos Darkbargainer.
// Ported: go_shadowforge_brazier, at_ring_of_law, npc_grimstone, mob_phalanx, npc_rocknot, go_dark_keeper_portrait,
//         go_thunderbrew_laguer_keg, go_relic_coffer_door, npc_watchman_doomgrip, npc_golem_lord_argelmach,
//         at_shadowforge_bridge, npc_mistress_nagmara, boss_plugger_spazzring, go_bar_ale_mug, npc_dughal_stormwing,
//         npc_marshal_reginald_windsor, npc_marshal_windsor, npc_tobias_seecher, go_cell_door,
//         spell_five_fat_finger_exploding_heart_technique
// Added: classic_spell_brd_summon_thelrin_dnd (27517) - replaces the VMaNGOS instance CustomSpellCasted() core hook.

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "CreatureAIImpl.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "WaypointDefines.h"
#include "classic_blackrock_depths.h"
#include "classic_script_text.h"
#include <list>
#include <vector>

namespace
{
// VMaNGOS npc_escortAI reads its path from world.script_waypoint (by creature entry). TC EscortAI needs the path
// added by the script: node ids must be 0-based and consecutive (EscortAI::MovementInform indexes the path with them).
struct BrdEscortPoint
{
    uint32 PointId;     // VMaNGOS script_waypoint.pointid
    float X, Y, Z;
    uint32 WaitMs;
};

template <size_t N>
void BrdLoadEscortPath(EscortAI* ai, BrdEscortPoint const (&points)[N], bool run)
{
    ai->ResetPath();
    for (uint32 i = 0; i < N; ++i)
        ai->AddWaypoint(i, points[i].X, points[i].Y, points[i].Z, 0.0f,
            points[i].WaitMs ? Optional<Milliseconds>(Milliseconds(points[i].WaitMs)) : Optional<Milliseconds>(), run);
}

// VMaNGOS Player::GroupEventFailHappens (not in TC)
void BrdGroupEventFailHappens(Player* player, uint32 questId)
{
    if (Group* group = player->GetGroup())
    {
        for (GroupReference const& itr : group->GetMembers())
        {
            Player* member = itr.GetSource();
            if (member && member->IsInMap(player) && member->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                member->FailQuest(questId);
        }
    }
    else if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
        player->FailQuest(questId);
}
}

/*######
## go_shadowforge_brazier
######*/

struct classic_go_shadowforge_brazier : public GameObjectAI
{
    classic_go_shadowforge_brazier(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            if (instance->GetData(TYPE_LYCEUM) == IN_PROGRESS)
                instance->SetData(TYPE_LYCEUM, DONE);
            else
                instance->SetData(TYPE_LYCEUM, IN_PROGRESS);
        }
        return false;
    }
};

/*######
## npc_grimstone
######*/

enum Grimstone
{
    //4 or 6 in total? 1+2+1 / 2+2+2 / 3+3. Depending on this, code should be changed.
    MAX_MOB_AMOUNT          = 8,

    SAY_GRIMSTONE_ARENA1    = 5441,
    SAY_GRIMSTONE_ARENA2    = 5442,
    SAY_GRIMSTONE_ARENA3    = 5443,
    SAY_GRIMSTONE_ARENA4    = 5444,
    SAY_GRIMSTONE_ARENA5    = 5445,
    SAY_GRIMSTONE_ARENA6    = 5446,

    SPELL_GRIMSTONE_TELEPORT = 6422,
};

namespace
{
uint32 const BrdRingMob[] =
{
    8925,                                                   // Dredge Worm
    8926,                                                   // Deep Stinger
    8927,                                                   // Dark Screecher
    8928,                                                   // Burrowing Thundersnout
    8933,                                                   // Cave Creeper
    8932,                                                   // Borer Beetle
};

uint32 const BrdRingBoss[] =
{
    9027,                                                   // Gorosh
    9028,                                                   // Grizzle
    9029,                                                   // Eviscerator
    9030,                                                   // Ok'thor
    9031,                                                   // Anub'shiah
    9032,                                                   // Hedrum
};

// VMaNGOS script_waypoint entry 10096 (Start(false): walk)
BrdEscortPoint const GrimstonePath[] =
{
    { 0, 604.803f, -191.082f, -54.0586f, 0 },   // ring
    { 1, 604.073f, -222.107f, -52.7438f, 0 },   // first gate
    { 2, 621.4f,   -214.499f, -52.8145f, 0 },   // hiding in corner
    { 3, 601.301f, -198.557f, -53.9503f, 0 },   // ring
    { 4, 631.818f, -180.548f, -52.6548f, 0 },   // second gate
    { 5, 627.39f,  -201.076f, -52.6929f, 0 }    // hiding in corner
};
}

struct classic_npc_grimstone : public EscortAI
{
    classic_npc_grimstone(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        MobSpawnId = urand(0, 5);
        BrdLoadEscortPath(this, GrimstonePath, false);
        ResetState();
    }

    InstanceScript* m_pInstance;

    uint8 EventPhase;
    uint32 Event_Timer;

    uint8 MobSpawnId;
    uint8 MobCount;
    uint32 MobDeath_Timer;

    ObjectGuid RingMobGUID[MAX_MOB_AMOUNT];
    ObjectGuid RingBossGUID;

    ObjectGuid ChallengeMobGUID[4];

    bool ArenaChallenge;

    bool CanWalk;

    bool GroupIsWiped;

    void ResetState()
    {
        EventPhase = 0;
        Event_Timer = 1000;

        MobCount = 0;
        MobDeath_Timer = 0;

        for (ObjectGuid& guid : RingMobGUID)
            guid.Clear();

        for (ObjectGuid& guid : ChallengeMobGUID)
            guid.Clear();

        RingBossGUID.Clear();

        CanWalk = false;
        ArenaChallenge = false;
        GroupIsWiped = false;
    }

    void Reset() override
    {
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // VMaNGOS UNIT_FLAG_SPAWNING
        ResetState();
    }

    // VMaNGOS only calls npc_escortAI::UpdateAI() while CanWalk is set (its escort moves point by point).
    // TC EscortAI moves the whole path with one movement generator, so CanWalk pauses/resumes the escort instead.
    void SetCanWalk(bool canWalk)
    {
        CanWalk = canWalk;
        if (!canWalk && !HasEscortState(STATE_ESCORT_PAUSED))
            SetEscortPaused(true);
        else if (canWalk && HasEscortState(STATE_ESCORT_PAUSED))
            SetEscortPaused(false);
    }

    void DoGate(uint32 id, GOState state)
    {
        if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(id)))
            go->SetGoState(state);

        TC_LOG_DEBUG("scripts", "classic_npc_grimstone, arena gate update state.");
    }

    //TODO: move them to center
    void SummonRingMob()
    {
        // No array overflow
        if (MobCount >= MAX_MOB_AMOUNT)
            return;
        if (Creature* tmp = me->SummonCreature(BrdRingMob[MobSpawnId], 608.960f, -235.322f, -53.907f, 1.857f, TEMPSUMMON_DEAD_DESPAWN, 0s))
        {
            RingMobGUID[MobCount] = tmp->GetGUID();
            tmp->GetMotionMaster()->MovePoint(1, 596.285156f, -188.698944f, -54.132176f);
            tmp->SetHomePosition(596.285156f, -188.698944f, -54.132176f, 0);
            CreatureAI::DoZoneInCombat(tmp);    // VMaNGOS SetInCombatWithZone()

            ++MobCount;
        }

        if (MobCount == MAX_MOB_AMOUNT)
            MobDeath_Timer = 2500;
    }

    void SummonRingBoss()
    {
        float spawnX, spawnY, spawnZ, spawnO;
        float homeX, homeY, homeZ, homeO;
        spawnX = 644.300f;
        spawnY = -175.989f;
        spawnZ = -53.739f;
        spawnO = 3.418f;

        homeX = 596.285156f;
        homeY = -188.698944f;
        homeZ = -54.132176f;
        homeO = 0;

        // T0.5 Challenge has been put down, summon Theldren and his random adds
        if (m_pInstance->GetData(DATA_THELDREN) == IN_PROGRESS)
        {
            Player* challenger = ObjectAccessor::GetPlayer(*me, m_pInstance->GetGuidData(DATA_ARENA_CHALLENGER));
            if (!challenger)
            {
                TC_LOG_ERROR("scripts", "[Blackrock Depths] Ring of Law challenger player not found!");
                return;
            }

            ArenaChallenge = true;

            // Can spawn up to 5 creatures. One is guaranteed to be Theldren, a DPS warrior
            // Always have at least one 'healer', priest or shaman
            // The last three are DPS. Can both be ranged, both melee or one from each.
            // https://web.archive.org/web/20060523124717/http://wow.allakhazam.com:80/db/quest.html?wquest=9015&mid=114423967727961

            uint32 HealerEntries[2];
            uint32 DPSEntries[6];

            HealerEntries[0] = 16053;
            HealerEntries[1] = 16055;

            DPSEntries[0] = 16058;
            DPSEntries[1] = 16051;
            DPSEntries[2] = 16052;
            DPSEntries[3] = 16049;
            DPSEntries[4] = 16050;
            DPSEntries[5] = 16054;
            // Offset spawns slightly so the NPCs aren't stacked
            if (Creature* healer = me->SummonCreature(HealerEntries[urand(0, 1)], spawnX + 3, spawnY - 1, spawnZ - 1, spawnO, TEMPSUMMON_DEAD_DESPAWN, 0s))
            {
                healer->GetMotionMaster()->MovePoint(1, spawnX, spawnY, spawnZ);
                healer->SetHomePosition(homeX, homeY, homeZ, homeO);
                CreatureAI::DoZoneInCombat(healer);
                ChallengeMobGUID[0] = healer->GetGUID();
                ++MobCount;
            }

            // Spawn 3 more random DPS!
            for (uint8 i = 0; i < 3; ++i)
            {
                float x, y, z;
                x = spawnX + 1.5f;
                y = spawnY - 3 + 3 * i;
                z = spawnZ;

                if (Creature* dps = me->SummonCreature(DPSEntries[urand(0, 5)], x, y, z, spawnO, TEMPSUMMON_DEAD_DESPAWN, 0s))
                {
                    dps->GetMotionMaster()->MovePoint(1, spawnX, spawnY, spawnZ);
                    dps->SetHomePosition(homeX, homeY, homeZ, homeO);
                    CreatureAI::DoZoneInCombat(dps);
                    ChallengeMobGUID[i + 1] = dps->GetGUID();
                    ++MobCount;
                }
            }

            // Lastly, spawn Theldren
            if (Creature* theldren = me->SummonCreature(NPC_THELDREN, spawnX, spawnY, spawnZ, spawnO, TEMPSUMMON_DEAD_DESPAWN, 0s))
            {
                RingBossGUID = theldren->GetGUID();
                theldren->GetMotionMaster()->MovePoint(1, spawnX, spawnY, spawnZ);
                theldren->SetHomePosition(homeX, homeY, homeZ, homeO);
                CreatureAI::DoZoneInCombat(theldren);
                ++MobCount;
            }
        }
        else
        {
            if (Creature* tmp = me->SummonCreature(BrdRingBoss[urand(0, 5)], spawnX, spawnY, spawnZ, spawnO, TEMPSUMMON_DEAD_DESPAWN, 0s))
            {
                RingBossGUID = tmp->GetGUID();
                tmp->GetMotionMaster()->MovePoint(1, 596.285156f, -188.698944f, -54.132176f);
                tmp->SetHomePosition(homeX, homeY, homeZ, homeO);
                CreatureAI::DoZoneInCombat(tmp);
                ++MobCount;
            }
        }

        MobDeath_Timer = 2500;
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_GRIMSTONE_ARENA2, me);
                SetCanWalk(false);
                Event_Timer = 5000;
                break;
            case 1:
                ClassicScriptText(SAY_GRIMSTONE_ARENA3, me);
                SetCanWalk(false);
                Event_Timer = 5000;
                break;
            case 2:
                SetCanWalk(false);
                break;
            case 3:
                //ClassicScriptText(SAY_GRIMSTONE_ARENA4, me);//5
                break;
            case 4:
                ClassicScriptText(SAY_GRIMSTONE_ARENA6, me);
                SetCanWalk(false);
                Event_Timer = 5000;
                break;
            case 5:
                if (m_pInstance)
                {
                    m_pInstance->SetData(TYPE_RING_OF_LAW, DONE);

                    if (m_pInstance->GetData(DATA_THELDREN) == IN_PROGRESS)
                        m_pInstance->SetData(DATA_THELDREN, DONE);
                    TC_LOG_DEBUG("scripts", "classic_npc_grimstone: event reached end and set complete.");
                }
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance)
            return;

        if (MobDeath_Timer)
        {
            if (MobDeath_Timer <= diff)
            {
                MobDeath_Timer = 2500;

                if (!RingBossGUID.IsEmpty())
                {
                    Creature* boss = me->GetMap()->GetCreature(RingBossGUID);
                    if (boss && !boss->IsAlive() && boss->isDead())
                    {
                        RingBossGUID.Clear();
                        Event_Timer = 5000;
                        MobDeath_Timer = 0;

                        --MobCount;
                        // End of the event if boss dies, even if some adds are left alive
                        return;
                    }

                    if (ArenaChallenge)
                    {
                        for (ObjectGuid& guid : ChallengeMobGUID)
                        {
                            Creature* mob = me->GetMap()->GetCreature(guid);
                            if (mob && !mob->IsAlive() && mob->isDead())
                            {
                                guid.Clear();
                                --MobCount;
                            }
                        }
                    }
                }
                else
                {
                    for (ObjectGuid& guid : RingMobGUID)
                    {
                        Creature* mob = me->GetMap()->GetCreature(guid);
                        if (mob && !mob->IsAlive() && mob->isDead())
                        {
                            guid.Clear();
                            --MobCount;

                            //seems all are gone, so set timer to continue and discontinue this
                            if (!MobCount)
                            {
                                Event_Timer = 10000;
                                MobDeath_Timer = 0;
                            }
                        }
                    }
                }

                // Group wiped?
                if (CheckForWipe())
                {
                    GroupIsWiped = true;
                    return;
                }
            }
            else
                MobDeath_Timer -= diff;
        }

        if (Event_Timer)
        {
            if (Event_Timer <= diff)
            {
                switch (EventPhase)
                {
                    case 0:
                        ClassicScriptText(SAY_GRIMSTONE_ARENA1, me);
                        DoGate(DATA_ARENA4, GO_STATE_READY);
                        Start();
                        SetCanWalk(true);
                        Event_Timer = 0;
                        break;
                    case 1:
                        SetCanWalk(true);
                        Event_Timer = 0;
                        break;
                    case 2:
                        Event_Timer = 2000;
                        break;
                    case 3:
                        DoGate(DATA_ARENA1, GO_STATE_ACTIVE);
                        Event_Timer = 3000;
                        DoCastSelf(SPELL_GRIMSTONE_TELEPORT);
                        break;
                    case 4:
                        SetCanWalk(true);
                        me->SetVisible(false);
                        SummonRingMob();
                        Event_Timer = 3000;
                        break;
                    case 5:
                        SummonRingMob();
                        SummonRingMob();
                        Event_Timer = 4000;
                        break;
                    case 6:
                        SummonRingMob();
                        Event_Timer = 12000;
                        break;
                    case 7:
                        MobSpawnId = urand(0, 5);
                        SummonRingMob();
                        Event_Timer = 3000;
                        break;
                    case 8:
                        SummonRingMob();
                        SummonRingMob();
                        me->SetVisible(true);
                        ClassicScriptText(SAY_GRIMSTONE_ARENA4, me);
                        Event_Timer = 4000;
                        break;
                    case 9:
                        SummonRingMob();
                        ClassicScriptText(SAY_GRIMSTONE_ARENA5, me);
                        me->SetVisible(false);
                        Event_Timer = 0;
                        break;
                    case 10:
                        me->SetVisible(true);
                        DoGate(DATA_ARENA1, GO_STATE_READY);
                        SetCanWalk(true);
                        Event_Timer = 0;
                        break;
                    case 11:
                        DoGate(DATA_ARENA2, GO_STATE_ACTIVE);
                        Event_Timer = 3000;
                        break;
                    case 12:
                        DoCastSelf(SPELL_GRIMSTONE_TELEPORT);
                        Event_Timer = 2000;
                        break;
                    case 13:
                        me->SetVisible(false);
                        SummonRingBoss();
                        Event_Timer = 0;
                        break;
                    case 14:
                        //if quest, complete
                        DoGate(DATA_ARENA2, GO_STATE_READY);
                        DoGate(DATA_ARENA3, GO_STATE_ACTIVE);
                        DoGate(DATA_ARENA4, GO_STATE_ACTIVE);
                        SetCanWalk(true);
                        Event_Timer = 0;
                        break;
                }
                ++EventPhase;
            }
            else
                Event_Timer -= diff;
        }

        EscortAI::UpdateAI(diff);
    }

    bool CheckForWipe()
    {
        if (GroupIsWiped)
            return true;
        // If there are no players within the vicinity of Grimstone in combat
        // and there are mobs alive, it's a wipe
        bool wiped = MobCount > 0;
        if (!wiped)
            return wiped;

        for (MapReference const& itr : me->GetMap()->GetPlayers())
        {
            Player* player = itr.GetSource();

            if (player && player->IsWithinDistInMap(me, 80.0f) && player->IsInCombat())
            {
                wiped = false;
                break;
            }
        }

        if (wiped)
        {
            // Players wiped, open the gates.
            DoGate(DATA_ARENA1, GO_STATE_READY);
            DoGate(DATA_ARENA2, GO_STATE_READY);
            DoGate(DATA_ARENA4, GO_STATE_ACTIVE); // jail entrance

            // If the phase is before the boss has spawned, reset the event
            if (!RingBossGUID)
            {
                m_pInstance->SetData(TYPE_RING_OF_LAW, NOT_STARTED);

                for (ObjectGuid const& guid : RingMobGUID)
                    if (Creature* mob = me->GetMap()->GetCreature(guid))
                        mob->DespawnOrUnsummon();

                ResetState();
                me->DespawnOrUnsummon();
            }
        }

        return wiped;
    }

    void PlayerEnteredArena(Player* player)
    {
        // Re-enter zone after wipe, have boss. Close gates
        if (GroupIsWiped)
        {
            if (Creature* boss = me->GetMap()->GetCreature(RingBossGUID))
            {
                DoGate(DATA_ARENA4, GO_STATE_READY); // jail entrance

                // Charge!
                boss->SetInCombatWith(player);
            }

            GroupIsWiped = false;
        }
    }
};

class classic_at_ring_of_law : public AreaTriggerScript
{
public:
    classic_at_ring_of_law() : AreaTriggerScript("classic_at_ring_of_law") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
    {
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            if (instance->GetData(TYPE_RING_OF_LAW) == IN_PROGRESS || instance->GetData(TYPE_RING_OF_LAW) == DONE)
            {
                // Player triggered ring of law while it's in progress. Might have been after a wipe
                if (Creature* creature = player->GetMap()->GetCreature(instance->GetGuidData(NPC_GRIMSTONE)))
                {
                    if (classic_npc_grimstone* grimstoneAI = dynamic_cast<classic_npc_grimstone*>(creature->AI()))
                        grimstoneAI->PlayerEnteredArena(player);
                }
                return false;
            }

            instance->SetData(TYPE_RING_OF_LAW, IN_PROGRESS);
            player->SummonCreature(NPC_GRIMSTONE, 625.559f, -205.618f, -52.735f, 2.609f, TEMPSUMMON_DEAD_DESPAWN, 0s);

            return false;
        }
        return false;
    }
};

/*######
## mob_phalanx
######*/

enum Phalanx
{
    SPELL_THUNDERCLAP       = 15588,
    SPELL_FIREBALLVOLLEY    = 15285,
    SPELL_MIGHTYBLOW        = 14099,

    YELL_PHALANX_AGGRO      = 5300
};

struct classic_mob_phalanx : public ScriptedAI
{
    classic_mob_phalanx(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_bActivated = false;
        ResetTimers();
    }

    InstanceScript* m_pInstance;

    uint32 ThunderClap_Timer;
    uint32 FireballVolley_Timer;
    uint32 MightyBlow_Timer;

    uint32 m_uiCallPatrolTimer;

    float m_fKeepDoorOrientation;
    bool m_bActivated;

    void ResetTimers()
    {
        m_fKeepDoorOrientation = 2.06059f;
        m_uiCallPatrolTimer    = 0;

        ThunderClap_Timer      = 12000;
        FireballVolley_Timer   = 0;
        MightyBlow_Timer       = 15000;
    }

    void Reset() override
    {
        ResetTimers();
    }

    void Activate()
    {
        if (m_bActivated || !m_pInstance)
            return;

        if (m_pInstance->GetData(TYPE_PLUGGER) == DONE || m_pInstance->GetData(TYPE_PLUGGER) == IN_PROGRESS)
        {
            m_uiCallPatrolTimer = 10000;
            m_pInstance->SetData(TYPE_PLUGGER, DONE);
        }
        ClassicScriptText(YELL_PHALANX_AGGRO, me);
        me->SetHomePosition(868.122f, -223.884f, -43.695f, m_fKeepDoorOrientation);
        me->GetMotionMaster()->MovePoint(0, 865.555f, -219.056f, -43.70f);
        me->SetFaction(14);
        m_bActivated = true;
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (pointId == 0)
            me->GetMotionMaster()->MovePoint(1, 868.122f, -223.884f, -43.695f, true, 2.06059f);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance)
            return;

        if (m_uiCallPatrolTimer)
        {
            if (m_uiCallPatrolTimer <= diff && m_pInstance->GetData(TYPE_PATROL) != DONE)
            {
                m_pInstance->SetData(TYPE_PATROL, IN_PROGRESS);
                m_uiCallPatrolTimer = 0;
            }
            else if (m_uiCallPatrolTimer > diff)
                m_uiCallPatrolTimer -= diff;
            else
                m_uiCallPatrolTimer = 0;    // VMaNGOS underflows the timer here (patrol already done)
        }

        if (!UpdateVictim())
            return;

        //ThunderClap_Timer
        if (ThunderClap_Timer < diff)
        {
            DoCastVictim(SPELL_THUNDERCLAP);
            ThunderClap_Timer = 10000;
        }
        else ThunderClap_Timer -= diff;

        //FireballVolley_Timer
        if (me->GetHealthPct() < 51.0f)
        {
            if (FireballVolley_Timer < diff)
            {
                DoCastVictim(SPELL_FIREBALLVOLLEY);
                FireballVolley_Timer = 15000;
            }
            else FireballVolley_Timer -= diff;
        }

        //MightyBlow_Timer
        if (MightyBlow_Timer < diff)
        {
            DoCastVictim(SPELL_MIGHTYBLOW);
            MightyBlow_Timer = 10000;
        }
        else MightyBlow_Timer -= diff;
    }
};

/*######
## npc_mistress_nagmara
######*/

enum MistressNagmara
{
    GOSSIP_MENU_NAGMARA         = 2076,     // gossip_menu_option 2076/0 = VMaNGOS broadcast text 5040
    GOSSIP_ID_NAGMARA           = 2727,
    GOSSIP_ID_NAGMARA_2         = 2729,
    SPELL_POTION_LOVE           = 14928,
    SPELL_NAGMARA_ROCKNOT       = 15064,

    SAY_NAGMARA_1               = 5000,
    SAY_NAGMARA_2               = 5001,
    TEXTEMOTE_NAGMARA           = 5002,
    TEXTEMOTE_ROCKNOT           = 5003,

    QUEST_POTION_LOVE           = 4201
};

struct classic_npc_mistress_nagmara : public ScriptedAI
{
    classic_npc_mistress_nagmara(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_uiPhase = 0;
        m_uiPhaseTimer = 0;
    }

    InstanceScript* m_pInstance;
    ObjectGuid m_rocknotGuid;       // VMaNGOS caches a Creature* pRocknot

    uint8 m_uiPhase;
    uint32 m_uiPhaseTimer;

    void Reset() override
    {
        m_uiPhase = 0;
        m_uiPhaseTimer = 0;
    }

    void DoPotionOfLoveIfCan()
    {
        if (!m_pInstance)
            return;

        Creature* rocknot = me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_ROCKNOT));
        if (!rocknot)
            return;

        m_rocknotGuid = rocknot->GetGUID();

        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        rocknot->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);

        me->GetMotionMaster()->MoveIdle();
        me->GetMotionMaster()->MoveFollow(rocknot, 2.0f, ChaseAngle(0.0f));
        m_uiPhase = 1;
    }

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (me->IsQuestGiver())
            player->PrepareQuestMenu(me->GetGUID());

        if (player->GetQuestRewardStatus(QUEST_POTION_LOVE))
        {
            AddGossipItemFor(player, GOSSIP_MENU_NAGMARA, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            SendGossipMenuFor(player, GOSSIP_ID_NAGMARA_2, me->GetGUID());
        }
        else
            SendGossipMenuFor(player, GOSSIP_ID_NAGMARA, me->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF + 1:
                CloseGossipMenuFor(player);
                DoPotionOfLoveIfCan();
                break;
        }
        return true;
    }

    void OnQuestReward(Player* /*player*/, Quest const* quest, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (!m_pInstance)
            return;

        if (quest->GetQuestId() == QUEST_POTION_LOVE)
            DoPotionOfLoveIfCan();
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_uiPhaseTimer)
        {
            if (m_uiPhaseTimer <= diff)
                m_uiPhaseTimer = 0;
            else
            {
                m_uiPhaseTimer -= diff;
                return;
            }
        }

        Creature* rocknot = !m_rocknotGuid.IsEmpty() ? me->GetMap()->GetCreature(m_rocknotGuid) : nullptr;
        if (!rocknot)
            return;

        switch (m_uiPhase)
        {
            case 0:     // Phase 0 : Nagmara patrols in the bar to serve patrons or is following Rocknot passively
                break;
            case 1:     // Phase 1 : Nagmara is moving towards Rocknot
                if (me->IsWithinDist2d(rocknot->GetPositionX(), rocknot->GetPositionY(), 5.0f))
                {
                    me->GetMotionMaster()->MoveIdle();
                    me->SetFacingToObject(rocknot);
                    rocknot->SetFacingToObject(me);
                    ClassicScriptText(SAY_NAGMARA_1, me);
                    m_uiPhase++;
                    m_uiPhaseTimer = 5000;
                }
                else
                    me->GetMotionMaster()->MoveFollow(rocknot, 2.0f, ChaseAngle(0.0f));
                break;
            case 2:     // Phase 2 : Nagmara is "seducing" Rocknot
                ClassicScriptText(SAY_NAGMARA_2, me);
                m_uiPhaseTimer = 4000;
                m_uiPhase++;
                break;
            case 3:     // Phase 3: Nagmara give potion to Rocknot and Rocknot escort AI will handle the next part of the event
                if (DoCastSelf(SPELL_POTION_LOVE) == SPELL_CAST_OK)
                {
                    m_uiPhase = 0;
                    if (m_pInstance)
                        m_pInstance->SetData(TYPE_NAGMARA, SPECIAL);
                    me->GetMotionMaster()->MoveFollow(rocknot, 2.0f, ChaseAngle(0.0f));
                }
                break;
            case 4:     // Phase 4 : make the lovers face each other
                me->SetFacingToObject(rocknot);
                rocknot->SetFacingToObject(me);
                m_uiPhaseTimer = 4000;
                m_uiPhase++;
                if (m_pInstance)
                    m_pInstance->SetData(TYPE_NAGMARA, DONE);
                break;
            case 5:     // Phase 5 : Nagmara and Rocknot are under the stair kissing (this phase repeats endlessly)
                ClassicScriptText(TEXTEMOTE_NAGMARA, me);
                ClassicScriptText(TEXTEMOTE_ROCKNOT, rocknot);
                DoCastSelf(SPELL_NAGMARA_ROCKNOT);
                rocknot->CastSpell(rocknot, SPELL_NAGMARA_ROCKNOT, false);
                m_uiPhaseTimer = 12000;
                break;
        }
    }
};

/*######
## npc_rocknot
######*/

enum PrivateRocknot
{
    SAY_GOT_BEER       = 5172,
    SAY_MORE_BEER      = 5166,
    SAY_BARREL_1       = 5167,
    SAY_BARREL_2       = 5168,
    SAY_BARREL_3       = 5169,

    SPELL_DRUNKEN_RAGE = 14872,

    QUEST_ALE          = 4295
};

namespace
{
float const aPosNagmaraRocknot[3] = { 878.1779f, -222.0662f, -49.96714f };

// VMaNGOS script_waypoint entry 9503, points 0..8 (beer event; stops "home" at 8)
BrdEscortPoint const RocknotBeerPath[] =
{
    { 0, 885.185f, -194.007f, -43.4584f, 0 },
    { 1, 885.185f, -194.007f, -43.4584f, 0 },
    { 2, 872.764f, -185.606f, -43.7037f, 5000 },    // b1
    { 3, 867.923f, -188.006f, -43.7037f, 5000 },    // b2
    { 4, 863.296f, -190.795f, -43.7037f, 5000 },    // b3
    { 5, 856.14f,  -194.653f, -43.7037f, 5000 },    // b4
    { 6, 851.879f, -196.928f, -43.7037f, 15000 },   // b5
    { 7, 877.035f, -187.048f, -43.7037f, 0 },
    { 8, 891.198f, -197.924f, -43.6204f, 0 }        // home
};

// VMaNGOS script_waypoint entry 9503: the Nagmara escort starts at point 0 and jumps to point 9 (setCurrentWP(9))
BrdEscortPoint const RocknotNagmaraPath[] =
{
    { 0,  885.185f, -194.007f, -43.4584f, 0 },
    { 9,  876.935f, -189.007f, -43.4584f, 0 },      // Nagmara escort
    { 10, 885.185f, -194.007f, -43.4584f, 0 },
    { 11, 869.124f, -202.852f, -43.7088f, 0 },
    { 12, 869.465f, -202.878f, -43.4588f, 0 },
    { 13, 864.244f, -210.826f, -43.459f,  0 },
    { 14, 866.824f, -220.959f, -43.4472f, 0 },
    { 15, 867.074f, -221.959f, -43.4472f, 0 },
    { 16, 870.419f, -225.675f, -43.5566f, 2000 },   // open door
    { 17, 872.169f, -227.425f, -43.5566f, 0 },
    { 18, 872.919f, -228.175f, -43.5566f, 0 },
    { 19, 875.919f, -230.925f, -43.5566f, 0 },
    { 20, 876.919f, -230.175f, -43.5566f, 0 },
    { 21, 877.919f, -229.425f, -43.5566f, 0 },
    { 22, 882.395f, -225.949f, -46.7405f, 0 },
    { 23, 885.895f, -223.699f, -49.2405f, 0 },
    { 24, 887.645f, -222.449f, -49.2405f, 0 },
    { 25, 885.937f, -223.351f, -49.2954f, 0 },
    { 26, 887.437f, -222.351f, -49.2954f, 0 },
    { 27, 888.937f, -221.601f, -49.5454f, 0 },
    { 28, 887.687f, -220.101f, -49.5454f, 0 },
    { 29, 886.687f, -218.851f, -49.5454f, 0 },
    { 30, 887.567f, -220.04f,  -49.7059f, 0 },
    { 31, 886.567f, -218.79f,  -49.7059f, 0 },
    { 32, 886.067f, -218.29f,  -49.7059f, 0 },
    { 33, 880.825f, -221.389f, -49.9562f, 1000 },   // stop
    { 34, 880.825f, -221.389f, -49.9562f, 0 }
};
}

struct classic_npc_rocknot : public EscortAI
{
    classic_npc_rocknot(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_fInitialOrientation   = 3.21141f;
        m_uiBreakKegTimer       = 0;
        m_uiBreakDoorTimer      = 0;
        m_uiEmoteTimer          = 0;
        m_uiBarReactTimer       = 0;
        m_currentPath           = nullptr;
        // VMaNGOS pauses the escort forever at the last used point instead of ending it
        SetDespawnAtEnd(false);
    }

    InstanceScript* m_pInstance;

    uint32 m_uiBreakKegTimer;
    uint32 m_uiBreakDoorTimer;
    uint32 m_uiEmoteTimer;
    uint32 m_uiBarReactTimer;

    float m_fInitialOrientation;

    BrdEscortPoint const* m_currentPath;

    Creature* GetNagmara() const
    {
        return m_pInstance ? me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_NAGMARA)) : nullptr;
    }

    void Reset() override
    {
        if (!m_pInstance)
            return;

        if (HasEscortState(STATE_ESCORT_ESCORTING))
            return;

        m_fInitialOrientation   = 3.21141f;
        m_uiBreakKegTimer       = 0;
        m_uiBreakDoorTimer      = 0;
        m_uiEmoteTimer          = 0;
        m_uiBarReactTimer       = 0;
    }

    // VMaNGOS npc_escortAI::Start() does not touch the npc flags (TC EscortAI::Start() clears them)
    template <size_t N>
    void StartRocknotEscort(BrdEscortPoint const (&points)[N], bool instantRespawn)
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))     // VMaNGOS Start() refuses to start while escorting
            return;

        m_currentPath = points;
        BrdLoadEscortPath(this, points, false);
        NPCFlags npcFlags = me->GetNpcFlags();
        Start(true, ObjectGuid::Empty, nullptr, instantRespawn);
        me->ReplaceAllNpcFlags(npcFlags);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (!m_pInstance || !m_currentPath)
            return;

        Creature* nagmara = GetNagmara();

        switch (m_currentPath[waypointId].PointId)
        {
            case 0:     // if Nagmara and Potion of Love event is in progress, switch to second part of the escort
                // VMaNGOS setCurrentWP(9) when TYPE_NAGMARA == IN_PROGRESS: done by starting RocknotNagmaraPath
                break;
            case 2:
                ClassicScriptText(SAY_BARREL_1, me);
                break;
            case 3:
                ClassicScriptText(SAY_BARREL_2, me);
                break;
            case 4:
                ClassicScriptText(SAY_BARREL_2, me);
                break;
            case 5:
                ClassicScriptText(SAY_BARREL_1, me);
                break;
            case 6:
                DoCastSelf(SPELL_DRUNKEN_RAGE);
                m_uiBreakKegTimer = 2000;
                break;
            case 8:     // Back home stop here (last point of RocknotBeerPath: the escort ends there)
                me->SetFacingTo(m_fInitialOrientation);
                break;
            case 9:     // This step is the start of the "alternate" waypoint path used with Nagmara
                // Make Nagmara follow Rocknot
                if (!nagmara)
                {
                    // TODO(classic): VMaNGOS jumps back to point 8 (setCurrentWP(8)) and walks home; the TC escort just stops here
                    SetEscortPaused(true);
                }
                else
                    nagmara->GetMotionMaster()->MoveFollow(me, 2.0f, ChaseAngle(0.0f));
                break;
            case 16:
                // Open the bar back door if relevant
                if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(DATA_GO_BAR_DOOR)))
                {
                    if (go->GetGoState() == GO_STATE_READY) // Closed
                        go->SetGoState(GO_STATE_ACTIVE);
                }
                if (nagmara)
                    nagmara->GetMotionMaster()->MoveFollow(me, 2.0f, ChaseAngle(0.0f));
                break;
            case 33: // Reach under the stair, make Nagmara move to her position and give the handle back to Nagmara AI script
                if (!nagmara)
                    break;

                nagmara->GetMotionMaster()->MoveIdle();
                nagmara->GetMotionMaster()->MovePoint(0, aPosNagmaraRocknot[0], aPosNagmaraRocknot[1], aPosNagmaraRocknot[2]);
                if (classic_npc_mistress_nagmara* nagmaraAI = dynamic_cast<classic_npc_mistress_nagmara*>(nagmara->AI()))
                {
                    nagmaraAI->m_uiPhase = 4;
                    nagmaraAI->m_uiPhaseTimer = 5000;
                }
                SetEscortPaused(true);
                break;
        }
    }

    void OnQuestReward(Player* player, Quest const* quest, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (!m_pInstance)
            return;

        if (m_pInstance->GetData(TYPE_ROCKNOT) == DONE || m_pInstance->GetData(TYPE_ROCKNOT) == SPECIAL)
            return;

        if (quest->GetQuestId() == QUEST_ALE)
        {
            if (m_pInstance->GetData(TYPE_ROCKNOT) != IN_PROGRESS)
                m_pInstance->SetData(TYPE_ROCKNOT, IN_PROGRESS);

            me->SetFacingToObject(player);
            ClassicScriptText(SAY_GOT_BEER, me);
            m_uiEmoteTimer = 1500;

            // We keep track of amount of beers given in the instance script by setting data to SPECIAL
            // Once the correct amount is reached, the script will also returns SPECIAL, if not, it returns IN_PROGRESS/DONE
            // the return state and the following of the script are handled in the Update->emote part of the Rocknot NPC escort AI script
            m_pInstance->SetData(TYPE_ROCKNOT, SPECIAL);
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_pInstance)
            return;

        // When Nagmara is in Potion of Love event and reach Rocknot, she set TYPE_NAGMARA to SPECIAL
        // in order to make Rocknot start the second part of his escort quest
        if (m_pInstance->GetData(TYPE_NAGMARA) == SPECIAL)
        {
            m_pInstance->SetData(TYPE_NAGMARA, IN_PROGRESS);
            StartRocknotEscort(RocknotNagmaraPath, true);
            return;
        }

        if (m_uiBreakKegTimer)
        {
            if (m_uiBreakKegTimer <= diff)
            {
                if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(DATA_GO_BAR_KEG)))
                {
                    go->SetGoState(GO_STATE_ACTIVE);
                    m_uiBreakKegTimer  = 0;
                    m_uiBreakDoorTimer = 1000;
                    m_uiBarReactTimer  = 5000;
                }
            }
            else
                m_uiBreakKegTimer -= diff;
        }

        if (m_uiBreakDoorTimer)
        {
            if (m_uiBreakDoorTimer <= diff)
            {
                // Open the bar back door if relevant
                if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(DATA_GO_BAR_DOOR)))
                {
                    if (go->GetGoState() == GO_STATE_READY) // Closed
                        go->SetGoState(GO_STATE_DESTROYED);         // VMaNGOS GO_STATE_ACTIVE_ALTERNATIVE
                }

                ClassicScriptText(SAY_BARREL_3, me);
                if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(DATA_GO_BAR_KEG_TRAP)))
                    go->SetGoState(GO_STATE_ACTIVE);                  // doesn't work very well, leaving code here for future
                // spell by trap has effect61

                m_uiBreakDoorTimer = 0;
            }
            else
                m_uiBreakDoorTimer -= diff;
        }

        if (m_uiBarReactTimer)
        {
            if (m_uiBarReactTimer <= diff)
            {
                // Activate Phalanx and handle nearby patrons says
                if (Creature* phalanx = me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_PHALANX)))
                {
                    if (classic_mob_phalanx* phalanxAI = dynamic_cast<classic_mob_phalanx*>(phalanx->AI()))
                        if (phalanx->IsAlive())
                            phalanxAI->Activate();
                }
                m_pInstance->SetData(TYPE_ROCKNOT, DONE);

                m_uiBarReactTimer = 0;
            }
            else
                m_uiBarReactTimer -= diff;
        }

        // Several times Rocknot is supposed to perform an action (text, spell cast...) followed closely by an emote
        // we handle it here
        if (m_uiEmoteTimer)
        {
            if (m_uiEmoteTimer <= diff)
            {
                // If event is SPECIAL (Rocknot moving to barrel), then we want him to say a special text and start moving
                // if not, he is still accepting beers, so we want him to cheer player
                if (m_pInstance->GetData(TYPE_ROCKNOT) == SPECIAL)
                {
                    ClassicScriptText(SAY_MORE_BEER, me);
                    StartRocknotEscort(RocknotBeerPath, false);
                }
                else
                    me->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);

                m_uiEmoteTimer = 0;
            }
            else
                m_uiEmoteTimer -= diff;
        }
    }
};

/*######
## go_dark_keeper_portrait
######*/

enum DarkKeeperPortrait
{
    NPC_DARK_KEEPER_VORFALK    = 9437,
    NPC_DARK_KEEPER_BETHEK     = 9438,
    NPC_DARK_KEEPER_UGGEL      = 9439,
    NPC_DARK_KEEPER_ZIMREL     = 9441,
    NPC_DARK_KEEPER_OFGUT      = 9442,
    NPC_DARK_KEEPER_PELVER     = 9443,

    GO_VORFALK                 = 164820,
    GO_BETHEK                  = 164821,
    GO_UGGEL                   = 164822,
    GO_ZIMREL                  = 164823,
    GO_OFGUT                   = 164824,
    GO_PELVER                  = 164825,
};

struct classic_go_dark_keeper_portrait : public GameObjectAI
{
    classic_go_dark_keeper_portrait(GameObject* go) : GameObjectAI(go) { }

    void SummonKeeper(Player* player, uint32 npcEntry, float x, float y, float z, float o, uint32 goEntry)
    {
        player->SummonCreature(npcEntry, x, y, z, o, TEMPSUMMON_DEAD_DESPAWN, 0s);
        player->SummonGameObject(goEntry, 831.54f, -339.529f, -46.682f, 0.802851f, QuaternionData::fromEulerAnglesZYX(0.802851f, 0.0f, 0.0f), 0s);
    }

    bool OnGossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();

        if (!instance)
            return true;

        if (instance->GetData(TYPE_VAULT) != DONE)
        {
            switch (urand(0, 5))
            {
                case 0:
                    SummonKeeper(player, NPC_DARK_KEEPER_VORFALK, 815.60f, -168.54f, -49.75f, 5.97f, GO_VORFALK);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
                case 1:
                    SummonKeeper(player, NPC_DARK_KEEPER_BETHEK, 846.66f, -317.18f, -50.29f, 3.90f, GO_BETHEK);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
                case 2:
                    SummonKeeper(player, NPC_DARK_KEEPER_UGGEL, 963.27f, -343.73f, -71.74f, 2.22f, GO_UGGEL);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
                case 3:
                    SummonKeeper(player, NPC_DARK_KEEPER_ZIMREL, 545.49f, -162.49f, -35.46f, 5.86f, GO_ZIMREL);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
                case 4:
                    SummonKeeper(player, NPC_DARK_KEEPER_OFGUT, 681.52f, -11.55f, -60.06f, 1.98f, GO_OFGUT);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
                case 5:
                    SummonKeeper(player, NPC_DARK_KEEPER_PELVER, 803.64f, -248.00f, -43.30f, 2.60f, GO_PELVER);
                    instance->SetData(TYPE_VAULT, DONE);
                    break;
            }
        }
        return false;
    }
};

/*######
## go_thunderbrew_laguer_keg
######*/

enum ThunderbrewLaguerKeg
{
    NPC_HURLEY             = 9537,
    NPC_HURLEY_CRONY       = 9541,
};

namespace
{
// VMaNGOS creature_movement_template entry 9537 (MoveWaypoint(0, 0, 1000, 0, 0, false))
WaypointPath BuildHurleyPath()
{
    std::vector<WaypointNode> nodes;
    nodes.emplace_back(0, 855.816f, -149.763f, -49.671f, 0.575959f, Optional<Milliseconds>(), WaypointMoveType::Run);
    nodes.emplace_back(1, 884.958f, -147.708f, -49.7599f, Optional<float>(), Optional<Milliseconds>(), WaypointMoveType::Run);
    nodes.emplace_back(2, 890.479f, -147.318f, -49.7617f, Optional<float>(), Optional<Milliseconds>(), WaypointMoveType::Run);
    nodes.emplace_back(3, 896.846f, -147.319f, -49.7627f, Optional<float>(), Optional<Milliseconds>(5000ms), WaypointMoveType::Run);
    nodes.emplace_back(4, 896.846f, -147.319f, -49.7627f, Optional<float>(), Optional<Milliseconds>(), WaypointMoveType::Run);
    return WaypointPath(0, std::move(nodes), WaypointMoveType::Run);
}
}

struct classic_go_thunderbrew_laguer_keg : public GameObjectAI
{
    classic_go_thunderbrew_laguer_keg(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();

        if (!instance)
            return true;

        if (instance->GetData(TYPE_THUNDERBREW) == DONE)
            return false;

        if (instance->GetData(TYPE_THUNDERBREW) == NOT_STARTED)
            instance->SetData(TYPE_THUNDERBREW, IN_PROGRESS);

        if (instance->GetData(TYPE_THUNDERBREW) == DONE)
        {
            // Summon Hurley Blackbreath
            Creature* hurley = player->SummonCreature(NPC_HURLEY, 856.087f, -149.747f, -49.672f, 0.059f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 300s);
            if (!hurley)
                return true;

            hurley->SetWalk(false);
            hurley->GetMotionMaster()->Clear();
            // TODO(classic): VMaNGOS creature_movement_scripts 953701 (point 1: yell 4934) and 953704 (point 4: remove
            // UNIT_FLAG_IMMUNE_TO_PC, set home position, move idle, react aggressive, start script on cronies) are not ported.
            hurley->GetMotionMaster()->MovePath(BuildHurleyPath(), false);

            // Summon cronies around Hurley
            for (uint8 i = 0; i < 4; ++i)
            {
                Position pos = player->GetRandomPoint(Position(856.087f, -149.747f, -49.672f), 2.0f);
                if (Creature* summoned = player->SummonCreature(NPC_HURLEY_CRONY, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.059f, TEMPSUMMON_DEAD_DESPAWN, 0s))
                {
                    // TODO(classic): VMaNGOS JoinCreatureGroup(hurley, 3.0, i * PI/2, FORMATION_MOVE | AGGRO_TOGETHER | EVADE_TOGETHER);
                    // TC formations need spawn ids, the cronies just follow Hurley here.
                    summoned->GetMotionMaster()->MoveFollow(hurley, 3.0f, ChaseAngle(i * (float(M_PI) / 2.0f)));
                }
            }
        }

        return false;
    }
};

/*######
## go_relic_coffer_door
######*/

enum RelicCofferDoor
{
    RUINEPOIGNE_ENTRY    = 9476,
};

struct classic_go_relic_coffer_door : public GameObjectAI
{
    classic_go_relic_coffer_door(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();

        if (!instance)
            return true;

        if (instance->GetData(TYPE_RELIC_COFFER) != IN_PROGRESS || instance->GetData(TYPE_RELIC_COFFER) != DONE)
            instance->SetData(TYPE_RELIC_COFFER, IN_PROGRESS);

        instance->SetData(TYPE_RELIC_COFFER, SPECIAL);

        if (instance->GetData(TYPE_RELIC_COFFER) == DONE)
        {
            if (Creature* creature = player->SummonCreature(RUINEPOIGNE_ENTRY, 819.45f, -348.96f, -50.49f, 0.35f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 300s))
            {
                // pCreature->MonsterYell("Ne les laissez pas s'emparer du Coeur de la montagne!!", 0, pPlayer);
                creature->Yell("Don't let them take the moutain hearth!", LANG_UNIVERSAL, player);
                creature->AI()->AttackStart(player);
            }
        }

        return false;
    }
};

/*######
## npc_watchman_doomgrip
######*/

enum WatchmanDoomgrip
{
    SPELL_BOIRE_LA_POTION_DE_SOINS  = 15504,
    SPELL_FRACASSER_ARMURE          = 11971,
    NPC_WARBRINGER_CONSTRUCT        = 8905
};

struct classic_npc_watchman_doomgrip : public ScriptedAI
{
    classic_npc_watchman_doomgrip(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        BoireLaPotionDeSoins_Timer = 0;
        FracasserArmure_Timer = 1000;
    }

    InstanceScript* m_pInstance;

    uint32 BoireLaPotionDeSoins_Timer;
    uint32 FracasserArmure_Timer;

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_DOOMGRIP, DONE);
    }

    void Reset() override
    {
        BoireLaPotionDeSoins_Timer = 0;
        FracasserArmure_Timer = 1000;
    }

    void JustEngagedWith(Unit* who) override
    {
        std::list<Creature*> golems;
        GetCreatureListWithEntryInGrid(golems, me, NPC_WARBRINGER_CONSTRUCT, 20.0f);
        for (Creature* golem : golems)
        {
            if (golem->IsAlive())
            {
                golem->RemoveAurasDueToSpell(10255);
                golem->SetUninteractible(false);                    // UNIT_FLAG_UNINTERACTIBLE
                golem->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);    // UNIT_FLAG_SPAWNING
                golem->SetImmuneToNPC(false);                       // UNIT_FLAG_IMMUNE_TO_NPC
                if (who && golem->AI())
                    golem->AI()->AttackStart(who);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        //BoireLaPotionDeSoins_Timer
        if (me->GetHealthPct() < 51.0f)
        {
            if (BoireLaPotionDeSoins_Timer < diff)
            {
                DoCastVictim(SPELL_BOIRE_LA_POTION_DE_SOINS);
                BoireLaPotionDeSoins_Timer = 15000;
            }
            else BoireLaPotionDeSoins_Timer -= diff;
        }

        //FracasserArmure_Timer
        if (FracasserArmure_Timer < diff)
        {
            DoCastVictim(SPELL_FRACASSER_ARMURE);
            FracasserArmure_Timer = 10000;
        }
        else FracasserArmure_Timer -= diff;
    }
};

/*######
## npc_golem_lord_argelmach
######*/

enum GolemLordArgelmach
{
    SPELL_BOUCLIER_DE_FOUDRE    = 15507,
    SPELL_CHAINE_D_ECLAIRES     = 15305,
    SPELL_HORION                = 15605
};

struct classic_npc_golem_lord_argelmach : public ScriptedAI
{
    classic_npc_golem_lord_argelmach(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        BouclierDeFoudre_Timer = 0;
        ChaineDEclaires_Timer = 5000;
        Horion_Timer = 2000;
    }

    InstanceScript* m_pInstance;

    uint32 BouclierDeFoudre_Timer;
    uint32 ChaineDEclaires_Timer;
    uint32 Horion_Timer;

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->GetMotionMaster()->MovePoint(0, 846.801025f, 16.280600f, -53.639500f);
        //me->MonsterYell("Golems, votre Seigneur a besoin de vous!", 0, pWho);
        //me->MonsterYell(NOST_TEXT(155), 0, pWho); // seems to be custom

        if (m_pInstance)
            m_pInstance->SetData(DATA_ARGELMACH_AGGRO, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(DATA_ARGELMACH_AGGRO, DONE);
    }

    void Reset() override
    {
        BouclierDeFoudre_Timer = 0;
        ChaineDEclaires_Timer = 5000;
        Horion_Timer = 2000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        //BouclierDeFoudre_Timer
        if (BouclierDeFoudre_Timer < diff)
        {
            if (!me->HasAura(SPELL_BOUCLIER_DE_FOUDRE))
                if (DoCastSelf(SPELL_BOUCLIER_DE_FOUDRE) == SPELL_CAST_OK)
                    BouclierDeFoudre_Timer = 15000;
        }
        else BouclierDeFoudre_Timer -= diff;

        //ChaineDEclaires_Timer
        if (ChaineDEclaires_Timer < diff)
        {
            if (DoCastVictim(SPELL_CHAINE_D_ECLAIRES) == SPELL_CAST_OK)
                ChaineDEclaires_Timer = 14000;
        }
        else ChaineDEclaires_Timer -= diff;

        //Horion_Timer
        if (Horion_Timer < diff)
        {
            if (DoCastVictim(SPELL_HORION) == SPELL_CAST_OK)
                Horion_Timer = 6000;
        }
        else Horion_Timer -= diff;
    }
};

/*######
## at_shadowforge_bridge
######*/

namespace
{
float const aGuardSpawnPositions[2][4] =
{
    { 642.3660f, -274.5155f, -43.10918f, 0.4712389f },               // First guard spawn position
    { 740.1137f, -283.3448f, -42.75082f, 2.8623400f }                // Second guard spawn position
};
}

enum ShadowforgeBridge
{
    NPC_ANVILRAGE_GUARDMAN             = 8891,
    SAY_GUARD_AGGRO                    = 5271
};

// When players cross the shadowforge bridge for the first time, two guards spawn and attack.
class classic_at_shadowforge_bridge : public AreaTriggerScript
{
public:
    classic_at_shadowforge_bridge() : AreaTriggerScript("classic_at_shadowforge_bridge") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
    {
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            if (player->IsGameMaster() || !player->IsAlive() || instance->GetData(TYPE_BRIDGE) == DONE)
                return false;

            if (Creature* masterGuard = player->SummonCreature(NPC_ANVILRAGE_GUARDMAN, aGuardSpawnPositions[0][0], aGuardSpawnPositions[0][1], aGuardSpawnPositions[0][2], aGuardSpawnPositions[0][3], TEMPSUMMON_DEAD_DESPAWN, 0s))
            {
                masterGuard->SetWalk(false);
                // VMaNGOS MoveWaypoint(): entry 8891 has no creature_movement_template path, the MovePoint below replaces it anyway
                ClassicScriptText(SAY_GUARD_AGGRO, masterGuard);
                float x, y, z;
                player->GetContactPoint(masterGuard, x, y, z);
                masterGuard->GetMotionMaster()->MovePoint(1, x, y, z);

                if (Creature* slaveGuard = player->SummonCreature(NPC_ANVILRAGE_GUARDMAN, aGuardSpawnPositions[1][0], aGuardSpawnPositions[1][1], aGuardSpawnPositions[1][2], aGuardSpawnPositions[1][3], TEMPSUMMON_DEAD_DESPAWN, 0s))
                    slaveGuard->GetMotionMaster()->MoveFollow(masterGuard, 2.0f, ChaseAngle(0.0f));
            }
            instance->SetData(TYPE_BRIDGE, DONE);
        }
        return false;
    }
};

/*######
## boss_plugger_spazzring
######*/

enum PluggerSpazzring
{
    SAY_OOC_1                       = 5310,
    SAY_OOC_2                       = 5308,
    SAY_OOC_3                       = 5307,
    SAY_OOC_4                       = 5309,

    YELL_STOLEN_1                   = 5054,
    YELL_STOLEN_2                   = 5053,
    YELL_STOLEN_3                   = 5055,

    YELL_AGRRO_1                    = 5060,
    YELL_AGRRO_2                    = 5267,
    YELL_PICKPOCKETED               = 5266,

    // spells
    SPELL_BANISH                    = 8994,
    SPELL_CURSE_OF_TONGUES          = 13338,
    SPELL_DEMON_ARMOR               = 13787,
    SPELL_IMMOLATE                  = 12742,
    SPELL_SHADOW_BOLT               = 12739,
    SPELL_PICKPOCKET                = 921,
};

namespace
{
uint32 const aRandomSays[] = { SAY_OOC_1, SAY_OOC_2, SAY_OOC_3, SAY_OOC_4 };

uint32 const aRandomYells[] = { YELL_STOLEN_1, YELL_STOLEN_2, YELL_STOLEN_3 };
}

struct classic_boss_plugger_spazzring : public ScriptedAI
{
    classic_boss_plugger_spazzring(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        ResetTimers();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiOocSayTimer;
    uint32 m_uiDemonArmorTimer;
    uint32 m_uiBanishTimer;
    uint32 m_uiImmolateTimer;
    uint32 m_uiShadowBoltTimer;
    uint32 m_uiCurseOfTonguesTimer;
    uint32 m_uiPickpocketTimer;

    void ResetTimers()
    {
        m_uiOocSayTimer          = 10000;
        m_uiDemonArmorTimer      = 1000;
        m_uiBanishTimer          = 0;
        m_uiImmolateTimer        = 0;
        m_uiShadowBoltTimer      = 0;
        m_uiCurseOfTonguesTimer  = 0;
        m_uiPickpocketTimer      = 0;
    }

    void Reset() override
    {
        ResetTimers();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        m_uiBanishTimer          = urand(9000, 15000);
        m_uiImmolateTimer        = urand(5000, 8000);
        m_uiShadowBoltTimer      = 1000;
        m_uiCurseOfTonguesTimer  = 14000;
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        // Activate Phalanx and handle patrons faction
        m_pInstance->SetData(TYPE_PLUGGER, IN_PROGRESS); // The event is set IN_PROGRESS even if Plugger is dead because his death triggers more actions that are part of the event
        m_pInstance->SetData(EVENT_BAR_PATRONS, PATRON_HOSTILE);
        if (Creature* phalanx = me->GetMap()->GetCreature(m_pInstance->GetGuidData(DATA_PHALANX)))
        {
            if (classic_mob_phalanx* phalanxAI = dynamic_cast<classic_mob_phalanx*>(phalanx->AI()))
                if (phalanx->IsAlive())
                    phalanxAI->Activate();
        }
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (caster->GetTypeId() == TYPEID_PLAYER)
        {
            if (spellInfo->Id == SPELL_PICKPOCKET)
                m_uiPickpocketTimer = 5000;
        }
    }

    // Players stole one of the ale mug/roasted boar: warn them
    void WarnThief(Player* player)
    {
        ClassicScriptText(aRandomYells[urand(0, 2)], me);
        me->SetFacingToObject(player);
    }

    // Players stole too much of the ale mug/roasted boar: attack them
    void AttackThief(Player* player)
    {
        if (player)
        {
            ClassicScriptText(urand(0, 1) < 1 ? YELL_AGRRO_1 : YELL_AGRRO_2, me);
            me->SetFacingToObject(player);
            me->SetFaction(BRD_FACTION_DARK_IRON);     // VMaNGOS SetFactionTemporary(.., TEMPFACTION_RESTORE_RESPAWN)
            AttackStart(player);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        // Combat check
        if (UpdateVictim())
        {
            if (m_uiBanishTimer < diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
                {
                    if (DoCast(target, SPELL_BANISH) == SPELL_CAST_OK)
                        m_uiBanishTimer = urand(26, 28) * 1000;
                }
            }
            else
                m_uiBanishTimer -= diff;

            if (m_uiImmolateTimer < diff)
            {
                if (DoCastVictim(SPELL_IMMOLATE) == SPELL_CAST_OK)
                    m_uiImmolateTimer = 25000;
            }
            else
                m_uiImmolateTimer -= diff;

            if (m_uiShadowBoltTimer < diff)
            {
                if (DoCastVictim(SPELL_SHADOW_BOLT) == SPELL_CAST_OK)
                    m_uiShadowBoltTimer = urand(36, 63) * 100;
            }
            else
                m_uiShadowBoltTimer -= diff;

            if (m_uiCurseOfTonguesTimer < diff)
            {
                // VMaNGOS SELECT_FLAG_POWER_MANA
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, [](Unit* unit) { return unit->GetPowerType() == POWER_MANA; }))
                {
                    if (DoCast(target, SPELL_CURSE_OF_TONGUES) == SPELL_CAST_OK)
                        m_uiCurseOfTonguesTimer = urand(19, 31) * 1000;
                }
            }
            else
                m_uiCurseOfTonguesTimer -= diff;
        }
        // Out of Combat (OOC)
        else
        {
            if (m_uiOocSayTimer)
            {
                if (m_uiOocSayTimer <= diff)
                {
                    ClassicScriptText(aRandomSays[urand(0, 3)], me);
                    m_uiOocSayTimer = urand(30000, 35000);
                }
                else
                    m_uiOocSayTimer -= diff;
            }

            if (m_uiPickpocketTimer)
            {
                if (m_uiPickpocketTimer <= diff)
                {
                    ClassicScriptText(YELL_PICKPOCKETED, me);
                    me->SetFaction(BRD_FACTION_DARK_IRON);     // VMaNGOS SetFactionTemporary(.., TEMPFACTION_RESTORE_RESPAWN)
                    m_uiPickpocketTimer = 0;
                    m_uiOocSayTimer = 0;
                }
                else
                    m_uiPickpocketTimer -= diff;
            }

            if (m_uiDemonArmorTimer < diff)
            {
                if (DoCastSelf(SPELL_DEMON_ARMOR) == SPELL_CAST_OK)
                    m_uiDemonArmorTimer = 5 * MINUTE * IN_MILLISECONDS;
            }
            else
                m_uiDemonArmorTimer -= diff;
        }
    }
};

/*######
## go_bar_ale_mug
######*/

struct classic_go_bar_ale_mug : public GameObjectAI
{
    classic_go_bar_ale_mug(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            if (instance->GetData(TYPE_PLUGGER) == IN_PROGRESS || instance->GetData(TYPE_PLUGGER) == DONE) // GOs despawning on use, this check should never be true but this is proper to have it there
                return false;
            else
            {
                if (Creature* plugger = me->GetMap()->GetCreature(instance->GetGuidData(DATA_PLUGGER)))
                {
                    if (classic_boss_plugger_spazzring* pluggerAI = dynamic_cast<classic_boss_plugger_spazzring*>(plugger->AI()))
                    {
                        // Every time we set the event to SPECIAL, the instance script increments the number of stolen mugs/boars, capping at 3
                        instance->SetData(TYPE_PLUGGER, SPECIAL);
                        // If the cap is reached the instance script changes the type from SPECIAL to IN_PROGRESS
                        // Plugger then aggroes and engage players, else he just warns them
                        if (instance->GetData(TYPE_PLUGGER) == IN_PROGRESS)
                            pluggerAI->AttackThief(player);
                        else
                            pluggerAI->WarnThief(player);
                    }
                }
            }
        }
        return false;
    }
};

/*######
## quest_jail_break
######*/

enum JailBreak
{
    SAY_DUGHAL_FREE             = 5210,
    SAY_WINDSOR_AGGRO1          = 5253,
    SAY_WINDSOR_AGGRO2          = 5252,
    SAY_WINDSOR_AGGRO3          = 5250,
    SAY_WINDSOR_1               = 5205,
    SAY_WINDSOR_4_1             = 5207,
    SAY_WINDSOR_4_2             = 5230,
    SAY_WINDSOR_4_3             = 5213,
    SAY_WINDSOR_6               = 5214,
    SAY_WINDSOR_9               = 5215,

    SAY_REGINALD_WINDSOR_0_1    = 5216,
    SAY_REGINALD_WINDSOR_0_2    = 5217,
    SAY_REGINALD_WINDSOR_5_1    = 5222,
    SAY_REGINALD_WINDSOR_5_2    = 5223,
    SAY_REGINALD_WINDSOR_5_3    = 5265,
    SAY_REGINALD_WINDSOR_7_1    = 5224,
    SAY_REGINALD_WINDSOR_7_2    = 5225,
    SAY_REGINALD_WINDSOR_7_3    = 5227,
    SAY_REGINALD_WINDSOR_7_4    = 5249,
    SAY_REGINALD_WINDSOR_13_1   = 5228,
    SAY_REGINALD_WINDSOR_13_2   = 5249,
    SAY_REGINALD_WINDSOR_13_3   = 5229,
    SAY_REGINALD_WINDSOR_14_1   = 5230,
    SAY_REGINALD_WINDSOR_14_2   = 5221,
    SAY_REGINALD_WINDSOR_20_1   = 5231,
    SAY_REGINALD_WINDSOR_20_2   = 5232,

    SAY_TOBIAS_FREE             = 5218,

    SAY_SHILL_DINGER            = 5203,
    SAY_CREST_KILLER            = 5258,
    SAY_OGRABISI                = 5199,

    NPC_REGINALD_WINDSOR        = 9682,
    NPC_DUGHAL                  = 9022,
    NPC_TOBIAS                  = 9679,

    SPELL_WINDSORS_FRENZY       = 15167,

    // VMaNGOS gossip_menu_option action scripts 2209 / 2210 (script command 85 -> OnScriptEventHappened: start escort)
    GOSSIP_MENU_DUGHAL          = 2209,
    GOSSIP_MENU_TOBIAS          = 2210
};

namespace
{
// VMaNGOS script_waypoint entry 9022 (Start(true): run)
BrdEscortPoint const DughalPath[] =
{
    { 0, 280.42f, -82.86f, -77.12f, 0 },
    { 1, 287.64f, -87.01f, -76.79f, 0 },
    { 2, 354.63f, -64.95f, -67.53f, 0 }
};

// VMaNGOS script_waypoint entry 9682 (Start(false): walk)
BrdEscortPoint const ReginaldWindsorPath[] =
{
    { 0,  403.61f, -52.71f,  -63.92f, 4000 },
    { 1,  403.61f, -52.71f,  -63.92f, 4000 },
    { 2,  406.33f, -54.87f,  -63.95f, 0 },
    { 3,  403.86f, -73.88f,  -62.02f, 0 },
    { 4,  557.03f, -119.71f, -61.83f, 0 },
    { 5,  573.4f,  -124.39f, -65.07f, 0 },
    { 6,  593.91f, -130.29f, -69.25f, 0 },
    { 7,  593.21f, -132.16f, -69.25f, 0 },
    { 8,  593.21f, -132.16f, -69.25f, 3000 },
    { 9,  622.81f, -135.55f, -71.92f, 0 },
    { 10, 634.68f, -151.29f, -70.32f, 0 },
    { 11, 635.06f, -153.25f, -70.32f, 0 },
    { 12, 635.06f, -153.25f, -70.32f, 3000 },
    { 13, 635.06f, -153.25f, -70.32f, 1500 },
    { 14, 655.25f, -172.39f, -73.72f, 0 },
    { 15, 654.79f, -226.3f,  -83.06f, 0 },
    { 16, 622.85f, -268.85f, -83.96f, 0 },
    { 17, 579.45f, -275.56f, -80.44f, 0 },
    { 18, 561.19f, -266.85f, -75.59f, 0 },
    { 19, 547.91f, -253.92f, -70.34f, 0 },
    { 20, 549.2f,  -252.4f,  -70.34f, 0 },
    { 21, 549.2f,  -252.4f,  -70.34f, 4000 },
    { 22, 555.33f, -269.16f, -74.4f,  0 },
    { 23, 554.31f, -270.88f, -74.4f,  0 },
    { 24, 554.31f, -270.88f, -74.4f,  4000 },
    { 25, 536.1f,  -249.6f,  -67.47f, 0 },
    { 26, 520.94f, -216.65f, -59.28f, 0 },
    { 27, 505.99f, -148.74f, -62.17f, 0 },
    { 28, 484.21f, -56.24f,  -62.43f, 0 },
    { 29, 470.39f, -6.01f,   -70.1f,  0 },
    { 30, 451.27f, 30.85f,   -70.07f, 0 },
    { 31, 452.45f, 29.85f,   -70.37f, 1500 },
    { 32, 452.45f, 29.85f,   -70.37f, 7000 },
    { 33, 452.45f, 29.85f,   -70.37f, 10000 },
    { 34, 451.27f, 31.85f,   -70.07f, 0 }
};

// VMaNGOS script_waypoint entry 9023 (Start(false): walk)
BrdEscortPoint const MarshalWindsorPath[] =
{
    { 0,  316.336f, -225.528f, -77.7258f, 7000 },
    { 1,  316.336f, -225.528f, -77.7258f, 2000 },
    { 2,  322.96f,  -207.13f,  -77.87f,   0 },
    { 3,  281.05f,  -172.16f,  -75.12f,   0 },
    { 4,  272.19f,  -139.14f,  -70.61f,   0 },
    { 5,  283.62f,  -116.09f,  -70.21f,   0 },
    { 6,  296.18f,  -94.3f,    -74.08f,   0 },
    { 7,  294.57f,  -93.11f,   -74.08f,   0 },
    { 8,  314.31f,  -74.31f,   -76.09f,   0 },
    { 9,  360.22f,  -62.93f,   -66.77f,   0 },
    { 10, 383.38f,  -69.4f,    -63.25f,   0 },
    { 11, 389.99f,  -67.86f,   -62.57f,   0 },
    { 12, 400.98f,  -72.01f,   -62.31f,   0 },
    { 13, 404.22f,  -62.3f,    -63.5f,    2300 },
    { 14, 404.22f,  -62.3f,    -63.5f,    1500 },
    { 15, 407.65f,  -51.86f,   -63.96f,   0 },
    { 16, 403.61f,  -51.71f,   -63.92f,   1000 },
    { 17, 403.61f,  -51.71f,   -63.92f,   2000 },
    { 18, 403.61f,  -51.71f,   -63.92f,   1000 },
    { 19, 403.61f,  -51.71f,   -63.92f,   0 }
};

// VMaNGOS script_waypoint entry 9679 (Start(true): run)
BrdEscortPoint const TobiasPath[] =
{
    { 0, 549.21f, -281.07f, -75.27f, 0 },
    { 1, 554.39f, -267.39f, -73.68f, 0 },
    { 2, 533.59f, -249.38f, -67.04f, 0 },
    { 3, 519.44f, -217.02f, -59.34f, 0 },
    { 4, 506.55f, -153.49f, -62.34f, 0 }
};
}

struct classic_npc_dughal_stormwing : public EscortAI
{
    classic_npc_dughal_stormwing(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;

    void Reset() override { }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        switch (waypointId)
        {
            case 0:
                if (Player* temp = GetPlayerForEscort())
                    ClassicScriptText(SAY_DUGHAL_FREE, me, temp);
                break;
            case 1:
                m_pInstance->SetData(TYPE_JAIL_DUGHAL, IN_PROGRESS);
                break;
            case 2:
                m_pInstance->SetData(TYPE_JAIL_DUGHAL, DONE);
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        if (m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) == FAIL ||
            m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) == DONE ||
            m_pInstance->GetData(TYPE_JAIL_DUGHAL) == DONE)
        {
            me->SetVisible(false);
        }
        else
            me->SetVisible(true);

        EscortAI::UpdateEscortAI(diff);
    }

    // VMaNGOS OnScriptEventHappened (sent by the gossip_menu_option 2209 action script)
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 /*gossipListId*/) override
    {
        if (menuId != GOSSIP_MENU_DUGHAL)
            return false;

        CloseGossipMenuFor(player);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        BrdLoadEscortPath(this, DughalPath, true);
        Start(true, player->GetGUID());
        return true;
    }
};

// npc_marshal_reginald_windsor
struct classic_npc_marshal_reginald_windsor : public EscortAI
{
    classic_npc_marshal_reginald_windsor(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_uiWP = 0;
        m_bEncounterStarted = false;
        BrdLoadEscortPath(this, ReginaldWindsorPath, false);
    }

    InstanceScript* m_pInstance;
    uint32 m_uiWP;
    bool m_bEncounterStarted;

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_PAUSED))
            m_uiWP = 0;
        m_bEncounterStarted = false;
    }

    void DoJailBreakQuestCredit()
    {
        if (Player* player = GetPlayerForEscort())
            player->GroupEventHappens(QUEST_JAIL_BREAK, me);
    }

    void ResumeEscort()
    {
        if (HasEscortState(STATE_ESCORT_PAUSED))
            SetEscortPaused(false);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS || !player)
            return;

        m_uiWP = waypointId;
        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_REGINALD_WINDSOR_0_1, me, player);
                me->SetFacingToObject(player);
                break;
            case 1:
                ClassicScriptText(SAY_REGINALD_WINDSOR_0_2, me);
                me->SetFacingToObject(player);
                break;
            case 7:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_JAZ))
                {
                    me->HandleEmoteCommand(EMOTE_STATE_POINT);
                    ClassicScriptText(SAY_REGINALD_WINDSOR_5_1, me);
                }
                SetEscortPaused(true);
                break;
            case 8:
                ClassicScriptText(SAY_REGINALD_WINDSOR_5_2, me);
                break;
            case 11:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_SHILL))
                {
                    me->HandleEmoteCommand(EMOTE_STATE_POINT);
                    ClassicScriptText(SAY_REGINALD_WINDSOR_7_1, me);
                }
                SetEscortPaused(true);
                break;
            case 12:
                ClassicScriptText(SAY_REGINALD_WINDSOR_7_2, me);
                break;
            case 13:
                ClassicScriptText(SAY_REGINALD_WINDSOR_7_3, me);
                break;
            case 20:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_CREST))
                {
                    me->HandleEmoteCommand(EMOTE_STATE_POINT);
                    ClassicScriptText(SAY_REGINALD_WINDSOR_13_1, me);
                }
                SetEscortPaused(true);
                break;
            case 21:
                ClassicScriptText(SAY_REGINALD_WINDSOR_13_3, me);
                break;
            case 23:
            {
                if (!m_pInstance->GetData(GO_JAIL_DOOR_TOBIAS))
                {
                    me->HandleEmoteCommand(EMOTE_STATE_POINT);
                    ClassicScriptText(SAY_REGINALD_WINDSOR_14_1, me);
                }

                if (Creature* tobias = me->FindNearestCreature(NPC_TOBIAS, 200.0f))
                    tobias->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);

                SetEscortPaused(true);
                break;
            }
            case 24:
                ClassicScriptText(SAY_REGINALD_WINDSOR_14_2, me, player);
                break;
            case 31:
                ClassicScriptText(SAY_REGINALD_WINDSOR_20_1, me);
                break;
            case 32:
                ClassicScriptText(SAY_REGINALD_WINDSOR_20_2, me);

                DoJailBreakQuestCredit();

                m_pInstance->SetData(TYPE_QUEST_JAIL_BREAK, DONE);
                break;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        me->CastSpell(me, SPELL_WINDSORS_FRENZY, true);

        switch (who->GetEntry())
        {
            case NPC_OGRABISI:
            case NPC_JAZ:
                ClassicScriptText(SAY_REGINALD_WINDSOR_5_3, me); break;
            case NPC_CREST:
                ClassicScriptText(SAY_REGINALD_WINDSOR_13_2, me); break;
            case NPC_SHILL:
                ClassicScriptText(SAY_REGINALD_WINDSOR_7_4, me); break;
        }
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        me->RemoveAurasDueToSpell(SPELL_WINDSORS_FRENZY);
        EscortAI::EnterEvadeMode(why);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Player* player = GetPlayerForEscort())
            BrdGroupEventFailHappens(player, QUEST_JAIL_BREAK);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_QUEST_JAIL_BREAK, FAIL);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        switch (m_uiWP)
        {
            case 7:
            {
                Creature* jaz = me->GetMap()->GetCreature(m_pInstance->GetGuidData(NPC_JAZ));
                Creature* ograbisi = me->GetMap()->GetCreature(m_pInstance->GetGuidData(NPC_OGRABISI));
                if (jaz && ograbisi && jaz->IsAlive() && ograbisi->IsAlive() && m_pInstance->GetData(GO_JAIL_DOOR_JAZ) && !m_bEncounterStarted)
                {
                    jaz->SetFaction(BRD_FACTION_DARK_IRON);
                    jaz->AI()->AttackStart(me);
                    ograbisi->SetFaction(BRD_FACTION_DARK_IRON);
                    ograbisi->AI()->AttackStart(me);
                    m_pInstance->SetData(GO_JAIL_DOOR_JAZ, false);
                    ClassicScriptText(SAY_OGRABISI, ograbisi);
                    m_bEncounterStarted = true;
                }
                if (jaz && ograbisi && jaz->isDead() && ograbisi->isDead())
                {
                    ResumeEscort();
                    m_bEncounterStarted = false;
                }
                break;
            }
            case 11:
            {
                Creature* shill = me->GetMap()->GetCreature(m_pInstance->GetGuidData(NPC_SHILL));
                if (shill && shill->IsAlive() && m_pInstance->GetData(GO_JAIL_DOOR_SHILL) && !m_bEncounterStarted)
                {
                    shill->SetFaction(BRD_FACTION_DARK_IRON);
                    shill->AI()->AttackStart(me);
                    m_pInstance->SetData(GO_JAIL_DOOR_SHILL, false);
                    ClassicScriptText(SAY_SHILL_DINGER, shill);
                    m_bEncounterStarted = true;
                }
                if (shill && shill->isDead())
                {
                    ResumeEscort();
                    m_bEncounterStarted = false;
                }
                break;
            }
            case 20:
            {
                Creature* crest = me->GetMap()->GetCreature(m_pInstance->GetGuidData(NPC_CREST));
                if (crest && crest->IsAlive() && m_pInstance->GetData(GO_JAIL_DOOR_CREST) && !m_bEncounterStarted)
                {
                    crest->SetFaction(BRD_FACTION_DARK_IRON);
                    crest->AI()->AttackStart(me);
                    m_pInstance->SetData(GO_JAIL_DOOR_CREST, false);
                    m_bEncounterStarted = true;
                }
                if (crest && crest->isDead())
                {
                    ResumeEscort();
                    m_bEncounterStarted = false;
                }
                break;
            }
        }

        if (m_pInstance->GetData(TYPE_JAIL_TOBIAS) == IN_PROGRESS)
            ResumeEscort();

        EscortAI::UpdateEscortAI(diff);
    }
};

// npc_marshal_windsor
struct classic_npc_marshal_windsor : public EscortAI
{
    classic_npc_marshal_windsor(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_uiWP = 0;
        m_uiSaidJustOnce = false;
        BrdLoadEscortPath(this, MarshalWindsorPath, false);
    }

    InstanceScript* m_pInstance;
    uint32 m_uiWP;
    bool m_uiSaidJustOnce;

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_PAUSED))
            m_uiWP = 0;
        m_uiSaidJustOnce = false;
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_JAIL_BREAK)
        {
            if (m_pInstance)
            {
                if (m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) == NOT_STARTED)
                {
                    m_pInstance->SetData(TYPE_QUEST_JAIL_BREAK, IN_PROGRESS);
                    me->SetFaction(11);

                    Start(true, player->GetGUID(), quest);
                }
            }
        }
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        m_uiWP = waypointId;
        switch (waypointId)
        {
            case 1:
                ClassicScriptText(SAY_WINDSOR_1, me);
                break;
            case 7:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_DUGHAL))
                {
                    if (Player* temp = GetPlayerForEscort())
                        ClassicScriptText(SAY_WINDSOR_4_1, me, temp);
                    me->HandleEmoteCommand(EMOTE_STATE_POINT);
                }

                if (Creature* dughal = me->FindNearestCreature(NPC_DUGHAL, 200.0f))
                    dughal->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);

                SetEscortPaused(true);
                break;
            case 12:
                if (Player* temp = GetPlayerForEscort())
                    ClassicScriptText(SAY_WINDSOR_6, me, temp);
                m_pInstance->SetData(TYPE_JAIL_SUPPLY_ROOM, IN_PROGRESS);
                break;
            case 13:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_SUPPLY))
                    me->HandleEmoteCommand(EMOTE_STATE_USESTANDING);
                break;
            case 14:
                if (!m_pInstance->GetData(GO_JAIL_DOOR_SUPPLY))
                    m_pInstance->DoUseDoorOrButton(m_pInstance->GetGuidData(GO_JAIL_DOOR_SUPPLY));
                break;
            case 16:
                ClassicScriptText(SAY_WINDSOR_9, me);
                break;
            case 17:
                me->HandleEmoteCommand(EMOTE_STATE_USESTANDING);
                break;
            case 18:
                if (GameObject* go = me->GetMap()->GetGameObject(m_pInstance->GetGuidData(GO_JAIL_SUPPLY_CRATE)))
                    go->Delete();
                break;
            case 19:
                me->SetVisible(false);
                me->SetUninteractible(true);                    // UNIT_FLAG_UNINTERACTIBLE
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);      // UNIT_FLAG_SPAWNING
                if (Creature* temp = me->SummonCreature(NPC_REGINALD_WINDSOR, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 3.600f, TEMPSUMMON_DEAD_DESPAWN, 0s))
                {
                    if (classic_npc_marshal_reginald_windsor* escortAI = dynamic_cast<classic_npc_marshal_reginald_windsor*>(temp->AI()))
                    {
                        temp->SetFaction(11);
                        m_pInstance->SetData(TYPE_JAIL_SUPPLY_ROOM, DONE);
                        if (Player* player = GetPlayerForEscort())
                            escortAI->Start(true, player->GetGUID());
                    }
                }
                break;
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!player)
            return;

        switch (urand(0, 2))
        {
            case 0: ClassicScriptText(SAY_WINDSOR_AGGRO1, me); break;
            case 1: ClassicScriptText(SAY_WINDSOR_AGGRO2, me); break;
            case 2: ClassicScriptText(SAY_WINDSOR_AGGRO3, me, player); break;
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Player* player = GetPlayerForEscort())
            BrdGroupEventFailHappens(player, QUEST_JAIL_BREAK);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_QUEST_JAIL_BREAK, FAIL);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        if (m_pInstance->GetData(GO_JAIL_DOOR_DUGHAL) && m_pInstance->GetData(TYPE_JAIL_DUGHAL) == NOT_STARTED && m_uiWP == 7)
        {
            ClassicScriptText(SAY_WINDSOR_4_2, me);
            m_pInstance->SetData(GO_JAIL_DOOR_DUGHAL, false);
        }

        if (m_pInstance->GetData(TYPE_JAIL_DUGHAL) == IN_PROGRESS && !m_uiSaidJustOnce && m_uiWP == 7)
        {
            SetEscortPaused(false);
            m_uiSaidJustOnce = true;
            if (Player* temp = GetPlayerForEscort())
                ClassicScriptText(SAY_WINDSOR_4_3, me, temp);
        }

        EscortAI::UpdateEscortAI(diff);
    }
};

// npc_tobias_seecher
struct classic_npc_tobias_seecher : public EscortAI
{
    classic_npc_tobias_seecher(Creature* creature) : EscortAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;

    void Reset() override { }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_TOBIAS_FREE, me);
                break;
            case 2:
                m_pInstance->SetData(TYPE_JAIL_TOBIAS, IN_PROGRESS);
                break;
            case 4:
                m_pInstance->SetData(TYPE_JAIL_TOBIAS, DONE);
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) != IN_PROGRESS)
            return;

        if (m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) == FAIL || m_pInstance->GetData(TYPE_QUEST_JAIL_BREAK) == DONE || m_pInstance->GetData(TYPE_JAIL_TOBIAS) == DONE)
            me->SetVisible(false);
        else
            me->SetVisible(true);

        EscortAI::UpdateEscortAI(diff);
    }

    // VMaNGOS OnScriptEventHappened (sent by the gossip_menu_option 2210 action script)
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 /*gossipListId*/) override
    {
        if (menuId != GOSSIP_MENU_TOBIAS)
            return false;

        CloseGossipMenuFor(player);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        BrdLoadEscortPath(this, TobiasPath, true);
        Start(true, player->GetGUID());
        return true;
    }
};

struct classic_go_cell_door : public GameObjectAI
{
    classic_go_cell_door(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        InstanceScript* instance = me->GetInstanceScript();

        if (!instance)
            return true;

        instance->SetData(me->GetEntry(), true);
        Creature* temp = GetClosestCreatureWithEntry(me, NPC_CREST, 50.0f);

        if (me->GetEntry() == GO_JAIL_DOOR_CREST && temp)
            ClassicScriptText(SAY_CREST_KILLER, temp);

        return false;
    }
};

// 27673 - Five Fat Finger Exploding Heart Technique
enum FiveFatFinger
{
    SPELL_EXPLODING_HEART = 27676
};

class classic_spell_five_fat_finger_exploding_heart_technique : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EXPLODING_HEART });
    }

    // VMaNGOS OnAuraInit
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        _originalPosition = GetTarget()->GetPosition();
    }

    // VMaNGOS OnPeriodicTrigger + OnPeriodicTickEnd: the aura triggers nothing (trigger spell 0) until the target
    // moved 5 yards with 5 stacks, then it triggers Exploding Heart and is removed.
    void HandlePeriodic(AuraEffect const* aurEff)
    {
        PreventDefaultAction();

        if (GetStackAmount() < 5)
            return;

        // 5 steps 5 yards?
        Unit* target = GetTarget();
        if (target->GetDistance(_originalPosition) >= 5.0f)
        {
            target->CastSpell(target, SPELL_EXPLODING_HEART, CastSpellExtraArgs(aurEff).SetOriginalCaster(GetCasterGUID()));
            Remove();
        }
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(classic_spell_five_fat_finger_exploding_heart_technique::AfterApply, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(classic_spell_five_fat_finger_exploding_heart_technique::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }

    Position _originalPosition;
};

// 27517 - Summon Thelrin DND
// VMaNGOS instance_blackrock_depths::CustomSpellCasted(27517, caster, target) (a VMaNGOS core hook) starts the T0.5
// "The Challenge" event (Theldren in the Ring of Law) for the casting player.
class classic_spell_brd_summon_thelrin_dnd : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->ToPlayer())
            return;

        if (!InstanceHasScript(caster, ClassicBRDScriptName))
            return;

        if (InstanceScript* instance = caster->GetInstanceScript())
            instance->SetGuidData(DATA_ARENA_CHALLENGER, caster->GetGUID());
    }

    void Register() override
    {
        AfterCast += SpellCastFn(classic_spell_brd_summon_thelrin_dnd::HandleAfterCast);
    }
};

void AddSC_classic_blackrock_depths()
{
    RegisterGameObjectAI(classic_go_shadowforge_brazier);
    new classic_at_ring_of_law();
    RegisterCreatureAI(classic_npc_grimstone);
    RegisterCreatureAI(classic_mob_phalanx);
    RegisterCreatureAI(classic_npc_rocknot);
    RegisterGameObjectAI(classic_go_dark_keeper_portrait);
    RegisterGameObjectAI(classic_go_thunderbrew_laguer_keg);
    RegisterGameObjectAI(classic_go_relic_coffer_door);
    RegisterCreatureAI(classic_npc_watchman_doomgrip);
    RegisterCreatureAI(classic_npc_golem_lord_argelmach);
    new classic_at_shadowforge_bridge();

    // The Grim Guzzler
    RegisterCreatureAI(classic_npc_mistress_nagmara);
    RegisterCreatureAI(classic_boss_plugger_spazzring);
    RegisterGameObjectAI(classic_go_bar_ale_mug);

    // Jail Break!
    RegisterCreatureAI(classic_npc_dughal_stormwing);
    RegisterCreatureAI(classic_npc_marshal_reginald_windsor);
    RegisterCreatureAI(classic_npc_marshal_windsor);
    RegisterCreatureAI(classic_npc_tobias_seecher);
    RegisterGameObjectAI(classic_go_cell_door);

    RegisterSpellScript(classic_spell_five_fat_finger_exploding_heart_technique);
    RegisterSpellScript(classic_spell_brd_summon_thelrin_dnd);
}
