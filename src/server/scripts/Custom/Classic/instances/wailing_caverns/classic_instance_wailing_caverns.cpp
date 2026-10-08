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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/wailing_caverns/instance_wailing_caverns.cpp
// (ScriptDev2 lineage, GPL-2)
// Ported: instance_wailing_caverns (map 43), at_dmf_chest_wc

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Loot.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "QuestDef.h"
#include "classic_script_text.h"
#include "classic_wailing_caverns.h"
#include <array>
#include <memory>
#include <vector>

namespace
{
constexpr uint32 CWC_FACTION_FRIENDLY = 35;

constexpr std::array<char const*, CWC_MAX_ENCOUNTER> ClassicWcEncounterNames =
{
    "Anacondra", "Cobrahn", "Pythas", "Serpentis", "Disciple", "Mutanus"
};

// hide a gameobject from players (VMaNGOS GameObject::SetVisible)
void ClassicWailingCavernsSetGoVisible(GameObject* go, bool visible, bool update)
{
    go->m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GM, visible ? SEC_PLAYER : SEC_GAMEMASTER);
    if (update)
        go->UpdateObjectVisibility();
}
}

class classic_instance_wailing_caverns : public InstanceMapScript
{
public:
    classic_instance_wailing_caverns() : InstanceMapScript(ClassicWailingCavernsScriptName, 43) { }

    struct classic_instance_wailing_caverns_InstanceScript : public InstanceScript
    {
        classic_instance_wailing_caverns_InstanceScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders("CWC");

            // VMaNGOS m_auiEncounter[] + Save()/Load() string -> TC persistent instance values
            for (uint32 i = 0; i < CWC_MAX_ENCOUNTER; ++i)
                m_auiEncounter[i] = std::make_unique<PersistentInstanceScriptValue<uint32>>(*this, ClassicWcEncounterNames[i], uint32(NOT_STARTED));

            Assaulted = false;
        }

        std::array<std::unique_ptr<PersistentInstanceScriptValue<uint32>>, CWC_MAX_ENCOUNTER> m_auiEncounter;

        uint32 Enc(uint32 index) const { return *m_auiEncounter[index]; }
        void SetEnc(uint32 index, uint32 value) { *m_auiEncounter[index] = value; }

        ObjectGuid m_uiDiscipleGUID;
        ObjectGuid m_uiNaralexGUID;
        ObjectGuid m_uiAnacondraGUID;
        ObjectGuid m_uiSerpentisGUID;
        bool Assaulted;

