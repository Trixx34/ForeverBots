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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_four_horsemen.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_lady_blaumeux, boss_highlord_mograine, boss_thane_korthazz, boss_sir_zeliek
// (each script is used by the horseman and by its spirit: 16776, 16775, 16778, 16777)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include "Log.h"

namespace
{
enum ClassicNaxxFourHorsemenData : uint32
{
    // All horsemen
    SPELL_4HM_SHIELDWALL         = 29061,
    SPELL_4HM_BESERK             = 26662,
    SPELL_4HM_MARK               = 28836,
    SPELL_4HM_SUMMON_PLAYER      = 25104,

    // Lady Blaumeux
    SAY_BLAU_AGGRO           = 13010,
    SAY_BLAU_TAUNT1          = 13014,
    SAY_BLAU_TAUNT2          = 13015,
    // SAY_BLAU_TAUNT3          = 13016, // randomly called by instance script
    SAY_BLAU_SPECIAL         = 13013,
    SAY_BLAU_SLAY            = 13012,
    SAY_BLAU_DEATH           = 13011,

    SPELL_MARK_OF_BLAUMEUX   = 28833,
    SPELL_SPIRIT_OF_BLAUMEUX = 28931,
    SPELL_4HM_VOIDZONE       = 28863,
    NPC_4HM_VOID_ZONE        = 16697,

    // Highlord Mograine
    SAY_MOG_AGGRO1         = 13051,
    SAY_MOG_AGGRO2         = 13052,
    SAY_MOG_AGGRO3         = 13053,
    SAY_MOG_SLAY1          = 13055,
    SAY_MOG_SLAY2          = 13056,
    SAY_MOG_SPECIAL        = 13057,
    SAY_MOG_TAUNT1         = 13058,
    SAY_MOG_TAUNT2         = 13059,
    // SAY_MOG_TAUNT3         = 13060, // randomly called by instance script
    SAY_MOG_DEATH          = 13054,

    SPELL_MARK_OF_MOGRAINE   = 28834,
    SPELL_SPIRIT_OF_MOGRAINE = 28928,
    SPELL_RIGHTEOUS_FIRE     = 28881, // Trigger 28882
    SPELL_RIGHTEOUS_FIRE_DMG = 28882,

    // Thane Korthazz
    SAY_KORT_AGGRO          = 13034,
    // SAY_KORT_TAUNT1         = 13038, // randomly called by instance script
    SAY_KORT_TAUNT2         = 13039,
    SAY_KORT_TAUNT3         = 13040,
    SAY_KORT_SPECIAL        = 13037,
    SAY_KORT_SLAY           = 13036,
    SAY_KORT_DEATH          = 13035,

    SPELL_MARK_OF_KORTHAZZ   = 28832,
    SPELL_SPIRIT_OF_KORTHAZZ = 28932,
    SPELL_4HM_METEOR         = 28884, // wowhead dmg amount suggests spell 26558, but 28884 makes way more sense due to the id range

    // Sir Zeliek
    SAY_ZELI_AGGRO          = 13097,

    // SAY_ZELI_TAUNT1         = 13101, // called by instance script after gothik kill
    // SAY_ZELI_TAUNT2         = 13102, // called by instance script after gothik kill
    // SAY_ZELI_TAUNT3         = 13103, // randomly called by instance script

    SAY_ZELI_SPECIAL        = 13100,
    SAY_ZELI_SLAY           = 13099,
    SAY_ZELI_DEATH          = 13098,

    SPELL_MARK_OF_ZELIEK     = 28835,
    SPELL_SPIRIT_OF_ZELIEK   = 28934,
    SPELL_4HM_HOLY_WRATH     = 28883,

    // horseman spirits
    NPC_SPIRIT_OF_BLAUMEUX    = 16776,
    NPC_SPIRIT_OF_MOGRAINE    = 16775,
    NPC_SPIRIT_OF_KORTHAZZ    = 16778,
    NPC_SPIRIT_OF_ZELIEK      = 16777
};

enum ClassicNaxxFourHorsemenEvents : uint32
{
    EVENT_4HM_AGGRO_TEXT = 1,

