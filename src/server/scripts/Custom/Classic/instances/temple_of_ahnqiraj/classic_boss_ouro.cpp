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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_ouro.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_ouro, npc_ouro_spawner, npc_dirt_mound, npc_ouro_scarab
// go_sandworm_base is folded into boss_ouro (see CAQ40_OuroBaseSpawnAnim / CAQ40_OuroDespawnBase below).

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_temple_of_ahnqiraj.h"
#include <algorithm>
#include <list>
#include <vector>

namespace
{
enum ClassicAq40Ouro : uint32
{
    // ground spells
    SPELL_OURO_SWEEP                = 26103,
    SPELL_OURO_SANDBLAST            = 26102,
    SPELL_OURO_BOULDER              = 26616,
    SPELL_OURO_BERSERK              = 26615,

    // emerge spells
    SPELL_OURO_BIRTH                = 26262,        // The Birth Animation
    SPELL_OURO_GROUND_RUPTURE       = 26100,        // spell not confirmed
    SPELL_OURO_SUMMON_BASE          = 26133,        // summons gameobject 180795
    SPELL_OURO_DESPAWN_BASE         = 26594,

    // submerge spells
    SPELL_OURO_SUBMERGE_VISUAL      = 26063,
    SPELL_OURO_SUMMON_OURO_MOUNDS   = 26058,        // summons 5 dirt mounds
    SPELL_OURO_SUMMON_TRIGGER       = 26284,

    SPELL_OURO_SUMMON_OURO          = 26061,        // used by the script to summon the boss directly

    // other spells
    SPELL_OURO_SUMMON_SCARABS       = 26060,        // triggered after 30 secs - cast by the Dirt Mounds
    SPELL_OURO_DIRTMOUND_PASSIVE    = 26092,        // casts 26093 every 1 sec
    SPELL_OURO_SUMMON_OURO_MOUND    = 26617,

    // summoned npcs (not in the shared header)
    CAQ40_NPC_OURO_TRIGGER          = 15717,
    CAQ40_NPC_DIRT_MOUND            = 15712,
};

/*
 * Sand Blast and sweep timers are based on June 2006 values (20-25s) as shown in
 * https://www.youtube.com/watch?v=REmX3uRTFkQ. The video is slightly sped up, so time was counted by the ticking of buffs on the player.
 * More confirmation from classic sniffs https://i.imgur.com/YIA8veT.png
 */
uint32 const OURO_SUBMERGE_ANIMATION_INVIS = 2000;
uint32 const OURO_SWEEP_TIMER              = 20500;

// VMaNGOS: sWorld.GetWowPatch() >= WOW_PATCH_110 ? x : y. Classic 1.60 is 1.12 content -> patch 1.10+ values.
uint32 const OURO_SANDBLAST_TIMER_MIN      = 20000;
uint32 const OURO_SANDBLAST_TIMER_MAX      = 25000;
uint32 const OURO_SUBMERGE_TIMER           = 90000;

// VMaNGOS casts Despawn Base (26594, SPELL_EFFECT_ACTIVATE_OBJECT, target nearest GO) and go_sandworm_base::OnUse did:
// first use -> custom animation (emerge), second use -> delete. TC master maps that effect's misc value (15) to
// GameObjectActions::Despawn and needs a conditions row for the implicit target, so the two cases are done directly here.
void CAQ40_OuroBaseSpawnAnim(Creature* ouro)
{
    if (GameObject* base = ouro->FindNearestGameObject(GO_SANDWORM_BASE, 20.0f))
        base->SendCustomAnim(0);
}

void CAQ40_OuroDespawnBase(Creature* ouro)
{
    if (GameObject* base = ouro->FindNearestGameObject(GO_SANDWORM_BASE, 20.0f))
    {
        base->SetRespawnTime(0);
        base->DespawnOrUnsummon();
    }
}
}

