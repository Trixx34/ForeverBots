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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_cthun.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_eye_of_cthun, boss_cthun, mob_eye_tentacle, mob_claw_tentacle, mob_giant_claw_tentacle,
//          mob_giant_eye_tentacle, mob_giant_flesh_tentacle
// VMaNGOS header: Nostalrius (inital version Scriptcraft), rewritten by Gemt.
// The stomach (players in stomach, digestive acid, punt/knockback) lives in the instance script, as in VMaNGOS.

#include "ScriptMgr.h"
#include "Containers.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace
{
enum ClassicAq40CthunCreatures : uint32
{
    EMOTE_CTHUN_WEAKENED            = 11476,
    MOB_EYE_TENTACLE                = 15726,
    MOB_CLAW_TENTACLE               = 15725,
    MOB_GIANT_CLAW_TENTACLE         = 15728,
    MOB_GIANT_EYE_TENTACLE          = 15334,
    MOB_FLESH_TENTACLE              = 15802,

    // MOB_CTHUN_PORTAL = 15896 -> NPC_CTHUN_PORTAL (shared header)
    MOB_SMALL_PORTAL                = 15904,
    MOB_GIANT_PORTAL                = 15910,
};

// C'Thun hotfixes: http://blue.cardplace.com/cache/wow-general/7950998.htm
enum ClassicAq40CthunSpells : uint32
{
    // Phase 1 spells
    SPELL_CTHUN_FREEZE_ANIMATION    = 16245, // Dummy spell to avoid the eye gazing around during dark glare
    SPELL_CTHUN_ROTATE_TRIGGER      = 26137,
    SPELL_CTHUN_ROTATE_NEGATIVE_360 = 26136,
    SPELL_CTHUN_ROTATE_POSITIVE_360 = 26009,

    // Shared spells
    SPELL_CTHUN_GREEN_EYE_BEAM      = 26134,

    // Mob spells
    SPELL_CTHUN_THRASH              = 3391,
    SPELL_CTHUN_GROUND_TREMOR       = 6524,
    SPELL_CTHUN_TENTACLE_BIRTH      = 26262,
    SPELL_CTHUN_SUBMERGE_VISUAL     = 26234,
    SPELL_CTHUN_SUBMERGE_EFFECT     = 21859, // Must be removed after re-emerge after a submerge to remove immunity

    SPELL_CTHUN_GROUND_RUPTURE_PHYSICAL = 26139, // used by small tentacles
    SPELL_CTHUN_HAMSTRING           = 26141, // 26211 is in DBC with more correct ID?
    SPELL_CTHUN_MIND_FLAY           = 26143,
    SPELL_CTHUN_GROUND_RUPTURE_NATURE = 26478, // used by giant tentacles

    // C'thun spells
    SPELL_CTHUN_CARAPACE            = 26156, // Makes C'thun invulnerable
    SPELL_CTHUN_TRANSFORM           = 26232, // Initiates the p1->p2 transform
    SPELL_CTHUN_VULNERABLE          = 26235, // Adds the red color. Does not actually him vulnerable, need to remove carapace for that.
    SPELL_CTHUN_MOUTH_TENTACLE      = 26332, // Spawns the tentacle that "eats" you to stomach and mounts the player on it.
};

std::vector<uint32> const allTentacleTypes =
{
    MOB_EYE_TENTACLE,
    MOB_CLAW_TENTACLE,
    MOB_GIANT_CLAW_TENTACLE,
    MOB_GIANT_EYE_TENTACLE,
    MOB_FLESH_TENTACLE,
    MOB_SMALL_PORTAL,
    MOB_GIANT_PORTAL
};

// VMaNGOS also checks UNIT_FLAG_SILENCED; TC master has no such unit flag (silence is a per-school mask), see CAQ40_CthunCannotCast.
constexpr uint32 CANNOT_CAST_SPELL_MASK = (UNIT_FLAG_PACIFIED | UNIT_FLAG_STUNNED
                                          | UNIT_FLAG_CONFUSED | UNIT_FLAG_FLEEING);

// Rough check against common states that prevent the creature from casting (VMaNGOS HasUnitFlag(CANNOT_CAST_SPELL_MASK))
bool CAQ40_CthunCannotCast(Creature const* caster)
{
    return caster->HasUnitFlag(UnitFlags(CANNOT_CAST_SPELL_MASK)) || caster->IsSilenced(SPELL_SCHOOL_MASK_MAGIC);
}

constexpr float stomachPortPosition[4] =
{
    -8562.0f, 2037.0f, -96.0f, 5.05f
};

constexpr float fleshTentaclePositions[2][4] =
{
    { -8571.0f, 1990.0f, -98.0f, 1.22f },
    { -8525.0f, 1994.0f, -98.0f, 2.12f }
};

constexpr float eyeTentaclePositions[8][3] =
{
    { -8547.269531f, 1986.939941f, 100.490351f },
    { -8556.047852f, 2008.144653f, 100.598129f },
    { -8577.246094f, 2016.939941f, 100.320351f },
    { -8598.457031f, 2008.178467f, 100.320351f },
    { -8607.269531f, 1986.987671f, 100.490351f },
    { -8598.525391f, 1965.769043f, 100.490351f },
    { -8577.340820f, 1956.940063f, 100.536636f },
    { -8556.115234f, 1965.667725f, 100.598129f }
};

// VMaNGOS CreatureAI::DoCastSpellIfCan(target, spell, triggered ? CF_TRIGGERED : 0) == CAST_OK
bool CAQ40_CthunDoCastIfCan(Creature* caster, Unit* target, uint32 spellId, bool triggered = false)
{
    if (!caster || !target)
        return false;
    if (!triggered && caster->HasUnitState(UNIT_STATE_CASTING))
        return false;
    return caster->CastSpell(target, spellId, CastSpellExtraArgs(triggered)) == SPELL_CAST_OK;
}

using SpellTarSelectFunction = std::function<Unit*(Creature*)>;

class CAQ40_CthunSpellTimer
{
public:
    CAQ40_CthunSpellTimer(Creature* creature,
        uint32 spellID,
        uint32 initialCD,
        std::function<uint32()> resetCD,
        bool triggeredSpell,
        SpellTarSelectFunction targetSelectFunc,
        bool retryOnFail = false) :
        m_creature(creature),
        spellID(spellID),
        cooldown(initialCD),
        resetCD(resetCD),
        triggered(triggeredSpell),
        retryOnFail(retryOnFail),
        timeSinceLast(std::numeric_limits<uint32>::max()),
        targetSelectFunc(targetSelectFunc)
    { }

    virtual ~CAQ40_CthunSpellTimer() = default;

    virtual void Reset(int custom = -1)
    {
        if (custom >= 0)
            cooldown = static_cast<uint32>(custom);
        else
        {
            if (!resetCD)
                cooldown = 0;
            else
                cooldown = resetCD();
        }
    }

    // Returns true when the cooldown reaches < diff, a cast is attempted, and cooldown is reset
    virtual bool Update(uint32 diff)
    {
        if (cooldown < diff)
        {
            Unit* target = targetSelectFunc(m_creature);
            bool didCast = false;
            if (target)
            {
                if (CAQ40_CthunDoCastIfCan(m_creature, target, spellID, triggered))
                    didCast = true;
            }
            if (retryOnFail && !didCast)
                return false;
            else
            {
                if (!resetCD)
                    cooldown = 0;
                else
                    cooldown = resetCD();
                timeSinceLast = 0;
                return true;
            }
        }
        else
        {
            cooldown -= diff;
            timeSinceLast += diff;
        }
        return false;
    }

protected:
    Creature* m_creature;
    uint32 spellID;
    uint32 cooldown;
    std::function<uint32()> resetCD;
    bool triggered;
    bool retryOnFail;
    uint32 timeSinceLast;
    SpellTarSelectFunction targetSelectFunc;
};

class CAQ40_CthunOnlyOnceSpellTimer : public CAQ40_CthunSpellTimer
{
public:
    CAQ40_CthunOnlyOnceSpellTimer(Creature* creature, uint32 spellID, uint32 initialCD, std::function<uint32()> resetCD,
        bool triggeredSpell, SpellTarSelectFunction targetSelectFunc, bool retryOnFail = false) :
        CAQ40_CthunSpellTimer(creature, spellID, initialCD, resetCD, triggeredSpell, targetSelectFunc, retryOnFail),
        didOnce(false)
    { }

    void Reset(int custom = -1) override
    {
        CAQ40_CthunSpellTimer::Reset(custom);
        didOnce = false;
    }

    bool Update(uint32 diff) override
    {
        if (!didOnce)
        {
            if (CAQ40_CthunSpellTimer::Update(diff))
                didOnce = true;
        }
        else
            timeSinceLast += diff;
        return didOnce;
    }

private:
    bool didOnce;
};

Player* SelectRandomAliveNotStomach(ClassicTempleOfAhnQirajInstanceScript* instance)
{
    if (!instance)
        return nullptr;

    std::vector<Player*> temp;
    for (MapReference const& ref : instance->instance->GetPlayers())
    {
        if (Player* player = ref.GetSource())
        {
            if (player->IsAlive() && !player->IsGameMaster() && player->IsInCombat() && !instance->PlayerInStomach(player))
                temp.push_back(player);
        }
    }

    if (temp.empty())
        return nullptr;

    return Trinity::Containers::SelectRandomContainerElement(temp);
}

// Helper functions for SpellTimer users
Unit* selectSelfFunc(Creature* c)
{
    return c;
}

Unit* selectTargetFunc(Creature* c)
{
    return c->GetVictim();
}

// ================== PHASE 1 CONSTANTS ==================
constexpr uint32 P1_EYE_TENTACLE_RESPAWN_TIMER   = 45000;
constexpr uint32 SPELL_ROTATE_TRIGGER_CASTTIME   = 3000;
constexpr uint32 GREEN_BEAM_PHASE_DURATION       = 45000;
constexpr uint32 DARK_GLARE_PHASE_DURATION       = 38000;
constexpr uint32 DARK_GLARE_COOLING_DOWN         = 1000;
constexpr uint32 P1_GREEN_BEAM_COOLDOWN          = 3000;  // Green beam has a 2 sec cast time. If this number is > 2000,
                                                          // the cooldown will be P1_GREEN_BEAM_COOLDOWN - 2000
constexpr uint32 P1_CLAW_TENTACLE_RESPAWN_TIMER  = 5000;  // checked against old footage & current fight
// =======================================================

// ================= TRANSITION CONSTANTS ================
constexpr uint32 EYE_DEAD_TO_BODY_EMERGE_DELAY   = 4000;
constexpr uint32 CTHUN_EMERGE_ANIM_DURATION      = 8000;
// =======================================================

// ================== PHASE 2 CONSTANTS ==================
constexpr uint32 P2_EYE_TENTACLE_RESPAWN_TIMER   = 30000;
constexpr uint32 GIANT_CLAW_RESPAWN_TIMER        = 60000;
constexpr uint32 STOMACH_GRAB_COOLDOWN           = 10000;
constexpr uint32 GIANT_EYE_RESPAWN_TIMER         = 60000;
constexpr uint32 STOMACH_GRAB_DURATION           = 3250;
constexpr uint32 WEAKNESS_DURATION               = 45000;
constexpr uint32 P2_FIRST_GIANT_CLAW_SPAWN       = 8000;
constexpr uint32 P2_FIRST_EYE_TENTACLE_SPAWN     = 38000;
constexpr uint32 P2_FIRST_GIANT_EYE_SPAWN        = 38000;
constexpr uint32 P2_FIRST_STOMACH_GRAB           = 18000 - STOMACH_GRAB_DURATION;
// =======================================================

// ======================= MISC ==========================
constexpr uint32 GROUND_RUPTURE_DELAY                   = 0;    // ms after spawn that the ground rupture will be cast
constexpr uint32 HAMSTRING_INITIAL_COOLDOWN             = 2000; // Claw tentacle hamstring cooldown after spawn/tp
uint32 hamstringResetCooldownFunc()                     { return 5000; } // Claw tentacle hamstring cooldown after use
uint32 trashResetCooldownFunc()                         { return urand(6000, 12000); }
uint32 groundTremorResetCooldownFunc()                  { return urand(6000, 12000); }
constexpr uint32 CLAW_TENTACLE_EVADE_PORT_COOLDOWN      = 5000; // How long does a claw tentacle evade before TPing to new target

constexpr uint32 TENTACLE_BIRTH_DURATION                = 3000; // Duration of birth animation and /afk before tentacles start doing stuff

constexpr uint32 GIANT_EYE_BEAM_COOLDOWN                = 2500; // How often will giant eye tentacles cast green beam
constexpr uint32 GIANT_EYE_INITIAL_GREEN_BEAM_COOLDOWN  = 0;    // How long will giant eye wait after spawn before casting
constexpr uint32 MIND_FLAY_COOLDOWN_ON_RESIST           = 1500; // How long do we wait if Eye Tentacle MF resists before retrying cast
constexpr uint32 MIND_FLAY_INITIAL_WAIT_DURATION        = 0;    // How long do we wait after Eye tentacle has spawned until first MF
constexpr uint32 TELEPORT_BURIED_DURATION               = 1000; // How long will a claw tentacle say underground before re-emerging on teleport.
// =======================================================

// ================== THE PULL ===========================
// VMaNGOS defines USE_POSTFIX_PRENERF_PULL_LOGIC: the encounter aggroes the initial puller and places everyone else
// in combat DELAYED_COMBAT_DURATION later (sources: https://pastebin.com/BiC33bU5). Only that branch is ported.
constexpr uint32 DELAYED_COMBAT_DURATION = 9000;
// =======================================================

constexpr TempSummonType TENTACLE_DESPAWN_FLAG = TEMPSUMMON_CORPSE_TIMED_DESPAWN;
}

