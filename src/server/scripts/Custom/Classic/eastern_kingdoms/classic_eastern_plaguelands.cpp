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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/eastern_plaguelands.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_eris_havenfire (quest 7622 The Balance of Light) + npc_eris_havenfire_peasant (summoned-only, required by the
// event), go_mark_of_detonation (quest 6041), npc_guard_didier, npc_caravan_mule.
// Not ported (not requested): go_darrowshire_trigger, npc_joseph_redpath, npc_demetria.

#include "ScriptMgr.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include <array>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <vector>

/*######
## npc_eris_havenfire
######*/

enum ErisHavenfireData
{
    NPC_ERIS_HAVENFIRE      = 14494,
    NPC_PEASANT_0           = 14484,        // Injured
    NPC_PEASANT_1           = 14485,        // Plagued
    NPC_WARRIOR             = 14486,
    NPC_ARCHER              = 14489,
    NPC_CLEANER             = 14503,

    GO_LIGHT                = 179693,
    GO_DEATH_POST           = 179694,
    GO_SLAIN_PEASANT1       = 179695,
    GO_SLAIN_PEASANT2       = 179696,
    GO_SLAIN_PEASANT3       = 179698,
    GO_SLAIN_PEASANT4       = 179699,

    DEATH_POST_SPAWNS_COUNT = 14,

    SPELL_SEETHING_PLAGUE        = 23072,
    SPELL_SHOOT                  = 23073,
    SPELL_ENTER_THE_LIGHT        = 23107,
    SPELL_BLESSING_OF_NORDRASSIL = 23108,
    SPELL_CONJURE_PEASANT        = 23119,
    SPELL_DEATHS_DOOR            = 23127,

    SAY_PEASANT_RANDOM_3    = 9683,
    SAY_PEASANT_RANDOM_2    = 9680,
    SAY_PEASANT_RANDOM_1    = 9682,
    SAY_PEASANT_END_4       = 9653,
    SAY_PEASANT_END_3       = 9650,
    SAY_PEASANT_END_2       = 9652,
    SAY_PEASANT_END_1       = 9654,
    SAY_ERIS_FAIL_1         = 9648,
    SAY_ERIS_FAIL_2         = 9649,
    SAY_PEASANT_SPAWN_1     = 9712,
    SAY_PEASANT_SPAWN_2     = 9713,
    SAY_PEASANT_SPAWN_3     = 9714,
    SAY_PEASANT_SPAWN_4     = 9715,
    SAY_ERIS_END            = 9728,
    SAY_ERIS_HEAL           = 9655,

    QUEST_BALANCE_OF_LIGHT  = 7622,

    POINT_START_COMBAT      = 0,
    POINT_END_EVENT         = 1,

    POS_PEASANT_SPAWN       = 0,
    POS_PEASANT_DEST        = 1,
    POS_WARRIOR_SPAWN0      = 2,
    POS_WARRIOR_SPAWN1      = 3,
    POS_WARRIOR_SPAWN2      = 4,
    POS_ARCHER_SPAWN0       = 5,
    POS_ARCHER_SPAWN1       = POS_ARCHER_SPAWN0 + 1,
    POS_ARCHER_SPAWN2       = POS_ARCHER_SPAWN1 + 1,
    POS_ARCHER_SPAWN3       = POS_ARCHER_SPAWN2 + 1,
    POS_ARCHER_SPAWN4       = POS_ARCHER_SPAWN3 + 1,
    POS_ARCHER_SPAWN5       = POS_ARCHER_SPAWN4 + 1,
    POS_ARCHER_SPAWN6       = POS_ARCHER_SPAWN5 + 1,
    POS_ARCHER_SPAWN7       = POS_ARCHER_SPAWN6 + 1,
    POS_END                 = POS_ARCHER_SPAWN7 + 1
};

Position const ErisHavenfireEvent[] =
{
    {3358.1096f, -3049.8063f, 166.226f, 1.87f},  // Depart
    {3327.0f, -2970.0f, 161.0f, 0.0f},           // Arrive
    {3366.0f, -3045.0f, 166.0f, 3.3f},           // Warrior 0
    {3345.0f, -3054.0f, 167.0f, 0.4f},           // Warrior 1
    {3364.0f, -3057.0f, 166.0f, 2.0f},           // Warrior 2
    {3327.076f, -3017.9831f, 171.5497f, 5.777f}, // Archer 0
    {3313.686f, -3038.0459f, 168.5863f, 0.072f}, // Archer 1
    {3333.0f, -3052.0f, 175.0f, 0.61f},          // Archer 2
    {3380.0f, -3040.0f, 174.0f, 3.3885f},        // Archer 3
    {3381.0f, -3060.0f, 184.0f, 2.5991f},        // Archer 4
    {3371.4809f, -3070.0302f, 175.166f, 1.952f}, // Archer 5
    {3347.1079f, -3071.3110f, 177.910f, 1.356f}, // Archer 6
    {3358.7299f, -3075.9846f, 174.794f, 1.575f}  // Archer 7
};

struct ErisDeathPostSpawn
{
    uint32 entry;
    float x, y, z, o;
    float rot0, rot1, rot2, rot3;
};

ErisDeathPostSpawn const ErisDeathPostSpawns[DEATH_POST_SPAWNS_COUNT] =
{
    { GO_DEATH_POST,     3355.48f, -3010.68f, 175.212f, 5.06146f, 0.0f, 0.0f, -0.573576f, 0.819152f },
    { GO_SLAIN_PEASANT3, 3352.82f, -3007.79f, 177.409f, 2.53072f, 0.0f, 0.0f, 0.953716f, 0.300708f },
    { GO_SLAIN_PEASANT1, 3353.07f, -3009.16f, 176.615f, 3.01941f, 0.0f, 0.0f, 0.998135f, 0.0610518f },
    { GO_SLAIN_PEASANT4, 3354.08f, -3010.35f, 172.769f, 2.46091f, 0.0f, 0.0f, 0.942641f, 0.333808f },
    { GO_SLAIN_PEASANT2, 3353.87f, -3007.88f, 171.79f, 5.18363f, 0.0f, 0.0f, -0.522498f, 0.852641f },
    { GO_SLAIN_PEASANT2, 3353.28f, -3013.75f, 173.584f, 1.20428f, 0.0f, 0.0f, 0.566406f, 0.824126f },
    { GO_SLAIN_PEASANT2, 3353.34f, -3009.84f, 173.532f, 1.98967f, 0.0f, 0.0f, 0.83867f, 0.54464f },
    { GO_SLAIN_PEASANT3, 3353.8f, -3009.6f, 175.499f, 0.802851f, 0.0f, 0.0f, 0.390731f, 0.920505f },
    { GO_SLAIN_PEASANT4, 3355.88f, -3014.61f, 173.609f, 5.58505f, 0.0f, 0.0f, -0.34202f, 0.939693f },
    { GO_SLAIN_PEASANT4, 3354.33f, -3012.57f, 173.045f, 5.06146f, 0.0f, 0.0f, -0.573576f, 0.819152f },
    { GO_SLAIN_PEASANT2, 3354.29f, -3011.48f, 171.916f, 2.80997f, 0.0f, 0.0f, 0.986285f, 0.16505f },
    { GO_SLAIN_PEASANT1, 3354.74f, -3013.16f, 176.816f, 2.61799f, 0.0f, 0.0f, 0.965925f, 0.258821f },
    { GO_SLAIN_PEASANT3, 3355.1f, -3013.5f, 176.482f, 0.139625f, 0.0f, 0.0f, 0.0697555f, 0.997564f },
    { GO_SLAIN_PEASANT4, 3351.66f, -3007.61f, 175.0f, 4.53786f, 0.0f, 0.0f, -0.766044f, 0.642789f },
};

struct classic_npc_eris_havenfire : public ScriptedAI
{
    classic_npc_eris_havenfire(Creature* creature) : ScriptedAI(creature)
    {
        // TODO(classic): VMaNGOS SetCreatureSummonLimit(200) - TC master has no per-creature summon limit.
        ResetEventData();
    }

    uint32 m_wave;
    std::array<uint32, 2> m_waveTimer;
    uint32 m_buffTimer;
    std::array<uint32, 8> m_archerTimer;
    uint32 m_villagerDiedCount;
    uint32 m_villagerSurvivedCount;
    ObjectGuid m_playerGUID;
    std::array<ObjectGuid, 8> m_archerGUIDs;
    std::array<ObjectGuid, 50> m_villagerGUIDs;
    std::array<ObjectGuid, DEATH_POST_SPAWNS_COUNT> m_deathPostGUIDs;

    bool m_questStarted;
    bool m_cleanerSpawn;

    Player* GetPlayer()
    {
        return ObjectAccessor::GetPlayer(*me, m_playerGUID);
    }

    void ResetEventData()
    {
        m_wave = 0;
        m_villagerDiedCount = 0;
        m_villagerSurvivedCount = 0;
        m_questStarted = false;
        m_cleanerSpawn = false;

        m_waveTimer[0] = 10000;
        m_waveTimer[1] = 110000;
        m_buffTimer = 100000;
        for (int i = 0; i < 8; i++)
        {
            m_archerTimer[i] = 5000;
            m_archerGUIDs[i].Clear();
        }
        for (ObjectGuid& guid : m_villagerGUIDs)
            guid.Clear();
        for (ObjectGuid& guid : m_deathPostGUIDs)
            guid.Clear();
    }

    void Reset() override
    {
        ResetEventData();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if ((who->IsPlayer() || who->IsPet()) && !m_cleanerSpawn && m_questStarted)
        {
            if (who->GetGUID() != m_playerGUID || who->IsPet())
            {
                if (Creature* cleaner = me->SummonCreature(NPC_CLEANER, 3358.1096f, -3049.8063f, 166.226f, 1.87f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1000ms))
                {
                    cleaner->SetInCombatWith(who);
                    cleaner->GetMotionMaster()->MoveChase(who);
                    m_questStarted = false;
                    m_cleanerSpawn = true;
                    FailEvent(GetPlayer(), false);
                }
            }
        }
    }

    void GetPeasantsAndMobs(std::list<Creature*>& list, std::initializer_list<uint32> entries, float range)
    {
        for (uint32 entry : entries)
            me->GetCreatureListWithEntryInGrid(list, entry, range);
    }

    void SetAttackOnPeasantOrPlayer(Creature* summoned)
    {
        if (!summoned)
            return;

        std::list<Creature*> tmpMobsList;
        GetPeasantsAndMobs(tmpMobsList, { NPC_PEASANT_0, NPC_PEASANT_1 }, 100.0f);
        for (Creature* curr : tmpMobsList)
        {
            if (curr->IsAlive())
                summoned->GetThreatManager().AddThreat(curr, float(urand(100, 200)));
        }

        if (Player* player = GetPlayer())
        {
            if (player->IsAlive())
            {
                summoned->GetThreatManager().AddThreat(player, 50.0f);
                if ((rand32() % 4) > 0)
                {
                    summoned->GetThreatManager().AddThreat(player, 200.0f);
                    summoned->GetMotionMaster()->Clear();
                    summoned->GetMotionMaster()->MoveChase(player);
                }
            }
        }
    }

    void DespawnAll()
    {
        std::list<Creature*> tmpMobsList;
        GetPeasantsAndMobs(tmpMobsList, { NPC_PEASANT_0, NPC_PEASANT_1, NPC_WARRIOR, NPC_ARCHER }, 150.0f);
        for (Creature* curr : tmpMobsList)
        {
            if (curr->IsAlive())
                curr->DespawnOrUnsummon();
        }

        for (ObjectGuid& guid : m_deathPostGUIDs)
        {
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, guid))
                go->DespawnOrUnsummon();
            guid.Clear();
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        int j = 0;

        switch (summoned->GetEntry())
        {
            case NPC_ARCHER:
                summoned->SetSheath(SHEATH_STATE_RANGED);
                while (!m_archerGUIDs[j].IsEmpty() && j < 7)
                    ++j;

                m_archerGUIDs[j] = summoned->GetGUID();
                break;
            case NPC_WARRIOR:
                SetAttackOnPeasantOrPlayer(summoned);
                break;
            case NPC_PEASANT_1:
                summoned->CastSpell(summoned, SPELL_SEETHING_PLAGUE, true);
                [[fallthrough]];
            case NPC_PEASANT_0:
                while (!m_villagerGUIDs[j].IsEmpty() && j < 49)
                    ++j;

                if (j < 50)
                    m_villagerGUIDs[j] = summoned->GetGUID();

                summoned->SetPvP(true); // VMaNGOS UNIT_FLAG_PVP
                break;
        }
    }

    // VMaNGOS SummonedMovementInform(POINT_END_EVENT) - called by the peasant AI (TC has no summoned movement hook)
    void PeasantReachedEnd(Creature* summoned)
    {
        ++m_villagerSurvivedCount;
        switch (rand32() % 15)
        {
            case 0:
                ClassicScriptText(SAY_PEASANT_END_1, summoned);
                break;
            case 1:
                ClassicScriptText(SAY_PEASANT_END_2, summoned);
                break;
            case 2:
                ClassicScriptText(SAY_PEASANT_END_3, summoned);
                break;
            case 3:
                ClassicScriptText(SAY_PEASANT_END_4, summoned);
                break;
        }

        int j = 0;
        while (m_villagerGUIDs[j] != summoned->GetGUID() && j < 49)
            ++j;

        if (j < 50)
            m_villagerGUIDs[j].Clear();

        summoned->DespawnOrUnsummon();

        if (m_villagerSurvivedCount >= 50)
            if (Player* player = GetPlayer())
                CompleteEvent(player);
    }

    void SummonedCreatureDies(Creature* summoned, Unit* /*killer*/) override
    {
        if (summoned->GetEntry() == NPC_PEASANT_0 || summoned->GetEntry() == NPC_PEASANT_1)
        {
            if (m_villagerDiedCount < DEATH_POST_SPAWNS_COUNT)
            {
                ErisDeathPostSpawn const& spawn = ErisDeathPostSpawns[m_villagerDiedCount];
                if (GameObject* go = me->SummonGameObject(spawn.entry, spawn.x, spawn.y, spawn.z, spawn.o, QuaternionData(spawn.rot0, spawn.rot1, spawn.rot2, spawn.rot3), 1200000s))
                    m_deathPostGUIDs[m_villagerDiedCount] = go->GetGUID();
            }
            ++m_villagerDiedCount;
        }

        if (m_villagerDiedCount >= 15)
        {
            FailEvent(GetPlayer(), true);
            return;
        }

        int j = 0;
        while (m_villagerGUIDs[j] != summoned->GetGUID() && j < 49)
            ++j;

        if (j < 50)
            m_villagerGUIDs[j].Clear();
    }

    void FailEvent(Player* player, bool npcDespawn)
    {
        if (player && player->GetQuestStatus(QUEST_BALANCE_OF_LIGHT) == QUEST_STATUS_INCOMPLETE)
            player->FailQuest(QUEST_BALANCE_OF_LIGHT);

        if (rand32() % 2)
            ClassicScriptText(SAY_ERIS_FAIL_1, me);
        else
            ClassicScriptText(SAY_ERIS_FAIL_2, me);

        if (GameObject* light = me->FindNearestGameObject(GO_LIGHT, 100.0f))
            light->DespawnOrUnsummon();

        me->CombatStop();

        DespawnAll();

        if (npcDespawn)
            me->DespawnOrUnsummon();
        else
            ResetEventData();
    }

    void BeginEvent(Player* player)
    {
        if (!player)
            return;

        m_wave = 0;
        m_villagerDiedCount = 0;
        m_villagerSurvivedCount = 0;
        m_cleanerSpawn = false;
        m_questStarted = true;
        m_playerGUID = player->GetGUID();

        m_waveTimer[0] = 10000;
        m_waveTimer[1] = 100000;
        m_buffTimer = 95000;
        for (int i = 0; i < 8; i++)
        {
            m_archerTimer[i] = 5000;
            m_archerGUIDs[i].Clear();
        }
        for (ObjectGuid& guid : m_villagerGUIDs)
            guid.Clear();

        for (int i = POS_ARCHER_SPAWN0; i < POS_END; i++)
            me->SummonCreature(NPC_ARCHER, ErisHavenfireEvent[i], TEMPSUMMON_DEAD_DESPAWN, 0s);

        if (!me->FindNearestGameObject(GO_LIGHT, 100.0f))
            me->SummonGameObject(GO_LIGHT, 3327.0f, -2970.0f, 160.034f, 5.2135f, QuaternionData::fromEulerAnglesZYX(5.2135f, 0.0f, 0.0f), 0s);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_BALANCE_OF_LIGHT)
            BeginEvent(player);
    }

    void NewWave(bool peasants)
    {
        int count = GenerateWaveNumber(peasants);
        int rnd = urand(1, 4);
        bool yell = false;

        for (int i = 0; i < count; i++)
        {
            if (peasants)
            {
                uint32 entry = i >= rnd ? NPC_PEASANT_0 : NPC_PEASANT_1;
                Position pos = me->GetRandomPoint(ErisHavenfireEvent[POS_PEASANT_SPAWN], 6.0f);
                if (Creature* peasant = me->SummonCreature(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_DEAD_DESPAWN, 0s))
                {
                    if (!yell)
                    {
                        ++m_wave;
                        yell = true;
                        switch (urand(0, 3))
                        {
                            case 0:
                                ClassicScriptText(SAY_PEASANT_SPAWN_1, peasant);
                                break;
                            case 1:
                                ClassicScriptText(SAY_PEASANT_SPAWN_2, peasant);
                                break;
                            case 2:
                                ClassicScriptText(SAY_PEASANT_SPAWN_3, peasant);
                                break;
                            case 3:
                                ClassicScriptText(SAY_PEASANT_SPAWN_4, peasant);
                                break;
                        }
                    }
                }
            }
            else
            {
                uint32 warriorPos = urand(POS_WARRIOR_SPAWN0, POS_WARRIOR_SPAWN2);
                Position pos = me->GetRandomPoint(ErisHavenfireEvent[warriorPos], 5.0f);
                me->SummonCreature(NPC_WARRIOR, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_DEAD_DESPAWN, 0s);
            }
        }
    }

    int GenerateWaveNumber(bool peasants)
    {
        if (m_wave > 4 && peasants)
            return 0;

        int count = 0;
        if (peasants)
        {
            count = 12;
            if (m_wave == 3)
                count = 13;
            else if (m_wave == 4)
                count = 16;
        }
        else
            count = urand(2, 6);

        return count;
    }

    void CompleteEvent(Player* player)
    {
        if (!player)
            return;

        if (player->GetQuestStatus(QUEST_BALANCE_OF_LIGHT) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(QUEST_BALANCE_OF_LIGHT);

        ClassicScriptText(SAY_ERIS_END, me);

        if (GameObject* light = me->FindNearestGameObject(GO_LIGHT, 30.0f))
            light->DespawnOrUnsummon();

        me->CombatStop();

        DespawnAll();

        ResetEventData();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_questStarted || m_cleanerSpawn)
            return;

        // TODO(classic): VMaNGOS keeps the event player in combat (Player::SetCombatTimer(1500)); TC master has no equivalent API.

        for (int i = 0; i < 2; i++)
        {
            if (m_waveTimer[i] < diff)
            {
                if (i == 0)
                {
                    NewWave(true);
                    m_waveTimer[i] = 80000;
                }
                else
                {
                    m_waveTimer[i] = urand(10000, 14000);
                    if ((rand32() % 7) > 0) // 85% chance
                        NewWave(false);
                }
            }
            else
                m_waveTimer[i] -= diff;

            if (!m_questStarted) // event ended while spawning
                return;
        }

        if (m_buffTimer < diff)
        {
            if (DoCastSelf(SPELL_BLESSING_OF_NORDRASSIL) == SPELL_CAST_OK)
            {
                if (Player* player = GetPlayer())
                    ClassicScriptText(SAY_ERIS_HEAL, me, player);
                m_buffTimer = urand(75000, 90000);
            }
        }
        else
            m_buffTimer -= diff;

        for (int i = 0; i < 8; i++)
        {
            if (m_archerTimer[i] < diff)
            {
                if (!m_archerGUIDs[i].IsEmpty())
                {
                    if (Creature* archer = ObjectAccessor::GetCreature(*me, m_archerGUIDs[i]))
                    {
                        std::vector<ObjectGuid> aliveVillagers;
                        for (ObjectGuid const& guid : m_villagerGUIDs)
                        {
                            if (guid.IsEmpty())
                                continue;

                            Creature* villager = ObjectAccessor::GetCreature(*me, guid);
                            if (villager && villager->IsAlive())
                                aliveVillagers.push_back(guid);
                        }

                        if (aliveVillagers.empty())
                            continue;

                        if (Creature* target = ObjectAccessor::GetCreature(*me, aliveVillagers[urand(0, uint32(aliveVillagers.size()) - 1)]))
                            archer->CastSpell(target, SPELL_SHOOT, true);

                        m_archerTimer[i] = urand(3000, 4400);
                    }
                }
            }
            else
                m_archerTimer[i] -= diff;
        }
    }
};

