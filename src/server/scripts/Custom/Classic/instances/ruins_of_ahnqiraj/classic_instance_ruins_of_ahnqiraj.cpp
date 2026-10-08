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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/instance_ruins_of_ahnqiraj.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: instance_ruins_of_ahnqiraj
// VMaNGOS patch checks (sWorld.GetWowPatch()) always take the 1.10+ branch (Classic 1.60 client).

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureData.h"
#include "CreatureGroups.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuaternionData.h"
#include "ReputationMgr.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <list>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{
// DungeonEncounter.db2 ids used by TC for AQ20; only registered when present in the client data
struct ClassicAQ20EncounterId
{
    uint32 BossId;
    uint32 DungeonEncounterId;
};

ClassicAQ20EncounterId const ClassicAQ20EncounterIds[] =
{
    { CLASSIC_AQ20_TYPE_KURINNAXX, 718 },
    { CLASSIC_AQ20_TYPE_RAJAXX,    719 },
    { CLASSIC_AQ20_TYPE_MOAM,      720 },
    { CLASSIC_AQ20_TYPE_BURU,      721 },
    { CLASSIC_AQ20_TYPE_AYAMISS,   722 },
    { CLASSIC_AQ20_TYPE_OSSIRIAN,  723 }
};

bool ClassicAQ20IsBossType(uint32 type)
{
    return type < CLASSIC_AQ20_MAX_ENCOUNTER && type != CLASSIC_AQ20_TYPE_GENERAL_ANDOROV;
}

struct ClassicAQ20SpawnLocation
{
    float x, y, z;
};

std::array<ClassicAQ20SpawnLocation, 11> const ClassicAQ20CrystalSpawn =
{{
    { -9407.164062f, 1959.240845f, 85.558998f }, // central spawn, not initially spawned since it was a nerfed mechanic added in 1.11
    { -9357.931641f, 1930.596802f, 85.556198f },
    { -9383.113281f, 2011.042725f, 85.556389f },
    { -9243.36f, 1979.04f, 85.556f },
    { -9281.68f, 1886.66f, 85.5558f },
    { -9241.8f, 1806.39f, 85.5557f },
    { -9366.78f, 1781.76f, 85.5561f },
    { -9297.668945f, 1747.256348f, 85.5566f },
    { -9430.37f, 1786.86f, 85.557f },
    { -9187.087891f, 1940.501099f, 85.5564f },
    { -9406.73f, 1863.13f, 85.5558f }
}};

// VMaNGOS spawns Andorov + his 4 Kaldorei Elites from DB (guids 301311..301315, spawn_flags 2 = not spawned by default)
// with GetMap()->LoadCreatureSpawnWithGroup(ANDOROV_DB_GUID). Those spawns are not in the TC world DB, so they are summoned
// here at the same positions (slot 0 = Andorov, 1..4 = elites; follow distance/angle = VMaNGOS creature_groups).
struct ClassicAQ20SquadSlot
{
    uint32 Entry;
    float X, Y, Z, O;
    float FollowDist, FollowAngle;
};

std::array<ClassicAQ20SquadSlot, 5> const ClassicAQ20AndorovSquad =
{{
    { CLASSIC_AQ20_NPC_GENERAL_ANDOROV, -8538.18f, 1486.1f,  32.3905f, 4.05218f, 0.0f,   0.0f },
    { CLASSIC_AQ20_NPC_KALDOREI_ELITE,  -8540.54f, 1488.2f,  32.4336f, 4.31096f, 4.856f, 3.7f },
    { CLASSIC_AQ20_NPC_KALDOREI_ELITE,  -8538.09f, 1488.29f, 32.4843f, 4.11898f, 4.743f, 3.2f },
    { CLASSIC_AQ20_NPC_KALDOREI_ELITE,  -8536.24f, 1486.57f, 32.4327f, 3.92699f, 4.813f, 2.6f },
    { CLASSIC_AQ20_NPC_KALDOREI_ELITE,  -8535.95f, 1484.21f, 32.3366f, 4.06662f, 4.929f, 2.1f }
}};

enum ClassicAQ20InstanceMisc : uint32
{
    CLASSIC_AQ20_GO_AQ_DOOR              = 176149,
    CLASSIC_AQ20_FACTION_CENARION_CIRCLE = 609,

    // generic_scripts 154710 texts
    CLASSIC_AQ20_SAY_ANDOROV_START_1     = 11028,
    CLASSIC_AQ20_SAY_ANDOROV_START_2     = 11477,

    // VMaNGOS pylon guid kept spawned by OnObjectCreate (30000000 + 399461)
    CLASSIC_AQ20_PYLON_KEPT_SPAWN_ID     = 30399461
};

