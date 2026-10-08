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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/dire_maul/boss_gordok_king.cpp (ScriptDev2 lineage, GPL-2)
// Ported: boss_king_gordok, boss_chorush

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "classic_dire_maul.h"
#include "classic_script_text.h"

/*######
## boss_king_gordok
######*/

enum ClassicDMKingGordok
{
    SPELL_BERSERKER_CHARGE  = 22886,
    SPELL_MORTAL_STRIKE     = 15708,
    SPELL_WAR_STOMP         = 16727,
    SPELL_SUNDER_ARMOR      = 15572,

    SAY_AGGRO               = 9481
};

struct classic_boss_king_gordok : public ScriptedAI
{
    classic_boss_king_gordok(Creature* creature) : ScriptedAI(creature)
    {
        pInstance = creature->GetInstanceScript();
    }

    InstanceScript* pInstance;

    uint32 m_uiMortalStrike_Timer = 0;
    uint32 m_uiWarStomp_Timer = 0;
    uint32 m_uiBerserkerCharge_Timer = 0;
    uint32 m_uiSunderArmor_Timer = 0;
    uint32 m_uiPhase = 0;

    // World of Warcraft Client Patch 1.9.3 (2006-02-07)
    // - King Gordok can no longer be seperated from Cho'Rush the Observer in Dire Maul.
    // VMaNGOS: sWorld.GetWowPatch() >= WOW_PATCH_109 (always true for the Classic 1.60 content patch)
    bool const m_bLinkCheckEnabled = true;
    uint32 m_uiLinkCheckTimer = 2500;

    void Reset() override
    {
        m_uiWarStomp_Timer        = urand(7000, 8000);
        m_uiMortalStrike_Timer    = urand(15000, 25000);
        m_uiSunderArmor_Timer     = urand(4000, 8000);
        m_uiBerserkerCharge_Timer = urand(9000, 12000);
        m_uiPhase = 0;

        m_uiLinkCheckTimer = 2500;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_AGGRO, me);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // Sunder Armor
        if (m_uiSunderArmor_Timer < diff)
        {
            if (Unit* target = me->GetVictim())
            {
                DoCast(target, SPELL_SUNDER_ARMOR);
                if (Aura* aura = target->GetAura(SPELL_SUNDER_ARMOR))
                {
                    if (aura->GetStackAmount() == 5)
                        m_uiSunderArmor_Timer = urand(15000, 25000);
                    else
                        m_uiSunderArmor_Timer = urand(5000, 15000);
                }
                else
                    m_uiSunderArmor_Timer = urand(5000, 15000);
            }
        }
        else
            m_uiSunderArmor_Timer -= diff;

        // Mortal Strike
        if (m_uiMortalStrike_Timer < diff)
        {
            if (DoCastVictim(SPELL_MORTAL_STRIKE) == SPELL_CAST_OK)
                m_uiMortalStrike_Timer = urand(12000, 20000);
        }
        else
            m_uiMortalStrike_Timer -= diff;

        // War Stomp
        if (m_uiWarStomp_Timer < diff)
        {
            if (DoCastSelf(SPELL_WAR_STOMP) == SPELL_CAST_OK)
                m_uiWarStomp_Timer = urand(20000, 30000);
        }
        else
            m_uiWarStomp_Timer -= diff;

        // Berserker Charge
        if (m_uiBerserkerCharge_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                if (DoCast(target, SPELL_BERSERKER_CHARGE) == SPELL_CAST_OK)
                    m_uiBerserkerCharge_Timer = urand(25000, 30000);
        }
        else
            m_uiBerserkerCharge_Timer -= diff;

        if (m_bLinkCheckEnabled && pInstance)
        {
            // Prevent splitting King from the Observer
            if (m_uiLinkCheckTimer < diff)
            {
                if (Creature* chorush = me->GetMap()->GetCreature(pInstance->GetGuidData(NPC_CHORUSH)))
                {
                    if (chorush->IsAlive() && !chorush->IsInCombat() && chorush->AI() && me->GetVictim())
                        chorush->AI()->AttackStart(me->GetVictim());
                }
                m_uiLinkCheckTimer = 2500;
            }
            else
                m_uiLinkCheckTimer -= diff;
        }
    }
};