struct classic_boss_ouro : public ScriptedAI
{
    classic_boss_ouro(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = creature->GetInstanceScript();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiSweepTimer = 0;
    uint32 m_uiSandBlastTimer = 0;
    uint32 m_uiRestoreTargetTimer = 0;
    uint32 m_uiSubmergeTimer = 0;
    bool m_SummonBase = true;
    uint32 m_uiNoMeleeTimer = 0;
    uint32 m_uiSubmergeInvisTimer = 0;
    uint32 m_justEmergedGraceTimer = 0;
    uint32 m_uiSummonMoundTimer = 0;

    bool m_bEnraged = false;
    bool m_bSubmerged = false;

    ObjectGuid m_ouroTriggerGuid;

    void Reset() override
    {
        // This makes the mob behave like rooted mobs etc, that is,
        // retargetting another top-threat target if current leaves melee range
        me->StopMoving();
        me->SetControlled(true, UNIT_STATE_ROOT);

        m_uiSweepTimer          = OURO_SWEEP_TIMER;
        m_uiSandBlastTimer      = urand(OURO_SANDBLAST_TIMER_MIN, OURO_SANDBLAST_TIMER_MAX);
        m_uiRestoreTargetTimer  = 0;
        m_uiSubmergeTimer       = OURO_SUBMERGE_TIMER;
        m_SummonBase            = true;
        m_uiSubmergeInvisTimer  = OURO_SUBMERGE_ANIMATION_INVIS;

        m_uiNoMeleeTimer        = 3000;
        // Source : http://wowwiki.wikia.com/wiki/Ouro
        // "Ouro seems to give you about 10 seconds to get a MT in there when he pops up"
        m_justEmergedGraceTimer = 10000;

        m_uiSummonMoundTimer    = 10000;
        m_bEnraged              = false;
        m_bSubmerged            = false;

        m_ouroTriggerGuid.Clear();
        me->SetCanMelee(true);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_OURO, IN_PROGRESS);
    }

    void DespawnCreatures(bool ShouldDespawnScarabs)
    {
        std::list<Creature*> lCreature;
        me->GetCreatureListWithEntryInGrid(lCreature, CAQ40_NPC_DIRT_MOUND, 250.0f);
        if (ShouldDespawnScarabs)
            me->GetCreatureListWithEntryInGrid(lCreature, NPC_OURO_SCARAB, 250.0f);
        for (Creature* creature : lCreature)
            creature->DespawnOrUnsummon();
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_OURO, FAIL);

        DespawnCreatures(true);
        CAQ40_OuroDespawnBase(me);  // VMaNGOS: CastSpell(me, SPELL_OURO_DESPAWN_BASE, true)

        Submerge(true);
        me->DespawnOrUnsummon(2s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_OURO, DONE);