struct classic_aq40_cthun_tentacle : public ScriptedAI
{
    ClassicTempleOfAhnQirajInstanceScript* m_pInstance;
    float defaultOrientation;

    classic_aq40_cthun_tentacle(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = GetClassicTempleOfAhnQirajInstance(creature);
        if (!m_pInstance)
            TC_LOG_ERROR("scripts", "C'thun tentacle could not find it's instance");

        SetCombatMovement(false);
        defaultOrientation = me->GetOrientation();
    }

    void Reset() override
    {
        me->StopMoving();
        me->SetControlled(true, UNIT_STATE_ROOT);
        DoZoneInCombat();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
    }

    bool UpdateCthunTentacle(uint32 /*diff*/)
    {
        if (!m_pInstance)
            return false;

        if (!m_pInstance->GetPlayerInMap(true, false))
        {
            if (TempSummon* tmpS = me->ToTempSummon())
                tmpS->UnSummon();
            else
                TC_LOG_ERROR("scripts", "CThunTentacle could not cast creature to TemporarySummon*");
            return false;
        }

        // This makes the mob behave like frostnovaed mobs etc, that is,
        // retargetting another top-threat target if current leaves melee range
        me->StopMoving();
        me->SetControlled(true, UNIT_STATE_ROOT);
        return true;
    }

    bool UpdateMelee(bool resetOrientation)
    {
        if (!SelectHostileTargetMelee())
        {
            DoStopAttack();
            if (resetOrientation)
                me->SetOrientation(defaultOrientation);
            return false;
        }
        // VMaNGOS DoMeleeAttackIfReady() - TC melee is automatic
        return true;
    }