    EVENT_4HM_BOSS_ABILITY,
};

// VMaNGOS CanInitiateAttack() && IsTargetableBy() && IsHostileTo() && IsInAccessablePlaceFor() && IsWithinLOSInMap()
bool ClassicNaxx4hmCanAggro(Creature* me, Unit* who)
{
    return me->HasReactState(REACT_AGGRESSIVE) && !me->IsInEvadeMode() && me->IsValidAttackTarget(who) && me->IsHostileTo(who)
        && who->isInAccessiblePlaceFor(me) && me->IsWithinLOSInMap(who);
}
}

struct classic_boss_four_horsemen_shared : public ScriptedAI
{
    classic_instance_naxxramas_InstanceScript* m_pInstance;
    bool m_bShieldWall1 = true;
    bool m_bShieldWall2 = true;
    uint32 m_uiMarkTimer = 20000;
    uint32 m_uiShieldWallTimer = 0;
    uint32 const m_uiMarkId;
    uint32 const m_uiGhostId;
    bool const m_bIsSpirit;
    uint32 pullCheckTimer = 1000;
    EventMap m_events;
    uint32 killSayCooldown = 0;

    classic_boss_four_horsemen_shared(Creature* creature, uint32 uiMarkId, uint32 uiGhostId) :
        ScriptedAI(creature),
        m_pInstance(GetClassicNaxxInstance(creature)),
        m_uiMarkId(uiMarkId),
        m_uiGhostId(uiGhostId),
        m_bIsSpirit(
            creature->GetEntry() == NPC_SPIRIT_OF_BLAUMEUX
            || creature->GetEntry() == NPC_SPIRIT_OF_MOGRAINE
            || creature->GetEntry() == NPC_SPIRIT_OF_KORTHAZZ
            || creature->GetEntry() == NPC_SPIRIT_OF_ZELIEK)
    {
        if (!m_pInstance && creature->GetMapId() == MAP_NAXXRAMAS)
            TC_LOG_ERROR("scripts", "classic_boss_four_horsemen_shared ctor could not get instance data");

        if (m_bIsSpirit)
            SetCombatMovement(false);
    }

    // Parent horseman of a spirit (used to copy the mark timer)
    Creature* GetHorseman(uint32 entry) const
    {
        return m_pInstance ? m_pInstance->GetSingleCreatureFromStorage(entry) : nullptr;
    }

    void AggroRadius(uint32 diff)
    {
        // He is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS || !m_pInstance)
            return;

        if (m_pInstance->GetData(TYPE_FOUR_HORSEMEN) != NOT_STARTED && m_pInstance->GetData(TYPE_FOUR_HORSEMEN) != FAIL)
            return;

        if (pullCheckTimer < diff)
        {
            pullCheckTimer = 1000;
        }
        else
        {
            pullCheckTimer -= diff;
            return;
        }

        // Large aggro radius
        for (MapReference const& itr : me->GetMap()->GetPlayers())
        {
            Player* pPlayer = itr.GetSource();
            if (!pPlayer)
                continue;

            if (me->GetExactDist(pPlayer) > 74.0f)
                continue;

            if (!me->CanSeeOrDetect(pPlayer))
                return;

            if (ClassicNaxx4hmCanAggro(me, pPlayer))
            {
                if (!me->GetVictim())
                {
                    AttackStart(pPlayer);
                    return;
                }
                else if (me->GetMap()->IsDungeon())
                {
                    me->EngageWithTarget(pPlayer);
                    return;
                }
            }
        }
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // He is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS)
            return;

        if (!me->IsWithinDistInMap(pWho, 75.0f))
            return;

        if (ClassicNaxx4hmCanAggro(me, pWho))
        {
            if (!me->GetVictim())
                AttackStart(pWho);
            else if (me->GetMap()->IsDungeon())
                me->EngageWithTarget(pWho);
        }
    }

    void AttackStart(Unit* pWho) override
    {
        if (!m_bIsSpirit)
            ScriptedAI::AttackStart(pWho);
    }

