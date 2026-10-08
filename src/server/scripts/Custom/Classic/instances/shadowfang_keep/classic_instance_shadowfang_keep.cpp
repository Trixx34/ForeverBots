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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/silverpine_forest/shadowfang_keep/instance_shadowfang_keep.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: instance_shadowfang_keep (map 33), spell_haunting_spirits (7057)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Random.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Unit.h"
#include "classic_shadowfang_keep.h"
#include <array>
#include <list>
#include <memory>

namespace
{
constexpr uint32 CSFK_FACTION_FRIENDLY          = 35;
constexpr uint32 CSFK_FACTION_MONSTER           = 17;
constexpr uint32 CSFK_RESPAWN_BARON_PATROL      = 7201;
constexpr uint32 CSFK_RESPAWN_SPRINGVALE_PATROL = 7202;

constexpr std::array<char const*, CSFK_MAX_ENCOUNTER> ClassicSfkEncounterNames =
{
    "FreeNpc", "Rethilgore", "Fenrus", "Nandos", "Intro", "Voidwalker"
};
}

class classic_instance_shadowfang_keep : public InstanceMapScript
{
public:
    classic_instance_shadowfang_keep() : InstanceMapScript(ClassicShadowfangKeepScriptName, 33) { }

    struct classic_instance_shadowfang_keep_InstanceScript : public InstanceScript
    {
        classic_instance_shadowfang_keep_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders("CSFK");

            // VMaNGOS m_auiEncounter[] + Save()/Load() string -> TC persistent instance values
            for (uint32 i = 0; i < CSFK_MAX_ENCOUNTER; ++i)
                m_auiEncounter[i] = std::make_unique<PersistentInstanceScriptValue<uint32>>(*this, ClassicSfkEncounterNames[i], uint32(NOT_STARTED));

            m_uiVoidWalkerCount = 0;
            showSilverlainePatrol = false;
            showSpringvalePatrol = false;
            m_uiSpawnPatrolOnBaronDeath = 6000;
            m_uiSpawnPatrolOnCmdDeath = 6000;
            _fenrusInCombat = false;
        }

        std::array<std::unique_ptr<PersistentInstanceScriptValue<uint32>>, CSFK_MAX_ENCOUNTER> m_auiEncounter;

        uint32 Enc(uint32 index) const { return *m_auiEncounter[index]; }
        void SetEnc(uint32 index, uint32 value) { *m_auiEncounter[index] = value; }

        ObjectGuid m_uiAshGUID;
        ObjectGuid m_uiAdaGUID;

        ObjectGuid m_uiDoorCourtyardGUID;
        ObjectGuid m_uiDoorSorcererGUID;
        ObjectGuid m_uiDoorArugalGUID;
        ObjectGuid m_uiBaronSilverlaineGUID;
        ObjectGuid m_uiCmdSpringvaleGUID;

        ObjectGuid m_uiFenrusGUID;
        ObjectGuid m_uiVincentGUID;
        ObjectGuid m_uiNandosGUID;

        uint32 m_uiVoidWalkerCount;

        uint32 m_uiSpawnPatrolOnBaronDeath;
        uint32 m_uiSpawnPatrolOnCmdDeath;

        bool showSilverlainePatrol;
        bool showSpringvalePatrol;

        bool _fenrusInCombat;

