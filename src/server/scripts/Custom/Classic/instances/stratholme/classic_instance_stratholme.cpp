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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/instance_stratholme.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Pet.h"
#include "Player.h"
#include "Random.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_stratholme.h"
#include <list>
#include <set>

namespace
{
enum ClassicStratholmeInstanceMisc : uint32
{
    GO_SERVICE_ENTRANCE         = 175368,
    GO_GAUNTLET_GATE1           = 175357,
    GO_SLAUGHTER_SQUARE_GATE    = 175358,
    GO_ZIGGURAT1                = 175380,                   // baroness
    GO_ZIGGURAT2                = 175379,                   // nerub'enkan
    GO_ZIGGURAT3                = 175381,                   // maleki
    GO_ZIGGURAT4                = 175405,                   // rammstein
    GO_ZIGGURAT5                = 175796,                   // baron
    GO_PORT_GAUNTLET            = 175374,                   // port from gauntlet to slaugther
    GO_PORT_SLAUGTHER           = 175373,                   // port at slaugther
    GO_PORT_ELDERS              = 175377,                   // port at elders square
    GO_PORT_TRAP_GATE_1         = 175351,                   // Portcullis used in the gate traps (rats trap)
    GO_PORT_TRAP_GATE_2         = 175350,                   // Scarlet side
    GO_PORT_TRAP_GATE_3         = 175355,                   // Undead side
    GO_PORT_TRAP_GATE_4         = 175354,
    GO_CAGE_YSIDA               = 181071,                   // in 2 parts, the base is: 181072

    NPC_CRYSTAL                 = 10415,                    // three ziggurat crystals
    NPC_BARON                   = 10440,
    NPC_YSIDA_TRIGGER           = 16100,
    NPC_TIMMY                   = 10808,
    NPC_DATHROHAN               = NPC_STRAT_DATHROHAN,
    NPC_MAGISTRATE              = 10435,

    NPC_RAMSTEIN                = 10439,
    NPC_ABOM_BILE               = 10416,
    NPC_ABOM_VENOM              = 10417,
    NPC_BLACK_GUARD             = 10394,
    NPC_YSIDA                   = 16031,
    NPC_PLAGUED_RAT             = 10441,
    NPC_PLAGUED_INSECT          = 10461,
    NPC_PLAGUED_MAGGOT          = 10536,
    NPC_MINDLESS_UNDEAD         = 11030,
    NPC_VENGEFUL_PHANTOM        = 10387,
    NPC_THE_UNFORGIVEN          = 10516,

    RIVENDARE_YELL_45MIN        = 11812,
    RIVENDARE_YELL_10MIN        = 11813,
    RIVENDARE_YELL_5MIN         = 11815,
    YSIDA_YELL_5MIN             = 11816,
    RIVENDARE_YELL_FAILED       = 11814,
    RIVENDARE_YELL_RAMMSTEIN    = 6398,
    RAMMSTEIN_YELL_SPAWN        = 6425,
    BLACKGUARD_YELL_SPAWN       = 6415,
    RIVENDARE_YELL_READY        = 6401,
    YSIDA_YELL_FAILED           = 11817,
    YSIDA_SAY_REWARD            = 11931,

    SPELL_YSIDA_FREED           = 27773,
    SPELL_STRAT_DEATHTOUCH      = 5
};

Position const StratGateTrapPos[] =                     // Positions of the two Gate Traps
{
    { 3612.29f, -3335.39f, 124.077f, 3.14159f },            // Scarlet side
    { 3919.88f, -3547.34f, 134.269f, 2.94961f }             // Undead side
};

uint32 const StratPlaguedCritters[] =
{
    NPC_PLAGUED_RAT, NPC_PLAGUED_MAGGOT, NPC_PLAGUED_INSECT
};

Position const StratUnforgivenTriggerSpot = { 3712.607f, -3429.338f, 131.001f, 0.0f };

constexpr uint32 STRAT_MINUTE_MS = MINUTE * IN_MILLISECONDS;
}

class classic_instance_stratholme : public InstanceMapScript
{
public:
    classic_instance_stratholme() : InstanceMapScript(ClassicStratholmeScriptName, CLASSIC_STRATHOLME_MAP_ID) { }

    struct classic_instance_stratholme_InstanceScript : public InstanceScript
    {
        classic_instance_stratholme_InstanceScript(InstanceMap* map) : InstanceScript(map),
            _savedEncounters(*this, "ClassicStratEncounters", uint64(0))
        {
            SetHeaders("CSTR");
            Initialize();
        }

        uint32 m_auiEncounter[STRAT_MAX_ENCOUNTER];

        uint32 m_uiGateTrapTimers[2][3];
        bool IsSilverHandDead[5];

        uint8 m_phaseBaron;
        uint32 m_uiBaronRun_Timer;
        uint32 m_uiSlaugtherSquare_Timer;
        uint32 m_uiSlaugtherAboMob_Timer;

