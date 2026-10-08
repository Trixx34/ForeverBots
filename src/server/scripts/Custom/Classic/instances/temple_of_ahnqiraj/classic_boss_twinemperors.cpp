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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_twinemperors.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_veknilash, boss_veklor, mob_twins_bug, spell_emperor_mutate_bug (802), spell_emperor_explode_bug (804)
// VMaNGOS SDComment: uncertain which dialogue should be used on enrage. Rewritten by Gemt.

#include "ScriptMgr.h"
#include "Containers.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <algorithm>
#include <list>
#include <vector>

namespace
{
enum ClassicAq40TwinsSpells : uint32
{
    SPELL_TWINS_BERSERK             = 26662,

    SPELL_TWIN_TELEPORT_SCRIPT      = 799,   // should have a script effect, dosent seem to have one.
    SPELL_TWIN_TELEPORT_MSG         = 800,   // CTRA watches for this spell to start its teleport timer
    SPELL_TWIN_TELEPORT_VISUAL      = 26638,

    SPELL_TWINS_HEAL_BROTHER        = 7393,

    // Vek'nilash
    SPELL_VEKNILASH_UPPERCUT        = 26007,
    SPELL_VEKNILASH_UNBALANCING_STRIKE = 26613,
    SPELL_VEKNILASH_MUTATE_BUG      = 802,
    SPELL_VEKNILASH_DOUBLE_ATTACK   = 18943, // 50% chance. Based on sniff provided by Tobschinski (https://github.com/LightsHope/server/pull/1208#issuecomment-356985526)

    // Vek'lor
    SPELL_VEKLOR_SHADOWBOLT         = 26006,
    SPELL_VEKLOR_BLIZZARD           = 26607,
    SPELL_VEKLOR_ARCANEBURST        = 568,
    SPELL_VEKLOR_EXPLODEBUG         = 804,

    TWINS_BUG_TYPE_1                = 15316,
    TWINS_BUG_TYPE_2                = 15317,

    SPELL_TWINS_BUG_PIERCE_ARMOR    = 6016,
    SPELL_TWINS_BUG_ACID_SPIT       = 26050,