    // Custom targetting function.
    // Will only target hostile players/pets that are in melee range.
    // If current target leaves melee range, his threat is reset.
    // IF there are no targets in melee range, the creature will
    // not target anyone.
    // Returns true when the creature has a target.
    bool SelectHostileTargetMelee()
    {
        if (!me->IsAlive())
            return false;

        // If we're casting something its sort-of counter intuitive to return true,
        // but it also means we do have a valid target already, even if it's not melee.
        if (me->IsNonMeleeSpellCast(false))
            return true;

        Unit* oldTarget = me->GetVictim();
        Unit* target = nullptr;

        // First checking if we have some taunt on us (newest first, like VMaNGOS' reverse iteration)
        Unit::AuraEffectList const& tauntAuras = me->GetAuraEffectsByType(SPELL_AURA_MOD_TAUNT);
        std::vector<AuraEffect*> taunts(tauntAuras.begin(), tauntAuras.end());
        for (auto it = taunts.crbegin(); it != taunts.crend(); ++it)
        {
            Unit* caster = (*it)->GetCaster();
            if (!caster)
                continue;

            if (caster->IsInMap(me) && me->IsValidAttackTarget(caster) && me->IsWithinMeleeRange(caster))
            {
                target = caster;
                break;
            }
            else // Target is not in melee and reset his threat
                me->GetThreatManager().ModifyThreatByPercent(caster, -100);
        }
        // So far so good. If we have a target after this loop it means we have a valid target in melee range.

        // If we dont have a target we need to keep searching through the threatlist
        if (!target)
        {
            Creature* self = me;
            Unit* tmpTarget = SelectTarget(SelectTargetMethod::Random, 0, [self](Unit* u) { return self->IsWithinMeleeRange(u); });

            // Resetting threat of old target if it has left melee range
            if (oldTarget && tmpTarget != oldTarget && !oldTarget->IsWithinMeleeRange(me))
                me->GetThreatManager().ModifyThreatByPercent(oldTarget, -100);

            // Need to call getHostileTarget to force an update of the threatlist, bleh
            if (tmpTarget)
                target = me->GetThreatManager().GetCurrentVictim();
        }

        if (target)
        {
            // Nostalrius : Correction bug sheep/fear
            if (!me->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING)
                && (!me->HasAuraType(SPELL_AURA_MOD_FEAR) || me->HasAuraType(SPELL_AURA_PREVENTS_FLEEING)) && !me->HasAuraType(SPELL_AURA_MOD_CONFUSE))
            {
                me->SetInFront(target);
                AttackStart(target);
            }
            return true;
        }
        return false;
    }
};

struct classic_aq40_cthun_portal_tentacle : public classic_aq40_cthun_tentacle
{
private:
    uint32 birthTimer = TENTACLE_BIRTH_DURATION;
public:
    ObjectGuid portalGuid;
    CAQ40_CthunOnlyOnceSpellTimer groundRuptureTimer;

    classic_aq40_cthun_portal_tentacle(Creature* creature, uint32 groundRuptSpellId, uint32 portalId) :
        classic_aq40_cthun_tentacle(creature),
        groundRuptureTimer(creature, groundRuptSpellId, GROUND_RUPTURE_DELAY, nullptr, true, selectSelfFunc)
    {
        Creature* pPortal = DoSpawnCreature(portalId, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 500ms);
        if (pPortal)
        {
            CreatureAI::DoZoneInCombat(pPortal);
            portalGuid = pPortal->GetGUID();
            FixPortalPosition();
        }
        else
            TC_LOG_ERROR("scripts", "cthunPortalTentacle failed to spawn portal with entry {}", portalId);
    }

    void DespawnPortal()
    {
        if (Creature* pCreature = ObjectAccessor::GetCreature(*me, portalGuid))
        {
            if (TempSummon* ts = pCreature->ToTempSummon())
            {
                ts->UnSummon();
                portalGuid.Clear();
            }
            else
                TC_LOG_ERROR("scripts", "Unable to despawn cthunPortalTentacle portal, could not cast to temporarySummon*");
        }
    }

    void Reset() override
    {
        classic_aq40_cthun_tentacle::Reset();
        groundRuptureTimer.Reset();
        birthTimer = TENTACLE_BIRTH_DURATION;
        me->AttackStop();
    }

    void JustDied(Unit* /*killer*/) override
    {
        DespawnPortal();
    }

    bool UpdatePortalTentacle(uint32 diff)
    {
        if (!classic_aq40_cthun_tentacle::UpdateCthunTentacle(diff))
        {
            DespawnPortal();
            return false;
        }

        if (groundRuptureTimer.Update(diff))
        {
            if (birthTimer > diff)
            {
                // Only want to cast it once, and it cant be done in ctor because groundRupture interrupts the animation.
                if (birthTimer == TENTACLE_BIRTH_DURATION)
                    CAQ40_CthunDoCastIfCan(me, me, SPELL_CTHUN_TENTACLE_BIRTH);
                birthTimer -= diff;
            }
        }
        return birthTimer <= diff;
    }

    void FixPortalPosition()
    {
        Creature* pPortal = nullptr;
        if (!portalGuid.IsEmpty())
            pPortal = ObjectAccessor::GetCreature(*me, portalGuid);
        if (!pPortal)
            return;

        // VMaNGOS: pPortal->AI()->SetMeleeAttack(false); pPortal->AI()->SetCombatMovement(false);
        pPortal->SetCanMelee(false);
        if (ScriptedAI* portalAI = dynamic_cast<ScriptedAI*>(pPortal->AI()))
            portalAI->SetCombatMovement(false);

        uint32 portalEntry = pPortal->GetEntry();
        float radius;
        switch (portalEntry)
        {
            case MOB_SMALL_PORTAL: radius = 3.0f; break;
            case MOB_GIANT_PORTAL: radius = 8.0f; break;
            default:
                radius = 3.0f;
                TC_LOG_ERROR("scripts", "C'thun FixPortalPosition unknown portalID {}", portalEntry);
                break;
        }
        // Searching for best z-coordinate to place the portal
        float centerX = me->GetPositionX();
        float centerY = me->GetPositionY();
        float useZ = me->GetPositionZ();
        float angle = float(M_PI) / 4.0f;
        float highZ = useZ;
        float avg_height = 0.0f;
        uint8 inliers = 0;
        for (uint8 i = 0; i < 8; i++)
        {
            float x = centerX + std::cos(float(i) * angle) * radius;
            float y = centerY + std::sin(float(i) * angle) * radius;
            float z = me->GetMap()->GetHeight(me->GetPhaseShift(), x, y, useZ);
            float deviation = std::fabs(useZ - z);
            // Any deviation >= 0.5 we consider outliers as we dont want to handle sloped terrain
            if (deviation < 0.5f)
            {
                if (z > highZ)
                    highZ = z;
                avg_height += z;
                inliers++;
            }
        }
        // VMaNGOS divides by inliers unconditionally (NaN when 0, which never moves the portal up)
        if (inliers)
        {
            avg_height /= inliers;
            // Only move portal up if the average height is higher than the creatures height
            if (avg_height > useZ)
                useZ = highZ;
        }
        pPortal->NearTeleportTo(me->GetPositionX(), me->GetPositionY(), useZ, 0.0f);   // VMaNGOS NearLandTo
    }
};

struct classic_aq40_cthun_claw_tentacle : public classic_aq40_cthun_portal_tentacle
{
    uint32 EvadeTimer;
    CAQ40_CthunSpellTimer hamstringTimer;
    uint32 teleportBuriedTimer = 0;
    uint32 feignDeathTimer = 0;