    void Reset() override
    {
        // Mograine is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS)
            return;

        pullCheckTimer = 1000;
        m_events.Reset();

        m_bShieldWall1 = true;
        m_bShieldWall2 = true;
        m_uiShieldWallTimer = 0;
        m_uiMarkTimer = 20000;
        killSayCooldown = 0;

        if (m_bIsSpirit)
        {
            me->SetControlled(true, UNIT_STATE_ROOT);
            if (me->IsInWorld())
                DoZoneInCombat();
        }
        else
        {
            Creature* pSpirit = nullptr;
            switch (me->GetEntry())
            {
                case NPC_BLAUMEUX:
                    pSpirit = GetClosestCreatureWithEntry(me, NPC_SPIRIT_OF_BLAUMEUX, 300.0f);
                    break;
                case NPC_MOGRAINE:
                    pSpirit = GetClosestCreatureWithEntry(me, NPC_SPIRIT_OF_MOGRAINE, 300.0f);
                    break;
                case NPC_THANE:
                    pSpirit = GetClosestCreatureWithEntry(me, NPC_SPIRIT_OF_KORTHAZZ, 300.0f);
                    break;
                case NPC_ZELIEK:
                    pSpirit = GetClosestCreatureWithEntry(me, NPC_SPIRIT_OF_ZELIEK, 300.0f);
                    break;
            }
            // despawn the spirit on reset
            if (pSpirit)
                pSpirit->DespawnOrUnsummon();
        }
    }

    // TC: a spirit is summoned before it is in the world, so its zone-in-combat from Reset() is repeated here
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        if (m_bIsSpirit && me->GetMapId() == MAP_NAXXRAMAS)
        {
            me->SetControlled(true, UNIT_STATE_ROOT);
            DoZoneInCombat();
        }
    }

    // VMaNGOS Aggro()
    void JustEngagedWith(Unit* pWho) override
    {
        // Mograine is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS || !m_pInstance)
            return;

        if (m_pInstance->GetData(TYPE_FOUR_HORSEMEN) == IN_PROGRESS)
            return;

        if (me->GetEntry() != NPC_THANE)
            if (Creature* pC = m_pInstance->GetSingleCreatureFromStorage(NPC_THANE))
                pC->AI()->AttackStart(pWho);
        if (me->GetEntry() != NPC_MOGRAINE)
            if (Creature* pC = m_pInstance->GetSingleCreatureFromStorage(NPC_MOGRAINE))
                pC->AI()->AttackStart(pWho);
        if (me->GetEntry() != NPC_ZELIEK)
            if (Creature* pC = m_pInstance->GetSingleCreatureFromStorage(NPC_ZELIEK))
                pC->AI()->AttackStart(pWho);
        if (me->GetEntry() != NPC_BLAUMEUX)
            if (Creature* pC = m_pInstance->GetSingleCreatureFromStorage(NPC_BLAUMEUX))
                pC->AI()->AttackStart(pWho);

        m_pInstance->SetData(TYPE_FOUR_HORSEMEN, IN_PROGRESS);
    }

    void JustReachedHome() override
    {
        if (m_pInstance && me->GetMapId() == MAP_NAXXRAMAS)
            m_pInstance->SetData(TYPE_FOUR_HORSEMEN, FAIL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_uiGhostId)
            DoCastSelf(m_uiGhostId, true);

        if (m_pInstance && me->GetMapId() == MAP_NAXXRAMAS)
            m_pInstance->SetData(TYPE_FOUR_HORSEMEN, SPECIAL);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        // TODO: find if hitten by mark target are the only ones to drop 50% aggro
        if (spellInfo->Id == m_uiMarkId && target)
        {
            Unit* pTarget = target->ToUnit();
            if (!pTarget)
                return;

            Aura* holder = pTarget->GetAura(m_uiMarkId);
            if (!holder || holder->GetStackAmount() <= 1)
                return;

            int32 damage;
            switch (holder->GetStackAmount())
            {
                case 2:
                    damage = 250;
                    break;
                case 3:
                    damage = 1000;
                    break;
                case 4:
                    damage = 3000;
                    break;
                default:
                    damage = 1000 * holder->GetStackAmount();
                    break;
            }

            me->CastSpell(pTarget, SPELL_4HM_MARK, CastSpellExtraArgs(TRIGGERED_FULL_MASK).AddSpellBP0(damage));
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        // He is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS)
            return;

        if (!m_bIsSpirit)
        {
            if (Unit* pVictim = me->GetVictim())
            {
                if (!me->IsWithinDistInMap(pVictim, VISIBILITY_DISTANCE_NORMAL))
                    DoCast(pVictim, SPELL_4HM_SUMMON_PLAYER, true);
            }
        }

        m_events.Update(uiDiff);
        killSayCooldown -= std::min(killSayCooldown, uiDiff);

        // Shield Wall - All 4 horsemen will shield wall at 50% hp and 20% hp for 20 seconds
        if (m_bShieldWall1 && me->GetHealthPct() < 50.0f)
        {
            if (DoCastSelf(SPELL_4HM_SHIELDWALL) == SPELL_CAST_OK)
            {
                m_bShieldWall1 = false;
                m_uiShieldWallTimer = 0;
            }
        }
        else if (m_bShieldWall2 && me->GetHealthPct() < 20.0f)
        {
            if (m_uiShieldWallTimer < 30000) // If a Horseman is taken from 50% to 20% health in less than 30 seconds, the second Shield Wall will never trigger.
            {
                m_bShieldWall2 = false;
            }
            else if (DoCastSelf(SPELL_4HM_SHIELDWALL) == SPELL_CAST_OK)
                m_bShieldWall2 = false;
        }
        m_uiShieldWallTimer += uiDiff;

        if (m_uiMarkTimer < uiDiff)
        {
            if (DoCastSelf(m_uiMarkId) == SPELL_CAST_OK)
            {
                m_uiMarkTimer = 12000;
                //todo: this behavior should get some more confirmation
                for (ThreatReference* ref : me->GetThreatManager().GetModifiableThreatList())
                {
                    if (ref->GetThreat())
                        ref->ModifyThreatByPercent(-50);
                }
            }
        }
        else
            m_uiMarkTimer -= uiDiff;
    }
};