    CAQ40_TWINS_FACTION_HOSTILE     = 14,
    CAQ40_TWINS_FACTION_BUG         = 7,
};

// NO SNIFFED BROADCAST_TEXT DATA EXISTS FOR MOST OF THESE!!
enum ClassicAq40TwinsTexts : uint32
{
    SAY_VEKLOR_AGGRO_2      = 11453,    // you will not escape death
    SAY_VEKNILASH_SLAY      = 11455,    // your fate is sealed
};

// VMaNGOS script_texts (negative ids, no broadcast text): text, sound, VMaNGOS chat type (1 yell, 3 boss emote)
struct CAQ40_TwinsLegacyText
{
    char const* Text;
    uint32 Sound;
    uint8 Type;
};

CAQ40_TwinsLegacyText const SAY_VEKLOR_AGGRO_1    = { "It's too late to turn away.", 8623, 1 };             // -1531019
CAQ40_TwinsLegacyText const SAY_VEKLOR_SPECIAL    = { "To decorate our halls!", 8627, 1 };                  // -1531025 (wipe)
CAQ40_TwinsLegacyText const SAY_VEKNILASH_AGGRO[4] =
{
    { "Ah, lambs to the slaughter!", 8630, 1 },                                                             // -1531026
    { "Let none survive!", 8632, 1 },                                                                       // -1531027
    { "Join me brother, there is blood to be shed!", 8631, 1 },                                             // -1531028
    { "Look brother, fresh blood!", 8633, 1 },                                                              // -1531029
};
CAQ40_TwinsLegacyText const SAY_VEKNILASH_SPECIAL = { "Shall be your undoing!", 8634, 3 };                  // -1531032 (wipe)
// unused in VMaNGOS: -1531020 "Prepare to embrace oblivion!", -1531021 "Like a fly to the web.", -1531022 "Your brash arrogance!"

void CAQ40_TwinsLegacyScriptText(Creature* source, CAQ40_TwinsLegacyText const& text)
{
    switch (text.Type)
    {
        case 1:
            source->Yell(text.Text, LANG_UNIVERSAL);
            break;
        case 3:
            source->TextEmote(text.Text, nullptr, true);
            break;
        default:
            source->Say(text.Text, LANG_UNIVERSAL);
            break;
    }
    if (text.Sound)
        source->PlayDirectSound(text.Sound);
}

// Shared constants
constexpr float  PULL_RANGE                  = 50.0f;
constexpr uint32 ENRAGE_TIMER                = 60 * 60000;

constexpr uint32 JUST_TELEPORTED_FREEZE      = 2000;     // Emperor is "frozen", aka not doing anything, for this long after TP
constexpr uint32 AFTER_TELEPORT_THREAT       = 3000;     // Threat added to nearest player after TP. TODO: correct amount?

constexpr uint32 TELEPORTTIME_MIN_CD         = 30000;    // Shortest possible cooldown on teleport
constexpr uint32 TELEPORTTIME_MAX_CD         = 40000;    // Longest possible cooldown on teleport

constexpr uint32 TRY_HEAL_FREQUENCY          = 0;        // How ofthen will the emperors TRY to heal eachother, 0 for every update
constexpr uint32 SUCCESS_HEAL_FREQUENCY      = 1500;     // How ofthen will the emperors actually heal, when in range of each other
constexpr float  HEAL_BROTHER_RANGE          = 60.0f;

constexpr uint32 RESPAWN_BUG_FREQUENCY       = 10000;    // How often do we try to respawn a dead bug
constexpr float  BUG_SPELL_MAX_DIST          = 20.0f;    // Max distance a bug can be for the twin to choose it

// Vek'nilash constants
constexpr uint32 UPPERCUT_MIN_CD             = 14000;
constexpr uint32 UPPERCUT_MAX_CD             = 29000;
constexpr uint32 UNBALANCING_STRIKE_MIN_CD   = 8000;
constexpr uint32 UNBALANCING_STRIKE_MAX_CD   = 18000;
constexpr uint32 MUTATE_BUG_MIN_CD           = 10000;
constexpr uint32 MUTATE_BUG_MAX_CD           = 15000;

// Vek'lor constants
constexpr float  ARCANE_BURST_RANGE          = 10.0f;    // How close must a player be if VL should cast AB
constexpr uint32 ARCANE_BURST_MIN_CD         = 5000;
constexpr uint32 ARCANE_BURST_MAX_CD         = 10000;
constexpr uint32 BLIZZARD_MIN_CD             = 15000;    // todo: no source on blizzard cooldown. Duration is 10s
constexpr uint32 BLIZZARD_MAX_CD             = 20000;
constexpr float  VEKLOR_DIST                 = 20.0f;    // Vek'lor chase to this distance
constexpr uint32 SHADOWBOLT_RANGED_MIN_CD    = 1800;
constexpr uint32 SHADOWBOLT_RANGED_MAX_CD    = 2500;
constexpr uint32 SHADOWBOLT_MELEE_MIN_CD     = 2000;
constexpr uint32 SHADOWBOLT_MELEE_MAX_CD     = 10000;
constexpr uint32 VEKLOR_PULL_YELL_DELAY      = 3000;     // Vek'lors pull yell happens after Vek'nilash
constexpr uint32 EXPLODE_BUG_MIN_CD          = 7000;
constexpr uint32 EXPLODE_BUG_MAX_CD          = 10000;

// VMaNGOS looks up sSpellRangeStore with the *spell* id (26006 / 26607), which never matches a range entry, so the
// 45.0f fallback is what VMaNGOS actually uses.
constexpr float  VEKLOR_SHADOWBOLT_RANGE     = 45.0f;
constexpr float  VEKLOR_BLIZZARD_RANGE       = 45.0f;
}

struct classic_mob_twins_bug : public ScriptedAI
{
    classic_mob_twins_bug(Creature* creature) : ScriptedAI(creature) { }

    uint32 pierceArmorTimer = 5000;
    uint32 acidSpitTimer = 6000;

    void GoBeBadBug(uint32 whatKindOfbad)
    {
        me->AddAura(whatKindOfbad, me);
        me->SetFaction(CAQ40_TWINS_FACTION_HOSTILE);
        DoZoneInCombat();
        if (whatKindOfbad == SPELL_VEKNILASH_MUTATE_BUG)
            me->SetFullHealth();
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->SetFaction(CAQ40_TWINS_FACTION_BUG);
        me->RemoveAllAuras();
    }

    void Reset() override
    {
        me->SetFaction(CAQ40_TWINS_FACTION_BUG);
        me->RemoveAllAuras();
        pierceArmorTimer = 5000;
        acidSpitTimer = 6000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (pierceArmorTimer < diff)
        {
            if (DoCastVictim(SPELL_TWINS_BUG_PIERCE_ARMOR) == SPELL_CAST_OK)
                pierceArmorTimer = urand(5000, 9000);
        }
        else
            pierceArmorTimer -= diff;

        if (acidSpitTimer < diff)
        {
            if (DoCastVictim(SPELL_TWINS_BUG_ACID_SPIT) == SPELL_CAST_OK)
                acidSpitTimer = urand(6000, 12000);
        }
        else
            acidSpitTimer -= diff;
    }
};

