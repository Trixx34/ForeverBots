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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_viscidus.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_viscidus (incl. its pEffectAuraDummy for 25937 -> OnAuraRemoved), mob_viscidus_glob, mob_viscidus_trigger
// VMaNGOS SDComment: ToDo: Use aura proc to handle freeze event instead of direct function

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <bitset>
#include <list>

namespace
{
enum ClassicAq40Viscidus : uint32
{
    EMOTE_VISCIDUS_SLOW             = 11343,
    EMOTE_VISCIDUS_FREEZE           = 11345,
    EMOTE_VISCIDUS_FROZEN           = 11695,

    // These are handled by the core in VMaNGOS (UnitAuraProcHandler.cpp) - not present in the VMaNGOS checkout.
    // EMOTE_CRACK   = 11346,
    // EMOTE_SHATTER = 11347,

    // Timer spells
    SPELL_VISCIDUS_POISON_SHOCK     = 25993,
    SPELL_VISCIDUS_POISONBOLT_VOLLEY = 25991,
    SPELL_VISCIDUS_TOXIN            = 26575,        // Triggers toxin cloud - 25989
    SPELL_VISCIDUS_TOXIN_CLOUD      = 25989,
    SPELL_VISCIDUS_TOXIN_VISUAL     = 26601,

    // Debuffs gained by the boss on frost damage
    SPELL_VISCIDUS_SLOWED           = 26034,
    SPELL_VISCIDUS_SLOWED_MORE      = 26036,
    SPELL_VISCIDUS_FREEZE           = 25937,

    // When frost damage exceeds a certain limit, then boss explodes
    SPELL_VISCIDUS_REJOIN           = 25896,
    SPELL_VISCIDUS_EXPLODE          = 25938,
    SPELL_VISCIDUS_SUICIDE          = 26003,        // cast when boss explodes and is below 5% Hp - should trigger 26002
    SPELL_VISCIDUS_DESPAWN_GLOBS    = 26608,

    SPELL_VISCIDUS_MEMBRANE         = 25994,        // damage reduction spell
    SPELL_VISCIDUS_WEAKNESS         = 25926,        // aura which procs at damage - should trigger the slow spells
    SPELL_VISCIDUS_SHRINKS          = 25893,
    SPELL_VISCIDUS_SHRINKS_HP       = 27934,        // should be scripted properly
    SPELL_VISCIDUS_GROWS            = 25897,
    SPELL_VISCIDUS_TELEPORT         = 25904,        // teleport to room center

    SPELL_VISCIDUS_GLOB_SPEED       = 26633,        // apply aura 26634 each second

    NPC_VISCIDUS_GLOB               = 15667,
    NPC_VISCIDUS_TRIGGER            = 15922,        // handles aura 26575

    MAX_VISCIDUS_GLOBS              = 20,           // there are 20 summoned globs; each glob = 5% hp

    // hitcounts
    HITCOUNT_VISCIDUS_SLOW          = 100,
    HITCOUNT_VISCIDUS_SLOW_MORE     = 150,
    HITCOUNT_VISCIDUS_FREEZE        = 200,

    // phases
    PHASE_VISCIDUS_NORMAL           = 1,
    PHASE_VISCIDUS_FROZEN           = 2,
    PHASE_VISCIDUS_EXPLODED         = 3,

    SPELL_VISCIDUS_WAND_SHOOT       = 5019,

    // TC: glob -> boss notification (VMaNGOS SummonedMovementInform)
    CAQ40_VISCIDUS_GUID_GLOB_ARRIVED = 1,

    // faction 14 is a hostile faction
    CAQ40_VISCIDUS_FACTION_HOSTILE  = 14,
};

uint32 const auiGlobSummonSpells[MAX_VISCIDUS_GLOBS] = { 25865, 25866, 25867, 25868, 25869, 25870, 25871, 25872, 25873, 25874, 25875, 25876, 25877, 25878, 25879, 25880, 25881, 25882, 25883, 25884 };
}

//-----------------------------------------------------------------------------
// mob_viscidus_glob
//-----------------------------------------------------------------------------
struct classic_mob_viscidus_glob : public ScriptedAI
{
    // Acceleration delay
    uint32 m_uiGlobStartAccelerationTimer;
    // prevents SPELL_GLOB_SPEED casting multiple times
    bool m_spellCasted;

