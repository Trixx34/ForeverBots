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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_loatheb.cpp (GPL-2)
// Scripts: boss_loatheb, mob_rotting_maggot, mob_diseased_maggot, mob_eye_stalk, spell_loatheb_corrupted_mind_aoe

#include "ScriptMgr.h"
#include "Creature.h"
#include "GridDefines.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include <algorithm>
#include <limits>
#include <list>
#include <vector>

namespace
{
enum ClassicNaxxLoathebData : uint32
{
    SPELL_LOATHEB_CORRUPTED_MIND  = 29201, // this triggers the following spells on targets (based on class): 29185, 29194, 29196, 29198
    SPELL_LOATHEB_POISON_AURA     = 29865,
    SPELL_LOATHEB_INEVITABLE_DOOM = 29204,
    SPELL_LOATHEB_REMOVE_CURSE    = 30281, // He periodically removes all curses on himself
    SPELL_LOATHEB_FUNGAL_BLOOM    = 29232, // Cast by spores

    SPELL_LOATHEB_CM_DRUID        = 29194,
    SPELL_LOATHEB_CM_PALADIN      = 29196,
    SPELL_LOATHEB_CM_SHAMAN       = 29198,

    SPELL_NAXX_RETCHING_PLAGUE    = 30079,
    SPELL_NAXX_EYE_STALK_MIND_FLAY = 29407,
    SPELL_NAXX_EYE_STALK_SUBMERGE = 26234,

    NPC_LOATHEB_SPORE             = 16286
};

enum ClassicNaxxLoathebEvents : uint32
{
    EVENT_LOATHEB_SUMMON_SPORE = 1,
    EVENT_LOATHEB_CORRUPTED_MIND,
    EVENT_LOATHEB_POISON_AURA,
    EVENT_LOATHEB_INEVITABLE_DOOM,
    EVENT_LOATHEB_REMOVE_CURSE
};

// Can't really see much of a system in where the spores spawn.
// In guides it say "oposite side of where the majority of the raid stands".
// Unless this is a snapshot check by the boss on pull, which it dosent seem to be based on videos,
// it simply seems like one of 2 (potentially 4) location is chosed at random on pull, after that it's
// constantly spawning there throughout the fight.
// Presumably spell 29234 was used, it has a radius of 70yd. Can't figure out exactly how it would have been used though.
constexpr float NaxxLoathebSporeLocs[2][3] =
{
    {2951.0f, -4016.0f, 274.0f},
    {2870.0f, -3978.0f, 274.0f}
};

constexpr uint8 NAXX_LOATHEB_MAX_STALKS_UP = 6;
struct NaxxLoathebEyeStalkInfo
{
    enum eState
    {
        COOLDOWN,
        UP
    };
    eState currentState = COOLDOWN;
    uint32 timer = 0;
    ObjectGuid guid;
    uint8 myIndex = 0;
};

// VMaNGOS mob_rottingMaggotAI (shared by mob_rotting_maggot / mob_diseased_maggot)
struct classic_naxx_maggot_baseAI : public ScriptedAI
{
    classic_naxx_maggot_baseAI(Creature* creature, bool diseased) : ScriptedAI(creature), isDiseased(diseased)
    {
        me->SetNoCallAssistance(true);
    }

    bool const isDiseased;
    Position aggroPossition;

    void Reset() override { }

    void MoveInLineOfSight(Unit* who) override
    {
        // Custom, tiny aggro radius
        if (!me->IsWithinDistInMap(who, 1.5f))
            return;

        if (!me->HasReactState(REACT_PASSIVE) && me->CanStartAttack(who, true) && me->IsValidAttackTarget(who) && me->IsHostileTo(who))
        {
            if (who->isInAccessiblePlaceFor(me) && me->IsWithinLOSInMap(who))
            {
                me->SetNoCallAssistance(true);

                if (!me->GetVictim())
                    AttackStart(who);
                else if (me->GetMap()->IsDungeon())
                {
                    who->SetInCombatWith(me);
                    AddThreat(who, 0.0f);
                }
            }
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->SetNoCallAssistance(true);
        aggroPossition = me->GetPosition();
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        if (isDiseased)
        {
            if (!me->HasAura(SPELL_NAXX_RETCHING_PLAGUE))
                me->CastSpell(me, SPELL_NAXX_RETCHING_PLAGUE, true);
        }

        if (me->GetDistance(aggroPossition) > 40.0f)
            EnterEvadeMode();
    }
};
}

struct classic_mob_rotting_maggot : public classic_naxx_maggot_baseAI
{
    classic_mob_rotting_maggot(Creature* creature) : classic_naxx_maggot_baseAI(creature, false) { }
};

struct classic_mob_diseased_maggot : public classic_naxx_maggot_baseAI
{
    classic_mob_diseased_maggot(Creature* creature) : classic_naxx_maggot_baseAI(creature, true) { }
};

struct classic_mob_eye_stalk : public ScriptedAI
{
    classic_mob_eye_stalk(Creature* creature) : ScriptedAI(creature)
    {
        me->SetNoCallAssistance(true);
        SetCombatMovement(false);
        timeSinceSpawn = 0;
        haveSubmerged = false;
        haveCastSubmerge = false;
    }