/*######
## npc_eris_havenfire_peasant
######*/

struct classic_npc_eris_havenfire_peasant : public ScriptedAI
{
    classic_npc_eris_havenfire_peasant(Creature* creature) : ScriptedAI(creature),
        X(0.0f), Y(0.0f), Z(0.0f), cX(0.0f), cY(0.0f), cZ(0.0f), m_needToMove(true), m_uiSayPeasantTimer(10000) { }

    float X;
    float Y;
    float Z;
    float cX;
    float cY;
    float cZ;
    bool m_needToMove;

    uint32 m_uiSayPeasantTimer;

    void Reset() override
    {
        cX = 3347.801025f + float(urand(0, 12));
        cY = -3048.161865f + float(urand(0, 12));
        cZ = 163.679321f;
        X = 3324.0f + float(urand(0, 6));
        Y = -2973.0f + float(urand(0, 6));
        Z = 161.0f;
        m_needToMove = true;
        SetCombatMovement(false);

        m_uiSayPeasantTimer = urand(10000, 30000);
    }

    float GetEventSpeed() const
    {
        return me->GetEntry() == NPC_PEASANT_0 ? 1.0f : 1.7f;
    }

    classic_npc_eris_havenfire* GetErisAI() const
    {
        Creature* eris = nullptr;
        if (TempSummon* summon = me->ToTempSummon())
            eris = summon->GetSummonerCreatureBase();
        if (!eris)
            eris = me->FindNearestCreature(NPC_ERIS_HAVENFIRE, 100.0f, true);
        if (!eris)
            return nullptr;

        return dynamic_cast<classic_npc_eris_havenfire*>(eris->AI());
    }