struct classic_boss_lady_blaumeux : public classic_boss_four_horsemen_shared
{
    classic_boss_lady_blaumeux(Creature* creature) : classic_boss_four_horsemen_shared(creature, SPELL_MARK_OF_BLAUMEUX, SPELL_SPIRIT_OF_BLAUMEUX) { }

    void Reset() override
    {
        classic_boss_four_horsemen_shared::Reset();
        if (m_bIsSpirit)
        {
            if (Creature* pC = GetHorseman(NPC_BLAUMEUX))
                if (classic_boss_four_horsemen_shared* ai = dynamic_cast<classic_boss_four_horsemen_shared*>(pC->AI()))
                    m_uiMarkTimer = ai->m_uiMarkTimer;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustEngagedWith(who);
        ClassicScriptText(SAY_BLAU_AGGRO, me);

        m_events.ScheduleEvent(EVENT_4HM_BOSS_ABILITY, 12s);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        // Not sure about that
        if (m_bIsSpirit)
            return;

        if (!killSayCooldown)
        {
            ClassicScriptText(SAY_BLAU_SLAY, me);
            killSayCooldown = 5000;
        }
    }

    void JustDied(Unit* killer) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustDied(killer);
        ClassicScriptText(SAY_BLAU_DEATH, me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        AggroRadius(uiDiff);
        if (!m_bIsSpirit && !UpdateVictim())
            return;
        if (!m_bIsSpirit && m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;
        classic_boss_four_horsemen_shared::UpdateAI(uiDiff);

        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_4HM_AGGRO_TEXT:
                ClassicScriptText(SAY_KORT_AGGRO, me);
                break;
            case EVENT_4HM_BOSS_ABILITY:
                if (m_bIsSpirit)
                    break;

                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u)
                    {
                        return u->GetTypeId() == TYPEID_PLAYER && me->IsWithinLOSInMap(u);
                    }))
                {
                    float height = me->GetMap()->GetHeight(me->GetPhaseShift(), pTarget->GetPositionX(), pTarget->GetPositionY(), pTarget->GetPositionZ(), true, 5.0f) + 0.2f;
                    if (height < 241.35f)
                        height = 241.35f;
                    if (Creature* pVZ = me->SummonCreature(NPC_4HM_VOID_ZONE, pTarget->GetPositionX(), pTarget->GetPositionY(), height, 0, TEMPSUMMON_TIMED_DESPAWN, 90000ms))
                    {
                        pVZ->SetWanderDistance(0.1f);
                        pVZ->SetSpeedRate(MOVE_RUN, 0.1f);
                        pVZ->SetSpeedRate(MOVE_WALK, 0.1f);
                        pVZ->GetMotionMaster()->MoveRandom(0.1f);

                        ClassicScriptText(SAY_BLAU_SPECIAL, me);
                        m_events.Repeat(12s);
                        break;
                    }
                }
                m_events.Repeat(100ms);
                break;
            default:
                break;
            }
        }
    }
};

