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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/molten_core/boss_golemagg.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_golemagg, mob_core_rager, spell_golemaggs_trust (20556)

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "classic_molten_core.h"
#include "classic_script_text.h"
#include <list>

namespace
{
enum ClassicMcGolemagg : uint32
{
    // Auras (set in db - creature_template)
    // SPELL_MAGMASPLASH         = 13879,
    // SPELL_GOLEMAGG_TRUST_AURA = 20556,
    // SPELL_DOUBLE_ATTACK       = 18943,

    // Spells
    SPELL_GOLEMAGG_PYROBLAST      = 20228,
    SPELL_GOLEMAGG_EARTHQUAKE     = 19798,
    SPELL_GOLEMAGG_TRUST          = 20553,
    SPELL_GOLEMAGG_ATTRACK_RAGER  = 20544,

    // Events
    EVENT_GOLEMAGG_PYROBLAST      = 1,
    EVENT_GOLEMAGG_EARTHQUAKE     = 2,
    EVENT_GOLEMAGG_TRUST          = 3
};

enum ClassicMcCoreRager : uint32
{
    // Spells
    SPELL_CORE_RAGER_MANGLE       = 19820,
    SPELL_CORE_RAGER_FULL_HEAL    = 17683,

    // Texts
    EMOTE_CORE_RAGER_LOWHP        = 7865,

    // Events
    EVENT_CORE_RAGER_MANGLE       = 1,
    EVENT_CORE_RAGER_CHECK_LEASH  = 3
};
}

struct classic_boss_golemagg : public ScriptedAI
{
    classic_boss_golemagg(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    EventMap m_CombatEvents;
    InstanceScript* m_pInstance;
    std::list<ObjectGuid> m_addList;

    bool m_bEnraged = false;

    void Reset() override
    {
        m_bEnraged = false;
        m_CombatEvents.Reset();
        m_addList.clear();

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_MC_TYPE_GOLEMAGG, NOT_STARTED);

        // VMaNGOS kills every living Core Rager and respawns them all (fresh pack)
        std::list<Creature*> addList;
        me->GetCreatureListWithEntryInGrid(addList, CLASSIC_MC_NPC_CORE_RAGER, 250.0f);
        for (Creature* rager : addList)
        {
            if (rager->IsAlive())
                rager->DespawnOrUnsummon(0s, 1s);   // TC: despawn + forced respawn after 1s
            else
                rager->Respawn(true);
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GOLEMAGG, IN_PROGRESS);

        // Store add guids
        std::list<Creature*> adds;
        me->GetCreatureListWithEntryInGrid(adds, CLASSIC_MC_NPC_CORE_RAGER, 150.0f);
        m_addList.clear();
        for (Creature* add : adds)
            m_addList.push_back(add->GetGUID());

        ScheduleCombatEvents();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        KillAdds(true);
        ScriptedAI::EnterEvadeMode(why);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_MC_TYPE_GOLEMAGG, DONE);

