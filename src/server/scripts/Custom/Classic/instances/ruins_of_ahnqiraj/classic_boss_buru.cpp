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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/ruins_of_ahnqiraj/boss_buru.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_buru, mob_buru_egg

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MapReference.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_ruins_of_ahnqiraj.h"
#include "classic_script_text.h"
#include <array>
#include <limits>

namespace
{
enum ClassicAQ20Buru : uint32
{
    EMOTE_AQ20_BURU_TARGET          = 11074,    // %s sets eyes on $n!

    SPELL_AQ20_BURU_CREEPING_PLAGUE = 20512,
    SPELL_AQ20_BURU_DISMEMBER       = 96,
    SPELL_AQ20_BURU_GAIN_SPEED      = 1834,
    SPELL_AQ20_BURU_FULL_SPEED      = 1557,
    SPELL_AQ20_BURU_THORNS          = 25640,
    SPELL_AQ20_BURU_TRANSFORM       = 24721,
    SPELL_AQ20_BURU_SELF_FREEZE     = 29826,    // unused in VMaNGOS too

    NPC_AQ20_BURU_EGG               = 15514,
    SPELL_AQ20_BURU_SUMMON_HATCHLING = 1881,    // unused in VMaNGOS too
    SPELL_AQ20_BURU_EGG_EXPLODE     = 19593,
    NPC_AQ20_BURU_EGG_TRIGGER       = 15964,    // unused in VMaNGOS too

    NPC_AQ20_HIVEZARA_HATCHLING     = 15521,

    MODEL_AQ20_BURU_INVISIBLE       = 11686,    // unused in VMaNGOS too
    MODEL_AQ20_BURU_NORMAL          = 15654
};

// To lock the target, VMaNGOS applies a very high threat
float const ClassicAQ20BuruThreatLock = std::numeric_limits<float>::max();

struct ClassicAQ20BuruLocation
{
    float x, y, z;
};

ClassicAQ20BuruLocation const ClassicAQ20BuruAddPop[] =
{
    { -9312.0f, 1281.0f, -62.0f },
    { -9268.0f, 1249.0f, -62.0f },
    { -9263.0f, 1292.0f, -63.0f }
};

std::array<ClassicAQ20BuruLocation, 6> const ClassicAQ20BuruEggs =
{{
    { -9312.73f, 1281.51f, -63.56f },
    { -9300.03f, 1304.52f, -63.25f },
    { -9263.38f, 1293.48f, -63.84f },
    { -9245.11f, 1280.30f, -63.33f },
    { -9234.96f, 1244.95f, -63.05f },
    { -9267.78f, 1249.26f, -63.58f }
}};
}

/*######
## boss_buru
######*/

struct classic_boss_buru : public ScriptedAI
{
    classic_boss_buru(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    bool m_bIsEnraged = false;
    bool m_HatchPop = false;

    uint32 m_uiDismember_Timer = 0;
    uint32 m_uiSpeed_Timer = 0;
    uint32 m_uiCreepingPlague_Timer = 0;
    uint32 m_uiTransformTimer = 0;
    std::array<uint32, 6> m_uiRespawnEgg_Timer = {};
    uint8 m_transitionStep = 0;

    std::array<ObjectGuid, 6> m_eggsGUID;

    void Reset() override
    {
        me->SetDisplayId(MODEL_AQ20_BURU_NORMAL);
        me->RemoveAllAuras();
        me->SetSpeedRate(MOVE_RUN, 0.5f);
        m_bIsEnraged = false;
        m_HatchPop = false;
        m_uiDismember_Timer = 1000;
        m_uiSpeed_Timer = 30000;
        m_uiCreepingPlague_Timer = 6000;
        m_uiTransformTimer = 0;
        m_transitionStep = 0;

        for (uint32& i : m_uiRespawnEgg_Timer)
            i = 120000;

        for (uint8 i = 0; i < 6; ++i)
        {
            if (Creature* egg = ObjectAccessor::GetCreature(*me, m_eggsGUID[i]))
                egg->DespawnOrUnsummon();
            m_eggsGUID[i].Clear();
            if (Creature* egg = SummonEgg(i))
                m_eggsGUID[i] = egg->GetGUID();
        }

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_BURU, NOT_STARTED);
    }