struct classic_boss_highlord_mograine : public classic_boss_four_horsemen_shared
{
    classic_boss_highlord_mograine(Creature* creature) : classic_boss_four_horsemen_shared(creature, SPELL_MARK_OF_MOGRAINE, SPELL_SPIRIT_OF_MOGRAINE) { }

    uint32 specialSayCooldown = 12000;

    void Reset() override
    {
        classic_boss_four_horsemen_shared::Reset();
        if (m_bIsSpirit)
        {
            if (Creature* pC = GetHorseman(NPC_MOGRAINE))
                if (classic_boss_four_horsemen_shared* ai = dynamic_cast<classic_boss_four_horsemen_shared*>(pC->AI()))
                    m_uiMarkTimer = ai->m_uiMarkTimer;
        }

        if (m_bIsSpirit)
            return;
        specialSayCooldown = 12000;
    }

    void JustEngagedWith(Unit* who) override
    {
        if (m_bIsSpirit)
            return;
        classic_boss_four_horsemen_shared::JustEngagedWith(who);
        m_events.ScheduleEvent(EVENT_4HM_AGGRO_TEXT, 7s);

        // Should spirit have it too ?
        DoCastSelf(SPELL_RIGHTEOUS_FIRE, true);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        // He is used for SM event too, sooo
        if (me->GetMapId() != MAP_NAXXRAMAS)
            return;

        // Not sure about it
        if (m_bIsSpirit)
            return;

        if (!killSayCooldown)
        {
            ClassicScriptText(urand(0, 1) ? SAY_MOG_SLAY1 : SAY_MOG_SLAY2, me);
            killSayCooldown = 5000;
        }
    }

    void JustDied(Unit* killer) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustDied(killer);
        ClassicScriptText(SAY_MOG_DEATH, me);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        classic_boss_four_horsemen_shared::SpellHitTarget(target, spellInfo);
        if (spellInfo->Id == SPELL_RIGHTEOUS_FIRE_DMG && specialSayCooldown == 0) // Righteous Fire
        {
            ClassicScriptText(SAY_MOG_SPECIAL, me);
            specialSayCooldown = 12000;
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        AggroRadius(uiDiff);

        if (!m_bIsSpirit && !UpdateVictim())
            return;

        if (!m_bIsSpirit && m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        classic_boss_four_horsemen_shared::UpdateAI(uiDiff);
        specialSayCooldown -= std::min(uiDiff, specialSayCooldown);
        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_4HM_AGGRO_TEXT:
                ClassicScriptText(urand(SAY_MOG_AGGRO1, SAY_MOG_AGGRO3), me);
                break;
            default:
                break;
            }
        }
    }
};

struct classic_boss_thane_korthazz : public classic_boss_four_horsemen_shared
{
    classic_boss_thane_korthazz(Creature* creature) : classic_boss_four_horsemen_shared(creature, SPELL_MARK_OF_KORTHAZZ, SPELL_SPIRIT_OF_KORTHAZZ) { }