        // to be despawn when the nightmare is over
        std::vector<ObjectGuid> vNightmareMonsters;

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
                case CWC_NPC_DISCIPLE_OF_NARALEX:
                    m_uiDiscipleGUID = creature->GetGUID();
                    break;
                case CWC_NPC_NARALEX:
                    m_uiNaralexGUID = creature->GetGUID();
                    break;
                case CWC_NPC_LORD_SERPENTIS:
                    m_uiSerpentisGUID = creature->GetGUID();
                    break;
                case CWC_NPC_LADY_ANACONDRA:
                    m_uiAnacondraGUID = creature->GetGUID();
                    break;
                default:
                    break;
            }

            if ((creature->GetCreatureType() != CREATURE_TYPE_CRITTER) &&
                creature->GetFaction() != CWC_FACTION_FRIENDLY && // the 2 druids
                creature->GetEntry() != CWC_NPC_KRESH)             // Kresh is cool
                vNightmareMonsters.push_back(creature->GetGUID());
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            if (go->GetEntry() == CWC_GO_DMF_CHEST)
                ClassicWailingCavernsSetGoVisible(go, false, false);
        }

        void DoSerpentisYell()
        {
            if (Creature* serpentis = instance->GetCreature(m_uiSerpentisGUID))
                if (serpentis->IsAlive())
                    ClassicScriptText(CWC_SERPENTIS_YELL, serpentis);
            Assaulted = true;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case CWC_TYPE_ANACONDRA:
                    SetEnc(CWC_TYPE_ANACONDRA, data);
                    if (data == DONE && !Assaulted)
                        DoSerpentisYell();
                    if (data == SPECIAL)
                    {
                        if (Creature* ana = instance->GetCreature(m_uiAnacondraGUID))
                            if (ana->IsAlive())
                                if (Creature* druid = ana->FindNearestCreature(CWC_NPC_DRUID_OF_THE_FANG, INTERACTION_DISTANCE))
                                    if (druid->IsAlive())
                                        druid->DisappearAndDie();
                    }
                    break;
                case CWC_TYPE_COBRAHN:
                    SetEnc(CWC_TYPE_COBRAHN, data);
                    if (data == DONE && !Assaulted)
                        DoSerpentisYell();
                    break;
                case CWC_TYPE_PYTHAS:
                    SetEnc(CWC_TYPE_PYTHAS, data);
                    if (data == DONE && !Assaulted)
                        DoSerpentisYell();
                    break;
                case CWC_TYPE_MUTANUS:
                    SetEnc(type, data);
                    if (data == DONE) // despawn every hostile creature
                    {
                        for (ObjectGuid const& guid : vNightmareMonsters)
                        {
                            if (Creature* creature = instance->GetCreature(guid))
                            {
                                // VMaNGOS: IsAlive() || loot.empty()
                                if (creature->IsAlive() || !creature->m_loot || creature->m_loot->isLooted())
                                    creature->DespawnOrUnsummon();
                            }
                        }
                        vNightmareMonsters.clear();
                    }
                    break;
                case CWC_TYPE_SERPENTIS:
                case CWC_TYPE_DISCIPLE:
                    SetEnc(type, data);
                    break;
                default:
                    TC_LOG_ERROR("scripts", "Instance Wailing Caverns: ERROR SetData = {} for type {} does not exist/not implemented.", type, data);
                    break;
            }

            if (Enc(0) == DONE && Enc(1) == DONE && Enc(2) == DONE && Enc(3) == DONE && Enc(4) == NOT_STARTED)
            {
                TC_LOG_DEBUG("scripts", "Debug:Wailing Caverns encounters done");
                SetData(CWC_TYPE_DISCIPLE, SPECIAL);
                if (Creature* disciple = instance->GetCreature(m_uiDiscipleGUID))
                {
                    // VMaNGOS pDisciple->SetDefaultGossipMenuId(GOSSIP_DISCIPLE_SPECIAL): the disciple script shows
                    // gossip menu 202 while GetData(TYPE_DISCIPLE) == SPECIAL
                    ClassicScriptText(CWC_YELL_AFTER_GOSSIP, disciple);
                }
            }
            // VMaNGOS saved on DONE; persistent values are saved by TC whenever they change
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case CWC_TYPE_ANACONDRA:
                case CWC_TYPE_COBRAHN:
                case CWC_TYPE_PYTHAS:
                case CWC_TYPE_SERPENTIS:
                case CWC_TYPE_DISCIPLE:
                case CWC_TYPE_MUTANUS:
                    return Enc(type);
                default:
                    break;
            }
            return 0;
        }

        // VMaNGOS GetData64
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case CWC_DATA_NARALEX:
                    return m_uiNaralexGUID;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new classic_instance_wailing_caverns_InstanceScript(map);
    }
};

class classic_at_dmf_chest_wc : public AreaTriggerScript
{
public:
    classic_at_dmf_chest_wc() : AreaTriggerScript("classic_at_dmf_chest_wc") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
    {
        // Darkmoon chest visible only for players on the quest
        if (player->GetQuestStatus(CWC_QUEST_FORTUNE_AWAITS) == QUEST_STATUS_COMPLETE)
        {
            if (GameObject* go = player->FindNearestGameObject(CWC_GO_DMF_CHEST, 100.0f))
                ClassicWailingCavernsSetGoVisible(go, true, true);
        }

        return false;
    }
};

void AddSC_classic_instance_wailing_caverns()
{
    new classic_instance_wailing_caverns();
    new classic_at_dmf_chest_wc();
}