    enum eClawState
    {
        NORMAL,
        FEIGN_IN_PROCES,
        BURRIED,
    };
    eClawState clawState = NORMAL;

    classic_aq40_cthun_claw_tentacle(Creature* creature, uint32 groundRuptSpellId, uint32 portalId) :
        classic_aq40_cthun_portal_tentacle(creature, groundRuptSpellId, portalId),
        EvadeTimer(0),
        hamstringTimer(creature, SPELL_CTHUN_HAMSTRING, HAMSTRING_INITIAL_COOLDOWN, hamstringResetCooldownFunc, false, selectTargetFunc, true)
    { }

    void Reset() override
    {
        classic_aq40_cthun_portal_tentacle::Reset();
        hamstringTimer.Reset(HAMSTRING_INITIAL_COOLDOWN);
        EvadeTimer = CLAW_TENTACLE_EVADE_PORT_COOLDOWN;
        teleportBuriedTimer = 0;

        // If reset is called after a teleport, it regains full HP.
        // Todo: Should we also clear any debuffs?
        me->SetFullHealth();

        clawState = eClawState::NORMAL;
        feignDeathTimer = 0;
    }

    bool UpdateClawTentacle(uint32 diff)
    {
        if (!classic_aq40_cthun_portal_tentacle::UpdatePortalTentacle(diff))
            return false;

        switch (clawState)
        {
            case NORMAL:
                updateNormal(diff);
                return true;
            case FEIGN_IN_PROCES:
                updateFeign(diff);
                return false;
            case BURRIED:
                updateBurried(diff);
                return false;
            default:
                TC_LOG_ERROR("scripts", "Unknown UpdateClawTentacle state.");
                return false;
        }
    }

private:
    void updateNormal(uint32 diff)
    {
        if (UpdateMelee(false))
        {
            EvadeTimer = CLAW_TENTACLE_EVADE_PORT_COOLDOWN;
            hamstringTimer.Update(diff);
        }
        else
        {
            if (EvadeTimer < diff)
            {
                clawState = eClawState::FEIGN_IN_PROCES;
                feignDeathTimer = 1000;
                me->CastSpell(me, SPELL_CTHUN_SUBMERGE_VISUAL, false);
            }
            else
                EvadeTimer -= diff;
        }
    }

    void updateFeign(uint32 diff)
    {
        if (feignDeathTimer < diff)
        {
            clawState = eClawState::BURRIED;
            teleportBuriedTimer = TELEPORT_BURIED_DURATION;
            setVisibility(false);
        }
        else
            feignDeathTimer -= diff;
    }

    void updateBurried(uint32 diff)
    {
        if (teleportBuriedTimer < diff)
        {
            // Done being burried, time to teleport on a new target.
            // If we're successfull in selecting a new target, reset will reset
            // all necessary cooldowns, including setting the correct clawState (NORMAL)
            if (TeleportOnNewRandomTarget())
            {
                ResetThreatList();
                me->RemoveAurasDueToSpell(SPELL_CTHUN_SUBMERGE_VISUAL);
                setVisibility(true);
                me->RemoveAurasDueToSpell(SPELL_CTHUN_SUBMERGE_EFFECT);
                Reset();
            }
        }
        else
            teleportBuriedTimer -= diff;
    }

    bool TeleportOnNewRandomTarget()
    {
        if (Player* target = SelectRandomAliveNotStomach(m_pInstance))
        {
            Position pos = me->GetRandomPoint(target->GetPosition(), 0.5f);
            me->NearTeleportTo(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f);

            if (ObjectAccessor::GetCreature(*me, portalGuid))
                FixPortalPosition();
            return true;
        }
        return false;
    }

    void setVisibility(bool visiblityOn)
    {
        Creature* pCreature = ObjectAccessor::GetCreature(*me, portalGuid);
        me->SetVisible(visiblityOn);
        if (pCreature)
            pCreature->SetVisible(visiblityOn);
    }
};

struct classic_mob_eye_tentacle : public classic_aq40_cthun_portal_tentacle
{
    uint32 nextMFAttempt = MIND_FLAY_INITIAL_WAIT_DURATION;
    ObjectGuid currentMFTarget;

    classic_mob_eye_tentacle(Creature* creature) :
        classic_aq40_cthun_portal_tentacle(creature, SPELL_CTHUN_GROUND_RUPTURE_PHYSICAL, MOB_SMALL_PORTAL)
    { }

    void Reset() override
    {
        classic_aq40_cthun_portal_tentacle::Reset();
        nextMFAttempt = MIND_FLAY_INITIAL_WAIT_DURATION;
        currentMFTarget.Clear();
    }

    void AttackStart(Unit* who) override
    {
        // Prevents AttacStart from stopping the cast animation
        if (!me->IsNonMeleeSpellCast(false))
            ScriptedAI::AttackStart(who);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!classic_aq40_cthun_portal_tentacle::UpdatePortalTentacle(diff))
            return;

        if (nextMFAttempt > diff)
            nextMFAttempt -= diff;
        else
            nextMFAttempt = 0;

        // If we are not already casting, try to start casting
        if (!me->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        {
            currentMFTarget.Clear();
            bool didCast = false;
            // Rough check against common auras that prevent the creature from casting,
            // before getting a random target etc
            if (!CAQ40_CthunCannotCast(me))
            {
                // nextMFAttempt acts as a fake gcd in case of resist
                if (nextMFAttempt == 0)
                {
                    if (Player* target = SelectRandomAliveNotStomach(m_pInstance))
                    {
                        if (CAQ40_CthunDoCastIfCan(me, target, SPELL_CTHUN_MIND_FLAY))
                        {
                            currentMFTarget = target->GetGUID();
                            me->SetFacingToObject(target);
                            me->SetTarget(currentMFTarget);
                            didCast = true;
                            nextMFAttempt = MIND_FLAY_COOLDOWN_ON_RESIST;
                        }
                    }
                }
            }
            if (!didCast)
                UpdateMelee(false);
        }
        else
        {
            // Stop casting on current target if it's been ported to stomach
            if (Player* currentCastTarget = ObjectAccessor::GetPlayer(*me, currentMFTarget))
                if (m_pInstance->PlayerInStomach(currentCastTarget))
                    me->InterruptSpell(CURRENT_CHANNELED_SPELL);
        }
    }
};

struct classic_mob_claw_tentacle : public classic_aq40_cthun_claw_tentacle
{
    classic_mob_claw_tentacle(Creature* creature) :
        classic_aq40_cthun_claw_tentacle(creature, SPELL_CTHUN_GROUND_RUPTURE_PHYSICAL, MOB_SMALL_PORTAL)
    { }

    void Reset() override
    {
        classic_aq40_cthun_claw_tentacle::Reset();
    }

    void UpdateAI(uint32 diff) override
    {
        classic_aq40_cthun_claw_tentacle::UpdateClawTentacle(diff);
    }
};

struct classic_mob_giant_claw_tentacle : public classic_aq40_cthun_claw_tentacle
{
    CAQ40_CthunSpellTimer groundTremorTimer;
    CAQ40_CthunSpellTimer trashTimer;

    classic_mob_giant_claw_tentacle(Creature* creature) :
        classic_aq40_cthun_claw_tentacle(creature, SPELL_CTHUN_GROUND_RUPTURE_NATURE, MOB_GIANT_PORTAL),
        groundTremorTimer(creature, SPELL_CTHUN_GROUND_TREMOR, groundTremorResetCooldownFunc(), groundTremorResetCooldownFunc, true, selectTargetFunc, true),
        trashTimer(creature, SPELL_CTHUN_THRASH, trashResetCooldownFunc(), trashResetCooldownFunc, false, selectTargetFunc, true)
    { }