bool ClassicAQ20IsRajaxxWaveEntry(uint32 entry)
{
    switch (entry)
    {
        case CLASSIC_AQ20_NPC_CAPTAIN_QEEZ:
        case CLASSIC_AQ20_NPC_CAPTAIN_TUUBID:
        case CLASSIC_AQ20_NPC_CAPTAIN_DRENN:
        case CLASSIC_AQ20_NPC_CAPTAIN_XURREM:
        case CLASSIC_AQ20_NPC_MAJOR_YEGGETH:
        case CLASSIC_AQ20_NPC_MAJOR_PAKKON:
        case CLASSIC_AQ20_NPC_COLONEL_ZERRAN:
        case CLASSIC_AQ20_NPC_SWARMGUARD_NEEDLER:
        case CLASSIC_AQ20_NPC_QIRAJI_WARRIOR:
            return true;
        default:
            return false;
    }
}

// VMaNGOS CreatureGroup::GetOriginalLeaderGuid().GetEntry()
uint32 ClassicAQ20GetFormationLeaderEntry(Creature* creature)
{
    if (CreatureGroup* group = creature->GetFormation())
        if (CreatureData const* data = sObjectMgr->GetCreatureData(group->GetLeaderSpawnId()))
            return data->id;
    return 0;
}
}

class classic_instance_ruins_of_ahnqiraj : public InstanceMapScript
{
public:
    classic_instance_ruins_of_ahnqiraj() : InstanceMapScript(ClassicRuinsOfAhnQirajScriptName, CLASSIC_AQ20_MAP_ID) { }

    struct classic_instance_ruins_of_ahnqiraj_InstanceScript : public InstanceScript
    {
        explicit classic_instance_ruins_of_ahnqiraj_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(ClassicRuinsOfAhnQirajDataHeader);
            SetBossNumber(CLASSIC_AQ20_MAX_ENCOUNTER);

            std::vector<DungeonEncounterData> encounters;
            for (ClassicAQ20EncounterId const& enc : ClassicAQ20EncounterIds)
                if (sDungeonEncounterStore.LookupEntry(enc.DungeonEncounterId))
                    encounters.push_back({ enc.BossId, {{ enc.DungeonEncounterId }} });
            LoadDungeonEncounterData(encounters);

            m_uiAndorovState = NOT_STARTED;
            m_uiGladiatorDeath = 0;
            m_uiRajaxxEventResetTimer = 2000;
            m_bRajaxxEventIsToReset = false;
            m_squadRespawnTimer.fill(0);
            m_andorovScriptTimer = 0;
        }

        ObjectGuid m_uiKurinnaxxGUID, m_uiBuruGUID, m_uiAyamissGUID, m_uiMoamGUID, m_uiOssirianGUID, m_uiAndorovGUID;
        GuidList m_lKaldoreiElites;
        GuidList m_lYeggethShieldList;
        GuidList m_lOssirianPylons;
        std::unordered_map<ObjectGuid, uint32> crystalIndexes;
        GuidList crystalGuids;
        std::deque<uint32> crystalIndexHistory;

        ObjectGuid m_uiQeezGUID, m_uiTuubidGUID, m_uiDrennGUID, m_uiXurremGUID, m_uiYeggethGUID, m_uiPakkonGUID, m_uiZerranGUID, m_uiRajaxxGUID;

        uint32 m_uiAndorovState;   // VMaNGOS m_auiEncounter[TYPE_GENERAL_ANDOROV] (not a boss in TC; not saved, VMaNGOS resets it on load)
        uint32 m_uiGladiatorDeath;

        uint32 m_uiRajaxxEventResetTimer;
        bool m_bRajaxxEventIsToReset;

        ObjectGuid m_doorGuid;

        // Replaces VMaNGOS InstanceData::OnCreatureEnterCombat / OnCreatureEvade (no TC hooks): polled in Update()
        struct CombatWatch
        {
            bool Engaged = false;
            bool Evading = false;
        };
        std::unordered_map<ObjectGuid, CombatWatch> m_combatWatch;

        // Andorov squad (summoned, see ClassicAQ20AndorovSquad)
        std::array<ObjectGuid, 5> m_squadGuids;
        std::array<uint32, 5> m_squadRespawnTimer;   // ms, 0 = no pending respawn
        uint32 m_andorovScriptTimer;                  // generic_scripts 154710 delayed text

        bool IsEncounterInProgress() const override
        {
            if (InstanceScript::IsEncounterInProgress())
                return true;
            return m_uiAndorovState == IN_PROGRESS || m_uiAndorovState == SPECIAL;
        }