    void KilledUnit(Unit* /*victim*/) override { }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (attacker && attacker->GetEntry() == NPC_ARCHER)
            damage = urand(80, 105);
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_SHOOT)
        {
            if (!urand(0, 10))
                me->CastSpell(me, SPELL_DEATHS_DOOR, true);
        }
        else if (caster && caster->IsPlayer())
        {
            Creature* eris = me->FindNearestCreature(NPC_ERIS_HAVENFIRE, 100.0f, true);
            if (!eris)
                return;

            if (classic_npc_eris_havenfire* erisAI = dynamic_cast<classic_npc_eris_havenfire*>(eris->AI()))
            {
                if (caster->GetGUID() != erisAI->m_playerGUID && erisAI->m_questStarted && !erisAI->m_cleanerSpawn)
                {
                    if (Creature* cleaner = me->SummonCreature(NPC_CLEANER, 3358.1096f, -3049.8063f, 166.226f, 1.87f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1000ms))
                    {
                        cleaner->AI()->AttackStart(caster->ToPlayer());
                        erisAI->m_questStarted = false;
                        erisAI->m_cleanerSpawn = true;
                        erisAI->FailEvent(erisAI->GetPlayer(), false);
                    }
                }
            }
        }
    }

    void MoveInLineOfSight(Unit* /*who*/) override { }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (pointId == POINT_START_COMBAT)
        {
            me->SetWalk(true);
            me->GetMotionMaster()->MovePoint(POINT_END_EVENT, X, Y, Z, true, {}, GetEventSpeed());
            cX = 0.0f;
            cY = 0.0f;
            cZ = 0.0f;
        }
        else if (pointId == POINT_END_EVENT)
        {
            if (classic_npc_eris_havenfire* erisAI = GetErisAI())
                erisAI->PeasantReachedEnd(me);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_needToMove)
        {
            me->SetWalk(true);
            me->GetMotionMaster()->MovePoint(POINT_START_COMBAT, cX, cY, cZ, true, {}, GetEventSpeed());
            m_needToMove = false;
        }

        if (!me->IsWalking())
            me->SetWalk(true);

        if (m_uiSayPeasantTimer < diff)
        {
            switch (rand32() % 30)
            {
                case 0:
                    ClassicScriptText(SAY_PEASANT_RANDOM_1, me);
                    break;
                case 1:
                    ClassicScriptText(SAY_PEASANT_RANDOM_2, me);
                    break;
                case 2:
                    ClassicScriptText(SAY_PEASANT_RANDOM_3, me);
                    break;
            }
            m_uiSayPeasantTimer = urand(20000, 50000);
        }
        else
            m_uiSayPeasantTimer -= diff;
    }
};