    void Reset() override
    {
        classic_aq40_cthun_claw_tentacle::Reset();
        groundTremorTimer.Reset();
        trashTimer.Reset();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!classic_aq40_cthun_claw_tentacle::UpdateClawTentacle(diff))
            return;

        groundTremorTimer.Update(diff);
        trashTimer.Update(diff);
    }
};

struct classic_mob_giant_eye_tentacle : public classic_aq40_cthun_portal_tentacle
{
    uint32 BeamTimer = GIANT_EYE_INITIAL_GREEN_BEAM_COOLDOWN;
    ObjectGuid beamTargetGuid;
    bool isCasting = false;

    classic_mob_giant_eye_tentacle(Creature* creature) :
        classic_aq40_cthun_portal_tentacle(creature, SPELL_CTHUN_GROUND_RUPTURE_NATURE, MOB_GIANT_PORTAL)
    { }

    void Reset() override
    {
        classic_aq40_cthun_portal_tentacle::Reset();
        BeamTimer = GIANT_EYE_INITIAL_GREEN_BEAM_COOLDOWN;
        isCasting = false;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!isCasting)
        {
            if (!classic_aq40_cthun_portal_tentacle::UpdatePortalTentacle(diff))
                return;
        }

        if (!me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            beamTargetGuid.Clear();
            isCasting = false;
        }

        if (BeamTimer < diff)
        {
            // Rough check against common auras that prevent the creature from casting,
            // before getting a random target etc
            if (!CAQ40_CthunCannotCast(me))
            {
                if (Player* target = SelectRandomAliveNotStomach(m_pInstance))
                {
                    if (CAQ40_CthunDoCastIfCan(me, target, SPELL_CTHUN_GREEN_EYE_BEAM))
                    {
                        beamTargetGuid = target->GetGUID();
                        isCasting = true;
                        BeamTimer = GIANT_EYE_BEAM_COOLDOWN;
                    }
                }
            }
        }
        else
        {
            BeamTimer -= diff;
            if (me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            {
                // Stop casting on current target if it's been ported to stomach
                // and immediately start casting on a new target
                if (Player* currentCastTarget = ObjectAccessor::GetPlayer(*me, beamTargetGuid))
                {
                    if (m_pInstance->PlayerInStomach(currentCastTarget))
                    {
                        me->InterruptNonMeleeSpells(false);
                        BeamTimer = 0;
                    }
                }
            }
        }
        if (!isCasting)
            UpdateMelee(false);
    }
};

struct classic_mob_giant_flesh_tentacle : public classic_aq40_cthun_tentacle
{
    classic_mob_giant_flesh_tentacle(Creature* creature) : classic_aq40_cthun_tentacle(creature) { }

    void Reset() override
    {
        classic_aq40_cthun_tentacle::Reset();
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        UpdateMelee(true);
    }
};

struct classic_boss_eye_of_cthun : public ScriptedAI
{
    // The eye phases can be a bit difficult to decode due to cast time and "cooling down" duration
    // of red beam.
    // There should be 45 seconds of "green beam phase"
    // cast time of dark glare is 3 seconds
    // once dark glare is up, boss rotates and keeps dark glare up for 38 seconds
    // after 38 seconds he stops rotating and removes dark glare from himself, this is a 1 second period where nothing happens
    // he then starts a new 45 seconds of green beam
    // Dark glare should start casting every 86 seconds
    // 45 sec green beam
    // 3 sec cast dark glare
    // 38 sec dark glare
    // 1 sec "cooling down"
    // = 86 seconds
    ClassicTempleOfAhnQirajInstanceScript* m_pInstance;

    bool IsAlreadyPulled = false;

    uint32 eyeBeamCooldown = 0;
    uint32 eyeBeamCastCount = 0;

    ObjectGuid initialPullerGuid;
    enum CthunEyePhase
    {
        GREEN_BEAM,
        DARK_GLARE_CAST,
        DARK_GLARE,
        DARK_GLARE_COOLING
    };
    CthunEyePhase currentPhase = GREEN_BEAM;

    uint32 greenBeamPhaseTimer = 0;
    uint32 darkGlareCastTimer = 0;
    uint32 darkGlareTimer = 0;
    uint32 darkGlareCoolingTimer = 0;

    classic_boss_eye_of_cthun(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
        me->SetCanMelee(false); // VMaNGOS never calls DoMeleeAttackIfReady for the eye

        m_pInstance = GetClassicTempleOfAhnQirajInstance(creature);
        if (!m_pInstance)
            TC_LOG_ERROR("scripts", "SD0: No Instance eye_of_cthunAI");
    }

    void Pull(Unit* puller)
    {
        me->SetFaction(14);     // VMaNGOS SetFactionTemporary(14), restored in Reset()

        initialPullerGuid = puller->GetGUID();
        CastGreenBeam(puller);

        IsAlreadyPulled = true;
    }

    void JustEngagedWith(Unit* puller) override
    {
        // Just in case someone manages to get through the AggroRadius logic in C'thuns AI
        // we make sure the proper pull-sequence is initiated by calling C'thuns attackstart.
        if (!me->IsInCombat() && m_pInstance)
        {
            if (Creature* pCthun = m_pInstance->GetSingleCreatureFromStorage(NPC_CTHUN))
                if (pCthun->IsAIEnabled())
                    pCthun->AI()->AttackStart(puller);
        }
    }

    void Reset() override
    {
        currentPhase = GREEN_BEAM;
        initialPullerGuid.Clear();
        eyeBeamCastCount = 0;
        eyeBeamCooldown = P1_GREEN_BEAM_COOLDOWN;
        greenBeamPhaseTimer = GREEN_BEAM_PHASE_DURATION;

        IsAlreadyPulled = false;

        me->RestoreFaction();   // VMaNGOS temporary faction restore
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);   // VMaNGOS UNIT_FLAG_SPAWNING
        // need to reset the orientation in case of wipe during glare phase
        me->SetOrientation(3.44f);
        RemoveGlarePhaseSpells();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance)
            return;

        if (!IsAlreadyPulled)
        {
            me->SetTarget(ObjectGuid::Empty);
            return;
        }

        // No combat reset managment. C'thuns body will reset this creature when needed.

        // Yes, could easily make all these different timers into just two, but
        // this approach is much easier to understand, debug and tune.
        switch (currentPhase)
        {
            case GREEN_BEAM:
                if (greenBeamPhaseTimer < diff)
                {
                    if (EnterDarkGlarePhase())
                    {
                        darkGlareCastTimer = SPELL_ROTATE_TRIGGER_CASTTIME;
                        currentPhase = DARK_GLARE_CAST;
                    }
                }
                else
                {
                    greenBeamPhaseTimer -= diff;
                    UpdateGreenBeamPhase(diff);
                }
                break;
            case DARK_GLARE_CAST:
                if (darkGlareCastTimer < diff)
                {
                    currentPhase = DARK_GLARE;
                    darkGlareTimer = DARK_GLARE_PHASE_DURATION;
                }
                else
                    darkGlareCastTimer -= diff;
                break;
            case DARK_GLARE:
                if (darkGlareTimer < diff)
                {
                    RemoveGlarePhaseSpells();
                    currentPhase = DARK_GLARE_COOLING;
                    darkGlareCoolingTimer = DARK_GLARE_COOLING_DOWN;
                }
                else
                    darkGlareTimer -= diff;
                break;
            case DARK_GLARE_COOLING:
                if (darkGlareCoolingTimer < diff)
                {
                    currentPhase = GREEN_BEAM;
                    greenBeamPhaseTimer = GREEN_BEAM_PHASE_DURATION;
                    eyeBeamCooldown = 0;
                }
                else
                    darkGlareCoolingTimer -= diff;
                break;
            default:
                TC_LOG_ERROR("scripts", "CThun eye update called with incorrect state: {}", uint32(currentPhase));
                break;
        }
    }

    void UpdateGreenBeamPhase(uint32 diff)
    {
        if (me->HasAura(SPELL_CTHUN_FREEZE_ANIMATION))
            me->RemoveAurasDueToSpell(SPELL_CTHUN_FREEZE_ANIMATION);

        if (eyeBeamCooldown < diff)
        {
            // USE_POSTFIX_PRENERF_PULL_LOGIC
            if (Unit* target = SelectRandomAliveNotStomach(m_pInstance))
                CastGreenBeam(target);
        }
        else
            eyeBeamCooldown -= diff;
    }

    void AttackStart(Unit* /*who*/) override
    {
        // dont do nothin'
    }

    bool EnterDarkGlarePhase()
    {
        me->InterruptNonMeleeSpells(false);
        // Select random target for dark beam to start on and start the trigger
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
        {
            // Remove the target focus but allow the boss to face the current victim
            DoStopAttack();
            me->SetFacingToObject(target);
            if (CAQ40_CthunDoCastIfCan(me, me, SPELL_CTHUN_ROTATE_TRIGGER))
            {
                if (!me->HasAura(SPELL_CTHUN_FREEZE_ANIMATION))
                    me->CastSpell(me, SPELL_CTHUN_FREEZE_ANIMATION, true);
                me->SetTarget(ObjectGuid::Empty);
                return true;
            }
        }
        return false;
    }

    void RemoveGlarePhaseSpells()
    {
        if (me->HasAura(SPELL_CTHUN_ROTATE_NEGATIVE_360))
            me->RemoveAurasDueToSpell(SPELL_CTHUN_ROTATE_NEGATIVE_360);
        else if (me->HasAura(SPELL_CTHUN_ROTATE_POSITIVE_360))
            me->RemoveAurasDueToSpell(SPELL_CTHUN_ROTATE_POSITIVE_360);
    }

    bool CastGreenBeam(Unit* target)
    {
        if (CAQ40_CthunDoCastIfCan(me, target, SPELL_CTHUN_GREEN_EYE_BEAM))
        {
            me->SetTarget(target->GetGUID());
            ++eyeBeamCastCount;
            eyeBeamCooldown = P1_GREEN_BEAM_COOLDOWN;
            return true;
        }
        return false;
    }
};

