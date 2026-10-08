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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_grobbulus.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_grobbulus, spell_grobbulus_mutating_injection (28169), spell_grobbulus_cloud_poison (28241),
//          spell_grobbulus_mutagen_explosion (28206)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include <vector>

namespace
{
enum ClassicNaxxGrobbulusData : uint32
{
    SPELL_GROB_SLIME_STREAM       = 28137,
    SPELL_GROB_MUTATING_INJECTION = 28169,
    SPELL_GROB_SLIME_SPRAY        = 28157,
    SPELL_GROB_BERSERK            = 26662,
    SPELL_GROB_POISON_CLOUD       = 28240, // Summons a poison cloud npc
    SPELL_GROB_MUTAGEN_EXPLOSION  = 28206,
    SPELL_GROB_POISON_CLOUD_PASSIVE = 28158, // the visual poison cloud, triggers 28241 every second
    //SPELL_DISEASE_CLOUD         = 28362, // triggers ~300 dmg every 3 sec in 10yd radius, used by fallout slimes EventAI
    SPELL_GROB_BOMBARD_SLIME      = 28280, // todo: should spawn a slime at the room before patch every patroll round, if any are dead.

    NPC_GROB_FALLOUT_SLIME        = 16290,
    NPC_GROB_POISON_CLOUD         = 16363
};

enum ClassicNaxxGrobbulusEvents : uint32
{
    EVENT_GROB_MUTATING_INJECTION = 1,
    EVENT_GROB_POISON_CLOUD,
    EVENT_GROB_SLIME_SPRAY,
    EVENT_GROB_BERSERK
};

Milliseconds ClassicGrobPoisonCloudCd() { return 15s; } //return urand(20000, 25000); }
Milliseconds ClassicGrobSlimeSprayCd(bool initial) { return Milliseconds(initial ? urand(20000, 30000) : urand(30000, 35000)); }
constexpr Milliseconds CLASSIC_GROB_BERSERK_TIMER = 12min; // 12 minute enrage

constexpr uint32 CLASSIC_GROB_SLIMESTREAM_REPEAT_CD = 1500; // used every 1500ms if current target is out of melee range
}

struct classic_boss_grobbulus : public ScriptedAI
{
    classic_boss_grobbulus(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature)) { }

    classic_instance_naxxramas_InstanceScript* m_pInstance;
    uint32 m_uiSlimeStreamTimer = 5000;
    EventMap m_events;

    Milliseconds INJECTION_CD(bool initial)
    {
        // todo: its supposedly used more frequent after 30%. Need confirmation
        if (initial)
            return 12s;
        else
            if (me->GetHealthPct() > 30.0f)
                return Milliseconds(urand(7000, 13000));
            else
                return Milliseconds(urand(3000, 7000));
    }

    void Reset() override
    {
        m_events.Reset();
        m_uiSlimeStreamTimer = 5000; // allowing tank 5 sec to get to grobbulus on pull
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_GROBBULUS, IN_PROGRESS);

        m_events.ScheduleEvent(EVENT_GROB_MUTATING_INJECTION, INJECTION_CD(true));
        m_events.ScheduleEvent(EVENT_GROB_POISON_CLOUD, ClassicGrobPoisonCloudCd());
        m_events.ScheduleEvent(EVENT_GROB_SLIME_SPRAY, ClassicGrobSlimeSprayCd(true));
        m_events.ScheduleEvent(EVENT_GROB_BERSERK, CLASSIC_GROB_BERSERK_TIMER);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_GROBBULUS, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_GROBBULUS, FAIL);
    }

    // This custom selecting function, because we only want to select players without mutagen aura
    bool DoCastMutagenInjection()
    {
        if (me->IsNonMeleeSpellCast(true))
            return false;

        std::vector<Unit*> suitableTargets;
        for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
        {
            if (Player* pTarget = ref->GetVictim()->ToPlayer())
            {
                if (!pTarget->HasAura(SPELL_GROB_MUTATING_INJECTION))
                    suitableTargets.push_back(pTarget);
            }
        }

        if (suitableTargets.empty())
            return false;

        Unit* pTarget = suitableTargets[urand(0, suitableTargets.size() - 1)];
        if (DoCast(pTarget, SPELL_GROB_MUTATING_INJECTION) == SPELL_CAST_OK)
        {
            // DoScriptText(EMOTE_INJECTION, m_creature, pTarget);
            return true;
        }
        else
            return false;
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if ((spellInfo->Id == SPELL_GROB_SLIME_SPRAY) && target->GetTypeId() == TYPEID_PLAYER)
            if (Creature* pSlime = me->SummonCreature(NPC_GROB_FALLOUT_SLIME,
                target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 0.0f,
                TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 10s))
            {
                CreatureAI::DoZoneInCombat(pSlime);
            }
    }

    void UpdateSlimeStream(uint32 uiDiff)
    {
        if (me->IsWithinMeleeRange(me->GetVictim()))
            m_uiSlimeStreamTimer = CLASSIC_GROB_SLIMESTREAM_REPEAT_CD;
        else
        {
            if (m_uiSlimeStreamTimer < uiDiff)
            {
                if (DoCastSelf(SPELL_GROB_SLIME_STREAM) == SPELL_CAST_OK)
                    m_uiSlimeStreamTimer = CLASSIC_GROB_SLIMESTREAM_REPEAT_CD;
            }
            else
                m_uiSlimeStreamTimer -= uiDiff;
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        // Slime Stream if is cast if current target is not in melee range
        UpdateSlimeStream(uiDiff);

        m_events.Update(uiDiff);
        while (uint32 l_EventId = m_events.ExecuteEvent())
        {
            switch (l_EventId)
            {
                case EVENT_GROB_MUTATING_INJECTION:
                    if (DoCastMutagenInjection())
                        m_events.Repeat(INJECTION_CD(false));
                    else
                        m_events.Repeat(100ms);
                    break;
                case EVENT_GROB_POISON_CLOUD:
                    // todo: spell spawns the cloud slightly off center, make sure this is intended
                    if (DoCastSelf(SPELL_GROB_POISON_CLOUD) == SPELL_CAST_OK)
                        m_events.Repeat(ClassicGrobPoisonCloudCd());
                    else
                        m_events.Repeat(100ms);
                    break;
                case EVENT_GROB_SLIME_SPRAY:
                    if (DoCastVictim(SPELL_GROB_SLIME_SPRAY) == SPELL_CAST_OK)
                    {
                        m_events.Repeat(ClassicGrobSlimeSprayCd(false));
                        //DoScriptText(EMOTE_SPRAY_SLIME, m_creature);
                    }
                    else
                        m_events.Repeat(100ms);
                    break;
                case EVENT_GROB_BERSERK:
                    if (DoCastSelf(SPELL_GROB_BERSERK) != SPELL_CAST_OK)
                        m_events.Repeat(100ms);
                    break;
                default:
                    break;
            }
        }
    }
};