/*######
## boss_chorush
######*/

enum ClassicDMChorush
{
    // Mage
    SPELL_ARCANE_EXPLOSION  = 13745, // m_uiSpellTimers[1]
    SPELL_FIREBALL          = 17290, // m_uiSpellTimers[0]
    SPELL_FROST_NOVA        = 15331, // d 8s // m_uiSpellTimers[2]
    SPELL_BLOODLUST         = 16170, // d 30s // m_uiSpellTimers[3]

    // Shaman
    SPELL_CHAIN_LIGHTNING   = 15305, // m_uiSpellTimers[1]
    SPELL_EARTHGRAB_TOTEM   = 8376,  // d 30s m_uiSpellTimers[2]
    SPELL_HEALING_WAVE      = 15982, // m_uiSpellTimers[3]
    SPELL_LIGHTNING_BOLT    = 15234, // m_uiSpellTimers[0]

    // Priest
    SPELL_MIND_BLAST        = 17194, // m_uiSpellTimers[0]
    SPELL_HEAL              = 22883, // m_uiSpellTimers[3]
    SPELL_POWER_WORD_SHIELD = 17139, // m_uiSpellTimers[1]
    SPELL_PSYCHIC_SCREAM    = 22884, // m_uiSpellTimers[2]

    NPC_EARTHGRAB_TOTEM     = 6066,

    // VMaNGOS equipment template ids (not creature_equip_template ids of 14324)
    MAGE_EQUIPMENT          = 12072,
    SHAMAN_EQUIPMENT        = 12071,
    PRIST_EQUIPMENT         = 12070,

    MAX_SPELLS              = 4,
    SET_MAGE                = 1,
    SET_SHAMAN              = 2,
    SET_PRIST               = 3
};

struct classic_boss_chorush : public ScriptedAI
{
    classic_boss_chorush(Creature* creature) : ScriptedAI(creature)
    {
        pInstance = creature->GetInstanceScript();
    }

    InstanceScript* pInstance;

    uint8 m_uiEquipment = 0;
    uint32 m_uiSpellTimers[MAX_SPELLS] = { };
    bool m_bInMeele = true;

    // World of Warcraft Client Patch 1.9.3 (2006-02-07)
    // - King Gordok can no longer be seperated from Cho'Rush the Observer in Dire Maul.
    bool const m_bLinkCheckEnabled = true;
    uint32 m_uiLinkCheckTimer = 2500;

    // VMaNGOS instance_dire_maul::GetChoRushEquipment()
    uint8 GetChoRushEquipment()
    {
        uint8 equipment = uint8(pInstance->GetData(TYPE_CHORUSH_EQUIPMENT));
        if (!equipment)
        {
            equipment = uint8(urand(1, 3));
            pInstance->SetData(TYPE_CHORUSH_EQUIPMENT, equipment);
        }
        return equipment;
    }

    void Reset() override
    {
        m_uiLinkCheckTimer = 2500;
        m_uiEquipment = 0;
        m_bInMeele = true;
        if (pInstance)
            m_uiEquipment = GetChoRushEquipment();

        for (uint32& timer : m_uiSpellTimers)
            timer = urand(1000, 2000);

        // TODO(classic): VMaNGOS LoadEquipment(MAGE_EQUIPMENT 12072 / SHAMAN_EQUIPMENT 12071 / PRIST_EQUIPMENT 12070) loads raw
        // equipment templates; TC loads creature_equip_template rows of entry 14324 by id (1..3). Needs matching DB rows.
        switch (m_uiEquipment)
        {
            case SET_MAGE:
                me->LoadEquipment(SET_MAGE, true);
                SetCombatMovement(true);
                break;
            case SET_SHAMAN:
                me->LoadEquipment(SET_SHAMAN, true);
                SetCombatMovement(true);
                break;
            case SET_PRIST:
                me->LoadEquipment(SET_PRIST, true);
                SetCombatMovement(true);
                break;
            default:
                break;
        }
    }