    void Reset() override
    {
        classic_boss_four_horsemen_shared::Reset();
        if (m_bIsSpirit)
        {
            if (Creature* pC = GetHorseman(NPC_THANE))
                if (classic_boss_four_horsemen_shared* ai = dynamic_cast<classic_boss_four_horsemen_shared*>(pC->AI()))
                    m_uiMarkTimer = ai->m_uiMarkTimer;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustEngagedWith(who);
        m_events.ScheduleEvent(EVENT_4HM_AGGRO_TEXT, 4s);

        // unknown if it should be this long for initial cast. Might be right to get in possition
        m_events.ScheduleEvent(EVENT_4HM_BOSS_ABILITY, 30s);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        // Not sure about it
        if (m_bIsSpirit)
            return;

        if (!killSayCooldown)
        {
            ClassicScriptText(SAY_KORT_SLAY, me);
            killSayCooldown = 5000;
        }
    }

    void JustDied(Unit* killer) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustDied(killer);
        ClassicScriptText(SAY_KORT_DEATH, me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        AggroRadius(uiDiff);

        if (!m_bIsSpirit && !UpdateVictim())
            return;

        if (!m_bIsSpirit && m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        classic_boss_four_horsemen_shared::UpdateAI(uiDiff);

        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_4HM_AGGRO_TEXT:
                    ClassicScriptText(SAY_KORT_AGGRO, me);
                    break;
                case EVENT_4HM_BOSS_ABILITY:
                    if (m_bIsSpirit)
                        break;
                    if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u)
                        {
                            return u->GetTypeId() == TYPEID_PLAYER && me->IsWithinLOSInMap(u);
                        }))
                    {
                        if (DoCast(pTarget, SPELL_4HM_METEOR) == SPELL_CAST_OK)
                        {
                            ClassicScriptText(SAY_KORT_SPECIAL, me);
                            m_events.Repeat(Seconds(urand(12, 15)));
                            break;
                        }
                    }
                    m_events.Repeat(100ms);
                    break;
                default:
                    break;
            }
        }
    }
};

struct classic_boss_sir_zeliek : public classic_boss_four_horsemen_shared
{
    classic_boss_sir_zeliek(Creature* creature) : classic_boss_four_horsemen_shared(creature, SPELL_MARK_OF_ZELIEK, SPELL_SPIRIT_OF_ZELIEK) { }

    void Reset() override
    {
        classic_boss_four_horsemen_shared::Reset();
        if (m_bIsSpirit)
        {
            if (Creature* pC = GetHorseman(NPC_ZELIEK))
                if (classic_boss_four_horsemen_shared* ai = dynamic_cast<classic_boss_four_horsemen_shared*>(pC->AI()))
                    m_uiMarkTimer = ai->m_uiMarkTimer;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustEngagedWith(who);
        m_events.ScheduleEvent(EVENT_4HM_AGGRO_TEXT, 2s);
        m_events.ScheduleEvent(EVENT_4HM_BOSS_ABILITY, 12s);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        // Not sure about it
        if (m_bIsSpirit)
            return;

        if (!killSayCooldown)
        {
            ClassicScriptText(SAY_ZELI_SLAY, me);
            killSayCooldown = 5000;
        }
    }

    void JustDied(Unit* killer) override
    {
        if (m_bIsSpirit)
            return;

        classic_boss_four_horsemen_shared::JustDied(killer);
        ClassicScriptText(SAY_ZELI_DEATH, me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        AggroRadius(uiDiff);

        //Return since we have no target
        if (!m_bIsSpirit && !UpdateVictim())
            return;

        if (!m_bIsSpirit && m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        classic_boss_four_horsemen_shared::UpdateAI(uiDiff);

        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_4HM_AGGRO_TEXT:
                    ClassicScriptText(SAY_ZELI_AGGRO, me);
                    break;
                case EVENT_4HM_BOSS_ABILITY:
                    if (Unit* pTar = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u)
                        {
                            return me->IsWithinLOSInMap(u);
                        }))
                    {
                        if (DoCast(pTar, SPELL_4HM_HOLY_WRATH) == SPELL_CAST_OK)
                        {
                            m_events.Repeat(Seconds(urand(10, 14)));
                            ClassicScriptText(SAY_ZELI_SPECIAL, me);
                            break;
                        }
                    }
                    m_events.Repeat(100ms);
                    break;
                default:
                    break;
            }
        }
    }
};

void AddSC_classic_boss_four_horsemen()
{
    RegisterCreatureAI(classic_boss_lady_blaumeux);
    RegisterCreatureAI(classic_boss_highlord_mograine);
    RegisterCreatureAI(classic_boss_thane_korthazz);
    RegisterCreatureAI(classic_boss_sir_zeliek);
}