struct classic_boss_cthun : public ScriptedAI
{
    enum CThunPhase
    {
        PHASE_EYE_NORMAL = 0,
        PHASE_EYE_DARK_GLARE = 1,
        PHASE_PRE_TRANSITION = 2,
        PHASE_TRANSITION = 3,
        PHASE_CTHUN_INVULNERABLE = 4,
        PHASE_CTHUN_WEAKENED = 5,
        PHASE_CTHUN_DONE = 6,
    };

    ClassicTempleOfAhnQirajInstanceScript* m_pInstance;

    bool inProgress = false;
    CThunPhase currentPhase = PHASE_EYE_NORMAL;

    // P1 timers
    uint32 eyeTentacleTimer_p1 = 0;
    uint32 clawTentacleTimer_p1 = 0;

    // P2 timers
    uint32 weaknessTimer = 0;
    uint32 eyeTentacleTimer = 0;
    uint32 cthunEmergeTimer = 0;
    uint32 giantEyeTentacleTimer = 0;
    uint32 stomachEnterPortTimer = 0;
    uint32 giantClawTentacleTimer = 0;
    uint32 nextStomachEnterGrabTimer = 0;

    // USE_POSTFIX_PRENERF_PULL_LOGIC
    uint32 delayedCombatEntryTimer = 0;
    bool isInCombatWithZone = false;

    ObjectGuid eyeGuid;
    ObjectGuid puntCreatureGuid;
    ObjectGuid StomachEnterTargetGUID;

    std::vector<ObjectGuid> fleshTentacles;

    uint32 wipeRespawnEyeTimer = 0;

    // TC: summons are deferred from the AI constructor / initial Reset() (VMaNGOS) to the first JustAppeared()
    bool hasAppeared = false;

    classic_boss_cthun(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
        me->SetCanMelee(false); // VMaNGOS never calls DoMeleeAttackIfReady for C'Thun

        m_pInstance = GetClassicTempleOfAhnQirajInstance(creature);
        if (!m_pInstance)
            TC_LOG_ERROR("scripts", "SD0: No Instance for cthunAI");
    }

    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();

        if (hasAppeared)
            return;
        hasAppeared = true;