    Creature* SummonEgg(uint8 slot)
    {
        // VMaNGOS SummonCreature with default despawn type; the AI manages the eggs itself here
        return me->SummonCreature(NPC_AQ20_BURU_EGG, ClassicAQ20BuruEggs[slot].x, ClassicAQ20BuruEggs[slot].y, ClassicAQ20BuruEggs[slot].z, 0.0f);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
        me->SetArmor(20000, 0);
        DoCastSelf(SPELL_AQ20_BURU_THORNS);
        me->SetCanMelee(true);
        if (!IsCombatMovementAllowed())
            SetCombatMovement(true);
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_BURU, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        // The debuff should fade when dead, otherwise the raid gets decimated
        for (MapReference const& i : me->GetMap()->GetPlayers())
            if (Player* pPlayer = i.GetSource())
                pPlayer->RemoveAurasDueToSpell(SPELL_AQ20_BURU_CREEPING_PLAGUE);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_AQ20_TYPE_BURU, DONE);
    }

    void AttackStart(Unit* pVictim) override
    {
        if (m_bIsEnraged && m_uiTransformTimer)
            return;

        ScriptedAI::AttackStart(pVictim);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        for (uint8 i = 0; i < 6 && !m_bIsEnraged; i++)
        {
            Creature* egg = ObjectAccessor::GetCreature(*me, m_eggsGUID[i]);
            if (!egg)
            {
                if (Creature* newEgg = SummonEgg(i))
                    m_eggsGUID[i] = newEgg->GetGUID();
            }
            else if (!egg->IsAlive())
            {
                if (m_uiRespawnEgg_Timer[i] < uiDiff)
                {
                    m_uiRespawnEgg_Timer[i] = 120000;
                    // VMaNGOS egg->Respawn(); the egg is a temp summon: despawn it and let the loop above resummon it
                    egg->DespawnOrUnsummon();
                    m_eggsGUID[i].Clear();
                }
                else
                    m_uiRespawnEgg_Timer[i] -= uiDiff;
            }
        }

        // Phase transition. All this to have a nice visual :)
        if (m_bIsEnraged && m_uiTransformTimer)
        {
            if (m_uiTransformTimer <= uiDiff)
            {
                switch (m_transitionStep)
                {
                    case 0:
                    {
                        me->RemoveAllAuras(); // Delete Thorns ability during Phase 2
                        DoCastSelf(SPELL_AQ20_BURU_TRANSFORM);
                        m_transitionStep = 1;
                        m_uiTransformTimer = 5000;
                        break;
                    }
                    case 1:
                    {
                        DoCastSelf(SPELL_AQ20_BURU_FULL_SPEED, true);
                        me->SetCanMelee(true);
                        SetCombatMovement(true);
                        DoZoneInCombat();
                        m_transitionStep = 2;
                        m_uiTransformTimer = 0;
                        break;
                    }
                }
            }
            else
                m_uiTransformTimer -= uiDiff;

            return;
        }

        // VMaNGOS RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_SPAWNING)
        me->RemoveUnitFlag(UnitFlags(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE));

        if (!UpdateVictim())
            return;

        // If he is not enraged and no longer has a threat list, he chooses a person with focus at random
        if (!m_bIsEnraged)
        {
            if (me->GetHealthPct() < 20.0f)
            {
                m_bIsEnraged = true;

                // The transformation spell should not be cast right away if you want to have the visual.
                m_uiTransformTimer = 200;
                m_transitionStep = 0;

                // Removed armor, enrage, reset aggro list and reset speed to normal
                me->SetArmor(0, 0);
                me->GetThreatManager().ClearAllThreat();
                me->RemoveAurasDueToSpell(SPELL_AQ20_BURU_GAIN_SPEED);
                me->SetSpeedRate(MOVE_RUN, 1.0f);

                if (Unit* victim = me->GetVictim())
                    me->SetFacingToObject(victim);

                me->SetCanMelee(false);
                SetCombatMovement(false);
                me->AttackStop();

                // Despawn eggs when enraged
                for (ObjectGuid& guid : m_eggsGUID)
                {
                    if (!guid.IsEmpty())
                    {
                        if (Creature* egg = ObjectAccessor::GetCreature(*me, guid))
                            egg->DespawnOrUnsummon();

                        guid.Clear();
                    }
                }
            }
            else if (me->GetThreatManager().GetThreat(me->GetVictim()) < (ClassicAQ20BuruThreatLock / 1000))
            {
                std::array<ObjectGuid, 40> GUIDs = { };

                uint32 var = 0;
                for (ThreatReference const* itr : me->GetThreatManager().GetUnsortedThreatList())
                {
                    Player* pPlayer = itr->GetVictim()->ToPlayer();
                    if (pPlayer && pPlayer->IsAlive() && var < GUIDs.size())
                    {
                        GUIDs[var] = pPlayer->GetGUID();
                        ++var;
                    }
                }

                if (var)
                {
                    if (Player* pTarget = ObjectAccessor::GetPlayer(*me, GUIDs[urand(0, var - 1)]))
                    {
                        ClassicScriptText(EMOTE_AQ20_BURU_TARGET, me, pTarget);

                        // To lock the target, we apply a very high threat
                        me->GetThreatManager().AddThreat(pTarget, ClassicAQ20BuruThreatLock, nullptr, true, true);

                        me->RemoveAurasDueToSpell(SPELL_AQ20_BURU_GAIN_SPEED);
                        me->SetSpeedRate(MOVE_RUN, 0.5f);
                        m_uiSpeed_Timer = 30000;
                    }
                }
            }
        }

        // Dismember - A stacking bleed effect that does 1248 damage every 2 second. Buru will use this if he catches up to whoever he is targeting.
        if (m_uiDismember_Timer < uiDiff && !m_bIsEnraged)
        {
            if (DoCastVictim(SPELL_AQ20_BURU_DISMEMBER) == SPELL_CAST_OK)
                m_uiDismember_Timer = 6000;
        }
        else
            m_uiDismember_Timer -= uiDiff;

        // Creeping plague
        if (m_bIsEnraged)
        {
            if (m_uiCreepingPlague_Timer < uiDiff)
            {
                if (DoCastSelf(SPELL_AQ20_BURU_CREEPING_PLAGUE) == SPELL_CAST_OK)
                    m_uiCreepingPlague_Timer = 6000;
            }
            else
                m_uiCreepingPlague_Timer -= uiDiff;

            // Pop additions when enraged (3, but I'm not sure of this number and don't know if they all happen at once)
            if (!m_HatchPop)
            {
                for (ClassicAQ20BuruLocation const& i : ClassicAQ20BuruAddPop)
                {
                    if (Creature* summoned = me->SummonCreature(NPC_AQ20_HIVEZARA_HATCHLING, i.x, i.y, i.z, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s))
                        if (summoned->IsAIEnabled())
                            summoned->AI()->DoZoneInCombat();
                }
                m_HatchPop = true;
            }
        }

        // Gain speed
        if (m_uiSpeed_Timer < uiDiff)
        {
            me->CastSpell(me, SPELL_AQ20_BURU_GAIN_SPEED, true);
            m_uiSpeed_Timer = 30000;
        }
        else
            m_uiSpeed_Timer -= uiDiff;
    }
};

/*######
## mob_buru_egg
######*/

struct classic_mob_buru_egg : public ScriptedAI
{
    classic_mob_buru_egg(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    void Reset() override
    {
        me->SetCanMelee(false); // VMaNGOS AI never calls DoMeleeAttackIfReady
    }

    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!m_pInstance)
            return;

        // Aggro Buru on taking damage
        if (Creature* pBuru = ObjectAccessor::GetCreature(*me, m_pInstance->GetGuidData(CLASSIC_AQ20_DATA_BURU)))
        {
            if (!pBuru->IsInCombat())
                DoZoneInCombat(pBuru);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        // Explodes and spawns a creature when it dies
        me->CastSpell(me, SPELL_AQ20_BURU_EGG_EXPLODE, false);

        if (Creature* add = me->SummonCreature(NPC_AQ20_HIVEZARA_HATCHLING, me->GetPositionX(), me->GetPositionY(),
            me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s))
        {
            DoZoneInCombat(add);
        }
    }

    void UpdateAI(uint32 /*uiDiff*/) override { }
};

void AddSC_classic_boss_buru()
{
    RegisterCreatureAI(classic_boss_buru);
    RegisterCreatureAI(classic_mob_buru_egg);
}