    bool HasMeleeAttackerInRange(float range) const
    {
        for (Unit* attacker : me->getAttackers())
            if (me->IsInRange(attacker, 0.0f, range, false))
                return true;
        return false;
    }

    // returns true when the combat movement mode changed (VMaNGOS "return" out of the class update)
    bool UpdateRangeMode()
    {
        Unit* victim = me->GetVictim();
        if (!victim)
            return false;

        if (!IsCombatMovementAllowed())
        { //Melee
            if (!m_bInMeele && (me->GetDistance2d(victim) < 5.0f || me->GetDistance2d(victim) > 30.0f
                || !me->IsWithinLOSInMap(victim) || me->GetPowerPct(POWER_MANA) < 5.0f))
            {
                SetCombatMovement(true);
                DoStartMovement(victim);
                m_bInMeele = true;
                return true;
            }
        }
        else
        { //Range
            if (m_bInMeele && me->GetDistance2d(victim) >= 5.0f && me->GetDistance2d(victim) <= 30.0f
                && me->IsWithinLOSInMap(victim) && me->GetPowerPct(POWER_MANA) >= 5.0f)
            {
                SetCombatMovement(false);
                m_bInMeele = false;
                DoStartNoMovement(victim);
                return true;
            }
        }
        return false;
    }

    // MAGE
    void UpdateAIMage(uint32 diff)
    {
        // Fireball
        if (m_uiSpellTimers[0] < diff)
        {
            if (DoCastVictim(SPELL_FIREBALL) == SPELL_CAST_OK)
                m_uiSpellTimers[0] = (m_bInMeele ? urand(7000, 10000) : urand(3000, 4000));
        }
        else
            m_uiSpellTimers[0] -= diff;

        // Bloodlust
        if (m_uiSpellTimers[3] < diff)
        {
            Unit* target = nullptr;

            if (!me->HasAura(SPELL_BLOODLUST))
                target = me;

            Unit* king = me->FindNearestCreature(NPC_KING_GORDOK, 30.f);

            if (king && !king->HasAura(SPELL_BLOODLUST) && urand(0, 1))
                target = king;

            if (target && DoCast(target, SPELL_BLOODLUST) == SPELL_CAST_OK)
                m_uiSpellTimers[3] = urand(10000, 20000);
        }
        else
            m_uiSpellTimers[3] -= diff;

        // Arcane Explosion
        if (m_uiSpellTimers[1] < diff)
        {
            if (HasMeleeAttackerInRange(8.0f))
                if (DoCastSelf(SPELL_ARCANE_EXPLOSION) == SPELL_CAST_OK)
                    m_uiSpellTimers[1] = (m_bInMeele ? urand(9000, 13000) : urand(15000, 25000));
        }
        else
            m_uiSpellTimers[1] -= diff;

        // Frost Nova
        if (m_uiSpellTimers[2] < diff)
        {
            if (HasMeleeAttackerInRange(8.0f))
                if (DoCastSelf(SPELL_FROST_NOVA) == SPELL_CAST_OK)
                    m_uiSpellTimers[2] = urand(20000, 30000);
        }
        else
            m_uiSpellTimers[2] -= diff;

        UpdateRangeMode();
    }