struct classic_aq40_boss_twinemperorsAI : public ScriptedAI
{
    ClassicTempleOfAhnQirajInstanceScript* m_pInstance;
    uint32 EnrageTimer = 0;

    uint32 justTeleportedTimer = 0;
    bool justTeleported = false;
    bool didPullDialogue = false;

    uint32 bugMutationTimer = 0;
    uint32 respawnBugTimer = 0;

    uint32 killSayCooldown = 0;

    virtual uint32 GetBugSpellCooldown() = 0;
    virtual uint32 GetBugSpell() = 0;
    virtual void   OnEndTeleportVirtual() = 0;
    virtual void   UpdateEmperor(uint32) = 0;

    // Only one of the twins should implement these functions
    virtual void UpdateTeleportToMyBrother(uint32) { }
    virtual void TryHealBrother(uint32 /*diff*/) { }

    ObjectGuid closestTargetAfterTP;

    classic_aq40_boss_twinemperorsAI(Creature* creature) : ScriptedAI(creature)
    {
        m_pInstance = GetClassicTempleOfAhnQirajInstance(creature);
        if (!m_pInstance)
            TC_LOG_ERROR("scripts", "classic_aq40_boss_twinemperorsAI attempted to get the AQ40 instance script, but failed.");
        else
        {
            // If the encounter has not been started yet this ID they should be kneeling to the eye.
            if (!m_pInstance->TwinsDialogueStartedOrDone())
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
        }
        // Reset() is called by TC (InitializeAI)
    }

    void SharedReset()
    {
        respawnBugTimer = RESPAWN_BUG_FREQUENCY;
        EnrageTimer     = ENRAGE_TIMER;
        justTeleported  = false;
        didPullDialogue = false;
        killSayCooldown = 0;
        me->ClearUnitState(UNIT_STATE_STUNNED);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // Only want to run this if we are not in combat
        if (!who || me->GetVictim() || me->IsInCombat())
            return;

        // VMaNGOS: who->IsTargetableBy(me) && who->IsInAccessablePlaceFor(me) && me->IsHostileTo(who)
        if (me->IsValidAttackTarget(who) && who->isInAccessiblePlaceFor(me) && me->IsHostileTo(who))
        {
            float attackRadius = me->GetAttackDistance(who);
            if (attackRadius < PULL_RANGE)
                attackRadius = PULL_RANGE;

            // CREATURE_Z_ATTACK_RANGE there are stairs (VMaNGOS SizeFactor::None -> no bounding radius)
            if (me->IsWithinDistInMap(who, attackRadius, true, false, false) && me->GetDistanceZ(who) <= 7)
                AttackStart(who);
        }
    }

