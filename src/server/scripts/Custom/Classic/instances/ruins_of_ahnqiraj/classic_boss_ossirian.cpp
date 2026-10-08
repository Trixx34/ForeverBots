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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/boss_ossirian.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_ossirian, ossirian_crystal (generic_random_move is not registered in VMaNGOS and is not ported)
// SD%Complete: 99

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "Weather.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"
#include <array>
#include <vector>

namespace
{
enum ClassicAQ20Ossirian : uint32
{
    // TODO: UNUSED TEXTS (VMaNGOS)
    // SAY_SURPREME1 = -1509018, // I am rejuvinated! Sound: 8593
    // SAY_SURPREME2 = -1509019, // My powers are renewed! Sound: 8595
    // SAY_SURPREME3 = -1509020, // My powers return! Sound: 8596

    SAY_AQ20_OSSIRIAN_AGGRO             = 11449,
    SAY_AQ20_OSSIRIAN_SLAY              = 11450,
    SAY_AQ20_OSSIRIAN_DEATH             = 11451,

    SPELL_AQ20_OSSIRIAN_CURSE_OF_TONGUES = 25195,
    SPELL_AQ20_OSSIRIAN_STRENGTH        = 25176,
    SPELL_AQ20_OSSIRIAN_SUMMON_PLAYER   = 20477,    // unused in VMaNGOS too
    SPELL_AQ20_OSSIRIAN_WAR_STOMP       = 25188,

    // Tornado
    NPC_AQ20_OSSIRIAN_TORNADO           = 15428,
    SPELL_AQ20_OSSIRIAN_ENVELOPING_WINDS = 25189,
    SPELL_AQ20_OSSIRIAN_SANDSTORM       = 25160
};

// NOTE: the VMaNGOS boss file uses 25181 for arcane weakness (the VMaNGOS header wrongly has 25171)
std::array<uint32, 5> const ClassicAQ20OssirianWeakness =
{
    25177u, // Fire weakness
    25178u, // Frost weakness
    25180u, // Nature weakness
    25183u, // Shadow weakness
    25181u  // Arcane weakness
};

struct ClassicAQ20OssirianLocation
{
    float x, y, z;
};

std::array<ClassicAQ20OssirianLocation, 2> const ClassicAQ20TornadoSpawn =
{{
    { -9444.0f, 1857.0f, 85.55f },
    { -9352.0f, 2012.0f, 85.55f }
}};
}

/*######
## boss_ossirian
######*/