    // SHAMAN
    void UpdateAIShaman(uint32 diff)
    {
        // Earthgrab Totem
        if (m_uiSpellTimers[2] < diff)
        {
            if (HasMeleeAttackerInRange(6.0f))
                if (DoCastSelf(SPELL_EARTHGRAB_TOTEM) == SPELL_CAST_OK)
                    m_uiSpellTimers[2] = urand(20000, 30000);
        }
        else
            m_uiSpellTimers[2] -= diff;

        // Healing Wave
        if (m_uiSpellTimers[3] < diff)
        {
            Unit* target = DoSelectLowestHpFriendly(40.0f, 15000);
            if (!target && me->GetHealthPct() < 50.0f)
                target = me;

            if (target)
                if (DoCast(target, SPELL_HEALING_WAVE) == SPELL_CAST_OK)
                    m_uiSpellTimers[3] = urand(10000, 15000);
        }
        else
            m_uiSpellTimers[3] -= diff;

        // Lightning Bolt
        if (m_uiSpellTimers[0] < diff)
        {
            if (DoCastVictim(SPELL_LIGHTNING_BOLT) == SPELL_CAST_OK)
                m_uiSpellTimers[0] = (m_bInMeele ? urand(7000, 10000) : urand(3000, 4000));
        }
        else
            m_uiSpellTimers[0] -= diff;

        // Chain Lightning
        if (m_uiSpellTimers[1] < diff)
        {
            if (DoCastVictim(SPELL_CHAIN_LIGHTNING) == SPELL_CAST_OK)
                m_uiSpellTimers[1] = urand(15000, 25000);
        }
        else
            m_uiSpellTimers[1] -= diff;

        UpdateRangeMode();
    }

    // PRIEST
    void UpdateAIPrist(uint32 diff)
    {
        // Heal
        if (m_uiSpellTimers[3] < diff)
        {
            Unit* target = DoSelectLowestHpFriendly(40.0f, 15000);
            if (!target && me->GetHealthPct() < 50.0f)
                target = me;

            if (target)
                if (DoCast(target, SPELL_HEAL) == SPELL_CAST_OK)
                    m_uiSpellTimers[3] = urand(10000, 15000);
        }
        else
            m_uiSpellTimers[3] -= diff;

        // Mind Blast
        if (m_uiSpellTimers[0] < diff)
        {
            if (DoCastVictim(SPELL_MIND_BLAST) == SPELL_CAST_OK)
                m_uiSpellTimers[0] = (m_bInMeele ? urand(7000, 10000) : urand(2000, 3000));
        }
        else
            m_uiSpellTimers[0] -= diff;

        // Power Word Shield
        if (m_uiSpellTimers[1] < diff)
        {
            if (Unit* target = DoSelectLowestHpFriendly(40.0f))
            {
                if (DoCast(target, SPELL_POWER_WORD_SHIELD) == SPELL_CAST_OK)
                    m_uiSpellTimers[1] = urand(17000, 22000);
            }
        }
        else
            m_uiSpellTimers[1] -= diff;

        // Psychic Scream
        if (m_uiSpellTimers[2] < diff)
        {
            if (HasMeleeAttackerInRange(8.0f))
                if (DoCastSelf(SPELL_PSYCHIC_SCREAM) == SPELL_CAST_OK)
                    m_uiSpellTimers[2] = urand(15000, 20000);
        }
        else
            m_uiSpellTimers[2] -= diff;

        UpdateRangeMode();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        switch (m_uiEquipment)
        {
            case SET_MAGE:
                UpdateAIMage(diff);
                break;
            case SET_SHAMAN:
                UpdateAIShaman(diff);
                break;
            case SET_PRIST:
                UpdateAIPrist(diff);
                break;
            default:
                break;
        }

        if (m_bLinkCheckEnabled && pInstance)
        {
            // Prevent splitting Observer from the King
            if (m_uiLinkCheckTimer < diff)
            {
                if (Creature* king = me->GetMap()->GetCreature(pInstance->GetGuidData(NPC_KING_GORDOK)))
                {
                    if (!king->IsInCombat() && king->AI() && me->GetVictim())
                        king->AI()->AttackStart(me->GetVictim());
                }
                m_uiLinkCheckTimer = 2500;
            }
            else
                m_uiLinkCheckTimer -= diff;
        }
    }
};

void AddSC_classic_boss_gordok_king()
{
    RegisterCreatureAI(classic_boss_king_gordok);
    RegisterCreatureAI(classic_boss_chorush);
}