    // VMaNGOS AttackedBy(): ignored during the teleport-idle period. TC has no AttackedBy hook; the victim is only
    // (re)selected through UpdateVictim(), which UpdateAI skips while justTeleported.

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!m_pInstance)
            return;

        if (Creature* pTwin = GetOtherBoss())
        {
            if (pTwin->IsAlive())
            {
                float fDamPercent = float(damage) / float(me->GetMaxHealth());
                uint64 uiTwinDamage = uint64(fDamPercent * float(pTwin->GetMaxHealth()));
                uint64 uiTwinHealth = pTwin->GetHealth() - std::min(uiTwinDamage, pTwin->GetHealth());
                pTwin->SetHealth(uiTwinHealth);
                // VMaNGOS: pTwin->CountDamageTaken(uiTwinDamage, true) - no TC equivalent
            }
        }
    }

    void JustDied(Unit* killer) override
    {
        // Only need one of them to kill the other and update instance data
        if (m_pInstance)
        {
            if (m_pInstance->GetData(TYPE_TWINS) == DONE)
                return;
            else
                m_pInstance->SetData(TYPE_TWINS, DONE);
        }

        if (Creature* pOtherBoss = GetOtherBoss())
            if (pOtherBoss->IsAlive())
                Unit::Kill(killer ? killer : me, pOtherBoss, false);

        // Death text script-text is handled by instance upon receiving DONE data
    }

    void JustEngagedWith(Unit* who) override
    {
        if (m_pInstance)
        {
            if (m_pInstance->GetData(TYPE_TWINS) == IN_PROGRESS)
                return;
            m_pInstance->SetData(TYPE_TWINS, IN_PROGRESS);
        }

        bool bOpenEntrance = false;
        std::list<Creature*> lst;
        me->GetCreatureListWithEntryInGrid(lst, NPC_ANUBISATH_DEFENDER, 800.0f);
        for (Creature* pCreature : lst)
        {
            if (!pCreature->IsAlive())
                continue;
            pCreature->setActive(true);
            CreatureAI::DoZoneInCombat(pCreature);
            if (pCreature->IsAIEnabled())
                pCreature->AI()->AttackStart(who);
            bOpenEntrance = true;
        }

        if (m_pInstance && !bOpenEntrance)
            if (GameObject* pGo = m_pInstance->GetSingleGameObjectFromStorage(GO_TWINS_ENTER_DOOR))
                m_pInstance->DoCloseDoorOrButton(pGo->GetGUID());   // VMaNGOS DoResetDoor

        DoZoneInCombat();

        if (Creature* pOtherBoss = GetOtherBoss())
        {
            // The "other boss" will start running to the same initial puller.
            if (pOtherBoss->IsAIEnabled())
                pOtherBoss->AI()->AttackStart(who);
            CreatureAI::DoZoneInCombat(pOtherBoss);
        }
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_TWINS, FAIL);
    }

    // Workaround for the shared health pool
    void HealReceived(Unit* /*healer*/, uint32& uiHealedAmount) override
    {
        if (!m_pInstance)
            return;

        if (Creature* pTwin = GetOtherBoss())
        {
            float fHealPercent = float(uiHealedAmount) / float(me->GetMaxHealth());
            uint64 uiTwinHeal = uint64(fHealPercent * float(pTwin->GetMaxHealth()));
            uint64 uiTwinHealth = pTwin->GetHealth() + uiTwinHeal;
            pTwin->SetHealth(uiTwinHealth < pTwin->GetMaxHealth() ? uiTwinHealth : pTwin->GetMaxHealth());
        }
    }

    // VMaNGOS Unit::GetNearestVictimInRange(min, max): nearest unit of the threat list within [min, max]
    Unit* GetNearestVictimInRange(float min, float max)
    {
        Unit* nearest = nullptr;
        float nearestDist = max;
        for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
        {
            Unit* victim = ref->GetVictim();
            if (!victim || !victim->IsAlive())
                continue;
            float dist = me->GetDistance(victim);
            if (dist < min || dist > nearestDist)
                continue;
            nearest = victim;
            nearestDist = dist;
        }
        return nearest;
    }

    void UpdateAI(uint32 diff) override
    {
        // The rest of this script requires an instance, less managment and code duplication, and a bit of lazyness
        if (!m_pInstance)
            return;

        // Evade in case starts running after someone outside their room
        if (me->GetPositionZ() > -95.0f)
        {
            if (Creature* pOther = GetOtherBoss())
                if (pOther->IsAIEnabled())
                    pOther->AI()->EnterEvadeMode();
            EnterEvadeMode();
            return;
        }

        // prevent potential edge case
        if (!me->IsAlive())
            return;

        if (justTeleported)
        {
            // Delaying selection of new closest player until first update after TP
            // to be sure we will actually select a target on the new location.
            // (in other words, do it here instead of in OnStartTeleport())
            if (closestTargetAfterTP.IsEmpty())
            {
                // Making sure everyone is contained in threatlist
                DoZoneInCombat();
                if (Unit* closestPlayer = GetNearestVictimInRange(0.0f, 300.0f))
                {
                    closestTargetAfterTP = closestPlayer->GetGUID();
                    me->GetThreatManager().AddThreat(closestPlayer, float(AFTER_TELEPORT_THREAT));
                }
                else
                    TC_LOG_DEBUG("scripts", "Twins unable to select closest target during TP stun");
            }

            if (justTeleportedTimer <= diff)
                OnEndTeleport();
            else
                justTeleportedTimer -= diff;
        }
        else
        {
            // Not attempting to get a hostile target during teleport-idle
            if (!UpdateVictim())
                return;
        }

        if (killSayCooldown > 0)
            killSayCooldown -= std::min(diff, killSayCooldown);

        // We keep calling all of these updates also when TP-stunned,
        // but the functions themself will delay the actual spells until
        // they are no longer TP-stunned
        CheckEnrage(diff);
        UpdateTeleportToMyBrother(diff);
        HandleBugSpell(diff);
        TryHealBrother(diff);

        // We skip updating emperor-specific spells during teleport stun
        if (!justTeleported)
            UpdateEmperor(diff);
    }

    Creature* GetOtherBoss()
    {
        if (m_pInstance)
            return m_pInstance->GetSingleCreatureFromStorage(me->GetEntry() == NPC_VEKLOR ? NPC_VEKNILASH : NPC_VEKLOR);
        return nullptr;
    }

    // Called as teleport happens
    void OnStartTeleport(float x, float y, float z, float o)
    {
        justTeleported = true;
        justTeleportedTimer = JUST_TELEPORTED_FREEZE;
        me->InterruptNonMeleeSpells(true);
        DoStopAttack();
        ResetThreatList();
        me->StopMoving();
        me->NearTeleportTo(x, y, z, o);
        closestTargetAfterTP.Clear();
        me->CastSpell(me, SPELL_TWIN_TELEPORT_MSG, true);
        me->CastSpell(me, SPELL_TWIN_TELEPORT_VISUAL, true);
    }

    // Called JUST_TELEPORTED_FREEZE after teleport happened
    void OnEndTeleport()
    {
        justTeleported = false;

        if (Player* closestPlayer = ObjectAccessor::GetPlayer(*me, closestTargetAfterTP))
        {
            closestTargetAfterTP = closestPlayer->GetGUID();
            AttackStart(closestPlayer);
        }
        else
            TC_LOG_DEBUG("scripts", "Twins unable to select closest target after TP stun end");

        OnEndTeleportVirtual();
    }

    void HandleBugSpell(uint32 diff)
    {
        if (bugMutationTimer < diff)
        {
            // Wait with doing stuff until after idle
            if (justTeleported)
                return;

            std::list<Creature*> lUnitList;
            me->GetCreatureListWithEntryInGrid(lUnitList, TWINS_BUG_TYPE_1, BUG_SPELL_MAX_DIST);
            me->GetCreatureListWithEntryInGrid(lUnitList, TWINS_BUG_TYPE_2, BUG_SPELL_MAX_DIST);

            lUnitList.remove_if([](Creature* bug)
            {
                // Ignoring dead bugs and bugs that has already been affected by a spell
                return !bug->IsAlive() || bug->HasAura(SPELL_VEKNILASH_MUTATE_BUG) || bug->HasAura(SPELL_VEKLOR_EXPLODEBUG);
            });

            if (lUnitList.empty())
                return;

            Creature* c = Trinity::Containers::SelectRandomContainerElement(lUnitList);
            if (classic_mob_twins_bug* bugAI = dynamic_cast<classic_mob_twins_bug*>(c->AI()))
                bugAI->GoBeBadBug(GetBugSpell());

            bugMutationTimer = GetBugSpellCooldown();
        }
        else
            bugMutationTimer -= diff;
    }

    void CheckEnrage(uint32 diff)
    {
        if (EnrageTimer < diff && !me->HasAura(SPELL_TWINS_BERSERK))
        {
            // Wait with casting enrage until after TP idle
            if (justTeleported)
                return;

            // just force-apply berserk if it's time. No dilly-dally.
            me->CastSpell(me, SPELL_TWINS_BERSERK, true);
            EnrageTimer = 60000 * 5; // resetting to duration of enrage
        }
        else
            EnrageTimer -= std::min(diff, EnrageTimer);
    }

    // Get players in given range, optionally skip topaggro. Used where
    // we dont want to compare distance + bounding radius etc, but rather
    // simply center-to-center
    Player* GetPlayerInP2PRange(float min, float max, bool skipTopAggro)
    {
        ThreatManager& mgr = me->GetThreatManager();
        if (mgr.IsThreatListEmpty())
            return nullptr;

        std::vector<Player*> candidates;

        // skipping top-aggro if there are more than 1 person on threat list
        bool skipFirst = mgr.GetThreatListSize() > 1 && skipTopAggro;

        for (ThreatReference const* ref : mgr.GetSortedThreatList())
        {
            if (skipFirst)
            {
                skipFirst = false;
                continue;
            }

            Player* pPlayer = ref->GetVictim()->ToPlayer();
            if (!pPlayer)
                continue;

            if (me->IsInRange(pPlayer, min, max))
                candidates.push_back(pPlayer);
        }

        if (candidates.empty())
            return nullptr;

        return Trinity::Containers::SelectRandomContainerElement(candidates);
    }
};

