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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/instance_zulgurub.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: instance_zulgurub, event_summon_gahzranka (event 9104), + spell 24693 (Hakkar Power Down, VMaNGOS core script effect)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DB2Stores.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "classic_zulgurub.h"
#include <list>
#include <vector>

namespace
{
// DungeonEncounter.db2 ids of the vanilla Zul'Gurub (map 309); only registered when present in the client data
struct ClassicZgEncounterId
{
    uint32 BossId;
    uint32 DungeonEncounterId;
};

ClassicZgEncounterId const ClassicZgEncounterIds[] =
{
    { CLASSIC_ZG_BOSS_VENOXIS,   784 },
    { CLASSIC_ZG_BOSS_JEKLIK,    785 },
    { CLASSIC_ZG_BOSS_MARLI,     786 },
    { CLASSIC_ZG_BOSS_THEKAL,    789 },
    { CLASSIC_ZG_BOSS_GAHZRANKA, 790 },
    { CLASSIC_ZG_BOSS_ARLOKK,    791 },
    { CLASSIC_ZG_BOSS_JINDO,     792 },
    { CLASSIC_ZG_BOSS_HAKKAR,    793 }
    // TODO(classic): 787 (Bloodlord Mandokir) and 788 (Edge of Madness) have no VMaNGOS boss state
};

// VMaNGOS TYPE_* -> TC boss id (-1: not a boss state)
int32 ClassicZgTypeToBoss(uint32 type)
{
    switch (type)
    {
        case CLASSIC_ZG_TYPE_ARLOKK:    return CLASSIC_ZG_BOSS_ARLOKK;
        case CLASSIC_ZG_TYPE_JEKLIK:    return CLASSIC_ZG_BOSS_JEKLIK;
        case CLASSIC_ZG_TYPE_VENOXIS:   return CLASSIC_ZG_BOSS_VENOXIS;
        case CLASSIC_ZG_TYPE_MARLI:     return CLASSIC_ZG_BOSS_MARLI;
        case CLASSIC_ZG_TYPE_THEKAL:    return CLASSIC_ZG_BOSS_THEKAL;
        case CLASSIC_ZG_TYPE_HAKKAR:    return CLASSIC_ZG_BOSS_HAKKAR;
        case CLASSIC_ZG_TYPE_JINDO:     return CLASSIC_ZG_BOSS_JINDO;
        case CLASSIC_ZG_TYPE_GAHZRANKA: return CLASSIC_ZG_BOSS_GAHZRANKA;
        default:                        return -1;
    }
}

constexpr uint32 CLASSIC_ZG_SPELL_GREEN_GHOST_VISUAL = 25039;   // unused: SpawnRandomBoss() is deactivated in VMaNGOS
constexpr uint32 CLASSIC_ZG_AREA_MARLI = 3379;
constexpr uint32 CLASSIC_ZG_SPELL_GAHZRANKA_SPLASH = 12816;
}

class classic_instance_zulgurub : public InstanceMapScript
{
public:
    classic_instance_zulgurub() : InstanceMapScript(ClassicZulGurubScriptName, CLASSIC_ZG_MAP_ID) { }

    struct classic_instance_zulgurub_InstanceScript : public InstanceScript
    {
        explicit classic_instance_zulgurub_InstanceScript(InstanceMap* map) : InstanceScript(map),
            m_ohgan(*this, "Ohgan", NOT_STARTED), m_randomBoss(*this, "RandomBoss", 0)
        {
            SetHeaders(ClassicZulGurubDataHeader);
            SetBossNumber(CLASSIC_ZG_MAX_BOSSES);

            std::vector<DungeonEncounterData> encounters;
            for (ClassicZgEncounterId const& enc : ClassicZgEncounterIds)
                if (sDungeonEncounterStore.LookupEntry(enc.DungeonEncounterId))
                    encounters.push_back({ enc.BossId, {{ enc.DungeonEncounterId }} });
            LoadDungeonEncounterData(encounters);

            m_thekalDeathTime = 0;
            m_thekalRezTime = 0;
            m_randomBossSpawned = false;
            m_gahzrankaSpawnId = CLASSIC_ZG_GAHZRANKA_SPAWN_ID;
        }

        // VMaNGOS m_auiEncounter[]: boss slots are TC boss states, the rest are kept here
        PersistentInstanceScriptValue<uint32> m_ohgan;          // [7]
        PersistentInstanceScriptValue<uint32> m_randomBoss;     // [9]
        uint32 m_thekalDeathTime;                               // [5] (not persisted: transient timer)
        uint32 m_thekalRezTime;                                 // [6] (not persisted: transient timer)
        bool m_randomBossSpawned;