/******************************
*** go_mark_of_detonation ***
*******************************/

enum MarkOfDetonationData
{
    QUEST_WHEN_SMOKEY_SINGS_I_GET_VIOLENT   = 6041,
    SPELL_PLACING_SMOKEY_S_EXPLOSIVES       = 19250,
    TRIGGER_SCOURGE_STRUCTURE               = 12247
};

// Vanilla spawn import (sql/custom/world/2026_09_28_11_world_vanilla_spawns.sql): TC guid = base + VMaNGOS guid
constexpr ObjectGuid::LowType CLASSIC_CREATURE_GUID_BASE   = 20000000;
constexpr ObjectGuid::LowType CLASSIC_GAMEOBJECT_GUID_BASE = 30000000;

struct classic_go_mark_of_detonation : public GameObjectAI
{
    classic_go_mark_of_detonation(GameObject* go) : GameObjectAI(go) { }

    // VMaNGOS EffectDummyGameObj_go_mark_of_detonation (dummy effect of spell 19250 on this gameobject)
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_PLACING_SMOKEY_S_EXPLOSIVES || !caster)
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        if (Creature* creature = me->FindNearestCreature(TRIGGER_SCOURGE_STRUCTURE, 8.0f, true))
        {
            player->KilledMonsterCredit(creature->GetEntry(), creature->GetGUID());

            // VMaNGOS: SCRIPT_COMMAND_RESPAWN_GAMEOBJECT (despawn delay 180 s) for the fire objects of this structure
            // (key: VMaNGOS creature guid, values: VMaNGOS gameobject guids)
            static std::map<uint32, std::vector<uint32>> const fireObjectsMap =
            {
                { 53157, { 35947, 35948, 35949, 35950, 35951, 35952, 35953, 35954, 35955, 35956 } },
                { 53168, { 35936, 35937, 35938, 35939, 35940, 35941, 35942, 35943, 35944, 35945 } },
                { 54270, { 35903, 35904, 35905, 35906, 35907, 35908, 35909, 35910, 35911, 35912 } },
                { 54271, { 35892, 35893, 35894, 35895, 35896, 35897, 35898, 35899, 35900, 35901 } },
                { 56689, { 35958, 35959, 35960, 35961, 35962, 35963, 35964, 35965, 35966, 35967 } },
                { 92232, { 35881, 35882, 35883, 35884, 35885, 35886, 35887, 35888, 35889, 35890 } },
                { 92254, { 35925, 35926, 35927, 35928, 35929, 35930, 35931, 35932, 35933, 35934 } },
                { 92262, { 35914, 35915, 35916, 35917, 35918, 35919, 35920, 35921, 35922, 35923 } },
            };

            ObjectGuid::LowType const spawnId = creature->GetSpawnId();
            if (spawnId > CLASSIC_CREATURE_GUID_BASE)
            {
                auto itr = fireObjectsMap.find(uint32(spawnId - CLASSIC_CREATURE_GUID_BASE));
                if (itr != fireObjectsMap.end())
                {
                    for (uint32 goGuid : itr->second)
                    {
                        // same as TC MapScripts SCRIPT_COMMAND_RESPAWN_GAMEOBJECT
                        GameObject* fire = creature->GetMap()->GetGameObjectBySpawnId(CLASSIC_GAMEOBJECT_GUID_BASE + goGuid);
                        if (!fire || fire->isSpawned())
                            continue;

                        fire->SetLootState(GO_READY);
                        fire->SetRespawnTime(180);
                        fire->GetMap()->AddToMap(fire);
                    }
                }
            }

            creature->KillSelf();
        }

        // always return true when we are handling this spell and effect
        me->DespawnOrUnsummon();
    }
};