struct classic_boss_veklor : public classic_aq40_boss_twinemperorsAI
{
    classic_boss_veklor(Creature* creature) : classic_aq40_boss_twinemperorsAI(creature) { }

    // Making sure we have the correct range values
    float shadowboltRange = VEKLOR_SHADOWBOLT_RANGE;
    float blizzardRange = VEKLOR_BLIZZARD_RANGE;
    float casterChaseDistance = 0.0f;   // VMaNGOS Creature::SetCasterChaseDistance

    uint32 shadowBoltTimer = 0;
    uint32 blizzardTimer = 0;
    uint32 arcaneBurstTimer = 0;

    uint32 pullDialogueTimer = 0;

    uint32 teleportTimer = 0;
    uint32 healTimer = 0;
    uint32 timeSinceLastSB = 0;

    void Reset() override
    {
        SharedReset();
        shadowBoltTimer     = 0; // No cooldown on pull
        arcaneBurstTimer    = 0; // No cooldown on pull
        bugMutationTimer    = urand(EXPLODE_BUG_MIN_CD, EXPLODE_BUG_MAX_CD);
        blizzardTimer       = urand(BLIZZARD_MIN_CD, BLIZZARD_MAX_CD);
        teleportTimer       = urand(TELEPORTTIME_MIN_CD, TELEPORTTIME_MAX_CD);
        healTimer           = TRY_HEAL_FREQUENCY;
        pullDialogueTimer   = VEKLOR_PULL_YELL_DELAY;
        timeSinceLastSB     = SHADOWBOLT_RANGED_MIN_CD;

        // Can be removed if its included in DB.
        me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, true);