    uint32 timeSinceSpawn;
    bool haveSubmerged;
    bool haveCastSubmerge;

    void Reset() override
    {
        me->StopMoving();
        me->SetControlled(true, UNIT_STATE_ROOT);
        me->SetNoCallAssistance(true);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (timeSinceSpawn < 3000)
            return;

        if (!me->IsWithinDistInMap(who, 19.0f))
            return;

        if (!me->HasReactState(REACT_PASSIVE) && me->CanStartAttack(who, true) && me->IsValidAttackTarget(who) && me->IsHostileTo(who))
        {
            if (who->isInAccessiblePlaceFor(me) && me->IsWithinLOSInMap(who))
            {
                me->SetNoCallAssistance(true);
                if (!me->GetVictim())
                    AttackStart(who);
                else if (me->GetMap()->IsDungeon())
                {
                    who->SetInCombatWith(me);
                    AddThreat(who, 0.0f);
                }
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        me->SetNoCallAssistance(true);
        timeSinceSpawn += std::min(diff, std::numeric_limits<uint32>::max() - timeSinceSpawn);

        if (haveSubmerged)
        {
            if (!haveCastSubmerge)
            {
                haveCastSubmerge = true;
                me->CastSpell(me, SPELL_NAXX_EYE_STALK_SUBMERGE, false);
            }
            return;
        }

        if (!UpdateVictim())
            return;

        if (!me->IsNonMeleeSpellCast(false))
        {
            if (me->GetDistance(me->GetVictim()) < 35.0f)
                DoCastVictim(SPELL_NAXX_EYE_STALK_MIND_FLAY);
            else
                DoStopAttack();
        }
    }
};

struct classic_boss_loatheb : public ScriptedAI
{
    classic_boss_loatheb(Creature* creature) : ScriptedAI(creature)
    {
        int randLoc = urand(0, 1);
        for (int i = 0; i < 3; i++)
            SporeLoc[i] = NaxxLoathebSporeLocs[randLoc][i];

        m_pInstance = GetClassicNaxxInstance(creature);
        numDooms = 0;

        for (auto& eyeStalk : eyeStalks)
        {
            eyeStalk.currentState = NaxxLoathebEyeStalkInfo::COOLDOWN;
            eyeStalk.timer = urand(0, 10000);
            eyeStalk.guid.Clear();
        }
        availableEyeLocs.clear();
        for (uint8 i = 0; i < max_stalks; i++)
            availableEyeLocs.push_back(i);
    }

    classic_instance_naxxramas_InstanceScript* m_pInstance;

    EventMap events;
    uint32 numDooms;
    float SporeLoc[3];

    std::vector<uint8> availableEyeLocs;
    NaxxLoathebEyeStalkInfo eyeStalks[NAXX_LOATHEB_MAX_STALKS_UP];
    GuidSet m_sporeGuids;

    void Reset() override
    {
        events.Reset();
        numDooms = 0;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        numDooms = 0;
        events.ScheduleEvent(EVENT_LOATHEB_SUMMON_SPORE,    13s);
        events.ScheduleEvent(EVENT_LOATHEB_CORRUPTED_MIND,  5s);
        events.ScheduleEvent(EVENT_LOATHEB_POISON_AURA,     5s);
        events.ScheduleEvent(EVENT_LOATHEB_INEVITABLE_DOOM, 2min);
        events.ScheduleEvent(EVENT_LOATHEB_REMOVE_CURSE,    5s);
        if (m_pInstance)
            m_pInstance->SetData(TYPE_LOATHEB, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_LOATHEB, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_LOATHEB, FAIL);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // despawn all spores on wipe
        for (ObjectGuid const& guid : m_sporeGuids)
        {
            if (Creature* pSpore = me->GetMap()->GetCreature(guid))
                pSpore->DespawnOrUnsummon();
        }
        m_sporeGuids.clear();

        // remove fungal bloom buff on wipe
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, MAX_VISIBILITY_DISTANCE, true);
        for (Player* pPlayer : players)
            pPlayer->RemoveAurasDueToSpell(SPELL_LOATHEB_FUNGAL_BLOOM);

        ScriptedAI::EnterEvadeMode(why);
    }

    /*
    10 stalks
    rand 10-20 up
    rand 10-20 down
    avg 15 up, avg 15 down
    avg 5 up at any given time
    when killed, down time becomes rand 10-20 + additional 10 seconds

    we want an eyestalk to submerge and a new one to pop up every 4sec on avg.
    That means we need all 20 to have an average cd of 80sec.
    We also want, when no stalks are killed, on average
    when an eyestalk comes off coldown, it forces the oldest alive eyestalk to die, then summons itself. The dead eye stalk gets a 60-90sec cooldown
    with 20 stalks, and an avg cd of 75sec, this means one eye stalk switches with another one every 3.75 seconds on avg.
    */
    // NOTE(classic): as in VMaNGOS this runs from Loatheb's UpdateAI (in and out of combat) and uses the shared eye stalk
    // positions from naxxramas.h (Heigan's room).
    void WhackAStalk(uint32 diff)
    {
        for (auto& eyeStalk : eyeStalks)
        {
            if (eyeStalk.timer >= diff)
                eyeStalk.timer -= diff;

            switch (eyeStalk.currentState)
            {
                case NaxxLoathebEyeStalkInfo::COOLDOWN:
                {
                    // Summoning a new eye
                    if (eyeStalk.timer < diff)
                    {
                        if (availableEyeLocs.empty())
                        {
                            TC_LOG_ERROR("scripts", "classic_boss_loatheb - availableEyeLocs size 0, should not happen!");
                            return;
                        }
                        uint8 availableIndex = urand(0, uint32(availableEyeLocs.size()) - 1);
                        uint8 newEyeIdx = availableEyeLocs[availableIndex];
                        availableEyeLocs.erase(availableEyeLocs.begin() + availableIndex);

                        eyeStalk.myIndex = newEyeIdx;
                        float const* pos = eyeStalkPossitions[newEyeIdx];

                        Creature* pStalk = me->SummonCreature(NPC_EyeStalk, pos[0], pos[1], pos[2], pos[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5000ms);
                        if (!pStalk)
                        {
                            TC_LOG_ERROR("scripts", "classic_boss_loatheb WhackAStalk failed to summon eye stalk");
                            return;
                        }
                        eyeStalk.guid = pStalk->GetGUID();
                        eyeStalk.currentState = NaxxLoathebEyeStalkInfo::UP;
                        eyeStalk.timer = urand(15000, 20000);
                    }
                    break;
                }
                case NaxxLoathebEyeStalkInfo::UP:
                    // Initiating unsummon
                    if (eyeStalk.timer < diff)
                    {
                        if (Creature* pCreature = me->GetMap()->GetCreature(eyeStalk.guid))
                        {
                            // If the eye is currently channeling mind flay we wait with unsummoning it
                            if (!pCreature->IsNonMeleeSpellCast(false))
                            {
                                if (classic_mob_eye_stalk* ai = dynamic_cast<classic_mob_eye_stalk*>(pCreature->AI()))
                                {
                                    if (!ai->haveSubmerged)
                                    {
                                        ai->haveSubmerged = true;
                                        if (TempSummon* ts = pCreature->ToTempSummon())
                                            ts->UnSummon(1100);
                                    }
                                }
                            }
                        }
                    }
                    break;
            }
        }
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_EyeStalk)
        {
            for (auto& eyeStalk : eyeStalks)
            {
                if (eyeStalk.guid == summon->GetGUID())
                {
                    // if currentState already is COOLDOWN it means it was killed
                    if (eyeStalk.currentState != NaxxLoathebEyeStalkInfo::COOLDOWN)
                    {
                        eyeStalk.currentState = NaxxLoathebEyeStalkInfo::COOLDOWN;
                        eyeStalk.timer = urand(1000, 5000);
                    }

                    eyeStalk.guid.Clear();
                    availableEyeLocs.push_back(eyeStalk.myIndex);
                    break;
                }
            }
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_EyeStalk)
        {
            // was killed, so it receives an additional 10 seconds cooldown
            for (auto& eyeStalk : eyeStalks)
            {
                if (eyeStalk.guid == summon->GetGUID())
                {
                    eyeStalk.currentState = NaxxLoathebEyeStalkInfo::COOLDOWN;
                    eyeStalk.timer = urand(1000, 5000) + 20000;
                    break;
                }
            }
        }
    }

    /* Loatheb does not automatically remove Vampiric Embrace before TBC (VMaNGOS keeps the 15286 SpellHit handler commented out). */

    void UpdateAI(uint32 diff) override
    {
        WhackAStalk(diff);

        if (!UpdateVictim())
            return;

        // VMaNGOS dereferences m_pInstance unconditionally here
        if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        events.Update(diff);
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_LOATHEB_SUMMON_SPORE:
                    if (Creature* pSpore = me->SummonCreature(NPC_LOATHEB_SPORE, SporeLoc[0], SporeLoc[1], SporeLoc[2], 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5000ms))
                    {
                        m_sporeGuids.insert(pSpore->GetGUID());
                        if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                            pSpore->GetThreatManager().AddThreat(pTarget, 0.0f);
                    }
                    events.Repeat(13s);
                    break;
                case EVENT_LOATHEB_CORRUPTED_MIND:
                    // https://www.youtube.com/watch?v=a0z9qjLxD98&list=PLYsWP02PY54A3RkEJv_VaT-0ZhfMs5zxN&index=4
                    // shows it refreshing every ~10 sec
                    if (DoCastSelf(SPELL_LOATHEB_CORRUPTED_MIND) == SPELL_CAST_OK)
                        events.Repeat(10s);
                    else
                        events.Repeat(100ms);
                    break;
                case EVENT_LOATHEB_POISON_AURA:
                    if (DoCastSelf(SPELL_LOATHEB_POISON_AURA) == SPELL_CAST_OK)
                        events.Repeat(12s);
                    else
                        events.Repeat(100ms);
                    break;
                case EVENT_LOATHEB_INEVITABLE_DOOM:
                    if (DoCastSelf(SPELL_LOATHEB_INEVITABLE_DOOM) == SPELL_CAST_OK)
                    {
                        ++numDooms;
                        // 2, 2:30, 3, 3:30, 4, 4:30, 5
                        // after 7 dooms, or 5 minutes into the fight,
                        // the doom timer becomes 15 instead of 30 seconds.
                        if (numDooms > 6)
                            events.Repeat(15s);
                        else
                            events.Repeat(30s);
                    }
                    else
                        events.Repeat(100ms);
                    break;
                case EVENT_LOATHEB_REMOVE_CURSE:
                    if (DoCastSelf(SPELL_LOATHEB_REMOVE_CURSE) == SPELL_CAST_OK)
                        events.Repeat(30s);
                    else
                        events.Repeat(100ms);
                    break;
                default:
                    break;
            }
        }
    }
};