        ObjectGuid m_uiServiceEntranceGUID;
        ObjectGuid m_uiGauntletGate1GUID;
        ObjectGuid m_uiSlaughterSquareGateGUID;
        ObjectGuid m_uiZiggurat1GUID;
        ObjectGuid m_uiZiggurat2GUID;
        ObjectGuid m_uiZiggurat3GUID;
        ObjectGuid m_uiZiggurat4GUID;
        ObjectGuid m_uiZiggurat5GUID;
        ObjectGuid m_uiPortGauntletGUID;
        ObjectGuid m_uiPortSlaugtherGUID;
        ObjectGuid m_uiPortElderGUID;
        ObjectGuid m_cageYsidaGUID;
        ObjectGuid m_ratTrapGateGUID[4];

        ObjectGuid m_uiBaronGUID;
        ObjectGuid m_uiTimmyGUID;
        ObjectGuid m_uiYsidaTriggerGUID;
        ObjectGuid m_uiYsidaGUID;
        ObjectGuid m_uiRamsteinGUID;
        ObjectGuid m_uiDathrohanGUID;
        std::set<ObjectGuid> crystalsGUID;
        std::set<ObjectGuid> abomnationGUID;
        std::list<ObjectGuid> slaugtherAboGUID;
        bool m_summoningRammstein;

        uint8 m_uiBlackguardCount;
        uint32 m_uiYsidaReward_Timer;
        uint32 m_uiPostboxesUsed;

        // VMaNGOS saved "m_auiEncounter" as a string on DONE; here it is packed (4 bits per slot) into one persistent value.
        PersistentInstanceScriptValue<uint64> _savedEncounters;
        bool _summonRamsteinAfterLoad = false;

        void Initialize()
        {
            for (uint32& i : m_auiEncounter)
                i = NOT_STARTED;

            for (auto& trapTimer : m_uiGateTrapTimers)
                for (uint8 j = 0; j < 3; ++j)
                    trapTimer[j] = 0;

            for (bool& i : IsSilverHandDead)
                i = false;

            m_phaseBaron = 0;
            m_uiBaronRun_Timer = 0;
            m_uiSlaugtherSquare_Timer = 0;
            m_uiSlaugtherAboMob_Timer = 0;

            crystalsGUID.clear();
            abomnationGUID.clear();
            slaugtherAboGUID.clear();

            m_summoningRammstein = false;

            m_uiBlackguardCount = 5;
            m_uiYsidaReward_Timer = 0;
            m_uiPostboxesUsed = 0;
        }

        bool IsEncounterInProgress() const override
        {
            for (uint32 i : m_auiEncounter)
                if (i == IN_PROGRESS)
                    return true;
            return false;
        }

        // VMaNGOS instance->SummonCreature(entry, x, y, z, o, type, despawn)
        TempSummon* SummonInMap(uint32 entry, float x, float y, float z, float o, TempSummonType type, uint32 despawnMs)
        {
            TempSummon* summon = instance->SummonCreature(entry, Position(x, y, z, o), nullptr, Milliseconds(despawnMs));
            if (summon)
                summon->SetTempSummonType(type);
            return summon;
        }

        Player* GetPlayerInMap()
        {
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* player = ref.GetSource())
                    if (player->IsInWorld())
                        return player;
            return nullptr;
        }

        bool StartSlaugtherSquare()
        {
            uint32 uiCount = uint32(crystalsGUID.size());

            for (ObjectGuid const& guid : crystalsGUID)
            {
                if (Creature* pCristal = instance->GetCreature(guid))
                {
                    if (!pCristal->IsAlive())
                        --uiCount;
                }
                else
                    --uiCount;
            }

            if (!uiCount)
            {
                UpdateGoState(m_uiPortGauntletGUID, GO_STATE_ACTIVE, false);
                UpdateGoState(m_uiPortSlaugtherGUID, GO_STATE_ACTIVE, false);
                SetData(TYPE_CRISTAL_ALL_DIE, DONE);
                return true;
            }

            TC_LOG_DEBUG("scripts", "Instance Stratholme: Cannot open slaugther square yet.");
            return false;
        }

        // if withRestoreTime true, then newState will be ignored and GO should be restored to original state after 10 seconds
        void UpdateGoState(ObjectGuid goGuid, uint32 newState, bool withRestoreTime)
        {
            if (!goGuid)
                return;

            if (GameObject* pGo = instance->GetGameObject(goGuid))
            {
                if (withRestoreTime)
                    pGo->UseDoorOrButton(10 * IN_MILLISECONDS);
                else
                    pGo->SetGoState(GOState(newState));
            }
        }