// 28169 - Mutating Injection (Grobbulus)
class classic_spell_grobbulus_mutating_injection : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_GROB_MUTAGEN_EXPLOSION, SPELL_GROB_POISON_CLOUD });
    }

    void HandleRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        if (Unit* pCaster = GetCaster())
        {
            if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_ENEMY_SPELL)
            {
                // Mutagen Explosion (dispelled: no triggering aura -> reduced damage, see classic_spell_grobbulus_mutagen_explosion)
                pCaster->CastSpell(target, SPELL_GROB_MUTAGEN_EXPLOSION, true);
            }
            else
            {
                // Mutagen Explosion (expired: triggered by the aura -> full damage)
                pCaster->CastSpell(target, SPELL_GROB_MUTAGEN_EXPLOSION, CastSpellExtraArgs(aurEff));
            }
        }

        // Poison Cloud
        target->CastSpell(target, SPELL_GROB_POISON_CLOUD, CastSpellExtraArgs(aurEff));
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(classic_spell_grobbulus_mutating_injection::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 28241 - Poison (Grobbulus Cloud)
class classic_spell_grobbulus_cloud_poison : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        // Spell states 30yd radius, which you would think is the max radius once its all grown,
        // however, the visual of the spell goes no further than ~20yd, so lets stop it there.
        // It will instantly get a 2(?) yd radius, and grow to 20 from there
        Unit* caster = GetCaster();
        if (!caster)
            return;

        Aura const* auraHolder = caster->GetAura(SPELL_GROB_POISON_CLOUD_PASSIVE);
        if (!auraHolder)
            return;

        int32 const maxDur = auraHolder->GetMaxDuration();
        if (maxDur <= 0)
            return;

        int32 const currTick = maxDur - auraHolder->GetDuration();
        float const radius = 18.0f / maxDur * currTick + 2;
        //radius = 0.5f * (60000 - auraHolder->GetAuraDuration()) * 0.001f;

        targets.remove_if([caster, radius](WorldObject* target)
        {
            return !caster->IsWithinDist(target, radius);
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_grobbulus_cloud_poison::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// 28206 - Mutagen Explosion (Grobbulus)
class classic_spell_grobbulus_mutagen_explosion : public SpellScript
{
    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        // All sources say the explosion should do around 4.5k physical dmg if it runs out,
        // but "less" if dispelled. I have been able to find different variations of this spell,
        // so the hack has become to set the triggering aura when casting this spell
        // when 28169 expires, and NOT set it when 28169 is dispelled.
        if (GetTriggeringSpell())
            SetHitDamage(int32(GetHitDamage() * 1.5f));
        else
            SetHitDamage(int32(GetHitDamage() / 1.5f));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(classic_spell_grobbulus_mutagen_explosion::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

void AddSC_classic_boss_grobbulus()
{
    RegisterCreatureAI(classic_boss_grobbulus);
    RegisterSpellScript(classic_spell_grobbulus_mutating_injection);
    RegisterSpellScript(classic_spell_grobbulus_cloud_poison);
    RegisterSpellScript(classic_spell_grobbulus_mutagen_explosion);
}