        CAQ40_OuroDespawnBase(me);  // VMaNGOS: CastSpell(me, SPELL_OURO_DESPAWN_BASE, true)
    }

    void JustSummoned(Creature* summoned) override
    {
        switch (summoned->GetEntry())
        {
            case CAQ40_NPC_OURO_TRIGGER:
                m_ouroTriggerGuid = summoned->GetGUID();
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    summoned->GetMotionMaster()->MoveFollow(target, 0.0f);
                break;
            default:
                break;
        }
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_OURO_SANDBLAST && target)
        {
            if (Unit* unitTarget = target->ToUnit())
                if (me->GetThreatManager().GetThreat(unitTarget))
                    me->GetThreatManager().ModifyThreatByPercent(unitTarget, -100);
        }
    }

    void Submerge(bool isReset = false)
    {
        // Source : http://wowwiki.wikia.com/wiki/Ouro
        // "Ouro has a chance to submerge every 1.5minutes.
        // He will not submerge if he is busy casting a Sand Blast or Sweep, else he will submerge
        // (ie. the chance of submerging is totally random)"
        m_uiSubmergeTimer = OURO_SUBMERGE_TIMER;

        if (DoCastSelf(SPELL_OURO_SUBMERGE_VISUAL) == SPELL_CAST_OK)
        {
            if (!isReset)
            {
                DoCastSelf(SPELL_OURO_SUMMON_OURO_MOUNDS, true);
                DoCastSelf(SPELL_OURO_SUMMON_TRIGGER, true);
            }

            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);     // VMaNGOS UNIT_FLAG_NOT_SELECTABLE
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);     // VMaNGOS UNIT_FLAG_SPAWNING
            // TODO(classic): VMaNGOS ClearTargetIcon() clears raid target icons on Ouro; TC Group has no icon getter.

            m_bSubmerged      = true;
            m_uiSubmergeTimer = 30000;

            m_justEmergedGraceTimer = 10000;
            m_uiNoMeleeTimer        = 10000; // This has to be 10 sec and not 3 sec. Otherwise sometimes, if CanReachWithMeleeAutoAttack returns false for a whole 3 seconds, NoMeleeTimer will never be set to equal grace timer, and grace timer won't work.
            m_uiSubmergeInvisTimer = OURO_SUBMERGE_ANIMATION_INVIS;
            ResetThreatList();
            me->SetCanMelee(false); // VMaNGOS never calls DoMeleeAttackIfReady while submerged
        }
    }

    // Threat is preserved when Ouro swaps target. The only mechanics that affect threat are being hit by sandblast, and the threat wipe on submerge.
    void UpdateAI(uint32 uiDiff) override
    {
        // This is to let Ouro reset when he enters evade mode. Because rooted mobs don't reset when they evade until their root is removed.
        if (me->IsInEvadeMode())
            me->SetControlled(false, UNIT_STATE_ROOT);
        else
        {
            me->StopMoving();
            me->SetControlled(true, UNIT_STATE_ROOT);
        }

        // Return since we have no target
        // TODO(classic): VMaNGOS rooted-mob target selection prefers top-threat units in melee reach; TC picks plain top threat.
        if (!UpdateVictim())
            return;

        if (!m_bSubmerged)
        {
            // Summon sandworm base
            if (m_SummonBase)
            {
                // Note: server side spells should be cast directly
                me->CastSpell(me, SPELL_OURO_SUMMON_BASE, true);
                // Cast Despawn base to trigger spawn anim
                CAQ40_OuroBaseSpawnAnim(me);
                m_SummonBase = false;
            }

            // Sweep
            if (m_uiSweepTimer < uiDiff)
            {
                if (DoCastSelf(SPELL_OURO_SWEEP) == SPELL_CAST_OK)
                    m_uiSweepTimer = OURO_SWEEP_TIMER;
            }
            else
                m_uiSweepTimer -= uiDiff;

            // Sand Blast
            if (m_uiSandBlastTimer < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::MaxThreat, 0, 0.0f, true))
                {
                    // Ouro visually targets the victim of Sand Blast for its whole 2 second cast time.
                    me->SetFacingToObject(pTarget);
                    me->SetTarget(pTarget->GetGUID());
                    if (DoCast(pTarget, SPELL_OURO_SANDBLAST) == SPELL_CAST_OK)
                    {
                        m_uiSandBlastTimer = urand(OURO_SANDBLAST_TIMER_MIN, OURO_SANDBLAST_TIMER_MAX);
                        m_uiRestoreTargetTimer = 2100; // 2 second timer to swap visual target back to the tank. With 100ms grace window to make sure the visual animation of the boss spitting sand is facing the right direction.
                    }
                }
            }
            else
                m_uiSandBlastTimer -= uiDiff;

            // Get new visual target after casting Sand Blast at the highest threat.
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

            if (!m_bEnraged)
            {
                // Enrage at 20% HP
                if (me->GetHealthPct() < 20.0f)
                {
                    if (DoCastSelf(SPELL_OURO_BERSERK) == SPELL_CAST_OK)
                    {
                        m_bEnraged = true;
                        return;
                    }
                }

                // Submerge
                if (m_uiSubmergeTimer < uiDiff)
                    Submerge();
                else
                    m_uiSubmergeTimer -= uiDiff;
            }
            else
            {
                // Summon 1 mound every 10 secs when enraged
                if (m_uiSummonMoundTimer < uiDiff)
                {
                    if (DoCastSelf(SPELL_OURO_SUMMON_OURO_MOUND) == SPELL_CAST_OK)
                        m_uiSummonMoundTimer = 10000;
                }
                else
                    m_uiSummonMoundTimer -= uiDiff;
            }

            // Because the mob is rooted we check if the target is in melee range. If it is, autoattack it (TC: automatic)
            // and refresh the submerge timer.
            if (me->GetVictim() && me->IsWithinMeleeRange(me->GetVictim()))
            {
                m_uiNoMeleeTimer = std::max(uint32(3000), m_justEmergedGraceTimer);
            }
            // Spam Boulder spell when enraged and not tanked
            else if (m_bEnraged)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(pTarget, SPELL_OURO_BOULDER);
                }
            }
            // Submerge if no melee and not enraged
            else if (m_uiNoMeleeTimer < uiDiff)
            {
                Submerge();
            }
            else if (m_uiRestoreTargetTimer == 0) // Submerge timer only ticks down if Ouro is not casting Sandblast
                m_uiNoMeleeTimer -= uiDiff;

            m_justEmergedGraceTimer -= std::min(uiDiff, m_justEmergedGraceTimer);
        }
        else
        {
            // Resume combat
            if (m_uiSubmergeTimer < uiDiff)
            {
                me->SetVisible(true);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);

                // Teleport to the trigger in order to get a new location
                if (Creature* pTrigger = ObjectAccessor::GetCreature(*me, m_ouroTriggerGuid))
                    me->NearTeleportTo(pTrigger->GetPosition());

                if (DoCastSelf(SPELL_OURO_BIRTH) == SPELL_CAST_OK)
                {
                    me->RemoveAurasDueToSpell(SPELL_OURO_SUBMERGE_VISUAL);
                    me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

                    std::vector<Unit*> lGroundRuptureTargets;
                    for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                    {
                        Unit* pUnit = ref->GetVictim();
                        if (pUnit && pUnit->GetDistance2d(me) < 20.0f)
                            lGroundRuptureTargets.push_back(pUnit);
                    }
                    for (Unit* target : lGroundRuptureTargets)
                        me->CastSpell(target, SPELL_OURO_GROUND_RUPTURE, true);

                    m_bSubmerged           = false;
                    m_SummonBase           = true;
                    m_uiSubmergeTimer      = OURO_SUBMERGE_TIMER;
                    m_uiSubmergeInvisTimer = OURO_SUBMERGE_ANIMATION_INVIS;
                    m_uiSweepTimer         = OURO_SWEEP_TIMER;
                    m_uiSandBlastTimer     = urand(OURO_SANDBLAST_TIMER_MIN, OURO_SANDBLAST_TIMER_MAX);
                    me->SetCanMelee(true);

                    DespawnCreatures(false);

                    // Ouro should despawn instant on reset, not after going to his spawn point
                    me->SetHomePosition(me->GetPosition());
                }
            }
            else
            {
                if (me->IsVisible())
                {
                    if (m_uiSubmergeInvisTimer < uiDiff)
                        me->SetVisible(false);
                    else
                        m_uiSubmergeInvisTimer -= uiDiff;
                }

                m_uiSubmergeTimer -= uiDiff;
            }
        }
    }
};