        // VMaNGOS constructor
        if (Creature* pPortal = DoSpawnCreature(NPC_CTHUN_PORTAL, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_CORPSE_DESPAWN, 0ms))
        {
            pPortal->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);     // VMaNGOS UNIT_FLAG_NOT_SELECTABLE
            pPortal->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);     // VMaNGOS UNIT_FLAG_SPAWNING
        }

        // VMaNGOS Reset() from the constructor
        if (m_pInstance && me->IsAlive() && !wipeRespawnEyeTimer)
            CheckRespawnEye();
    }

    void AttackStart(Unit* who) override
    {
        if (!me->IsInCombat())
        {
            if (!m_pInstance)
                return;

            Creature* pEye = m_pInstance->GetCreatureByGuid(eyeGuid);
            if (!pEye)
            {
                TC_LOG_ERROR("scripts", "cthunAI::AggroRadius could not find pEye");
                return;
            }
            if (classic_boss_eye_of_cthun* eyeAI = dynamic_cast<classic_boss_eye_of_cthun*>(pEye->AI()))
                eyeAI->Pull(who);
            ScriptedAI::AttackStart(who);
            // USE_POSTFIX_PRENERF_PULL_LOGIC
            me->SetInCombatWith(who);
            pEye->SetInCombatWith(who);

            m_pInstance->SetData(TYPE_CTHUN, IN_PROGRESS);
        }
        else
            ScriptedAI::AttackStart(who);
    }

    void DespawnAllTentacles()
    {
        std::list<Creature*> creaturesToDespawn;
        for (uint32 entry : allTentacleTypes)
            me->GetCreatureListWithEntryInGrid(creaturesToDespawn, entry, 350.0f);

        for (Creature* creature : creaturesToDespawn)
        {
            if (classic_aq40_cthun_portal_tentacle* cpt = dynamic_cast<classic_aq40_cthun_portal_tentacle*>(creature->AI()))
                cpt->DespawnPortal();
            if (TempSummon* ts = creature->ToTempSummon())
                ts->UnSummon();
        }
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_CTHUN, FAIL);
    }

    void Reset() override
    {
        // USE_POSTFIX_PRENERF_PULL_LOGIC
        delayedCombatEntryTimer = DELAYED_COMBAT_DURATION;
        isInCombatWithZone = false;

        inProgress = false;
        currentPhase = PHASE_EYE_NORMAL;
        if (!m_pInstance)
            return;

        cthunEmergeTimer        = CTHUN_EMERGE_ANIM_DURATION;
        clawTentacleTimer_p1    = P1_CLAW_TENTACLE_RESPAWN_TIMER;
        eyeTentacleTimer_p1     = P1_EYE_TENTACLE_RESPAWN_TIMER;

        ResetartUnvulnerablePhase(false);

        // Reset visibility
        me->SetVisible(false);
        me->InterruptNonMeleeSpells(false);
        if (me->HasAura(SPELL_CTHUN_VULNERABLE))
            me->RemoveAurasDueToSpell(SPELL_CTHUN_VULNERABLE);

        // Demorph should set C'thuns modelId back to burrowed.
        // Also removing SPELL_TRANSFORM in case of reset just as he was casting that.
        me->RemoveAurasDueToSpell(SPELL_CTHUN_TRANSFORM);
        me->DeMorph();

        me->SetVisible(true);
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);     // VMaNGOS UNIT_FLAG_NOT_SELECTABLE
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);     // VMaNGOS UNIT_FLAG_SPAWNING

        // Hack to allow eye-respawning with .respawn chat-command.
        // On regular wipe in p2 it's respawned from UpdateAI()
        if (!wipeRespawnEyeTimer && hasAppeared)
            CheckRespawnEye();

        // Force despawn any tentacles or portals alive.
        DespawnAllTentacles();

        if (me->IsAlive())
            me->SetFullHealth();
    }

    void CheckRespawnEye()
    {
        Creature* pEye = nullptr;
        if (!me->IsAlive())
        {
            // Despawning the eye if something weird has happened and C'thun is dead.
            if ((pEye = m_pInstance->GetCreatureByGuid(eyeGuid)))
                pEye->DespawnOrUnsummon();
        }
        else
        {
            // Respawning eye if it exists, but is dead.
            // Otherwise attempting to spawn a new eye
            if ((pEye = m_pInstance->GetCreatureByGuid(eyeGuid)))
            {
                if (!pEye->IsAlive())
                {
                    pEye->Respawn();
                    if (pEye->IsAIEnabled())
                        pEye->AI()->Reset(); // todo: remove if we KNOW that creature::respawn() calls Reset()
                }
                else if (pEye->IsAIEnabled())
                    pEye->AI()->EnterEvadeMode();
            }
            else if ((pEye = DoSpawnCreature(NPC_EYE_OF_C_THUN, 0.0f, 0.0f, 0.0f, 3.44f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, Milliseconds(EYE_DEAD_TO_BODY_EMERGE_DELAY))))
                eyeGuid = pEye->GetGUID();
            else
                TC_LOG_ERROR("scripts", "C'thun was unable to summon it's eye");
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == MOB_FLESH_TENTACLE)
        {
            auto it = std::find(fleshTentacles.begin(), fleshTentacles.end(), summon->GetGUID());
            if (it != fleshTentacles.end())
                fleshTentacles.erase(it);
        }
        else if (summon->GetEntry() == NPC_EYE_OF_C_THUN)
            currentPhase = PHASE_PRE_TRANSITION;
    }

    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == MOB_FLESH_TENTACLE)
        {
            if (fleshTentacles.size() > 1)
                TC_LOG_ERROR("scripts", "Flesh tentacle summoned, but there are already {} tentacles up.", uint32(fleshTentacles.size()));
            fleshTentacles.emplace_back(summon->GetGUID());
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance || !me->IsAlive())
            return;

        // Delaying respawn of eye if it was a wipe so we get the re-emerge animation before spawn
        if (wipeRespawnEyeTimer > 0)
        {
            wipeRespawnEyeTimer -= std::min(diff, wipeRespawnEyeTimer);
            if (wipeRespawnEyeTimer < diff)
            {
                CheckRespawnEye();
                wipeRespawnEyeTimer = 0;
            }
        }

        if (!inProgress)
        {
            // Wait with calling aggroRadius until eye has respawned
            if (!wipeRespawnEyeTimer && AggroRadius())
                inProgress = true;
            else
                return;
        }
        // Not resetting during transition phase, just wait until it's over, then we reset.
        else if (!m_pInstance->GetPlayerInMap(true, false) && currentPhase != PHASE_TRANSITION && currentPhase != PHASE_PRE_TRANSITION)
        {
            inProgress = false;
            wipeRespawnEyeTimer = 5000;
            EnterEvadeMode();
            // C'thuns eye will enter evade mode through C'thuns Reset() call
        }

        // USE_POSTFIX_PRENERF_PULL_LOGIC
        if (!isInCombatWithZone)
        {
            if (delayedCombatEntryTimer < diff)
            {
                isInCombatWithZone = true;
                DoZoneInCombat();
                if (Creature* pEye = m_pInstance->GetCreatureByGuid(eyeGuid))
                    CreatureAI::DoZoneInCombat(pEye);
            }
            else
                delayedCombatEntryTimer -= diff;
        }

        me->SetTarget(ObjectGuid::Empty);

        switch (currentPhase)
        {
            case PHASE_EYE_NORMAL:
                UpdateTentaclesP1(diff);
                break;
            case PHASE_EYE_DARK_GLARE:
                UpdateTentaclesP1(diff);
                break;
            case PHASE_PRE_TRANSITION:
                // We just wait for eye to death animation before it's despawwn will trigger PHASE_TRANSITION
                break;
            case PHASE_TRANSITION:
                UpdateTransitionPhase(diff);
                break;
            case PHASE_CTHUN_INVULNERABLE:
                UpdateInvulnerablePhase(diff);
                CheckIfAllDead();
                break;
            case PHASE_CTHUN_WEAKENED:
                UpdateWeakenedPhase(diff);
                CheckIfAllDead();
                break;
            case PHASE_CTHUN_DONE:
                break;
            default:
                TC_LOG_ERROR("scripts", "C'Thun in bugged state: {}", uint32(currentPhase));
                break;
        }
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        // Despawn will happen EYE_DEAD_TO_BODY_EMERGE_DELAY time after eye death
        if (summon->GetEntry() == NPC_EYE_OF_C_THUN)
        {
            currentPhase = PHASE_TRANSITION;

            ResetartUnvulnerablePhase();

            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            me->SetVisible(false);
            me->CastSpell(me, SPELL_CTHUN_TRANSFORM, true);
            me->SetVisible(true);
            me->CastSpell(me, SPELL_CTHUN_TRANSFORM, true);
        }
        else if (summon->GetEntry() == MOB_FLESH_TENTACLE)
        {
            auto it = std::find(fleshTentacles.begin(), fleshTentacles.end(), summon->GetGUID());
            if (it != fleshTentacles.end())
                fleshTentacles.erase(it);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
        {
            currentPhase = PHASE_CTHUN_DONE;
            m_pInstance->SetData(TYPE_CTHUN, DONE);
            DespawnAllTentacles();
        }
    }

    void ResetartUnvulnerablePhase(bool spawnFleshTentacles = true)
    {
        giantClawTentacleTimer = P2_FIRST_GIANT_CLAW_SPAWN;
        eyeTentacleTimer = P2_FIRST_EYE_TENTACLE_SPAWN;
        giantEyeTentacleTimer = P2_FIRST_GIANT_EYE_SPAWN;

        StomachEnterTargetGUID.Clear();
        stomachEnterPortTimer = 0;
        nextStomachEnterGrabTimer = P2_FIRST_STOMACH_GRAB;

        weaknessTimer = 0;
        if (spawnFleshTentacles)
            SpawnFleshTentacles();
        me->CastSpell(me, SPELL_CTHUN_CARAPACE, true);
    }

    bool UnitShouldPull(Unit* unit)
    {
        float distToCthun = unit->GetExactDist(me);   // VMaNGOS GetDistance3dToCenter
        float zDist = std::fabs(unit->GetPositionZ() - 100.0f);
        // If we're at the same Z axis of cthun, or within the maximum possible pull distance
        if (zDist < 10.0f && distToCthun < 95.0f && unit->IsWithinLOSInMap(me))
        {
            //xxx: it will still be possible to hide behind one of the pillars in the room to avoid pulling,
            //but I don't think it's really something to take advantage of anyway
            return true;
        }
        return false;
    }

    bool AggroRadius()
    {
        // Large aggro radius
        for (MapReference const& ref : me->GetMap()->GetPlayers())
        {
            Player* pPlayer = ref.GetSource();
            if (pPlayer && pPlayer->IsAlive() && !pPlayer->IsGameMaster())
            {
                if (UnitShouldPull(pPlayer))
                {
                    AttackStart(pPlayer);
                    return true;
                }
                else if (Pet* pPet = pPlayer->GetPet())
                {
                    if (UnitShouldPull(pPet))
                    {
                        AttackStart(pPlayer); //screw the pet, go straight for the head!
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool CheckIfAllDead()
    {
        if (!SelectRandomAliveNotStomach(m_pInstance))
        {
            if (m_pInstance->KillPlayersInStomach())
            {
                // VMaNGOS: m_creature->OnLeaveCombat()
                me->CombatStop(true);
                return true;
            }
        }
        return false;
    }

    void UpdateTentaclesP1(uint32 diff)
    {
        if (SpawnTentacleIfReady(diff, clawTentacleTimer_p1, 0, MOB_CLAW_TENTACLE))
            clawTentacleTimer_p1 = P1_CLAW_TENTACLE_RESPAWN_TIMER;

        if (eyeTentacleTimer_p1 < diff)
        {
            SpawnEyeTentacles();
            eyeTentacleTimer_p1 = P1_EYE_TENTACLE_RESPAWN_TIMER;
        }
        else
            eyeTentacleTimer_p1 -= diff;
    }

    void UpdateTransitionPhase(uint32 diff)
    {
        UpdateTentaclesP2(diff);
        UpdateStomachGrab(diff);

        if (cthunEmergeTimer < diff)
        {
            me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);   // VMaNGOS UNIT_FLAG_SPAWNING
            DoZoneInCombat();

            currentPhase = PHASE_CTHUN_INVULNERABLE;
        }
        else
            cthunEmergeTimer -= diff;
    }

    void UpdateInvulnerablePhase(uint32 diff)
    {
        // Weaken if both Flesh Tentacles are killed
        if (fleshTentacles.empty())
        {
            weaknessTimer = WEAKNESS_DURATION;

            ClassicScriptText(EMOTE_CTHUN_WEAKENED, me);
            // If there is a grabbed player, release him.
            if (!StomachEnterTargetGUID.IsEmpty())
                if (Player* pPlayer = ObjectAccessor::GetPlayer(*me, StomachEnterTargetGUID))
                    pPlayer->RemoveAurasDueToSpell(SPELL_CTHUN_MOUTH_TENTACLE);

            // Remove the damage reduction aura
            me->CastSpell(me, SPELL_CTHUN_VULNERABLE, true);
            // Make him glow all red and nice
            me->RemoveAurasDueToSpell(SPELL_CTHUN_CARAPACE);

            currentPhase = PHASE_CTHUN_WEAKENED;
        }
        else
        {
            UpdateTentaclesP2(diff);
            UpdateStomachGrab(diff);
        }
    }

    void UpdateWeakenedPhase(uint32 diff)
    {
        // If weakend runs out
        if (weaknessTimer < diff)
        {
            ResetartUnvulnerablePhase();
            //note: can set visibility off and on again after removing vulnerable spell,
            // if it does not visually dissapear
            me->RemoveAurasDueToSpell(SPELL_CTHUN_VULNERABLE);

            currentPhase = PHASE_CTHUN_INVULNERABLE;
        }
        else
            weaknessTimer -= diff;
    }

    void SpawnFleshTentacles()
    {
        if (!fleshTentacles.empty())
            TC_LOG_ERROR("scripts", "SpawnFleshTentacles() called, but there are already {} tentacles up.", uint32(fleshTentacles.size()));

        //Spawn 2 flesh tentacles in C'thun stomach
        for (auto const& fleshTentaclePosition : fleshTentaclePositions)
        {
            me->SummonCreature(MOB_FLESH_TENTACLE,
                fleshTentaclePosition[0],
                fleshTentaclePosition[1],
                fleshTentaclePosition[2],
                fleshTentaclePosition[3],
                TENTACLE_DESPAWN_FLAG, 1500ms);
        }
    }

    void UpdateStomachGrab(uint32 diff)
    {
        if (!StomachEnterTargetGUID.IsEmpty())
        {
            if (stomachEnterPortTimer < diff)
            {
                if (Player* pPlayer = ObjectAccessor::GetPlayer(*me, StomachEnterTargetGUID))
                {
                    DoTeleportPlayer(pPlayer, stomachPortPosition[0], stomachPortPosition[1], stomachPortPosition[2], stomachPortPosition[3]);
                    pPlayer->RemoveAurasDueToSpell(SPELL_CTHUN_MOUTH_TENTACLE);
                    if (m_pInstance)
                        m_pInstance->AddPlayerToStomach(pPlayer);
                }

                StomachEnterTargetGUID.Clear();
                stomachEnterPortTimer = 0;
            }
            else
                stomachEnterPortTimer -= diff;
        }

        if (nextStomachEnterGrabTimer < diff)
        {
            if (Player* target = SelectRandomAliveNotStomach(m_pInstance))
            {
                target->InterruptNonMeleeSpells(false);
                target->CastSpell(target, SPELL_CTHUN_MOUTH_TENTACLE, CastSpellExtraArgs(true).SetOriginalCaster(me->GetGUID()));
                stomachEnterPortTimer = STOMACH_GRAB_DURATION;
                StomachEnterTargetGUID = target->GetGUID();
            }
            nextStomachEnterGrabTimer = STOMACH_GRAB_COOLDOWN;
        }
        else
            nextStomachEnterGrabTimer -= diff;
    }

    void UpdateTentaclesP2(uint32 diff)
    {
        SpawnTentacleIfReady(diff, giantClawTentacleTimer, GIANT_CLAW_RESPAWN_TIMER, MOB_GIANT_CLAW_TENTACLE);
        SpawnTentacleIfReady(diff, giantEyeTentacleTimer, GIANT_EYE_RESPAWN_TIMER, MOB_GIANT_EYE_TENTACLE);

        if (eyeTentacleTimer < diff)
        {
            SpawnEyeTentacles();
            eyeTentacleTimer = P2_EYE_TENTACLE_RESPAWN_TIMER;
        }
        else
            eyeTentacleTimer -= diff;
    }

    void SpawnEyeTentacles()
    {
        for (auto const& eyeTentaclePosition : eyeTentaclePositions)
        {
            float x = eyeTentaclePosition[0];
            float y = eyeTentaclePosition[1];
            float z = eyeTentaclePosition[2];
            if (Creature* Spawned = me->SummonCreature(MOB_EYE_TENTACLE, x, y, z, 0.0f, TENTACLE_DESPAWN_FLAG, 1500ms))
                CreatureAI::DoZoneInCombat(Spawned);
        }
    }

    bool SpawnTentacleIfReady(uint32 diff, uint32& timer, uint32 resetTo, uint32 id)
    {
        if (timer < diff)
        {
            if (Unit* target = SelectRandomAliveNotStomach(m_pInstance))
            {
                if (target->GetPositionZ() < -30.0f)
                    TC_LOG_ERROR("scripts", "Cthun trying to spawn {} <-30.0f", id);

                Position pos = me->GetRandomPoint(target->GetPosition(), 0.5f);
                if (Creature* Spawned = me->SummonCreature(id, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TENTACLE_DESPAWN_FLAG, 1500ms))
                    if (Spawned->IsAIEnabled())
                        Spawned->AI()->AttackStart(target);
                timer = resetTo;
                return true;
            }
        }
        else
            timer -= diff;
        return false;
    }
};

void AddSC_classic_boss_cthun()
{
    RegisterCreatureAI(classic_boss_eye_of_cthun);
    RegisterCreatureAI(classic_boss_cthun);
    RegisterCreatureAI(classic_mob_eye_tentacle);
    RegisterCreatureAI(classic_mob_claw_tentacle);
    RegisterCreatureAI(classic_mob_giant_claw_tentacle);
    RegisterCreatureAI(classic_mob_giant_eye_tentacle);
    RegisterCreatureAI(classic_mob_giant_flesh_tentacle);
}