        if (Creature* pTwin = GetOtherBoss())
            if (!pTwin->IsAlive())
                pTwin->Respawn();
    }

    void AttackStart(Unit* who) override
    {
        if (!who)
            return;

        float dist = me->GetExactDist(who);   // VMaNGOS GetDistance3dToCenter
        if (dist <= VEKLOR_DIST)
        {
            // if he is <= VEKLOR_DIST he should not start chasing again until
            // target is further away than shadowboltRange
            casterChaseDistance = shadowboltRange;
        }
        else if (dist > shadowboltRange)
        {
            // if he is further away than shadowboltRange we set
            // chase distance to VEKLOR_DIST
            casterChaseDistance = VEKLOR_DIST;
        }

        // VMaNGOS ScriptedAI::AttackStart with the caster chase distance applied to the chase movement
        if (me->Attack(who, true))
        {
            if (casterChaseDistance > 0.0f)
                me->GetMotionMaster()->MoveChase(who, ChaseRange(casterChaseDistance));
            else
                me->GetMotionMaster()->MoveChase(who);
        }
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (killSayCooldown == 0)
        {
            ClassicScriptText(SAY_VEKNILASH_SLAY, me);   // sic (VMaNGOS uses Vek'nilash's text here)
            killSayCooldown = urand(5000, 10000);
        }
    }

    void JustReachedHome() override
    {
        CAQ40_TwinsLegacyScriptText(me, SAY_VEKLOR_SPECIAL);
        classic_aq40_boss_twinemperorsAI::JustReachedHome();
    }

    uint32 GetBugSpellCooldown() override
    {
        return urand(EXPLODE_BUG_MIN_CD, EXPLODE_BUG_MAX_CD);
    }

    uint32 GetBugSpell() override
    {
        return SPELL_VEKLOR_EXPLODEBUG;
    }

    void UpdateTeleportToMyBrother(uint32 diff) override
    {
        // Updating time and returning if it's not yet time to teleport
        if (teleportTimer >= diff)
        {
            teleportTimer -= diff;
            return;
        }
        teleportTimer = urand(TELEPORTTIME_MIN_CD, TELEPORTTIME_MAX_CD);

        // If he is attacked during this periode he will instantly engage
        // If noone attacked during that periode, he start attacking after the period.

        Creature* pOtherBoss = GetOtherBoss();
        if (!pOtherBoss)
            return; // Well, that was too bad...

        float other_x = pOtherBoss->GetPositionX();
        float other_y = pOtherBoss->GetPositionY();
        float other_z = pOtherBoss->GetPositionZ();
        float other_o = pOtherBoss->GetOrientation();
        float me_x = me->GetPositionX();
        float me_y = me->GetPositionY();
        float me_z = me->GetPositionZ();
        float me_o = me->GetOrientation();

        OnStartTeleport(other_x, other_y, other_z, me_o);
        if (classic_aq40_boss_twinemperorsAI* pOtherAI = dynamic_cast<classic_aq40_boss_twinemperorsAI*>(pOtherBoss->AI()))
            pOtherAI->OnStartTeleport(me_x, me_y, me_z, other_o);
    }

    void TryHealBrother(uint32 diff) override
    {
        // Cant heal while tp-idle, but since the "stun" effect isent really working properly we return manually
        // https://www.youtube.com/watch?v=8mGchbCF1Lw
        if (justTeleported)
        {
            healTimer -= std::min(diff, healTimer);
            return;
        }

        if (healTimer < diff)
        {
            Creature* pOtherBoss = GetOtherBoss();
            if (pOtherBoss && pOtherBoss->IsAlive() && pOtherBoss->IsWithinDist(me, HEAL_BROTHER_RANGE))
            {
                if (DoCast(pOtherBoss, SPELL_TWINS_HEAL_BROTHER) == SPELL_CAST_OK)
                {
                    // triggered-cast from brother on me if we successfully healed the other way
                    pOtherBoss->CastSpell(me, SPELL_TWINS_HEAL_BROTHER, true);
                    healTimer = SUCCESS_HEAL_FREQUENCY;
                }
            }
            else
                healTimer = TRY_HEAL_FREQUENCY;
        }
        else
            healTimer -= diff;
    }

    void OnEndTeleportVirtual() override
    {
        // Seems rather random if he starts with an AB instantly or delays it
        // when looking at vanilla videos, so possibly because the timer is not reset?
        shadowBoltTimer = 0;
    }

    void UpdateBlizzard(uint32 diff)
    {
        if (blizzardTimer < diff)
        {
            if (Player* p = GetPlayerInP2PRange(0.0f, blizzardRange, true))
                if (DoCast(p, SPELL_VEKLOR_BLIZZARD) == SPELL_CAST_OK)
                    blizzardTimer = urand(BLIZZARD_MIN_CD, BLIZZARD_MAX_CD);
        }
        else
            blizzardTimer -= diff;
    }

    void updateArcaneBurst(uint32 diff)
    {
        if (arcaneBurstTimer < diff)
        {
            if (Unit* mvic = GetPlayerInP2PRange(0.0f, ARCANE_BURST_RANGE, false))
                if (DoCast(mvic, SPELL_VEKLOR_ARCANEBURST) == SPELL_CAST_OK)
                    arcaneBurstTimer = urand(ARCANE_BURST_MIN_CD, ARCANE_BURST_MAX_CD);
        }
        else
            arcaneBurstTimer -= diff;
    }

    void UpdateEmperor(uint32 diff) override
    {
        // Vek'lor does his yell second, so we wait out pullDialogueTimer before yelling
        if (!didPullDialogue)
        {
            if (pullDialogueTimer < diff)
            {
                didPullDialogue = true;
                if (urand(0, 1))
                    CAQ40_TwinsLegacyScriptText(me, SAY_VEKLOR_AGGRO_1);
                else
                    ClassicScriptText(SAY_VEKLOR_AGGRO_2, me);
            }
            else
                pullDialogueTimer -= diff;
        }

        // Always update blizzard and arcane burst, regardless of melee or not
        UpdateBlizzard(diff);
        updateArcaneBurst(diff);

        Unit* victim = me->GetVictim();
        if (!victim)
            return;

        bool isMelee = me->IsWithinMeleeRange(victim);
        bool isInLos = me->IsWithinLOSInMap(victim);

        // Overriding shadowboltTimer if we're not in melee and we have not casted
        // shadowbolt in at least SHADOWBOLT_RANGED_CD time. This will mostly be the
        // case if target was in melee, but then moved out, in which case we should
        // instantly re-cast a new shadowbolt unless it was just casted.
        if (!isMelee && timeSinceLastSB > SHADOWBOLT_RANGED_MAX_CD)
            shadowBoltTimer = 0;

        // If we're in los and melee range and melee attack succeeded we wait one update before
        // doing the shadowbolt.
        // TC: melee is automatic; VMaNGOS DoMeleeAttackIfReady() returning true == the swing timer is ready this update.
        if (isMelee && isInLos && !me->IsNonMeleeSpellCast(false) && me->isAttackReady(BASE_ATTACK))
        {
            shadowBoltTimer -= std::min(diff, shadowBoltTimer);
        }
        else if (!me->isMoving())
        {
            if (me->GetStandState() != UNIT_STAND_STATE_STAND)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            if (shadowBoltTimer < diff)
            {
                if (DoCastVictim(SPELL_VEKLOR_SHADOWBOLT) == SPELL_CAST_OK)
                {
                    timeSinceLastSB = 0;
                    if (isMelee)
                    {
                        // Looks like VL should prioritize shadowbolt differently if
                        // target is in melee range. He seems to get a random cooldown on it, and meleeing when he can.
                        // https://www.youtube.com/watch?v=SNOmg7kE68U&t=53s
                        // https://www.youtube.com/watch?v=dCrDisOWOjU
                        shadowBoltTimer = urand(SHADOWBOLT_MELEE_MIN_CD, SHADOWBOLT_MELEE_MAX_CD);
                    }
                    else
                    {
                        // When not in melee range, there is only a ~2 sec cooldown on shadowbolt, even though
                        // the cast-time is only 1.5 seconds.
                        // https://www.youtube.com/watch?v=nHXfSDVX_ZA
                        shadowBoltTimer = urand(SHADOWBOLT_RANGED_MIN_CD, SHADOWBOLT_RANGED_MAX_CD);
                    }
                }
            }
            else
                shadowBoltTimer -= diff;
        }

        timeSinceLastSB += diff;
    }
};