// 29201 - Corrupted Mind (Loatheb)
// VMaNGOS OnEffectExecute: returning false skips the default effect handler, true runs it.
class classic_spell_loatheb_corrupted_mind_aoe : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LOATHEB_CM_DRUID, SPELL_LOATHEB_CM_PALADIN, SPELL_LOATHEB_CM_SHAMAN });
    }

    void HandleEffect(SpellEffIndex effIndex)
    {
        Unit* target = GetHitUnit();
        if (!target)
            return;

        // Loatheb Corrupted Mind triggered sub spells
        uint32 spellid;
        switch (target->GetClass())
        {
            // priests should be getting 29185, but it triggers on dmg effects as well, don't know why.
            // stealing druid version for priests until anyone has a reason priests cant smite.s
            case CLASS_PRIEST:  spellid = SPELL_LOATHEB_CM_DRUID; break; //29185; break;
            case CLASS_DRUID:   spellid = SPELL_LOATHEB_CM_DRUID; break;
            case CLASS_PALADIN: spellid = SPELL_LOATHEB_CM_PALADIN; break;
            case CLASS_SHAMAN:  spellid = SPELL_LOATHEB_CM_SHAMAN; break;
            default:
                PreventHitDefaultEffect(effIndex);
                return;
        }

        if (Unit* caster = GetCaster())
            caster->CastSpell(target, spellid, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(classic_spell_loatheb_corrupted_mind_aoe::HandleEffect, EFFECT_0, SPELL_EFFECT_ANY);
    }
};

void AddSC_classic_boss_loatheb()
{
    RegisterCreatureAI(classic_boss_loatheb);
    RegisterCreatureAI(classic_mob_rotting_maggot);
    RegisterCreatureAI(classic_mob_diseased_maggot);
    RegisterCreatureAI(classic_mob_eye_stalk);
    RegisterSpellScript(classic_spell_loatheb_corrupted_mind_aoe);
}