/*************************
*** npc_guard_didier / npc_caravan_mule ***
*************************/

enum GuardDidierData
{
    NPC_GUARD_DIDIER    = 16226,
    NPC_CARAVAN_MULE    = 16232,

    SAY_MULE_DIED       = 12118,
    SPELL_MARK_OF_DIDIER = 28114,
    GOSSIP_NOT_STARTED  = 7165,
    GOSSIP_MULE_DIED    = 7168,

    ACTION_GROUP_MEMBER_DIED = 1
};

// VMaNGOS CreatureGroup::DoForAllMembers. TC's CreatureGroup does not expose its members: search the caravan creatures
// around and keep the ones in the same formation (members of Didier's caravan are Didier and the mules).
// TODO(classic): needs VMaNGOS creature_groups leader 1383 (Didier) imported into world.creature_formations
// (TC leaderGUID/memberGUID = 20000000 + VMaNGOS guid); without a formation these group actions do nothing, as in VMaNGOS.
static void ForEachCaravanMember(Creature* source, std::function<void(Creature*)> const& func)
{
    CreatureGroup* group = source->GetFormation();
    if (!group)
        return;

    std::list<Creature*> candidates;
    source->GetCreatureListWithEntryInGrid(candidates, NPC_GUARD_DIDIER, 100.0f);
    source->GetCreatureListWithEntryInGrid(candidates, NPC_CARAVAN_MULE, 100.0f);
    for (Creature* member : candidates)
        if (member->GetFormation() == group)
            func(member);
}