struct classic_boss_ossirian : public ScriptedAI
{
    classic_boss_ossirian(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    // VMaNGOS despawns Ossirian when there is no instance script; here every use is null-checked instead
    InstanceScript* m_pInstance;

    uint32 m_uiSpeed_Timer = 0;
    uint32 m_uiCurseOfTongues_Timer = 0;
    uint32 m_uiStrengthOfOssirian_Timer = 0;
    uint32 m_uiWarStomp_Timer = 0;
    uint32 m_uiEnvelopingWinds_Timer = 0;

    std::vector<ObjectGuid> TmpThreatList;
    std::vector<float> TmpThreatVal;
    std::vector<ObjectGuid> TornadoGUIDs;

    bool m_bIsEnraged = false;
    bool m_bAggro = false;

    void Reset() override
    {
        m_bAggro = false;

        DoCastSelf(SPELL_AQ20_OSSIRIAN_STRENGTH);

        m_uiSpeed_Timer = 10000;
        me->SetSpeedRate(MOVE_RUN, 1.0f);
        me->SetSpeedRate(MOVE_WALK, 1.0f);

        m_uiCurseOfTongues_Timer     = 30000;
        m_uiStrengthOfOssirian_Timer = 25000;
        m_uiWarStomp_Timer           = 25000;
        m_uiEnvelopingWinds_Timer    = 20000;

        TmpThreatList.clear();
        TmpThreatVal.clear();

        m_bIsEnraged = true;

        for (ObjectGuid const& guid : TornadoGUIDs)
        {
            if (Creature* pCrea = ObjectAccessor::GetCreature(*me, guid))
                pCrea->DespawnOrUnsummon();
        }

        TornadoGUIDs.clear();

        /** weather reseted to normal, seems like after aggro of Ossirian, weather needs to stay in sandstorm mode */
        me->GetMap()->SetZoneWeather(me->GetZoneId(), WEATHER_STATE_FINE, 0.0f);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_OSSIRIAN, FAIL);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_AQ20_OSSIRIAN_ENVELOPING_WINDS)
        {
            Unit* pUnit = target->ToUnit();
            if (!pUnit)
                return;

            TmpThreatVal.push_back(me->GetThreatManager().GetThreat(pUnit));
            TmpThreatList.push_back(pUnit->GetGUID());
            me->GetThreatManager().ModifyThreatByPercent(pUnit, -100);
        }
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        for (uint32 i : ClassicAQ20OssirianWeakness)
        {
            if (spellInfo->Id == i)
            {
                m_uiStrengthOfOssirian_Timer = 45000;

                if (m_bIsEnraged || me->HasAura(SPELL_AQ20_OSSIRIAN_STRENGTH))
                {
                    me->RemoveAurasDueToSpell(SPELL_AQ20_OSSIRIAN_STRENGTH);
                    m_bIsEnraged = false;
                }

                break;
            }
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_AQ20_OSSIRIAN_AGGRO, me);
        DoZoneInCombat();
        DoCastSelf(SPELL_AQ20_OSSIRIAN_STRENGTH);
        for (ClassicAQ20OssirianLocation const& i : ClassicAQ20TornadoSpawn)
        {
            Creature* pCreature = me->SummonCreature(NPC_AQ20_OSSIRIAN_TORNADO,
                                  i.x, i.y, i.z, 0.0f, TEMPSUMMON_MANUAL_DESPAWN);
            if (pCreature)
            {
                pCreature->CastSpell(pCreature, SPELL_AQ20_OSSIRIAN_SANDSTORM, true);
                pCreature->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE); // VMaNGOS UNIT_FLAG_NOT_SELECTABLE
                if (Unit* victim = me->GetVictim())
                    if (pCreature->IsAIEnabled())
                        pCreature->AI()->AttackStart(victim);
                TornadoGUIDs.push_back(pCreature->GetGUID());
            }
        }

        if (!m_bAggro)
        {
            m_bAggro = true;
            m_uiSpeed_Timer = 10000;
            if (m_pInstance)
                m_pInstance->SetData(CLASSIC_AQ20_DATA_CRYSTAL_INIT, 0); // VMaNGOS SpawnNewCrystals(ObjectGuid())
        }

        me->GetMap()->SetZoneWeather(me->GetZoneId(), WEATHER_STATE_HEAVY_SANDSTORM, 1.0f);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_OSSIRIAN, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_AQ20_OSSIRIAN_DEATH, me);
        for (ObjectGuid const& guid : TornadoGUIDs)
        {
            if (Creature* pCrea = ObjectAccessor::GetCreature(*me, guid))
                pCrea->DespawnOrUnsummon();
        }

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_OSSIRIAN, DONE);
    }

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->IsPlayer())
            ClassicScriptText(SAY_AQ20_OSSIRIAN_SLAY, me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiCurseOfTongues_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_OSSIRIAN_CURSE_OF_TONGUES) == SPELL_CAST_OK)
                m_uiCurseOfTongues_Timer = 10000 + urand(0, 9999);
        }
        else
            m_uiCurseOfTongues_Timer -= uiDiff;

        if (m_uiSpeed_Timer >= uiDiff)
        {
            m_uiSpeed_Timer -= uiDiff;
            me->SetSpeedRate(MOVE_RUN, 2.0f - float(m_uiSpeed_Timer) * 1.0f / 10000.0f);
        }

        if (!m_bIsEnraged && m_uiStrengthOfOssirian_Timer < uiDiff)
        {
            if (DoCastSelf(SPELL_AQ20_OSSIRIAN_STRENGTH) == SPELL_CAST_OK)
                m_bIsEnraged = true;
        }
        else
            m_uiStrengthOfOssirian_Timer -= uiDiff;

        if (m_uiWarStomp_Timer < uiDiff)
        {
            if (DoCastSelf(SPELL_AQ20_OSSIRIAN_WAR_STOMP) == SPELL_CAST_OK)
                m_uiWarStomp_Timer = 25000 + urand(0, 9999);
        }
        else
            m_uiWarStomp_Timer -= uiDiff;

        if (m_uiEnvelopingWinds_Timer < uiDiff)
        {
            if (DoCastVictim(SPELL_AQ20_OSSIRIAN_ENVELOPING_WINDS) == SPELL_CAST_OK)
                m_uiEnvelopingWinds_Timer = 15000;
        }
        else
            m_uiEnvelopingWinds_Timer -= uiDiff;

        // Restore the threat of targets whose Enveloping Winds faded (VMaNGOS erase-in-loop kept as-is)
        if (!TmpThreatList.empty())
        {
            for (size_t i = 0; i < TmpThreatList.size(); ++i)
            {
                if (Unit* unit = ObjectAccessor::GetUnit(*me, TmpThreatList[i]))
                {
                    if (unit->IsAlive())
                    {
                        if (unit->HasAura(SPELL_AQ20_OSSIRIAN_ENVELOPING_WINDS))
                            continue;
                        me->GetThreatManager().AddThreat(unit, TmpThreatVal[i], nullptr, true, true);
                    }
                }
                TmpThreatList.erase(TmpThreatList.begin() + i);
                TmpThreatVal.erase(TmpThreatVal.begin() + i);
            }
        }
    }
};