        // VMaNGOS Load(): encounters saved as IN_PROGRESS are reset
        void AfterDataLoad() override
        {
            for (auto& value : m_auiEncounter)
                if (uint32(*value) == IN_PROGRESS)
                    value->LoadValue(uint32(NOT_STARTED));
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case CSFK_NPC_ASH:
                    m_uiAshGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_ADA:
                    m_uiAdaGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_FENRUS:
                    m_uiFenrusGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_ARUGAL:
                    // if Arugal has done the intro, make him invisible!
                    if (Enc(4) == DONE)
                        creature->SetVisible(false);
                    break;
                case CSFK_NPC_VINCENT:
                    m_uiVincentGUID = creature->GetGUID();
                    // if Arugal has done the intro, make Vincent dead!
                    if (Enc(4) == DONE)
                        creature->SetStandState(UNIT_STAND_STATE_DEAD);
                    break;
                case CSFK_NPC_BARON_SILVERLAINE:
                    m_uiBaronSilverlaineGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_NANDOS:
                    m_uiNandosGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_CMD_SPRINGVALE:
                    m_uiCmdSpringvaleGUID = creature->GetGUID();
                    break;
                case CSFK_NPC_WOLF_GUARD:
                    creature->SetVisible(false);
                    creature->SetFaction(CSFK_FACTION_FRIENDLY);
                    break;
                default:
                    break;
            }
        }

        // VMaNGOS OnCreatureDeath
        void OnUnitDeath(Unit* unit) override
        {
            Creature* creature = unit->ToCreature();
            if (!creature)
                return;

            switch (creature->GetEntry())
            {
                case CSFK_NPC_BARON_SILVERLAINE:
                    showSilverlainePatrol = true;
                    break;
                case CSFK_NPC_CMD_SPRINGVALE:
                    showSpringvalePatrol = true;
                    break;
                default:
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case CSFK_GO_COURTYARD_DOOR:
                    m_uiDoorCourtyardGUID = go->GetGUID();
                    if (Enc(0) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                // for this we ignore voidwalkers, because if the server restarts
                // they won't be there, but Fenrus is dead so the door can't be opened!
                case CSFK_GO_SORCERER_DOOR:
                    m_uiDoorSorcererGUID = go->GetGUID();
                    if (Enc(2) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                case CSFK_GO_ARUGAL_DOOR:
                    m_uiDoorArugalGUID = go->GetGUID();
                    if (Enc(3) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                default:
                    break;
            }
        }

        void ShowPatrol(Creature* source, uint32 respawnDelay, bool& showFlag)
        {
            std::list<Creature*> escortList;
            source->GetCreatureListWithEntryInGrid(escortList, CSFK_NPC_WOLF_GUARD, 400.0f);
            for (Creature* creature : escortList)
            {
                if (creature->GetRespawnDelay() == respawnDelay && creature->GetEntry() == CSFK_NPC_WOLF_GUARD)
                {
                    creature->SetVisible(true);
                    creature->SetFaction(CSFK_FACTION_MONSTER);
                    showFlag = false; // do it only once
                }
            }
        }

        void Update(uint32 diff) override
        {
            // VMaNGOS OnCreatureEnterCombat (no TC instance hook): play Fenrus howl when he enters combat
            if (Creature* fenrus = instance->GetCreature(m_uiFenrusGUID))
            {
                bool inCombat = fenrus->IsAlive() && fenrus->IsInCombat();
                if (inCombat && !_fenrusInCombat)
                    fenrus->PlayDirectSound(CSFK_SOUND_FENRUS_AGGRO);
                _fenrusInCombat = inCombat;
            }

            if (showSilverlainePatrol)
            {
                if (Creature* baron = instance->GetCreature(m_uiBaronSilverlaineGUID))
                {
                    if (m_uiSpawnPatrolOnBaronDeath <= diff)
                        ShowPatrol(baron, CSFK_RESPAWN_BARON_PATROL, showSilverlainePatrol);
                    else
                        m_uiSpawnPatrolOnBaronDeath -= diff;
                }
            }

            if (showSpringvalePatrol)
            {
                if (Creature* cmd = instance->GetCreature(m_uiCmdSpringvaleGUID))
                {
                    if (m_uiSpawnPatrolOnCmdDeath <= diff)
                        ShowPatrol(cmd, CSFK_RESPAWN_SPRINGVALE_PATROL, showSpringvalePatrol);
                    else
                        m_uiSpawnPatrolOnCmdDeath -= diff;
                }
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case CSFK_TYPE_FREE_NPC:
                    if (data == DONE)
                        DoUseDoorOrButton(m_uiDoorCourtyardGUID);
                    SetEnc(0, data);
                    break;
                case CSFK_TYPE_RETHILGORE:
                    SetEnc(1, data);
                    break;
                case CSFK_TYPE_FENRUS:
                    SetEnc(2, data);
                    break;
                case CSFK_TYPE_NANDOS:
                    if (data == DONE)
                        DoUseDoorOrButton(m_uiDoorArugalGUID);
                    SetEnc(3, data);
                    break;
                case CSFK_TYPE_INTRO:
                    SetEnc(4, data);
                    break;
                case CSFK_TYPE_VOIDWALKER:
                    if (data == DONE)
                    {
                        SetEnc(5, Enc(5) + 1);
                        if (Enc(5) > 3)
                            DoUseDoorOrButton(m_uiDoorSorcererGUID);
                    }
                    break;
                default:
                    break;
            }
            // VMaNGOS saved on DONE; persistent values are saved by TC whenever they change
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case CSFK_TYPE_FREE_NPC:
                    return Enc(0);
                case CSFK_TYPE_RETHILGORE:
                    return Enc(1);
                case CSFK_TYPE_FENRUS:
                    return Enc(2);
                case CSFK_TYPE_NANDOS:
                    return Enc(3);
                case CSFK_TYPE_INTRO:
                    return Enc(4);
                default:
                    break;
            }
            return 0;
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_shadowfang_keep_InstanceScript(map);
    }
};

// 7057 - Haunting Spirits
class classic_spell_haunting_spirits : public AuraScript
{
    enum
    {
        SPELL_SUMMON_HAUNTING_SPIRIT = 7067
    };

    // VMaNGOS OnBeforeApply: periodic timer 5 seconds on effect 0
    void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
    {
        isPeriodic = true;
        amplitude = 5 * IN_MILLISECONDS;
    }

    void HandleDummyTick(AuraEffect const* /*aurEff*/)
    {
        if (roll_chance(5)) // 5% chance every tick
            GetTarget()->CastSpell(GetTarget(), SPELL_SUMMON_HAUNTING_SPIRIT, true); // Summon Haunting Spirit
    }

    void Register() override
    {
        DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(classic_spell_haunting_spirits::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectPeriodic += AuraEffectPeriodicFn(classic_spell_haunting_spirits::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

void AddSC_classic_instance_shadowfang_keep()
{
    new classic_instance_shadowfang_keep();
    RegisterSpellScript(classic_spell_haunting_spirits);
}