struct ClassicCaravanPassiveAI : public ScriptedAI
{
    ClassicCaravanPassiveAI(Creature* creature) : ScriptedAI(creature) { }

    void EnableCombat(Unit* attacker)
    {
        me->SetReactState(REACT_AGGRESSIVE);

        ForEachCaravanMember(me, [attacker](Creature* member)
        {
            if (!member->HasReactState(REACT_AGGRESSIVE) && member->IsAlive())
            {
                member->SetReactState(REACT_AGGRESSIVE);
                member->AI()->AttackStart(attacker);
            }
        });
    }

    void DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (attacker && !me->HasReactState(REACT_AGGRESSIVE))
        {
            EnableCombat(attacker);
            AttackStart(attacker);
        }
    }

    void AttackStart(Unit* victim) override
    {
        if (!victim)
            return;

        if (me->HasReactState(REACT_PASSIVE))
        {
            if (me->IsWithinDistInMap(victim, me->GetAttackDistance(victim)))
                EnableCombat(victim);
            else
            {
                // always add threat even if passive to avoid constantly evading
                me->GetThreatManager().AddThreat(victim, 0.0f);
                return;
            }
        }

        ScriptedAI::AttackStart(victim);
    }
};

struct classic_npc_guard_didier : public ClassicCaravanPassiveAI
{
    classic_npc_guard_didier(Creature* creature) : ClassicCaravanPassiveAI(creature), m_muleDied(false) { }