        ObjectGuid GetGuidData(uint32 uiData) const override
        {
            switch (uiData)
            {
                case CLASSIC_AQ20_DATA_OSSIRIAN:  return m_uiOssirianGUID;
                case CLASSIC_AQ20_DATA_BURU:      return m_uiBuruGUID;
                case CLASSIC_AQ20_DATA_AYAMISS:   return m_uiAyamissGUID;
                case CLASSIC_AQ20_DATA_MOAM:      return m_uiMoamGUID;
                case CLASSIC_AQ20_DATA_ANDOROV:   return m_uiAndorovGUID;
                case CLASSIC_AQ20_DATA_KURINNAXX: return m_uiKurinnaxxGUID;
                case CLASSIC_AQ20_DATA_QEEZ:      return m_uiQeezGUID;
                case CLASSIC_AQ20_DATA_TUUBID:    return m_uiTuubidGUID;
                case CLASSIC_AQ20_DATA_DRENN:     return m_uiDrennGUID;
                case CLASSIC_AQ20_DATA_XURREM:    return m_uiXurremGUID;
                case CLASSIC_AQ20_DATA_YEGGETH:   return m_uiYeggethGUID;
                case CLASSIC_AQ20_DATA_PAKKON:    return m_uiPakkonGUID;
                case CLASSIC_AQ20_DATA_ZERRAN:    return m_uiZerranGUID;
                case CLASSIC_AQ20_DATA_RAJAXX:    return m_uiRajaxxGUID;
                case CLASSIC_AQ20_DATA_YEGGETH_SHIELD:
                {
                    // VMaNGOS picks a random member of the Yeggeth group list (its loop is broken; intent kept)
                    if (m_lYeggethShieldList.empty())
                        return ObjectGuid::Empty;
                    auto itr = m_lYeggethShieldList.begin();
                    std::advance(itr, urand(0, uint32(m_lYeggethShieldList.size()) - 1));
                    return *itr;
                }
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            if (type == CLASSIC_AQ20_DATA_CRYSTAL)
                SpawnNewCrystals(data);
        }

        // VMaNGOS OnCreatureEnterCombat
        void OnCreatureEnterCombat(Creature* pCreature)
        {
            if (!ClassicAQ20IsRajaxxWaveEntry(pCreature->GetEntry()))
                return;

            /** Create Yeggeth list for Rajaxx Shield ability */
            if (ClassicAQ20GetFormationLeaderEntry(pCreature) == CLASSIC_AQ20_NPC_MAJOR_YEGGETH)
                m_lYeggethShieldList.push_back(pCreature->GetGUID());

            // Fight Change in 1.10.1
            // https://wowpedia.fandom.com/wiki/General_Rajaxx?oldid=180046
            // "as soon as you pull the liutenant runs in with his group and starts to fight"
            if (GetBossState(CLASSIC_AQ20_TYPE_RAJAXX) != DONE &&
                (m_uiAndorovState == NOT_STARTED || m_uiAndorovState == FAIL))
            {
                if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                {
                    if (!pAndorov->IsAlive())
                        pAndorov = RespawnSquadMember(0, pAndorov);
                    if (pAndorov)
                        StartAndorovScript(pAndorov);
                    SetData(CLASSIC_AQ20_TYPE_GENERAL_ANDOROV, IN_PROGRESS);
                }
            }
            m_bRajaxxEventIsToReset = false;
        }

        // VMaNGOS OnCreatureEvade
        void OnCreatureEvade(Creature* pCreature)
        {
            if (ClassicAQ20IsRajaxxWaveEntry(pCreature->GetEntry()))
            {
                // If any creature from Rajaxx's wave is on evade mode, reset Rajaxx.
                m_uiRajaxxEventResetTimer = 2000;
                m_bRajaxxEventIsToReset = true;
                return;
            }

            if (pCreature->GetEntry() == CLASSIC_AQ20_NPC_KALDOREI_ELITE)
            {
                if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                {
                    if (pAndorov->IsInCombat())
                    {
                        // VMaNGOS pCreature->AddThreatsOf(pAndorov)
                        // TODO(classic): TC may ignore threat added while the elite is still in evade mode.
                        for (ThreatReference const* ref : pAndorov->GetThreatManager().GetUnsortedThreatList())
                            pCreature->GetThreatManager().AddThreat(ref->GetVictim(), ref->GetThreat(), nullptr, true, true);
                    }
                }
            }
        }

        void OnCreatureCreate(Creature* pCreature) override
        {
            InstanceScript::OnCreatureCreate(pCreature);

            switch (pCreature->GetEntry())
            {
                case CLASSIC_AQ20_NPC_KURINNAXX:       m_uiKurinnaxxGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_CAPTAIN_QEEZ:    m_uiQeezGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_CAPTAIN_TUUBID:  m_uiTuubidGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_CAPTAIN_DRENN:   m_uiDrennGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_CAPTAIN_XURREM:  m_uiXurremGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_MAJOR_YEGGETH:   m_uiYeggethGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_MAJOR_PAKKON:    m_uiPakkonGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_COLONEL_ZERRAN:  m_uiZerranGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_RAJAXX:          m_uiRajaxxGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_BURU:            m_uiBuruGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_AYAMISS:         m_uiAyamissGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_MOAM:            m_uiMoamGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_OSSIRIAN:        m_uiOssirianGUID = pCreature->GetGUID(); break;
                case CLASSIC_AQ20_NPC_GENERAL_ANDOROV:
                    pCreature->SetImmuneToNPC(true);
                    m_uiAndorovGUID = pCreature->GetGUID();
                    break;
                case CLASSIC_AQ20_NPC_KALDOREI_ELITE:
                    pCreature->SetImmuneToNPC(true);
                    m_lKaldoreiElites.push_back(pCreature->GetGUID());
                    m_combatWatch[pCreature->GetGUID()] = CombatWatch();
                    break;
                default:
                    break;
            }

            if (ClassicAQ20IsRajaxxWaveEntry(pCreature->GetEntry()))
                m_combatWatch[pCreature->GetGUID()] = CombatWatch();
        }

        void OnGameObjectCreate(GameObject* pGo) override
        {
            InstanceScript::OnGameObjectCreate(pGo);

            switch (pGo->GetEntry())
            {
                case CLASSIC_AQ20_GO_OSSIRIAN_CRYSTAL:
                    // VMaNGOS hides the DB-spawned pylons (except one); crystals summoned by SpawnNewCrystals have no spawn id.
                    // TODO(classic): the TC world has no DB spawns of 180619 on map 509; kept for parity.
                    if (!pGo->GetSpawnId())
                        break;
                    m_lOssirianPylons.push_back(pGo->GetGUID());
                    if (pGo->GetSpawnId() != CLASSIC_AQ20_PYLON_KEPT_SPAWN_ID)
                        pGo->DespawnOrUnsummon(0s, Seconds(7 * DAY));
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS OnCreatureDeath
        void OnUnitDeath(Unit* unit) override
        {
            Creature* pCreature = unit->ToCreature();
            if (!pCreature)
                return;

            switch (pCreature->GetEntry())
            {
                case CLASSIC_AQ20_NPC_RAJAXX:
                    GiveRepAfterRajaxxDeath(pCreature);
                    // Rajaxx is DB-scripted in VMaNGOS (no C++ script sets TYPE_RAJAXX); safety net so the Andorov/door
                    // logic sees the encounter as done.
                    SetData(CLASSIC_AQ20_TYPE_RAJAXX, DONE);
                    break;
                case CLASSIC_AQ20_NPC_SWARMGUARD_NEEDLER:
                case CLASSIC_AQ20_NPC_QIRAJI_WARRIOR:
                    pCreature->DespawnOrUnsummon(3s, Seconds(CLASSIC_AQ20_RESPAWN_FOUR_DAYS));
                    break;
                case CLASSIC_AQ20_NPC_GENERAL_ANDOROV:
                    pCreature->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    [[fallthrough]];
                case CLASSIC_AQ20_NPC_KALDOREI_ELITE:
                    // No respawn before Kurinaxx's death or during Rajaxx event.
                    if (GetBossState(CLASSIC_AQ20_TYPE_KURINNAXX) != DONE || GetBossState(CLASSIC_AQ20_TYPE_RAJAXX) == IN_PROGRESS)
                        SetSquadMemberRespawnTime(pCreature, CLASSIC_AQ20_RESPAWN_FOUR_DAYS);
                    else
                        SetSquadMemberRespawnTime(pCreature, CLASSIC_AQ20_RESPAWN_15_MINUTES);
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 uiType) const override
        {
            switch (uiType)
            {
                case CLASSIC_AQ20_TYPE_QIRAJI_GLADIATOR:
                    return m_uiGladiatorDeath;
                case CLASSIC_AQ20_TYPE_GENERAL_ANDOROV:
                    return m_uiAndorovState;
                case CLASSIC_AQ20_TYPE_KURINNAXX:
                case CLASSIC_AQ20_TYPE_RAJAXX:
                case CLASSIC_AQ20_TYPE_BURU:
                case CLASSIC_AQ20_TYPE_MOAM:
                case CLASSIC_AQ20_TYPE_AYAMISS:
                case CLASSIC_AQ20_TYPE_OSSIRIAN:
                    return GetBossState(uiType);
                default:
                    return 0;
            }
        }

        void SetBoss(uint32 uiType, uint32 uiData)
        {
            // TC blocks DONE -> other state transitions
            if (GetBossState(uiType) == DONE && uiData != DONE)
                return;
            SetBossState(uiType, EncounterState(uiData));
        }

        void SetData(uint32 uiType, uint32 uiData) override
        {
            switch (uiType)
            {
                case CLASSIC_AQ20_TYPE_QIRAJI_GLADIATOR:
                    if (uiData > 0)
                        m_uiGladiatorDeath = m_uiGladiatorDeath + uiData;
                    else
                        m_uiGladiatorDeath = 0;
                    return;
                case CLASSIC_AQ20_DATA_CRYSTAL_INIT:
                    SpawnNewCrystals(ObjectGuid::Empty);
                    return;
                case CLASSIC_AQ20_TYPE_KURINNAXX:
                    SetBoss(uiType, uiData);
                    break;
                case CLASSIC_AQ20_TYPE_GENERAL_ANDOROV:
                    if (uiData == NOT_STARTED || uiData == FAIL)
                    {
                        if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                            pAndorov->SetGossipMenuId(CLASSIC_AQ20_ANDOROV_GOSSIP_NOT_STARTED);
                    }
                    if (uiData == IN_PROGRESS)
                    {
                        SetAndorovSquadImmunity(false);
                        if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                        {
                            pAndorov->SetGossipMenuId(CLASSIC_AQ20_ANDOROV_GOSSIP_IN_PROGRESS);
                            pAndorov->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);
                        }
                    }
                    m_uiAndorovState = uiData;
                    break;
                case CLASSIC_AQ20_TYPE_RAJAXX:
                    if (uiData == NOT_STARTED)
                    {
                        /** Respawn Andorov and elite in 15 minutes */
                        if (GetBossState(CLASSIC_AQ20_TYPE_KURINNAXX) == DONE)
                            SetAndorovSquadRespawnTime(CLASSIC_AQ20_RESPAWN_15_MINUTES);
                    }
                    if (uiData == DONE && GetBossState(CLASSIC_AQ20_TYPE_RAJAXX) != DONE)
                    {
                        if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                        {
                            // World of Warcraft Client Patch 1.10.0 (2006-03-28)
                            // - Lieutenant General Andorov will now offer supplies if kept alive through the battle.
                            pAndorov->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);
                            pAndorov->SetGossipMenuId(CLASSIC_AQ20_ANDOROV_GOSSIP_DONE);
                            SetSquadMemberRespawnTime(pAndorov, CLASSIC_AQ20_RESPAWN_FOUR_DAYS);
                        }
                        SetAndorovSquadImmunity(true);
                    }
                    m_bRajaxxEventIsToReset = false;
                    SetBoss(uiType, uiData);
                    break;
                case CLASSIC_AQ20_TYPE_OSSIRIAN:
                    if (uiData == FAIL || uiData == DONE)
                    {
                        for (ObjectGuid const& crystalGuid : crystalGuids)
                        {
                            if (GameObject* invoc = instance->GetGameObject(crystalGuid))
                                invoc->Delete();
                        }

                        crystalGuids.clear();
                        crystalIndexes.clear();
                        crystalIndexHistory.clear();
                    }
                    SetBoss(uiType, uiData);
                    break;
                case CLASSIC_AQ20_TYPE_BURU:
                case CLASSIC_AQ20_TYPE_MOAM:
                case CLASSIC_AQ20_TYPE_AYAMISS:
                    SetBoss(uiType, uiData);
                    break;
                default:
                    return;
            }
            // VMaNGOS SaveToDB on DONE: TC saves boss states itself.
        }

        void Update(uint32 uiDiff) override
        {
            UpdateCombatWatch();

            /*
            Fight Change in 1.10.1

            This fight has changed somewhat, first off if someone dies
            they are no longer able to return via release and run back,
            there is a big door that closes and you cannot get past it.

            https://wowwiki-archive.fandom.com/wiki/General_Rajaxx?oldid=121120#Fight_Change_in_1.10.1
            */
            if (IsAnyBossInCombat())
            {
                if (m_doorGuid.IsEmpty())
                {
                    Position const doorPos(-8526.0f, 1507.4f, 49.0f, 4.20662f);
                    if (GameObject* pAQDoor = GameObject::CreateGameObject(CLASSIC_AQ20_GO_AQ_DOOR, instance, doorPos,
                        QuaternionData(0.0f, 0.0f, 0.861534f, -0.5077f), 255, GO_STATE_READY))
                    {
                        pAQDoor->SetSpawnedByDefault(false);
                        if (instance->AddToMap(pAQDoor))
                            m_doorGuid = pAQDoor->GetGUID();
                        else
                            delete pAQDoor;
                    }
                }
            }
            else
            {
                if (!m_doorGuid.IsEmpty())
                {
                    if (GameObject* pAQDoor = instance->GetGameObject(m_doorGuid))
                        pAQDoor->Delete();
                    m_doorGuid.Clear();
                }
            }

            if (GetBossState(CLASSIC_AQ20_TYPE_KURINNAXX) == DONE && GetBossState(CLASSIC_AQ20_TYPE_RAJAXX) != DONE && m_uiAndorovGUID.IsEmpty())
            {
                if (Creature* pAndorov = SummonAndorovSquad())
                {
                    pAndorov->SetGossipMenuId(CLASSIC_AQ20_ANDOROV_GOSSIP_IN_PROGRESS);
                    // VMaNGOS MoveWaypoint(0) on the spawn's own creature_movement path (empty for guid 301311).
                }
            }

            if (m_bRajaxxEventIsToReset)
            {
                if (m_uiRajaxxEventResetTimer < uiDiff)
                {
                    if (Creature* pRajaxx = instance->GetCreature(m_uiRajaxxGUID))
                        if (pRajaxx->IsAIEnabled())
                            pRajaxx->AI()->EnterEvadeMode(EvadeReason::Other);
                    m_bRajaxxEventIsToReset = false;
                }
                else
                    m_uiRajaxxEventResetTimer -= uiDiff;
            }

            for (auto iter = crystalIndexes.begin(); iter != crystalIndexes.end();)
            {
                // Crystal was despawned but still in our index for some reason
                if (!instance->GetGameObject(iter->first))
                    iter = crystalIndexes.erase(iter);
                else
                    ++iter;
            }

            UpdateSquadRespawns(uiDiff);

            if (m_andorovScriptTimer)
            {
                if (m_andorovScriptTimer <= uiDiff)
                {
                    m_andorovScriptTimer = 0;
                    if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                        if (pAndorov->IsAlive())
                            ClassicScriptText(CLASSIC_AQ20_SAY_ANDOROV_START_2, pAndorov);
                }
                else
                    m_andorovScriptTimer -= uiDiff;
            }
        }

        /* Private methods */

        void UpdateCombatWatch()
        {
            // Callbacks may summon creatures (OnCreatureCreate -> m_combatWatch insert): collect first, then dispatch
            GuidVector enteredCombat;
            GuidVector evaded;
            for (auto itr = m_combatWatch.begin(); itr != m_combatWatch.end();)
            {
                Creature* creature = instance->GetCreature(itr->first);
                if (!creature)
                {
                    itr = m_combatWatch.erase(itr);
                    continue;
                }

                bool const engaged = creature->IsAlive() && creature->IsEngaged();
                bool const evading = creature->IsAlive() && creature->IsInEvadeMode();
                if (engaged && !itr->second.Engaged)
                    enteredCombat.push_back(itr->first);
                if (evading && !itr->second.Evading)
                    evaded.push_back(itr->first);
                itr->second.Engaged = engaged;
                itr->second.Evading = evading;
                ++itr;
            }

            for (ObjectGuid const& guid : enteredCombat)
                if (Creature* creature = instance->GetCreature(guid))
                    OnCreatureEnterCombat(creature);
            for (ObjectGuid const& guid : evaded)
                if (Creature* creature = instance->GetCreature(guid))
                    OnCreatureEvade(creature);
        }

        bool IsAnyBossInCombat()
        {
            if (GetData(CLASSIC_AQ20_TYPE_GENERAL_ANDOROV) == IN_PROGRESS)
                return true;

            for (uint32 i = CLASSIC_AQ20_DATA_KURINNAXX; i <= CLASSIC_AQ20_DATA_ZERRAN; ++i)
            {
                ObjectGuid guid = GetGuidData(i);
                if (guid.IsEmpty())
                    continue;
                if (Creature* pCreature = instance->GetCreature(guid))
                    if (pCreature->IsAlive() && pCreature->GetVictim())
                        return true;
            }
            return false;
        }

        // generic_scripts 154710 (Lieutenant General Andorov start script)
        void StartAndorovScript(Creature* pAndorov)
        {
            pAndorov->HandleEmoteCommand(EMOTE_ONESHOT_SHOUT);
            ClassicScriptText(CLASSIC_AQ20_SAY_ANDOROV_START_1, pAndorov);
            m_andorovScriptTimer = 3000;
            // TODO(classic): generic_scripts 154710 also starts waypoints (delay 2s, command 60: creature_movement_special
            // path 15471) whose point scripts 1547102/1547103 position the elites and start the Rajaxx waves (generic_scripts
            // 153911). The Rajaxx/Andorov wave event is DB-scripted in VMaNGOS and is not part of this C++ port.
        }

        int32 FindSquadSlot(ObjectGuid const& guid) const
        {
            for (uint32 i = 0; i < m_squadGuids.size(); ++i)
                if (m_squadGuids[i] == guid)
                    return int32(i);
            return -1;
        }

        Creature* SummonSquadMember(uint32 slot)
        {
            ClassicAQ20SquadSlot const& data = ClassicAQ20AndorovSquad[slot];
            Creature* member = instance->SummonCreature(data.Entry, Position(data.X, data.Y, data.Z, data.O));
            if (!member)
                return nullptr;

            m_squadGuids[slot] = member->GetGUID();
            m_squadRespawnTimer[slot] = 0;

            if (slot > 0)
            {
                if (Creature* pAndorov = instance->GetCreature(m_squadGuids[0]))
                    if (pAndorov->IsAlive())
                        member->GetMotionMaster()->MoveFollow(pAndorov, data.FollowDist, ChaseAngle(data.FollowAngle));
            }
            else
            {
                m_uiAndorovGUID = member->GetGUID();
                for (uint32 i = 1; i < m_squadGuids.size(); ++i)
                    if (Creature* elite = instance->GetCreature(m_squadGuids[i]))
                        if (elite->IsAlive() && !elite->IsInCombat())
                            elite->GetMotionMaster()->MoveFollow(member, ClassicAQ20AndorovSquad[i].FollowDist, ChaseAngle(ClassicAQ20AndorovSquad[i].FollowAngle));
            }
            return member;
        }

        // VMaNGOS GetMap()->LoadCreatureSpawnWithGroup(ANDOROV_DB_GUID)
        // TODO(classic): if the VMaNGOS spawns 301311..301315 are imported as a manual-spawn spawn group, spawn the group here instead.
        Creature* SummonAndorovSquad()
        {
            Creature* pAndorov = SummonSquadMember(0);
            if (!pAndorov)
                return nullptr;
            for (uint32 i = 1; i < ClassicAQ20AndorovSquad.size(); ++i)
                SummonSquadMember(i);
            return pAndorov;
        }

        // VMaNGOS pAndorov->Respawn()
        Creature* RespawnSquadMember(uint32 slot, Creature* current)
        {
            if (current && current->GetSpawnId())
            {
                current->Respawn(true);
                return current;
            }
            if (current)
                current->DespawnOrUnsummon();
            return SummonSquadMember(slot);
        }

        // VMaNGOS pCreature->SetRespawnTime(delay) for Andorov / Kaldorei Elites
        void SetSquadMemberRespawnTime(Creature* member, uint32 seconds)
        {
            if (member->GetSpawnId())
            {
                member->SetRespawnDelay(seconds);
                if (!member->IsAlive())
                {
                    member->SetRespawnTime(seconds);
                    member->SaveRespawnTime();
                }
                return;
            }

            int32 slot = FindSquadSlot(member->GetGUID());
            if (slot < 0)
                return;
            // "four days" = never within the instance lifetime
            m_squadRespawnTimer[slot] = seconds >= CLASSIC_AQ20_RESPAWN_FOUR_DAYS ? 0 : seconds * IN_MILLISECONDS;
        }

        void UpdateSquadRespawns(uint32 uiDiff)
        {
            for (uint32 slot = 0; slot < m_squadRespawnTimer.size(); ++slot)
            {
                if (!m_squadRespawnTimer[slot])
                    continue;

                if (m_squadRespawnTimer[slot] > uiDiff)
                {
                    m_squadRespawnTimer[slot] -= uiDiff;
                    continue;
                }

                m_squadRespawnTimer[slot] = 0;
                Creature* member = instance->GetCreature(m_squadGuids[slot]);
                if (member && member->IsAlive())
                    continue;
                RespawnSquadMember(slot, member);
            }
        }

        void SetAndorovSquadRespawnTime(uint32 nextRespawnDelay)
        {
            if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
            {
                if (!pAndorov->IsAlive())
                    SetSquadMemberRespawnTime(pAndorov, nextRespawnDelay);
            }
            for (ObjectGuid const& guid : m_lKaldoreiElites)
            {
                if (Creature* pElite = instance->GetCreature(guid))
                {
                    if (!pElite->IsAlive())
                        SetSquadMemberRespawnTime(pElite, nextRespawnDelay);
                }
            }
        }

        void SetAndorovSquadImmunity(bool immune)
        {
            if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                pAndorov->SetImmuneToNPC(immune);
            for (ObjectGuid const& guid : m_lKaldoreiElites)
            {
                if (Creature* pElite = instance->GetCreature(guid))
                    pElite->SetImmuneToNPC(immune);
            }
        }

        void GiveRepAfterRajaxxDeath(Creature* pRajaxx)
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(CLASSIC_AQ20_FACTION_CENARION_CIRCLE); // Cenarion Circle
            if (!factionEntry)
            {
                TC_LOG_ERROR("scripts", "Rajaxx just died, unable to find Cenarion Circle faction");
                return;
            }

            bool andorovAlive = false;
            if (Creature* pAndorov = instance->GetCreature(m_uiAndorovGUID))
                if (pAndorov->IsAlive())
                    andorovAlive = true;

            std::list<Creature*> helpers;
            pRajaxx->GetCreatureListWithEntryInGrid(helpers, CLASSIC_AQ20_NPC_KALDOREI_ELITE, 400.0f);

            int32 helpersAlive = 0;
            for (Creature* helper : helpers)
            {
                if (helper->IsAlive())
                {
                    helpersAlive++;
                    if (!andorovAlive)
                    {
                        // kaldorei despawn if andorov is dead
                        helper->DespawnOrUnsummon(0s, Seconds(CLASSIC_AQ20_RESPAWN_FOUR_DAYS));
                    }
                }
            }

            if (andorovAlive)
                helpersAlive++;

            // Rep gain was buffed in patch 1.10.
            int32 const repForKill = 50;
            int32 const repPerHelper = 45;

            auto GiveRep = [factionEntry, helpersAlive, repForKill, repPerHelper](Player* pPlayer)
            {
                if (pPlayer->GetReputationMgr().GetRank(factionEntry) <= REP_EXALTED)
                {
                    for (int32 i = 0; i < 3; i++)
                        pPlayer->GetReputationMgr().ModifyReputation(factionEntry, repForKill);

                    // you get 45 rep for every alive helper
                    for (int32 i = 0; i < helpersAlive; i++)
                        pPlayer->GetReputationMgr().ModifyReputation(factionEntry, repPerHelper);
                }
            };

            // VMaNGOS: loot recipient + every member of its group. TC: every player on Rajaxx's tap list.
            for (ObjectGuid const& tapperGuid : pRajaxx->GetTapList())
                if (Player* pPlayer = instance->GetPlayer(tapperGuid))
                    if (pPlayer->IsInWorld())
                        GiveRep(pPlayer);
        }

        /**
        * The spawn logic is as follows (approximated as much as possible from multiple videos):
        * 1. From the last crystal used, find a crystal location within 80 yards.
        * 2. Spawn two crystals. One at, and one near to, this location
        * There are some rules regarding how many crystals should be spawned and where.
        * 1. Only 2 crystals should be spawned at once
        * 2. We should not spawn the same crystal as the one used
        * 3. We should maintain a short history of spawned locations to ensure some variety
        */
        void SpawnNewCrystals(ObjectGuid usedCrystal)
        {
            uint32 used = 0;
            auto previous = crystalIndexes.find(usedCrystal);
            if (previous != crystalIndexes.end())
            {
                used = previous->second;
                crystalIndexes.erase(previous);
            }

            // Truncate the history first
            crystalIndexHistory.resize(2);

            crystalIndexHistory.push_front(used);

            ClassicAQ20SpawnLocation previousLoc = ClassicAQ20CrystalSpawn.at(used);
            // Expand the search after a certain number of attempts otherwise
            // it may be impossible to spawn one within range and we deadlock
            uint32 attempts = 0;
            float maxDistanceLimit = CLASSIC_AQ20_OSSIRIAN_CRYSTAL_INITIAL_DIST;
            float minDistanceLimit = maxDistanceLimit / 2;

            // We already have another crystal spawned. Use that as the hint
            if (!crystalIndexes.empty())
            {
                maxDistanceLimit *= 0.75f;
                minDistanceLimit *= 0.75f;
                uint32 hint = crystalIndexes.begin()->second;
                previousLoc = ClassicAQ20CrystalSpawn.at(hint);
                crystalIndexHistory.push_front(hint);
            }

            std::vector<uint32> possibleIndexes;
            possibleIndexes.reserve(ClassicAQ20CrystalSpawn.size());
            for (uint32 i = 0; i < ClassicAQ20CrystalSpawn.size(); ++i)
            {
                auto iter = std::find(crystalIndexHistory.cbegin(), crystalIndexHistory.cend(), i);
                if (iter != crystalIndexHistory.cend())
                    continue;

                possibleIndexes.push_back(i);
            }

            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(possibleIndexes.begin(), possibleIndexes.end(), g);

            while (crystalIndexes.size() < CLASSIC_AQ20_OSSIRIAN_CRYSTAL_NUM_ACTIVE)
            {
                // Safety: VMaNGOS loops forever if every candidate index was consumed
                if (possibleIndexes.empty())
                    return;

                for (auto iter = possibleIndexes.begin(); iter != possibleIndexes.end() && crystalIndexes.size() < CLASSIC_AQ20_OSSIRIAN_CRYSTAL_NUM_ACTIVE;)
                {
                    if (++attempts >= 5)
                    {
                        // increase distance to search further each extra attempt
                        minDistanceLimit *= 0.9f;
                        maxDistanceLimit *= 1.1f;
                    }

                    uint32 newIndex = *iter;
                    ClassicAQ20SpawnLocation const& newLoc = ClassicAQ20CrystalSpawn.at(newIndex);
                    float dist = std::sqrt(std::pow(newLoc.x - previousLoc.x, 2.0f) + std::pow(newLoc.y - previousLoc.y, 2.0f));
                    if (dist > maxDistanceLimit || dist < minDistanceLimit)
                    {
                        ++iter;
                        continue;
                    }

                    GameObject* pCrystal = GameObject::CreateGameObject(CLASSIC_AQ20_GO_OSSIRIAN_CRYSTAL, instance,
                        Position(newLoc.x, newLoc.y, newLoc.z, 0.0f), QuaternionData(), 255, GO_STATE_READY);
                    if (pCrystal)
                    {
                        pCrystal->SetSpawnedByDefault(false);
                        if (!instance->AddToMap(pCrystal))
                        {
                            delete pCrystal;
                            pCrystal = nullptr;
                        }
                    }

                    if (!pCrystal)
                    {
                        TC_LOG_ERROR("scripts", "[OSSIRIAN] Unable to spawn crystal {} at position #{}", uint32(CLASSIC_AQ20_GO_OSSIRIAN_CRYSTAL), newIndex);
                        return;
                    }

                    // Use the new location as a hint and reduce the limit so the next one
                    // spawns close as well
                    maxDistanceLimit *= 0.50f;
                    minDistanceLimit *= 0.50f;
                    previousLoc = newLoc;
                    iter = possibleIndexes.erase(iter); // remove selected index

                    crystalGuids.push_back(pCrystal->GetGUID());
                    crystalIndexes[pCrystal->GetGUID()] = newIndex;
                }
            }
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_ruins_of_ahnqiraj_InstanceScript(map);
    }
};

void AddSC_classic_instance_ruins_of_ahnqiraj()
{
    new classic_instance_ruins_of_ahnqiraj();
}