    // Everything is an approximated here. Need data from official.
    // Initial glob speed is 0.335625. They start acceleration with 4 seconds delay timer.
    // Each tick of the aura(id:26634) doubles their speed.
    // This solution gives smooth movement and acceleration.

    classic_mob_viscidus_glob(Creature* creature) : ScriptedAI(creature), m_uiGlobStartAccelerationTimer(4000), m_spellCasted(false) { }

    // dummy methods
    void Reset() override { }
    void AttackStart(Unit* /*who*/) override { }
    void MoveInLineOfSight(Unit* /*who*/) override { }

    // VMaNGOS boss_viscidusAI::SummonedMovementInform: TC has no such hook, the glob reports to its summoner instead.
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || !id)
            return;

        if (TempSummon* summon = me->ToTempSummon())
            if (Creature* viscidus = summon->GetSummonerCreatureBase())
                if (viscidus->IsAIEnabled())
                    viscidus->AI()->SetGUID(me->GetGUID(), CAQ40_VISCIDUS_GUID_GLOB_ARRIVED);
    }

    // Implements acceleration on timer, prevents combat.
    void UpdateAI(uint32 uiDiff) override
    {
        if (m_uiGlobStartAccelerationTimer <= uiDiff)
        {
            // SPELL_GLOB_SPEED should be casted only once
            if (!m_spellCasted)
            {
                m_spellCasted = true;
                me->CastSpell(me, SPELL_VISCIDUS_GLOB_SPEED, true);
            }
        }
        else
            m_uiGlobStartAccelerationTimer -= uiDiff;
    }
};

//-----------------------------------------------------------------------------
// mob_viscidus_trigger
//-----------------------------------------------------------------------------
struct classic_mob_viscidus_trigger : public ScriptedAI
{
    // Acceleration delay
    uint32 m_uiToxinDelayTimer;
    bool m_spellCasted;

    classic_mob_viscidus_trigger(Creature* creature) : ScriptedAI(creature), m_uiToxinDelayTimer(3000), m_spellCasted(false) { }

    // dummy methods
    void Reset() override { }
    void AttackStart(Unit* /*who*/) override { }
    void MoveInLineOfSight(Unit* /*who*/) override { }

    // Implements toxin cloud on timer, prevents combat.
    void UpdateAI(uint32 uiDiff) override
    {
        if (m_uiToxinDelayTimer <= uiDiff)
        {
            if (!m_spellCasted)
            {
                // set faction and flags before toxin cloud, so it won't damage a boss.
                me->SetFaction(CAQ40_VISCIDUS_FACTION_HOSTILE);
                me->SetUnitFlag(UNIT_FLAG_NOT_ATTACKABLE_1);

                m_spellCasted = true;
                // cast spell instantly, only once
                me->CastSpell(me, SPELL_VISCIDUS_TOXIN_CLOUD, true);
                // apply an aura, which will continously repeat this spell.
                me->CastSpell(me, SPELL_VISCIDUS_TOXIN, true);
            }
        }
        else
            m_uiToxinDelayTimer -= uiDiff;
    }
};

//-----------------------------------------------------------------------------
// boss_viscidus
//-----------------------------------------------------------------------------
struct classic_boss_viscidus : public ScriptedAI
{
    InstanceScript* m_pInstance;

    uint32 m_uiRestoreTargetTimer = 0;

    uint32 m_uiHitCount = 0;
    uint32 m_uiToxinTimer = 0;
    uint32 m_uiExplodeDelayTimer = 0;
    uint32 m_uiPoisonShockTimer = 0;
    uint32 m_uiPoisonBoltVolleyTimer = 0;
    uint32 m_uiEvadeCheckCooldown = 2500;   // VMaNGOS ScriptedAI::m_uiEvadeCheckCooldown

    uint16 m_uiGrowTimer = 0;
    uint8  m_uiPhase = PHASE_VISCIDUS_NORMAL;

    float m_initialScale;
    uint8 m_uiGrowCount = 0;

    GuidList m_lGlobesGuidList;
    std::bitset<20> track;

    classic_boss_viscidus(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
        m_initialScale = creature->GetObjectScale();
    }