    bool m_muleDied;

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        m_muleDied = false;
        me->SetReactState(REACT_PASSIVE);
        me->SetGossipMenuId(GOSSIP_NOT_STARTED);
        ClassicCaravanPassiveAI::JustAppeared();
    }

    void JustDied(Unit* /*killer*/) override
    {
        m_muleDied = false;

        ForEachCaravanMember(me, [](Creature* member)
        {
            if (member->IsAlive())
                member->DespawnOrUnsummon(1ms);
        });
    }

    void JustReachedHome() override
    {
        if (m_muleDied)
        {
            m_muleDied = false;
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveIdle();
            me->Say(SAY_MULE_DIED);
            me->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            me->SetGossipMenuId(GOSSIP_MULE_DIED);
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);

            ForEachCaravanMember(me, [](Creature* member)
            {
                if (member->IsAlive())
                {
                    if (member->HasAura(SPELL_MARK_OF_DIDIER))
                        member->RemoveAurasDueToSpell(SPELL_MARK_OF_DIDIER);

                    member->DespawnOrUnsummon(90s);
                }
            });
            me->DespawnOrUnsummon(90s);

            if (me->HasAura(SPELL_MARK_OF_DIDIER))
                me->RemoveAurasDueToSpell(SPELL_MARK_OF_DIDIER);
        }
        else
            me->SetReactState(REACT_PASSIVE);
    }

    // VMaNGOS GroupMemberJustDied (sent by the mule AI, TC has no formation member death hook)
    void DoAction(int32 action) override
    {
        if (action == ACTION_GROUP_MEMBER_DIED)
            m_muleDied = true;
    }
};

struct classic_npc_caravan_mule : public ClassicCaravanPassiveAI
{
    classic_npc_caravan_mule(Creature* creature) : ClassicCaravanPassiveAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (CreatureGroup* group = me->GetFormation())
            if (Creature* leader = group->GetLeader())
                if (leader != me && leader->IsAlive() && leader->AI())
                    leader->AI()->DoAction(ACTION_GROUP_MEMBER_DIED);
    }
};

void AddSC_classic_eastern_plaguelands()
{
    RegisterCreatureAI(classic_npc_eris_havenfire);
    RegisterCreatureAI(classic_npc_eris_havenfire_peasant);
    RegisterGameObjectAI(classic_go_mark_of_detonation);
    RegisterCreatureAI(classic_npc_guard_didier);
    RegisterCreatureAI(classic_npc_caravan_mule);
}