/*######
## ossirian_crystal
######*/

struct classic_ossirian_crystal : public GameObjectAI
{
    classic_ossirian_crystal(GameObject* go) : GameObjectAI(go), m_pInstance(go->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    // VMaNGOS GameObjectAI::OnUse(Unit*): returning true blocks the default use, false lets it continue
    bool OnGossipHello(Player* player) override
    {
        if (!m_pInstance)
        {
            TC_LOG_ERROR("scripts", "[OSSIRIAN/Crystal][Inst {}] ERROR: No instance", player->GetInstanceId());
            return false;
        }

        // Already used
        if (GetClosestCreatureWithEntry(me, CLASSIC_AQ20_CRYSTAL_TRIGGER, 5.0f))
            return true;

        // Spawn new crystals even if Ossirian is out of range
        m_pInstance->SetGuidData(CLASSIC_AQ20_DATA_CRYSTAL, me->GetGUID()); // VMaNGOS SpawnNewCrystals(GetObjectGuid())

        Creature* ossirian = GetClosestCreatureWithEntry(me, CLASSIC_AQ20_NPC_OSSIRIAN, 300.0f);

        if (!ossirian)
        {
            TC_LOG_ERROR("scripts", "[OSSIRIAN/Crystal][Inst {}] ERROR: No Ossirian found", player->GetInstanceId());
            return false;
        }

        // Encounter not started
        if (!ossirian->GetVictim())
            return true;

        Creature* triggerCrystalPylons = me->SummonCreature(CLASSIC_AQ20_CRYSTAL_TRIGGER,
                                            me->GetPositionX(),
                                            me->GetPositionY(),
                                            me->GetPositionZ(),
                                            me->GetOrientation(),
                                            TEMPSUMMON_TIMED_DESPAWN,
                                            8s);

        if (triggerCrystalPylons)
            triggerCrystalPylons->CastSpell(ossirian, ClassicAQ20OssirianWeakness[urand(0, 4)], true);

        return false;
    }
};

void AddSC_classic_boss_ossirian()
{
    RegisterCreatureAI(classic_boss_ossirian);
    RegisterGameObjectAI(classic_ossirian_crystal);
}