    void Reset() override
    {
        m_uiGrowTimer             = 0;
        m_uiPhase                 = PHASE_VISCIDUS_NORMAL;

        m_uiRestoreTargetTimer    = 0;

        m_uiHitCount              = 0;
        m_uiExplodeDelayTimer     = 0;
        m_uiToxinTimer            = urand(30000, 40000);
        m_uiPoisonShockTimer      = urand(7000, 12000);
        m_uiPoisonBoltVolleyTimer = urand(10000, 15000);
        m_uiGrowCount = 0;
        me->SetObjectScale(m_initialScale);
        track.reset();

        DoCastSelf(SPELL_VISCIDUS_MEMBRANE, true);
        DoCastSelf(SPELL_VISCIDUS_WEAKNESS, true);

        ResetViscidusState();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER && !me->GetVictim() && me->IsWithinDistInMap(who, 95.0f, true))
            AttackStart(who);

        ScriptedAI::MoveInLineOfSight(who);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_VISCIDUS, IN_PROGRESS);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_VISCIDUS, FAIL);

        DoCastSelf(SPELL_VISCIDUS_DESPAWN_GLOBS, true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_VISCIDUS, DONE);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_VISCIDUS_GLOB)
        {
            // Move all summoned globs to Viscidus's spawn point. The spawn point is essentially
            // the center of the room triangulated between 4 points with a distance of 100 between
            // points on the same gridline. Globs are spawned on a circle with 52 yard radius from
            // the center point
            summoned->GetMotionMaster()->MovePoint(1, -7993.956f, 926.309f, -52.699f, true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceRun);

            m_lGlobesGuidList.push_back(summoned->GetGUID());
        }
    }

    void ResetViscidusState()
    {
        if (!me->IsVisible())
            DoCast(me, SPELL_VISCIDUS_TELEPORT, true);

        ResetThreatList();
        me->SetVisible(true);
        // VMaNGOS also clears UNIT_STATE_FEIGN_DEATH and UNIT_DYNFLAG_DEAD here; neither exists in TC master.
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);   // VMaNGOS UNIT_FLAG_SPAWNING
        m_uiPhase = PHASE_VISCIDUS_NORMAL;
        m_uiHitCount = 0;

        SetCombatMovement(true);
    }

    void SummonedCreatureDies(Creature* summoned, Unit* /*killer*/) override
    {
        if (summoned->GetEntry() == NPC_VISCIDUS_GLOB)
        {
            // should be a spell script!
            if (DoCastSelf(SPELL_VISCIDUS_SHRINKS_HP, true) == SPELL_CAST_OK)
            {
                uint64 health = me->GetHealth() - uint64(me->GetMaxHealth() * 0.05f);

                if (health && health <= me->GetMaxHealth())
                    me->SetHealth(health);
                else
                    me->SetHealth(me->CountPctFromMaxHealth(1.0f));
            }

            m_lGlobesGuidList.remove(summoned->GetGUID());

            if (m_lGlobesGuidList.empty())
                ResetViscidusState();
        }
    }

    // VMaNGOS SummonedMovementInform (called by classic_mob_viscidus_glob::MovementInform)
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id != CAQ40_VISCIDUS_GUID_GLOB_ARRIVED)
            return;

        Creature* summoned = ObjectAccessor::GetCreature(*me, guid);
        if (!summoned || summoned->GetEntry() != NPC_VISCIDUS_GLOB)
            return;

        m_lGlobesGuidList.remove(summoned->GetGUID());
        summoned->CastSpell(me, SPELL_VISCIDUS_REJOIN, true);
        summoned->DespawnOrUnsummon(650ms);
        ++m_uiGrowCount; // should be done in spell script

        if (m_lGlobesGuidList.empty())
            ResetViscidusState();
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_VISCIDUS_EXPLODE)
        {
            // suicide if required
            if (me->GetHealthPct() < 5.0f)
            {
                me->SetVisible(true);
                // VMaNGOS: SetInvincibilityHpThreshold(0) - no TC equivalent

                if (DoCastSelf(SPELL_VISCIDUS_SUICIDE, true) == SPELL_CAST_OK)
                    Unit::DealDamage(me, me, me->GetHealth(), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NONE, nullptr, false);

                return;
            }

            m_uiPhase = PHASE_VISCIDUS_EXPLODED;
            m_lGlobesGuidList.clear();
            uint32 uiGlobeCount = uint32(me->GetHealthPct() / 5.0f);

            for (uint8 i = 0; i < uiGlobeCount && i < MAX_VISCIDUS_GLOBS; ++i)
                DoCastSelf(auiGlobSummonSpells[i], true);

            m_uiExplodeDelayTimer = 2500; // 2500(instead of 1000) makes glob summon effect more visible
            return;
        }

        if (m_uiPhase != PHASE_VISCIDUS_NORMAL)
            return;

        // VMaNGOS: pSpell->School == SPELL_SCHOOL_FROST
        bool bIsFrostSpell = spellInfo->GetSchoolMask() == SPELL_SCHOOL_MASK_FROST;

        // wand special case:
        // shoot's school is physical, as long we get a SpellEntry,
        // we need to check the caster currently equiped wand
        // if it's frost damage, use a trigger spell
        if (spellInfo->Id == SPELL_VISCIDUS_WAND_SHOOT)
        {
            Player const* pPlayer = caster ? caster->ToPlayer() : nullptr;
            if (!pPlayer)
                return;

            Item const* pItem = pPlayer->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            if (!pItem)
                return;

            ItemTemplate const* pProto = pItem->GetTemplate();
            if (!pProto)
                return;

            // Wand always have 1 damage type on vanilla
            bIsFrostSpell = pProto->GetDamageType() == uint32(SPELL_SCHOOL_FROST);
        }

        if (bIsFrostSpell)
        {
            ++m_uiHitCount;

            if (m_uiHitCount >= HITCOUNT_VISCIDUS_FREEZE)
            {
                m_uiPhase = PHASE_VISCIDUS_FROZEN;
                m_uiHitCount = 0;

                ClassicScriptText(EMOTE_VISCIDUS_FROZEN, me);
                me->RemoveAurasDueToSpell(SPELL_VISCIDUS_SLOWED_MORE);
                DoCastSelf(SPELL_VISCIDUS_FREEZE, true);
            }
            else if (m_uiHitCount >= HITCOUNT_VISCIDUS_SLOW_MORE)
            {
                if (m_uiHitCount == HITCOUNT_VISCIDUS_SLOW_MORE)
                {
                    ClassicScriptText(EMOTE_VISCIDUS_FREEZE, me);
                    me->RemoveAurasDueToSpell(SPELL_VISCIDUS_SLOWED);
                }
                DoCastSelf(SPELL_VISCIDUS_SLOWED_MORE, true);
            }
            else if (m_uiHitCount >= HITCOUNT_VISCIDUS_SLOW)
            {
                if (m_uiHitCount == HITCOUNT_VISCIDUS_SLOW)
                    ClassicScriptText(EMOTE_VISCIDUS_SLOW, me);
                DoCastSelf(SPELL_VISCIDUS_SLOWED, true);
            }
        }
    }

    // VMaNGOS EffectAuraDummy_spell_aura_dummy_viscidus_freeze: on removal of 25937 (effect 1) inform the boss
    void OnAuraRemoved(AuraApplication const* aurApp) override
    {
        if (aurApp->GetBase()->GetId() == SPELL_VISCIDUS_FREEZE)
            ResetFrozenPhase();
    }

    void ResetFrozenPhase()
    {
        if (m_uiPhase == PHASE_VISCIDUS_EXPLODED)
            return;

        // reset phase if not already exploded
        m_uiPhase = PHASE_VISCIDUS_NORMAL;
        m_uiHitCount = 0;
    }

    void HackyScaleUpdate()
    {
        auto hp = static_cast<size_t>(me->GetHealthPct() / 5);

        if (hp != track.size() && !track.test(hp))
        {
            track.set(hp);
            me->CastSpell(me, SPELL_VISCIDUS_SHRINKS, true);
        }
    }

    // VMaNGOS ScriptedAI::EnterEvadeIfOutOfCombatArea, NPC_VISCIDUS case
    bool EnterEvadeIfOutOfCombatArea(uint32 uiDiff)
    {
        if (m_uiEvadeCheckCooldown < uiDiff)
            m_uiEvadeCheckCooldown = 2500;
        else
        {
            m_uiEvadeCheckCooldown -= uiDiff;
            return false;
        }

        if (me->IsInEvadeMode() || !me->GetVictim())
            return false;

        if (me->GetPositionZ() < -30.0f)
            return false;

        EnterEvadeMode();
        return true;
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        // VMaNGOS only calls DoMeleeAttackIfReady() in the normal phase (TC: automatic melee)
        me->SetCanMelee(m_uiPhase == PHASE_VISCIDUS_NORMAL);

        HackyScaleUpdate();

        // should be spell scripted properly
        if (m_uiGrowTimer <= uiDiff)
        {
            if (m_uiGrowCount)
            {
                DoCastSelf(SPELL_VISCIDUS_GROWS, true);
                me->SetObjectScale(me->GetObjectScale() + 0.04f);
                --m_uiGrowCount;
            }
            m_uiGrowTimer = 150;
        }
        else
            m_uiGrowTimer -= uiDiff;

        if (m_uiExplodeDelayTimer)
        {
            if (m_uiExplodeDelayTimer <= uiDiff)
            {
                // Make invisible
                me->SetVisible(false);
                m_uiExplodeDelayTimer = 0;

                // should be part of SPELL_VISCIDUS_SHRINKS_HP script!
                float scale = me->GetObjectScale();
                scale -= 0.04f * m_lGlobesGuidList.size();
                me->SetObjectScale(scale);
            }
            else
                m_uiExplodeDelayTimer -= uiDiff;
        }

        // Restore original target after "casting" a Toxic Cloud near someone.
        if (m_uiRestoreTargetTimer)
        {
            if (m_uiRestoreTargetTimer <= uiDiff)
            {
                if (Unit* pTarget = me->GetVictim())
                {
                    me->SetFacingToObject(pTarget);
                    me->SetTarget(pTarget->GetGUID());
                    m_uiRestoreTargetTimer = 0;
                }
            }
            else
                m_uiRestoreTargetTimer -= uiDiff;
        }

        if (m_uiPhase != PHASE_VISCIDUS_NORMAL)
            return;

        if (m_uiPoisonShockTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_VISCIDUS_POISON_SHOCK) == SPELL_CAST_OK)
                m_uiPoisonShockTimer = urand(7000, 12000);
        }
        else
            m_uiPoisonShockTimer -= uiDiff;

        if (m_uiPoisonBoltVolleyTimer < uiDiff)
        {
            if (DoCastSelf(SPELL_VISCIDUS_POISONBOLT_VOLLEY) == SPELL_CAST_OK)
                m_uiPoisonBoltVolleyTimer = urand(10000, 15000);
        }
        else
            m_uiPoisonBoltVolleyTimer -= uiDiff;

        if (m_uiToxinTimer < uiDiff)
        {
            Creature* self = me;
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, [self](Unit* u) { return self->IsWithinLOSInMap(u); }))
            {
                Creature* trigger = me->SummonCreature(NPC_VISCIDUS_TRIGGER, pTarget->GetPositionX(), pTarget->GetPositionY(), pTarget->GetPositionZ(), pTarget->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 180s);
                m_uiToxinTimer = urand(30000, 40000);

                // visual spell effect, which predates Toxin Cloud.
                if (trigger)
                {
                    // visual effect doen't work if target is not selectable...
                    trigger->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    // set boss facing to the trigger
                    me->SetFacingToObject(trigger);
                    me->SetTarget(trigger->GetGUID());
                    // cast spell, which will be completely resisted by trigger(immune) to simulate a visual effect
                    me->CastSpell(trigger, SPELL_VISCIDUS_TOXIN_VISUAL, true);
                    // restore boss orientation in 800 ms
                    m_uiRestoreTargetTimer = 800;
                    // restore trigger flag
                    trigger->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                }
            }
        }
        else
            m_uiToxinTimer -= uiDiff;

        EnterEvadeIfOutOfCombatArea(uiDiff);
    }
};

void AddSC_classic_boss_viscidus()
{
    RegisterCreatureAI(classic_boss_viscidus);
    RegisterCreatureAI(classic_mob_viscidus_glob);
    RegisterCreatureAI(classic_mob_viscidus_trigger);
}