        KillAdds(false);
    }

    void KillAdds(bool respawn)
    {
        for (ObjectGuid const& guid : m_addList)
        {
            if (Creature* coreRager = me->GetMap()->GetCreature(guid))
            {
                // VMaNGOS: DisappearAndDie() (+ Respawn())
                if (respawn)
                    coreRager->DespawnOrUnsummon(0s, 1s);
                else
                    coreRager->DespawnOrUnsummon();
            }
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!m_bEnraged)
        {
            // At 10% health, Golemagg will Earthquake,
            // dealing Physical damage to all players every couple seconds.
            if (me->GetHealthPct() < 10.f)
            {
                if (DoCastSelf(SPELL_GOLEMAGG_ATTRACK_RAGER) == SPELL_CAST_OK)
                {
                    m_bEnraged = true;
                    m_CombatEvents.ScheduleEvent(EVENT_GOLEMAGG_EARTHQUAKE, 5s);
                }
            }
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        m_CombatEvents.Update(uiDiff);
        UpdateEvents();
    }

    void ScheduleCombatEvents()
    {
        m_CombatEvents.ScheduleEvent(EVENT_GOLEMAGG_PYROBLAST, 7s);
        m_CombatEvents.ScheduleEvent(EVENT_GOLEMAGG_TRUST,     10s);
    }

    void UpdateEvents()
    {
        while (uint32 const eventId = m_CombatEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_GOLEMAGG_PYROBLAST:
                {
                    // Golemagg Pyroblasts a random player,
                    // dealing Fire damage and leaving an additional Fire DOT on them.
                    if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        if (DoCast(pTarget, SPELL_GOLEMAGG_PYROBLAST) == SPELL_CAST_OK)
                        {
                            m_CombatEvents.Repeat(7s);
                            return;
                        }
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                case EVENT_GOLEMAGG_EARTHQUAKE:
                {
                    if (DoCastVictim(SPELL_GOLEMAGG_EARTHQUAKE) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(5s);
                        return;
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                case EVENT_GOLEMAGG_TRUST:
                {
                    if (DoCastSelf(SPELL_GOLEMAGG_TRUST) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(2s);
                        return;
                    }
                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                default:
                    break;
            }
        }
    }
};

struct classic_mob_core_rager : public ScriptedAI
{
    classic_mob_core_rager(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    EventMap m_CombatEvents;
    InstanceScript* m_pInstance;

    void Reset() override
    {
        m_CombatEvents.Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ScheduleCombatEvents();
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!m_pInstance)
            return;

        if (m_pInstance->GetData(CLASSIC_MC_TYPE_GOLEMAGG) == DONE)
            return;

        if (me->HealthBelowPctDamaged(50, damage))
        {
            damage = 0;
            ClassicScriptText(EMOTE_CORE_RAGER_LOWHP, me);
            // VMaNGOS casts Full Heal "on the victim"; the spell targets the caster (TARGET_UNIT_CASTER)
            if (DoCastSelf(SPELL_CORE_RAGER_FULL_HEAL) != SPELL_CAST_OK)
                me->SetFullHealth();
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        m_CombatEvents.Update(uiDiff);
        UpdateEvents();
    }

    void ScheduleCombatEvents()
    {
        m_CombatEvents.ScheduleEvent(EVENT_CORE_RAGER_MANGLE,      7s);
        m_CombatEvents.ScheduleEvent(EVENT_CORE_RAGER_CHECK_LEASH, 3s);
    }

    void UpdateEvents()
    {
        while (uint32 const eventId = m_CombatEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CORE_RAGER_MANGLE:
                {
                    if (DoCastVictim(SPELL_CORE_RAGER_MANGLE) == SPELL_CAST_OK)
                    {
                        m_CombatEvents.Repeat(10s);
                        return;
                    }

                    // Cast Failed: Try again in 1s
                    m_CombatEvents.Repeat(1s);
                    return;
                }
                case EVENT_CORE_RAGER_CHECK_LEASH:
                {
                    if (m_pInstance)
                    {
                        if (Creature* pGolemagg = me->GetMap()->GetCreature(m_pInstance->GetGuidData(CLASSIC_MC_DATA_GOLEMAGG)))
                        {
                            if (pGolemagg->IsAlive() && me->GetDistance2d(pGolemagg) > 100.f)
                            {
                                if (classic_boss_golemagg* pGolemaggAI = dynamic_cast<classic_boss_golemagg*>(pGolemagg->AI()))
                                    pGolemaggAI->EnterEvadeMode(EvadeReason::Other);
                            }
                        }
                    }

                    m_CombatEvents.Repeat(3s);
                    return;
                }
                default:
                    break;
            }
        }
    }
};

// 20556 - Golemagg's Trust
// TODO(classic): VMaNGOS hooks this as a periodic dummy; VMaNGOS spell data has EFFECT_0 = SPELL_AURA_PERIODIC_TRIGGER_SPELL
// (20553, 1s). Verify the 1.60 client data matches the hook below, and that 20553 needs no conditions rows.
class classic_spell_golemaggs_trust : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_GOLEMAGG_TRUST });
    }

    void HandlePeriodic(AuraEffect const* aurEff)
    {
        // The Core Rager buff is cast manually (explicit targets) instead of the default periodic trigger
        PreventDefaultAction();

        Unit* caster = GetCaster();
        if (!caster || !caster->IsAlive() || !caster->IsInCombat())
            return;

        // Golemagg's Core Ragers will deal increased damage
        // and have 50% increased attack speed if tanked too close to Golemagg.
        std::list<Creature*> addList;
        caster->GetCreatureListWithEntryInGrid(addList, CLASSIC_MC_NPC_CORE_RAGER, 30.0f);
        for (Creature* rager : addList)
            caster->CastSpell(rager, SPELL_GOLEMAGG_TRUST, CastSpellExtraArgs(aurEff));
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(classic_spell_golemaggs_trust::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

void AddSC_classic_boss_golemagg()
{
    RegisterCreatureAI(classic_boss_golemagg);
    RegisterCreatureAI(classic_mob_core_rager);
    RegisterSpellScript(classic_spell_golemaggs_trust);
}