struct classic_boss_veknilash : public classic_aq40_boss_twinemperorsAI
{
    classic_boss_veknilash(Creature* creature) : classic_aq40_boss_twinemperorsAI(creature) { }

    uint32 UpperCut_Timer = 0;
    uint32 UnbalancingStrike_Timer = 0;

    void Reset() override
    {
        SharedReset();
        bugMutationTimer        = urand(MUTATE_BUG_MIN_CD, MUTATE_BUG_MAX_CD);
        UpperCut_Timer          = urand(UPPERCUT_MIN_CD, UPPERCUT_MAX_CD);
        UnbalancingStrike_Timer = urand(UNBALANCING_STRIKE_MIN_CD, UNBALANCING_STRIKE_MAX_CD);

        // Added. Can be removed if its included in DB.
        me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_SPELL, true);

        if (Creature* pTwin = GetOtherBoss())
            if (!pTwin->IsAlive())
                pTwin->Respawn();
    }

    void JustReachedHome() override
    {
        CAQ40_TwinsLegacyScriptText(me, SAY_VEKNILASH_SPECIAL);
        classic_aq40_boss_twinemperorsAI::JustReachedHome();
    }

    void OnEndTeleportVirtual() override
    {
        // todo: anything that needs doing?
    }

    uint32 GetBugSpellCooldown() override
    {
        return urand(MUTATE_BUG_MIN_CD, MUTATE_BUG_MAX_CD);
    }

    uint32 GetBugSpell() override
    {
        return SPELL_VEKNILASH_MUTATE_BUG;
    }

    // Last (lowest threat) unit of the threat list that is in melee range
    Unit* GetPlayerInMeleeRange()
    {
        Unit* candidate = nullptr;
        for (ThreatReference const* ref : me->GetThreatManager().GetSortedThreatList())
        {
            Unit* pUnit = ref->GetVictim();
            if (!pUnit)
                continue;

            if (me->IsWithinMeleeRange(pUnit))
                candidate = pUnit;
        }
        return candidate;
    }

    void UpdateEmperor(uint32 diff) override
    {
        // Vek'nilash goes first, instantly does his yell when we are in combat.
        if (!didPullDialogue)
        {
            didPullDialogue = true;
            CAQ40_TwinsLegacyScriptText(me, SAY_VEKNILASH_AGGRO[urand(0, 3)]);
        }

        if (!me->HasAura(SPELL_VEKNILASH_DOUBLE_ATTACK))
            me->CastSpell(me, SPELL_VEKNILASH_DOUBLE_ATTACK, true);

        // UnbalancingStrike_Timer
        if (UnbalancingStrike_Timer < diff)
        {
            if (DoCastVictim(SPELL_VEKNILASH_UNBALANCING_STRIKE) == SPELL_CAST_OK)
                UnbalancingStrike_Timer = urand(UNBALANCING_STRIKE_MIN_CD, UNBALANCING_STRIKE_MAX_CD);
        }
        else
            UnbalancingStrike_Timer -= diff;

        if (UpperCut_Timer < diff)
        {
            if (Unit* randomMelee = GetPlayerInMeleeRange())
                if (DoCast(randomMelee, SPELL_VEKNILASH_UPPERCUT) == SPELL_CAST_OK)
                    UpperCut_Timer = urand(UPPERCUT_MIN_CD, UPPERCUT_MAX_CD);
        }
        else
            UpperCut_Timer -= diff;
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (killSayCooldown == 0)
        {
            ClassicScriptText(SAY_VEKNILASH_SLAY, me);
            killSayCooldown = urand(5000, 10000);
        }
    }
};

// 802 - Mutate Bug (AQ40, Emperor Vek'nilash)
class classic_spell_emperor_mutate_bug : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Trinity::Containers::RandomResize(targets, 1);   // VMaNGOS: unMaxTargets = 1
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_emperor_mutate_bug::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

// 804 - Explode Bug (AQ40, Emperor Vek'lor)
class classic_spell_emperor_explode_bug : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Trinity::Containers::RandomResize(targets, 1);   // VMaNGOS: unMaxTargets = 1
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_emperor_explode_bug::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

void AddSC_classic_boss_twinemperors()
{
    RegisterCreatureAI(classic_boss_veknilash);
    RegisterCreatureAI(classic_boss_veklor);
    RegisterCreatureAI(classic_mob_twins_bug);
    RegisterSpellScript(classic_spell_emperor_mutate_bug);
    RegisterSpellScript(classic_spell_emperor_explode_bug);
}