        // Storing Lorkhan, Zath and Thekal because we need to cast on them later. Jindo is needed for heal function too.
        ObjectGuid m_uiLorKhanGUID;
        ObjectGuid m_uiZathGUID;
        ObjectGuid m_uiThekalGUID;
        ObjectGuid m_uiJindoGUID;
        ObjectGuid m_uiHakkarGUID;
        ObjectGuid m_uiGahzrankaGUID;
        ObjectGuid::LowType m_gahzrankaSpawnId;

        ObjectGuid m_uiMarliGUID;
        std::list<ObjectGuid> m_lMarliTrashGUIDList;

        // VMaNGOS Create(): new instance -> pick the Edge of Madness boss
        void Create() override
        {
            InstanceScript::Create();
            m_randomBoss = GenerateRandomBoss();
            if (!m_randomBossSpawned)
                SpawnRandomBoss();
        }

        void AfterDataLoad() override
        {
            // VMaNGOS Load(): IN_PROGRESS -> NOT_STARTED (boss states are handled by TC)
            if (uint32(m_ohgan) == IN_PROGRESS)
                m_ohgan = NOT_STARTED;
            if (!m_randomBossSpawned)
                SpawnRandomBoss();
        }

        // each time High Priest dies lower Hakkar's HP
        void UpdateHakkarPowerStacks()
        {
            Creature* pHakkar = instance->GetCreature(m_uiHakkarGUID);
            if (!pHakkar || !pHakkar->IsAlive())
                return;

            uint32 neededStacks = 0;
            for (uint32 i = 0; i < CLASSIC_ZG_HIGH_PRIEST_COUNT; ++i)
                if (GetBossState(i) != DONE)
                    ++neededStacks;

            uint32 currentStacks = 0;
            if (Aura* pAura = pHakkar->GetAura(CLASSIC_ZG_SPELL_HAKKAR_POWER))
                currentStacks = pAura->GetStackAmount();

            if (neededStacks == currentStacks)
                return;

            if (neededStacks == 0)
            {
                pHakkar->RemoveAurasDueToSpell(CLASSIC_ZG_SPELL_HAKKAR_POWER);
                return;
            }

            if (currentStacks == 0)
            {
                for (uint32 i = 0; i < neededStacks; ++i)
                    pHakkar->CastSpell(pHakkar, CLASSIC_ZG_SPELL_HAKKAR_POWER, true);
            }
            else if (Aura* pAura = pHakkar->GetAura(CLASSIC_ZG_SPELL_HAKKAR_POWER))
                pAura->SetStackAmount(uint8(neededStacks));
        }

        bool IsEncounterInProgress() const override
        {
            for (uint32 i = 0; i < CLASSIC_ZG_MAX_BOSSES; ++i)
                if (GetBossState(i) == IN_PROGRESS || GetBossState(i) == SPECIAL)
                    return true;
            if (uint32(m_ohgan) == IN_PROGRESS || uint32(m_ohgan) == SPECIAL)
                return true;
            return false;
        }

        // VMaNGOS HandleLoadCreature(): a creature of a finished encounter is removed when it (re)spawns
        void HandleLoadCreature(uint32 dataType, ObjectGuid* storeGuid, Creature* pCrea)
        {
            if (GetData(dataType) == DONE)
            {
                if (storeGuid)
                    storeGuid->Clear();
                pCrea->DespawnOrUnsummon(0s, Seconds(7 * DAY));
                return;
            }
            if (storeGuid)
                *storeGuid = pCrea->GetGUID();
        }