struct classic_npc_ouro_spawner : public ScriptedAI
{
    classic_npc_ouro_spawner(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);   // VMaNGOS Scripted_NoMovementAI
    }

    bool m_bHasSummoned = false;

    void Reset() override
    {
        m_bHasSummoned = false;

        DoCastSelf(SPELL_OURO_DIRTMOUND_PASSIVE);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // Spawn Ouro on LoS check
        if (!m_bHasSummoned && who->GetTypeId() == TYPEID_PLAYER && !who->ToPlayer()->IsGameMaster() && me->IsWithinDistInMap(who, 25.0f))
        {
            if (DoCastSelf(SPELL_OURO_SUMMON_OURO) == SPELL_CAST_OK)
                m_bHasSummoned = true;
        }

        ScriptedAI::MoveInLineOfSight(who);
    }

    void JustSummoned(Creature* summoned) override
    {
        // Despawn when Ouro is spawned
        if (summoned->GetEntry() == NPC_OURO)
        {
            summoned->CastSpell(summoned, SPELL_OURO_BIRTH, false);
            CreatureAI::DoZoneInCombat(summoned);
            me->DespawnOrUnsummon();
        }
    }

    void UpdateAI(uint32 /*diff*/) override { }
};

struct classic_npc_dirt_mound : public ScriptedAI
{
    classic_npc_dirt_mound(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiChangeTargetTimer = 0;
    uint32 m_uiDespawnTimer = 30000;
    ObjectGuid m_TargetGUID;
    ObjectGuid m_CurrentTargetGUID;

    // VMaNGOS also has a JustRespawned() override setting UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_SPAWNING, but the VMaNGOS
    // core never calls JustRespawned(), so it is not ported.

    void Reset() override
    {
        m_uiDespawnTimer = 30000;
        m_TargetGUID.Clear();
        m_CurrentTargetGUID.Clear();

        DoCastSelf(SPELL_OURO_DIRTMOUND_PASSIVE);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (m_TargetGUID.IsEmpty() && who->GetTypeId() == TYPEID_PLAYER)
            m_TargetGUID = who->GetGUID();
    }

    void UpdateAI(uint32 uiDiff) override
    {
        Unit* pTarget = ObjectAccessor::GetUnit(*me, m_CurrentTargetGUID);
        bool const bForceChangeTarget = !pTarget || !pTarget->IsAlive()
            || pTarget->IsImmunedToDamage(SPELL_SCHOOL_MASK_NATURE);

        if (bForceChangeTarget || m_uiChangeTargetTimer < uiDiff)
        {
            m_CurrentTargetGUID.Clear();

            if (Unit* newTarget = ObjectAccessor::GetUnit(*me, m_TargetGUID))
            {
                me->GetMotionMaster()->MoveFollow(newTarget, 0.0f);
                m_CurrentTargetGUID = m_TargetGUID;
                m_TargetGUID.Clear();
            }
            else
                me->GetMotionMaster()->MoveRandom(me->GetWanderDistance());

            m_uiChangeTargetTimer = urand(0, 10000);
        }
        else
            m_uiChangeTargetTimer -= uiDiff;

        if (m_uiDespawnTimer < uiDiff)
        {
            me->CastSpell(me, SPELL_OURO_SUMMON_SCARABS, true);
            me->DespawnOrUnsummon();
        }
        else
            m_uiDespawnTimer -= uiDiff;
    }
};

struct classic_npc_ouro_scarab : public ScriptedAI
{
    classic_npc_ouro_scarab(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiDespawnTimer = 45000;

    void Reset() override
    {
        m_uiDespawnTimer = 45000;
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER && !me->GetVictim() && !urand(0, 5))
            AttackStart(who);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        // VMaNGOS: DoMeleeAttackIfReady() when it has a victim (TC: automatic melee)
        if (me->GetVictim())
            UpdateVictim();

        if (m_uiDespawnTimer < uiDiff)
            me->DespawnOrUnsummon();
        else
            m_uiDespawnTimer -= uiDiff;
    }
};

void AddSC_classic_boss_ouro()
{
    RegisterCreatureAI(classic_boss_ouro);
    RegisterCreatureAI(classic_npc_ouro_spawner);
    RegisterCreatureAI(classic_npc_dirt_mound);
    RegisterCreatureAI(classic_npc_ouro_scarab);
}