        void OnCreatureCreate(Creature* pCreature) override
        {
            InstanceScript::OnCreatureCreate(pCreature);

            switch (pCreature->GetEntry())
            {
                case NPC_BARON:
                    m_uiBaronGUID = pCreature->GetGUID();
                    if (GetData(TYPE_RAMSTEIN) != DONE)
                        pCreature->SetUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));
                    break;
                case NPC_YSIDA_TRIGGER:
                    m_uiYsidaTriggerGUID = pCreature->GetGUID();
                    break;
                case NPC_YSIDA:
                    m_uiYsidaGUID = pCreature->GetGUID();
                    break;
                case NPC_CRYSTAL:
                    crystalsGUID.insert(pCreature->GetGUID());
                    break;
                case NPC_ABOM_BILE:
                case NPC_ABOM_VENOM:
                    abomnationGUID.insert(pCreature->GetGUID());
                    slaugtherAboGUID.push_back(pCreature->GetGUID());
                    break;
                case NPC_RAMSTEIN:
                    m_uiRamsteinGUID = pCreature->GetGUID();
                    break;
                case NPC_TIMMY:
                    m_uiTimmyGUID = pCreature->GetGUID();
                    break;
                case NPC_DATHROHAN:
                    m_uiDathrohanGUID = pCreature->GetGUID();
                    break;
                case NPC_MAGISTRATE:
                    pCreature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);   // VMaNGOS UNIT_FLAG_SPAWNING
                    break;
                default:
                    break;
            }
            // VMaNGOS also stored every creature guid in an unused set (npc_placeEcarlateGUID); not ported.
        }

        void OnGameObjectCreate(GameObject* pGo) override
        {
            InstanceScript::OnGameObjectCreate(pGo);

            switch (pGo->GetEntry())
            {
                case GO_SERVICE_ENTRANCE:
                    m_uiServiceEntranceGUID = pGo->GetGUID();
                    break;
                case GO_GAUNTLET_GATE1:
                    //weird, but unless flag is set, client will not respond as expected. DB bug?
                    pGo->SetFlag(GO_FLAG_LOCKED);
                    m_uiGauntletGate1GUID = pGo->GetGUID();
                    break;
                case GO_SLAUGHTER_SQUARE_GATE:
                    m_uiSlaughterSquareGateGUID = pGo->GetGUID();
                    break;
                case GO_ZIGGURAT1:
                    m_uiZiggurat1GUID = pGo->GetGUID();
                    if (GetData(TYPE_NERUB) == DONE)            // sic (VMaNGOS checks TYPE_NERUB here)
                        pGo->UseDoorOrButton();
                    break;
                case GO_ZIGGURAT2:
                    m_uiZiggurat2GUID = pGo->GetGUID();
                    if (GetData(TYPE_NERUB) == DONE)
                        pGo->UseDoorOrButton();
                    break;
                case GO_ZIGGURAT3:
                    m_uiZiggurat3GUID = pGo->GetGUID();
                    if (GetData(TYPE_PALLID) == DONE)
                        pGo->UseDoorOrButton();
                    break;
                case GO_ZIGGURAT4:
                    if (GetData(TYPE_RAMSTEIN) == DONE)
                        pGo->UseDoorOrButton();
                    m_uiZiggurat4GUID = pGo->GetGUID();
                    break;
                case GO_ZIGGURAT5:
                    if (GetData(TYPE_RAMSTEIN) == DONE)
                        pGo->UseDoorOrButton();
                    m_uiZiggurat5GUID = pGo->GetGUID();
                    break;
                case GO_PORT_GAUNTLET:
                    m_uiPortGauntletGUID = pGo->GetGUID();
                    if (GetData(TYPE_CRISTAL_ALL_DIE) == DONE)
                        pGo->UseDoorOrButton();
                    break;
                case GO_PORT_SLAUGTHER:
                    m_uiPortSlaugtherGUID = pGo->GetGUID();
                    if (GetData(TYPE_CRISTAL_ALL_DIE) == DONE)
                        pGo->UseDoorOrButton();
                    break;
                case GO_PORT_ELDERS:
                    m_uiPortElderGUID = pGo->GetGUID();
                    break;
                case GO_PORT_TRAP_GATE_1:
                    m_ratTrapGateGUID[0] = pGo->GetGUID();
                    break;
                case GO_PORT_TRAP_GATE_2:
                    m_ratTrapGateGUID[1] = pGo->GetGUID();
                    break;
                case GO_PORT_TRAP_GATE_3:
                    m_ratTrapGateGUID[2] = pGo->GetGUID();
                    break;
                case GO_PORT_TRAP_GATE_4:
                    m_ratTrapGateGUID[3] = pGo->GetGUID();
                    break;
                case GO_CAGE_YSIDA:
                    m_cageYsidaGUID = pGo->GetGUID();
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS OnCreatureDeath
        void OnUnitDeath(Unit* who) override
        {
            if (!who->IsCreature())
                return;

            switch (who->GetEntry())
            {
                case NPC_BLACK_GUARD:
                    if (m_uiBlackguardCount)
                        --m_uiBlackguardCount;
                    if (!m_uiBlackguardCount)
                    {
                        if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                            ClassicScriptText(RIVENDARE_YELL_READY, pBaron);
                    }
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 uiType) const override
        {
            switch (uiType)
            {
                case TYPE_SH_QUEST:
                    if (IsSilverHandDead[0] && IsSilverHandDead[1] && IsSilverHandDead[2] && IsSilverHandDead[3] && IsSilverHandDead[4])
                        return 1;
                    return 0;
                default:
                    break;
            }
            if (uiType < STRAT_MAX_ENCOUNTER)
                return m_auiEncounter[uiType];
            return 0;
        }

        ObjectGuid GetGuidData(uint32 uiData) const override
        {
            switch (uiData)
            {
                case DATA_BARON:
                    return m_uiBaronGUID;
                case DATA_YSIDA_TRIGGER:
                    return m_uiYsidaTriggerGUID;
                case NPC_DATHROHAN:
                    return m_uiDathrohanGUID;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        void SetData(uint32 uiType, uint32 uiData) override
        {
            switch (uiType)
            {
                case TYPE_BARON_RUN:
                {
                    switch (uiData)
                    {
                        case IN_PROGRESS:
                            if (m_auiEncounter[TYPE_BARON_RUN] == IN_PROGRESS || m_auiEncounter[TYPE_BARON_RUN] == FAIL)
                                break;
                            m_uiBaronRun_Timer = 45 * STRAT_MINUTE_MS;
                            m_phaseBaron = 0;
                            TC_LOG_DEBUG("scripts", "Instance Stratholme: Baron run in progress.");
                            if (Creature* pYsidaT = instance->GetCreature(m_uiYsidaTriggerGUID))
                                pYsidaT->SummonCreature(NPC_YSIDA, 4044.163f, -3334.2f, 115.0596f, 4.2f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 1h);
                            break;
                        case FAIL:
                            //may add code to remove aura from players, but in theory the time should be up already and removed.
                            break;
                        case DONE:
                            if (GameObject* cage = instance->GetGameObject(m_cageYsidaGUID))
                                cage->SetGoState(GO_STATE_ACTIVE);
                            if (Creature* pYsida = instance->GetCreature(m_uiYsidaGUID))
                            {
                                pYsida->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                                pYsida->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                                pYsida->SetWalk(true);
                                pYsida->GetMotionMaster()->MovePoint(1, 4041.2f, -3339.0f, 115.1f);
                            }
                            m_uiYsidaReward_Timer = 5000;
                            m_uiBaronRun_Timer = 0;
                            break;
                        default:
                            break;
                    }
                    m_auiEncounter[TYPE_BARON_RUN] = uiData;
                    break;
                }
                case TYPE_BARONESS:
                {
                    m_auiEncounter[TYPE_BARONESS] = uiData;
                    if (uiData == DONE)
                        UpdateGoState(m_uiZiggurat1GUID, GO_STATE_ACTIVE, false);
                    break;
                }
                case TYPE_NERUB:
                {
                    m_auiEncounter[TYPE_NERUB] = uiData;
                    if (uiData == DONE)
                        UpdateGoState(m_uiZiggurat2GUID, GO_STATE_ACTIVE, false);
                    break;
                }
                case TYPE_PALLID:
                {
                    m_auiEncounter[TYPE_PALLID] = uiData;
                    if (uiData == DONE)
                        UpdateGoState(m_uiZiggurat3GUID, GO_STATE_ACTIVE, false);
                    break;
                }
                case TYPE_RAMSTEIN:
                {
                    if (uiData == SPECIAL) // on mob Aggro OK
                    {
                        if (GameObject* pGob = instance->GetGameObject(m_uiPortGauntletGUID))
                            if (pGob->GetGoState() != GO_STATE_READY)
                                UpdateGoState(m_uiPortGauntletGUID, GO_STATE_READY, false);
                        m_uiSlaugtherAboMob_Timer = 20000;
                    }
                    if (uiData == IN_PROGRESS) // on mob Death // on ramstein aggro OK
                    {
                        if (m_summoningRammstein)
                            return;
                        m_summoningRammstein = true; // Prevent stack overflow (SummonRamstein can lead to call SetData(TYPE_RAMSTEIN)).
                        if (!m_uiRamsteinGUID.IsEmpty())
                        {
                            if (GameObject* pGob = instance->GetGameObject(m_uiPortGauntletGUID))
                                if (pGob->GetGoState() != GO_STATE_READY)
                                    UpdateGoState(m_uiPortGauntletGUID, GO_STATE_READY, false);
                            m_auiEncounter[TYPE_RAMSTEIN] = uiData;
                            m_summoningRammstein = false;
                            break;
                        }

                        uint32 uiCount = uint32(abomnationGUID.size());
                        for (ObjectGuid const& guid : abomnationGUID)
                        {
                            if (Creature* pAbom = instance->GetCreature(guid))
                            {
                                if (!pAbom->IsAlive())
                                    --uiCount;
                            }
                        }

                        if (!uiCount)
                        {
                            if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                                ClassicScriptText(RIVENDARE_YELL_RAMMSTEIN, pBaron);
                            UpdateGoState(m_uiZiggurat4GUID, GO_STATE_ACTIVE, false);
                            m_uiSlaugtherSquare_Timer = 5000;
                            SummonRamstein();
                        }
                        else
                            TC_LOG_DEBUG("scripts", "Instance Stratholme: {} Abomnation left to kill.", uiCount);
                        m_summoningRammstein = false;
                    }
                    if (uiData == DONE) // on ramstein death OK
                    {
                        DoUseDoorOrButton(m_uiSlaughterSquareGateGUID, 91 * IN_MILLISECONDS);

                        for (uint8 i = 0; i < 34; ++i)
                        {
                            if (TempSummon* pUndead = SummonInMap(NPC_MINDLESS_UNDEAD, 3929.6f + frand(0.0f, 3.0f), -3384.3f + frand(0.0f, 3.0f), 120.0f, 4.88f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 1800000))
                            {
                                Position home = pUndead->GetRandomPoint(Position(4012.0f, -3418.92f, 117.294f), 10.0f);
                                pUndead->SetHomePosition(home.GetPositionX(), home.GetPositionY(), home.GetPositionZ(), frand(0.0f, 6.0f));
                                pUndead->GetMotionMaster()->Clear();
                                // TODO(classic): VMaNGOS set MoveRandom(5 yd) as idle movement before the MovePoint below; after reaching the
                                //                point the undead fall back to their default (DB) movement here.
                                pUndead->SetWalk(false);
                                pUndead->GetMotionMaster()->MovePoint(1, 3941.29f, -3394.84f, 119.69f, false); // MOVE_FORCE_DESTINATION | MOVE_STRAIGHT_PATH
                            }
                        }
                        m_uiSlaugtherSquare_Timer = 60000;
                        TC_LOG_DEBUG("scripts", "Instance Stratholme: Slaugther event will continue in 60 sec.");
                    }
                    if (uiData == FAIL) // on mob Evade // on ramstein evade
                    {
                        if (GameObject* pGob = instance->GetGameObject(m_uiPortGauntletGUID))
                            if (pGob->GetGoState() != GO_STATE_ACTIVE)
                                UpdateGoState(m_uiPortGauntletGUID, GO_STATE_ACTIVE, false);
                        m_uiSlaugtherAboMob_Timer = 0;
                    }
                    m_auiEncounter[TYPE_RAMSTEIN] = uiData;
                    break;
                }
                case TYPE_BARON:
                {
                    if (uiData == IN_PROGRESS)
                    {
                        if (GameObject* pGob = instance->GetGameObject(m_uiZiggurat4GUID))
                            if (pGob->GetGoState() != GO_STATE_READY)
                                UpdateGoState(m_uiZiggurat4GUID, GO_STATE_READY, false);
                        UpdateGoState(m_uiZiggurat5GUID, GO_STATE_READY, false);
                        if (GameObject* pGob = instance->GetGameObject(m_uiPortGauntletGUID))
                            if (pGob->GetGoState() != GO_STATE_READY)
                                UpdateGoState(m_uiPortGauntletGUID, GO_STATE_READY, false);
                    }
                    if (uiData == DONE)
                    {
                        if (GetData(TYPE_BARON_RUN) == IN_PROGRESS)
                        {
                            static uint32 const baronSpells[] =
                            {
                                SPELL_BARON_ULTIMATUM_45MIN, SPELL_BARON_ULTIMATUM_10MIN, SPELL_BARON_ULTIMATUM_5MIN, SPELL_BARON_ULTIMATUM_1MIN
                            };

                            for (MapReference const& ref : instance->GetPlayers())
                            {
                                if (Player* pPlayer = ref.GetSource())
                                {
                                    for (uint32 spellId : baronSpells)
                                        if (pPlayer->HasAura(spellId))
                                            pPlayer->RemoveAurasDueToSpell(spellId);

                                    if (pPlayer->GetQuestStatus(QUEST_DEAD_MAN_PLEA) == QUEST_STATUS_INCOMPLETE)
                                        pPlayer->KilledMonsterCredit(NPC_YSIDA, m_uiYsidaGUID);
                                }
                            }
                            SetData(TYPE_BARON_RUN, DONE);
                        }
                        if (GameObject* pGob = instance->GetGameObject(m_uiZiggurat4GUID))
                            if (pGob->GetGoState() != GO_STATE_ACTIVE)
                                UpdateGoState(m_uiZiggurat4GUID, GO_STATE_ACTIVE, false);
                        UpdateGoState(m_uiZiggurat5GUID, GO_STATE_ACTIVE, false);
                        UpdateGoState(m_uiPortGauntletGUID, GO_STATE_ACTIVE, false);
                    }
                    if (uiData == FAIL)
                    {
                        if (GameObject* pGob = instance->GetGameObject(m_uiZiggurat4GUID))
                            if (pGob->GetGoState() != GO_STATE_ACTIVE)
                                UpdateGoState(m_uiZiggurat4GUID, GO_STATE_ACTIVE, false);
                        UpdateGoState(m_uiZiggurat5GUID, GO_STATE_ACTIVE, false);
                        if (GameObject* pGob = instance->GetGameObject(m_uiPortGauntletGUID))
                            if (pGob->GetGoState() != GO_STATE_ACTIVE)
                                UpdateGoState(m_uiPortGauntletGUID, GO_STATE_ACTIVE, false);
                    }

                    m_auiEncounter[TYPE_BARON] = uiData;
                    break;
                }
                case TYPE_CRISTAL_DIE:
                {
                    StartSlaugtherSquare();
                    break;
                }
                case TYPE_CRISTAL_ALL_DIE:
                case TYPE_EVENT_AURIUS:
                case TYPE_RAMSTEIN_EVENT:
                case TYPE_UNFORGIVEN:
                {
                    m_auiEncounter[uiType] = uiData;
                    break;
                }
                case TYPE_SH_AELMAR:
                    IsSilverHandDead[0] = (uiData) != 0;
                    break;
                case TYPE_SH_CATHELA:
                    IsSilverHandDead[1] = (uiData) != 0;
                    break;
                case TYPE_SH_GREGOR:
                    IsSilverHandDead[2] = (uiData) != 0;
                    break;
                case TYPE_SH_NEMAS:
                    IsSilverHandDead[3] = (uiData) != 0;
                    break;
                case TYPE_SH_VICAR:
                    IsSilverHandDead[4] = (uiData) != 0;
                    break;
                case TYPE_POSTMASTER:
                {
                    m_auiEncounter[uiType] = uiData;
                    if (uiData == IN_PROGRESS)
                    {
                        ++m_uiPostboxesUsed;

                        // After the second post box prepare to spawn the Post Master
                        if (m_uiPostboxesUsed == 2)
                            SetData(TYPE_POSTMASTER, SPECIAL);
                    }
                    // No need to save anything here, so return
                    return;
                }
                default:
                    break;
            }

            if (uiData == DONE)
                SaveEncounters();
        }

        /** Load / save system */
        void SaveEncounters()
        {
            uint64 packed = 0;
            for (uint32 i = 0; i < STRAT_MAX_ENCOUNTER; ++i)
                packed |= uint64(m_auiEncounter[i] & 0xF) << (i * 4);
            _savedEncounters = packed;
        }

        void AfterDataLoad() override
        {
            uint64 packed = _savedEncounters;
            for (uint32 i = 0; i < STRAT_MAX_ENCOUNTER; ++i)
            {
                m_auiEncounter[i] = uint32((packed >> (i * 4)) & 0xF);
                if (m_auiEncounter[i] == IN_PROGRESS)
                    m_auiEncounter[i] = NOT_STARTED;
            }

            // VMaNGOS summoned Ramstein directly in Load(); deferred to the first Update() so the map is ready.
            if (GetData(TYPE_RAMSTEIN_EVENT) == DONE && GetData(TYPE_RAMSTEIN) != DONE)
                _summonRamsteinAfterLoad = true;
        }

        /** Custom functions */
        void MoveAbomnationMob()
        {
            if (slaugtherAboGUID.empty())
                return;

            uint32 randAbo = urand(0, uint32(slaugtherAboGUID.size()) - 1);
            auto iter = slaugtherAboGUID.begin();
            for (uint32 i = 0; i < randAbo; ++i)
                ++iter;

            ObjectGuid aboGuid = *iter;
            if (Creature* pAbom = instance->GetCreature(aboGuid))
            {
                if (pAbom->IsAlive() && !pAbom->IsInCombat())
                    pAbom->GetMotionMaster()->MovePoint(0, 4037.194f, -3473.741943f, 121.738808f);
                m_uiSlaugtherAboMob_Timer = (pAbom->GetEntry() == NPC_ABOM_BILE) ? 45000 : urand(35000, 40000);
                slaugtherAboGUID.remove(aboGuid);
            }
            // TODO(classic): VMaNGOS behaviour kept - if the chosen abomination is not loaded the timer is not reset and a
            //                new one is picked on the next update.
        }

        void CastUltimatumOnPlayers(uint32 spellId)
        {
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* player = ref.GetSource())
                    if (!player->HasAura(spellId))
                        player->CastSpell(player, spellId, true);
        }

        void Update(uint32 uiDiff) override
        {
            if (_summonRamsteinAfterLoad)
            {
                _summonRamsteinAfterLoad = false;
                SummonRamstein();
            }

            if (GetData(TYPE_UNFORGIVEN) == NOT_STARTED)
            {
                for (MapReference const& ref : instance->GetPlayers())
                {
                    if (Player* pPlayer = ref.GetSource())
                    {
                        // VMaNGOS: IsTargetableBy(nullptr)
                        if (pPlayer->IsAlive() && !pPlayer->IsGameMaster() && pPlayer->GetExactDist(StratUnforgivenTriggerSpot) < 10.0f)
                        {
                            SetData(TYPE_UNFORGIVEN, DONE);
                            SummonInMap(NPC_THE_UNFORGIVEN, 3719.82f, -3426.25f, 131.844f, 3.3412f, TEMPSUMMON_DEAD_DESPAWN, HOUR * IN_MILLISECONDS);
                            SummonInMap(NPC_VENGEFUL_PHANTOM, 3715.85f, -3428.25f, 131.442f, 3.57792f, TEMPSUMMON_DEAD_DESPAWN, HOUR * IN_MILLISECONDS);
                            SummonInMap(NPC_VENGEFUL_PHANTOM, 3714.14f, -3423.75f, 131.673f, 3.61283f, TEMPSUMMON_DEAD_DESPAWN, HOUR * IN_MILLISECONDS);
                            SummonInMap(NPC_VENGEFUL_PHANTOM, 3721.93f, -3429.88f, 131.844f, 3.33358f, TEMPSUMMON_DEAD_DESPAWN, HOUR * IN_MILLISECONDS);
                            SummonInMap(NPC_VENGEFUL_PHANTOM, 3718.09f, -3432.27f, 131.306f, 4.3381f, TEMPSUMMON_DEAD_DESPAWN, HOUR * IN_MILLISECONDS);
                            break;
                        }
                    }
                }
            }

            // Loop over the two Gate traps, each one has up to three timers (trap reset, gate opening delay, critters spawning delay)
            for (uint8 i = 0; i < 2; i++)
            {
                // Check that the trap is not on cooldown, if so check if player/pet is in range
                if (m_uiGateTrapTimers[i][0])
                {
                    if (m_uiGateTrapTimers[i][0] <= uiDiff * 2) // VMaNGOS: timer -= diff; if (timer <= diff) reset
                    {
                        TC_LOG_DEBUG("scripts", "Instance Stratholme - Rat Trap reseted {}.", i);
                        m_uiGateTrapTimers[i][0] = 0;
                    }
                    else
                        m_uiGateTrapTimers[i][0] -= uiDiff;
                }
                else
                {
                    for (MapReference const& ref : instance->GetPlayers())
                    {
                        if (Player* pPlayer = ref.GetSource())
                        {
                            if (!pPlayer->IsGameMaster() && pPlayer->IsWithinDist2d(StratGateTrapPos[i].GetPositionX(), StratGateTrapPos[i].GetPositionY(), 5.5f))
                                DoGateTrap(i);

                            Pet* pet = pPlayer->GetPet();
                            if (!pPlayer->IsGameMaster() && pet && pet->IsWithinDist2d(StratGateTrapPos[i].GetPositionX(), StratGateTrapPos[i].GetPositionY(), 5.5f))
                                DoGateTrap(i);
                        }
                    }
                }
                // Timer to reopen the gates
                if (m_uiGateTrapTimers[i][1])
                {
                    if (m_uiGateTrapTimers[i][1] <= uiDiff)
                    {
                        DoUseDoorOrButton(m_ratTrapGateGUID[2 * i]);
                        DoUseDoorOrButton(m_ratTrapGateGUID[2 * i + 1]);
                        m_uiGateTrapTimers[i][1] = 0;
                    }
                    else
                        m_uiGateTrapTimers[i][1] -= uiDiff;
                }
                // Delay timer to spawn the plagued critters once the gate are closing
                if (m_uiGateTrapTimers[i][2])
                {
                    if (m_uiGateTrapTimers[i][2] <= uiDiff)
                    {
                        if (Player* pPlayer = GetPlayerInMap())
                            DoSpawnPlaguedCritters(i, pPlayer);
                        m_uiGateTrapTimers[i][2] = 0;
                    }
                    else
                        m_uiGateTrapTimers[i][2] -= uiDiff;
                }
            }

            if (m_uiBaronRun_Timer)
            {
                if (m_uiBaronRun_Timer <= 45 * STRAT_MINUTE_MS && m_phaseBaron == 0)
                {
                    m_phaseBaron++;

                    if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                        ClassicScriptText(RIVENDARE_YELL_45MIN, pBaron);

                    CastUltimatumOnPlayers(SPELL_BARON_ULTIMATUM_45MIN);
                }
                if (m_uiBaronRun_Timer <= 10 * STRAT_MINUTE_MS && m_phaseBaron == 1)
                {
                    m_phaseBaron++;

                    if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                        ClassicScriptText(RIVENDARE_YELL_10MIN, pBaron);

                    CastUltimatumOnPlayers(SPELL_BARON_ULTIMATUM_10MIN);
                }
                if (m_uiBaronRun_Timer <= 5 * STRAT_MINUTE_MS && m_phaseBaron == 2)
                {
                    m_phaseBaron++;

                    if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                        ClassicScriptText(RIVENDARE_YELL_5MIN, pBaron);

                    CastUltimatumOnPlayers(SPELL_BARON_ULTIMATUM_5MIN);
                }
                if (m_uiBaronRun_Timer <= 5 * STRAT_MINUTE_MS - 3000 && m_phaseBaron == 3)
                {
                    m_phaseBaron++;

                    if (Creature* pYsida = instance->GetCreature(m_uiYsidaGUID))
                        ClassicScriptText(YSIDA_YELL_5MIN, pYsida);
                }
                if (m_uiBaronRun_Timer <= 1 * STRAT_MINUTE_MS && m_phaseBaron == 4)
                {
                    m_phaseBaron++;

                    CastUltimatumOnPlayers(SPELL_BARON_ULTIMATUM_1MIN);
                }
                if (m_uiBaronRun_Timer <= uiDiff)
                {
                    m_phaseBaron = 6;
                    m_uiBaronRun_Timer = 0;

                    if (GetData(TYPE_BARON_RUN) != DONE)
                        SetData(TYPE_BARON_RUN, FAIL);

                    if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                        ClassicScriptText(RIVENDARE_YELL_FAILED, pBaron);

                    if (GameObject* cage = instance->GetGameObject(m_cageYsidaGUID))
                        cage->SetGoState(GO_STATE_ACTIVE);

                    if (Creature* pYsida = instance->GetCreature(m_uiYsidaGUID))
                    {
                        ClassicScriptText(YSIDA_YELL_FAILED, pYsida);
                        pYsida->CastSpell(pYsida, SPELL_STRAT_DEATHTOUCH, true); // deathtouch
                    }

                    TC_LOG_DEBUG("scripts", "Instance Stratholme: Baron run event reached end. Event has state {}.", GetData(TYPE_BARON_RUN));
                }
                else
                    m_uiBaronRun_Timer -= uiDiff;
            }

            if (m_uiSlaugtherAboMob_Timer)
            {
                if (m_uiSlaugtherAboMob_Timer <= uiDiff)
                {
                    if (!slaugtherAboGUID.empty())
                        MoveAbomnationMob();
                    else
                        m_uiSlaugtherAboMob_Timer = 0;
                }
                else
                    m_uiSlaugtherAboMob_Timer -= uiDiff;
            }

            if (m_uiSlaugtherSquare_Timer)
            {
                if (m_uiSlaugtherSquare_Timer <= uiDiff)
                {
                    if (GetData(TYPE_RAMSTEIN) == IN_PROGRESS)
                    {
                        UpdateGoState(m_uiZiggurat4GUID, GO_STATE_READY, false);
                        if (Creature* pRamstein = instance->GetCreature(m_uiRamsteinGUID))
                            ClassicScriptText(RAMMSTEIN_YELL_SPAWN, pRamstein);
                    }
                    else
                    {
                        if (Creature* pBaron = instance->GetCreature(m_uiBaronGUID))
                        {
                            for (uint8 i = 0; i < 5; ++i)
                            {
                                if (Creature* pBlackGuard = pBaron->SummonCreature(NPC_BLACK_GUARD, 4032.84f + float(urand(0, 2)), -3380.567f + float(urand(0, 2)), 119.739571f, 4.7614f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 1800000ms))
                                {
                                    pBlackGuard->GetMotionMaster()->MovePoint(0, 4033.34f, -3419.75f, 116.35f);
                                    pBlackGuard->SetHomePosition(4033.34f, -3419.75f, 116.35f, 4.80f);
                                    if (i == 0)
                                        ClassicScriptText(BLACKGUARD_YELL_SPAWN, pBlackGuard);
                                }
                            }
                            UpdateGoState(m_uiZiggurat4GUID, GO_STATE_ACTIVE, false);
                            UpdateGoState(m_uiZiggurat5GUID, GO_STATE_ACTIVE, false);
                            pBaron->RemoveUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));

                            TC_LOG_DEBUG("scripts", "Instance Stratholme: Black guard sentries spawned. Opening gates to baron.");
                        }
                    }
                    m_uiSlaugtherSquare_Timer = 0;
                }
                else
                    m_uiSlaugtherSquare_Timer -= uiDiff;
            }

            if (m_uiYsidaReward_Timer)
            {
                if (m_uiYsidaReward_Timer <= uiDiff)
                {
                    if (Creature* pYsida = instance->GetCreature(m_uiYsidaGUID))
                        ClassicScriptText(YSIDA_SAY_REWARD, pYsida);
                    // VMaNGOS: +150 Argent Dawn reputation (SPELL_YSIDA_FREED) disabled until the t0.5 quest line is in.
                    m_uiYsidaReward_Timer = 0;
                }
                else
                    m_uiYsidaReward_Timer -= uiDiff;
            }
        }

        void SummonRamstein()
        {
            if (TempSummon* pRamstein = SummonInMap(NPC_RAMSTEIN, 4032.35f, -3380.567f, 119.739571f, 4.7614f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 1800000))
            {
                pRamstein->GetMotionMaster()->MovePoint(0, 4033.009f, -3404.3293f, 115.3554f);
                pRamstein->SetHomePosition(4033.009f, -3404.3293f, 115.3554f, 4.788970f);
                SetData(TYPE_RAMSTEIN_EVENT, DONE);
                TC_LOG_DEBUG("scripts", "Instance Stratholme: Ramstein spawned.");
            }
        }

        void DoGateTrap(uint8 uiGate)
        {
            // Check if timer was not already set by another player/pet a few milliseconds before
            if (m_uiGateTrapTimers[uiGate][0])
                return;

            TC_LOG_DEBUG("scripts", "Instance Stratholme - Rat Trap activated {}.", uiGate);
            // close the gates
            DoUseDoorOrButton(m_ratTrapGateGUID[2 * uiGate]);
            DoUseDoorOrButton(m_ratTrapGateGUID[2 * uiGate + 1]);

            // set timer to reset the trap
            m_uiGateTrapTimers[uiGate][0] = 30 * STRAT_MINUTE_MS;
            // set timer to reopen gates
            m_uiGateTrapTimers[uiGate][1] = 20 * IN_MILLISECONDS;
            // set timer to spawn the plagued critters
            m_uiGateTrapTimers[uiGate][2] = 2 * IN_MILLISECONDS;
        }

        void DoSpawnPlaguedCritters(uint8 uiGate, Player* pPlayer)
        {
            if (!pPlayer)
                return;

            uint32 uiEntry = StratPlaguedCritters[urand(0, 2)];
            for (uint8 i = 0; i < 30; ++i)
            {
                Position pos = pPlayer->GetRandomPoint(StratGateTrapPos[uiGate], 8.0f);
                pPlayer->SummonCreature(uiEntry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_DEAD_DESPAWN, 0s);
            }
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_stratholme_InstanceScript(map);
    }
};

void AddSC_classic_instance_stratholme()
{
    new classic_instance_stratholme();
}