        void OnCreatureCreate(Creature* pCreature) override
        {
            InstanceScript::OnCreatureCreate(pCreature);

            switch (pCreature->GetEntry())
            {
                case CLASSIC_ZG_NPC_LORKHAN:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_THEKAL, &m_uiLorKhanGUID, pCreature);
                    break;
                case CLASSIC_ZG_NPC_ZATH:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_THEKAL, &m_uiZathGUID, pCreature);
                    break;
                case CLASSIC_ZG_NPC_THEKAL:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_THEKAL, &m_uiThekalGUID, pCreature);
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_NPC_JINDO:
                    m_uiJindoGUID = pCreature->GetGUID();
                    break;
                case CLASSIC_ZG_NPC_HAKKAR:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_HAKKAR, &m_uiHakkarGUID, pCreature);
                    // Hakkar's own stacks are applied from his AI (JustAppeared), once he is in the world
                    break;
                case CLASSIC_ZG_NPC_VENOXIS:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_VENOXIS, nullptr, pCreature);
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_NPC_ARLOKK:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_ARLOKK, nullptr, pCreature);
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_NPC_MARLI:
                    HandleLoadCreature(CLASSIC_ZG_TYPE_MARLI, nullptr, pCreature);
                    m_uiMarliGUID = pCreature->GetGUID();
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_NPC_JEKLIK:
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_NPC_RAZZASHI_SKITTERER:
                case CLASSIC_ZG_NPC_RAZZASHI_VENOMBROOD:
                case CLASSIC_ZG_NPC_HAKARI_SHADOWCASTER:
                case CLASSIC_ZG_NPC_RAZZASHI_BROODWIDOW:
                    m_lMarliTrashGUIDList.push_back(pCreature->GetGUID());
                    break;
                case CLASSIC_ZG_NPC_GAHZRANKA:
                    m_uiGahzrankaGUID = pCreature->GetGUID();
                    if (pCreature->GetSpawnId())
                        m_gahzrankaSpawnId = pCreature->GetSpawnId();
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS OnCreatureDeath()
        void OnUnitDeath(Unit* unit) override
        {
            Creature* pCreature = unit->ToCreature();
            if (!pCreature)
                return;

            if (pCreature->GetEntry() >= CLASSIC_ZG_BOSS_ENTRY_GRILEK && pCreature->GetEntry() <= CLASSIC_ZG_BOSS_ENTRY_WUSHOOLAY)
                SetData(CLASSIC_ZG_TYPE_RANDOM_BOSS, DONE);

            if (pCreature->GetEntry() == CLASSIC_ZG_NPC_NIGHTMARE_ILLUSION)
                pCreature->DespawnOrUnsummon(3s, Seconds(345600000));    // VMaNGOS SetRespawnTime(345600000)
        }

        void SetData(uint32 uiType, uint32 uiData) override
        {
            int32 bossId = ClassicZgTypeToBoss(uiType);
            if (bossId >= 0)
            {
                // TC blocks DONE -> other state transitions
                if (GetBossState(uint32(bossId)) == DONE && uiData != DONE)
                    return;

                SetBossState(uint32(bossId), EncounterState(uiData));

                if (uiType == CLASSIC_ZG_TYPE_MARLI && uiData == IN_PROGRESS)
                {
                    Creature* Marli = instance->GetCreature(m_uiMarliGUID);
                    Unit* pVictim = Marli ? Marli->GetVictim() : nullptr;
                    if (pVictim)
                    {
                        for (ObjectGuid const& guid : m_lMarliTrashGUIDList)
                        {
                            if (Creature* MarliTrash = instance->GetCreature(guid))
                                if (MarliTrash->IsAlive() && !MarliTrash->IsInCombat() && MarliTrash->AI())
                                    if (MarliTrash->GetMapId() == CLASSIC_ZG_MAP_ID && MarliTrash->GetZoneId() == CLASSIC_ZG_ZONE_ID && MarliTrash->GetAreaId() == CLASSIC_ZG_AREA_MARLI)
                                        MarliTrash->AI()->AttackStart(pVictim);
                        }
                    }
                }
                return;
            }

            switch (uiType)
            {
                case CLASSIC_ZG_TYPE_HAKKAR_POWER:
                    UpdateHakkarPowerStacks();
                    break;
                case CLASSIC_ZG_TYPE_THEKAL_DEATH_TIME:
                    m_thekalDeathTime = uiData == SPECIAL ? uint32(GameTime::GetGameTime()) : uiData;
                    break;
                case CLASSIC_ZG_TYPE_THEKAL_REZ_TIME:
                    m_thekalRezTime = uiData == SPECIAL ? uint32(GameTime::GetGameTime()) : uiData;
                    break;
                case CLASSIC_ZG_TYPE_OHGAN:
                    m_ohgan = uiData;
                    break;
                case CLASSIC_ZG_TYPE_RANDOM_BOSS:
                    if (uiData == 0)
                    {
                        // m_auiEncounter[9] = GenerateRandomBoss();
                        if (sGameEventMgr->IsActiveEvent(29))
                            m_randomBoss = uint32(CLASSIC_ZG_BOSS_ENTRY_GRILEK);
                        else if (sGameEventMgr->IsActiveEvent(30))
                            m_randomBoss = uint32(CLASSIC_ZG_BOSS_ENTRY_HAZZARAH);
                        else if (sGameEventMgr->IsActiveEvent(31))
                            m_randomBoss = uint32(CLASSIC_ZG_BOSS_ENTRY_RENATAKI);
                        else if (sGameEventMgr->IsActiveEvent(32))
                            m_randomBoss = uint32(CLASSIC_ZG_BOSS_ENTRY_WUSHOOLAY);
                        // TODO(classic): game_event ids 29-32 are the VMaNGOS Edge of Madness events; verify they exist in the TC world DB
                    }
                    else
                        m_randomBoss = uiData;
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 uiType) const override
        {
            int32 bossId = ClassicZgTypeToBoss(uiType);
            if (bossId >= 0)
                return GetBossState(uint32(bossId));

            switch (uiType)
            {
                case CLASSIC_ZG_TYPE_THEKAL_DEATH_TIME:
                    return m_thekalDeathTime;
                case CLASSIC_ZG_TYPE_THEKAL_REZ_TIME:
                    return m_thekalRezTime;
                case CLASSIC_ZG_TYPE_OHGAN:
                    return m_ohgan;
                case CLASSIC_ZG_TYPE_RANDOM_BOSS:
                {
                    uint32 randomBoss = m_randomBoss;
                    if (randomBoss >= 15080 && randomBoss <= 15085)
                        return randomBoss;
                    return 0;
                }
                case CLASSIC_ZG_DATA_THEKAL_COND_CAN_REZ:
                    return Thekal_GetUnitThatCanRez() != nullptr ? 1 : 0;
                case CLASSIC_ZG_DATA_THEKAL_COND_NEEDS_REZ:
                {
                    uint32 now = uint32(GameTime::GetGameTime());
                    return (Thekal_GetUnitThatNeedsRez() != nullptr && Thekal_GetUnitCastingRez() == nullptr &&
                        (m_thekalDeathTime + 10 < now) && (m_thekalRezTime + 10 < now)) ? 1 : 0;
                }
                default:
                    break;
            }
            return 0;
        }

        ObjectGuid GetGuidData(uint32 uiData) const override
        {
            switch (uiData)
            {
                case CLASSIC_ZG_DATA_LORKHAN:
                    return m_uiLorKhanGUID;
                case CLASSIC_ZG_DATA_ZATH:
                    return m_uiZathGUID;
                case CLASSIC_ZG_DATA_THEKAL:
                    return m_uiThekalGUID;
                case CLASSIC_ZG_DATA_JINDO:
                    return m_uiJindoGUID;
                case CLASSIC_ZG_DATA_HAKKAR:
                    return m_uiHakkarGUID;
                case CLASSIC_ZG_DATA_GAHZRANKA:
                    return m_uiGahzrankaGUID;
                case CLASSIC_ZG_DATA_THEKAL_NEED_REZ:
                    if (Unit* pTarget = Thekal_GetUnitThatNeedsRez())
                        return pTarget->GetGUID();
                    return ObjectGuid::Empty;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        uint32 GenerateRandomBoss() const
        {
            // VMaNGOS sWorld.GetGameDay()
            uint32 dayCount = uint32(GameTime::GetGameTime() / DAY);
            uint32 weekmod = ((dayCount - (dayCount % 14)) / 14) % 3;
            uint32 bossId = CLASSIC_ZG_BOSS_ENTRY_GRILEK + weekmod;
            return bossId;
        }

        void SpawnRandomBoss()
        {
            m_randomBossSpawned = true;
            // function deactivated in VMaNGOS (would summon m_auiEncounter[9] at -11901.45, -1906.337, 65.37 with visual 25039)
            (void)CLASSIC_ZG_SPELL_GREEN_GHOST_VISUAL;
        }

        Unit* Thekal_GetUnitThatCanRez() const
        {
            if (Creature* pLorKhan = instance->GetCreature(m_uiLorKhanGUID))
                if (pLorKhan->IsAlive() && pLorKhan->GetStandState() != UNIT_STAND_STATE_DEAD)
                    return pLorKhan;
            if (Creature* pZath = instance->GetCreature(m_uiZathGUID))
                if (pZath->IsAlive() && pZath->GetStandState() != UNIT_STAND_STATE_DEAD)
                    return pZath;
            if (Creature* pThekal = instance->GetCreature(m_uiThekalGUID))
                if (pThekal->IsAlive() && pThekal->GetStandState() != UNIT_STAND_STATE_DEAD)
                    return pThekal;
            return nullptr;
        }

        Unit* Thekal_GetUnitThatNeedsRez() const
        {
            if (Creature* pLorKhan = instance->GetCreature(m_uiLorKhanGUID))
                if (pLorKhan->IsAlive() && pLorKhan->GetStandState() == UNIT_STAND_STATE_DEAD)
                    return pLorKhan;
            if (Creature* pZath = instance->GetCreature(m_uiZathGUID))
                if (pZath->IsAlive() && pZath->GetStandState() == UNIT_STAND_STATE_DEAD)
                    return pZath;
            if (Creature* pThekal = instance->GetCreature(m_uiThekalGUID))
                if (pThekal->IsAlive() && pThekal->GetStandState() == UNIT_STAND_STATE_DEAD)
                    return pThekal;
            return nullptr;
        }

        static bool IsCastingThekalRez(Unit const* unit)
        {
            if (Spell* pSpell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                if (pSpell->GetSpellInfo()->Id == CLASSIC_ZG_SPELL_THEKAL_RESURRECTION)
                    return true;
            return false;
        }

        Unit* Thekal_GetUnitCastingRez() const
        {
            if (Creature* pLorKhan = instance->GetCreature(m_uiLorKhanGUID))
                if (IsCastingThekalRez(pLorKhan))
                    return pLorKhan;
            if (Creature* pZath = instance->GetCreature(m_uiZathGUID))
                if (IsCastingThekalRez(pZath))
                    return pZath;
            if (Creature* pThekal = instance->GetCreature(m_uiThekalGUID))
                if (IsCastingThekalRez(pThekal))
                    return pThekal;
            return nullptr;
        }

        // Used by event_summon_gahzranka: TC removes despawned creatures from the map, so respawn by spawn id
        bool RespawnGahzranka()
        {
            if (Creature* pCreature = instance->GetCreature(m_uiGahzrankaGUID))
            {
                pCreature->Respawn();
                return true;
            }

            if (!m_gahzrankaSpawnId)
                return false;

            instance->Respawn(SPAWN_TYPE_CREATURE, m_gahzrankaSpawnId);
            return true;
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_zulgurub_InstanceScript(map);
    }
};

/*######
## event_summon_gahzranka (event 9104, Mudskunk Lure)
######*/

class classic_event_summon_gahzranka : public EventScript
{
public:
    classic_event_summon_gahzranka() : EventScript("classic_event_summon_gahzranka") { }

    // TC: object = VMaNGOS "target", invoker = VMaNGOS "source"
    void OnTrigger(WorldObject* /*object*/, WorldObject* invoker, uint32 /*eventId*/) override
    {
        // No target or source, block event
        if (!invoker)
            return;

        Player* pPlayer = invoker->ToPlayer();
        if (!pPlayer)
            return;

        InstanceScript* instanceScript = pPlayer->GetInstanceScript();
        if (!instanceScript)
            return;

        auto* m_pInstance = dynamic_cast<classic_instance_zulgurub::classic_instance_zulgurub_InstanceScript*>(instanceScript);
        if (!m_pInstance)
            return;

        // return if already summoned
        if (m_pInstance->GetData(CLASSIC_ZG_TYPE_GAHZRANKA) != NOT_STARTED)
            return;

        pPlayer->CastSpell(pPlayer, CLASSIC_ZG_SPELL_GAHZRANKA_SPLASH, true);

        // VMaNGOS: SetData(IN_PROGRESS) + Respawn() of the stored creature (its AI checks the state on spawn)
        m_pInstance->SetData(CLASSIC_ZG_TYPE_GAHZRANKA, IN_PROGRESS);
        if (!m_pInstance->RespawnGahzranka())
            m_pInstance->SetData(CLASSIC_ZG_TYPE_GAHZRANKA, NOT_STARTED);
    }
};

/*######
## 24693 - Hakkar Power Down
## VMaNGOS core script effect: instance SetData(TYPE_HAKKAR_POWER). Cast (triggered) by the High Priests on death.
######*/

class classic_spell_zg_hakkar_power_down : public SpellScript
{
    void HandleCast()
    {
        // OnCast instead of an effect hook: the caster is dead and targets itself (TARGET_UNIT_CASTER)
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (InstanceScript* instanceScript = caster->GetInstanceScript())
            instanceScript->SetData(CLASSIC_ZG_TYPE_HAKKAR_POWER, 0);
    }

    void Register() override
    {
        OnCast += SpellCastFn(classic_spell_zg_hakkar_power_down::HandleCast);
    }
};

void AddSC_classic_instance_zulgurub()
{
    new classic_instance_zulgurub();
    new classic_event_summon_gahzranka();
    RegisterSpellScript(classic_spell_zg_hakkar_power_down);
}
