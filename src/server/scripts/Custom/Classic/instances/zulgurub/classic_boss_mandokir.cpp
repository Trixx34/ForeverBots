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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_mandokir.cpp (GPL-2)
// Scripts: boss_mandokir, mob_ohgan, mob_chained_spirit (mob_vilebranche is not registered in VMaNGOS)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"
#include <vector>

namespace
{
enum ClassicZgMandokir : uint32
{
    NPC_MANDOKIR_OHGAN              = 14988,
    NPC_MANDOKIR_CHAINED_SPIRIT     = 15117,    // resing spirits
    NPC_MANDOKIR                    = 11382,
    NPC_MANDOKIR_VILEBRANCH_SPEAKER = 11391,

    SAY_MANDOKIR_AGGRO              = 10446,
    SAY_MANDOKIR_DING_KILL          = 10505,
    SAY_MANDOKIR_GRATS_JINDO        = 10601,
    SAY_MANDOKIR_WATCH              = 10604,
    SAY_MANDOKIR_WATCH_WHISPER      = 10628,
    EMOTE_MANDOKIR_RAGE             = 10545,

    SPELL_MANDOKIR_CHARGE           = 24408,
    SPELL_MANDOKIR_FEAR             = 19134,
    SPELL_MANDOKIR_WHIRLWIND        = 13736,
    SPELL_MANDOKIR_MORTAL_STRIKE    = 16856,
    SPELL_MANDOKIR_ENRAGE           = 24318,
    SPELL_MANDOKIR_WATCH            = 24314,
    SPELL_MANDOKIR_DECAPITATE       = 24315,
    SPELL_MANDOKIR_SUMMON_PLAYER    = 25104,    // unused
    SPELL_MANDOKIR_LEVEL_UP         = 24312,
    SPELL_MANDOKIR_OVERPOWER        = 24407,    // unused
    SPELL_MANDOKIR_MOUNT            = 23243,    // this spell may not be correct, it's the spell used by item

    // Ohgan's spells
    SPELL_OHGAN_SUNDERARMOR         = 24317,
    SPELL_OHGAN_THRASH              = 3391,
    SPELL_OHGAN_EXECUTE             = 7160,

    // Chained Spirit's spells
    SPELL_CHAINED_SPIRIT_REVIVE     = 24341,

    POINT_CHAINED_SPIRIT_REZ        = 1
};

struct ClassicZgSpawnLocation
{
    float fX, fY, fZ, fAng;
};

ClassicZgSpawnLocation const aClassicZgSpirits[] =
{
    { -12150.9f, -1956.24f, 133.407f, 2.57835f},
    { -12157.1f, -1972.78f, 133.947f, 2.64903f},
    { -12172.3f, -1982.63f, 134.061f, 1.48664f},
    { -12194.0f, -1979.54f, 132.194f, 1.45916f},
    { -12211.3f, -1978.49f, 133.580f, 1.35705f},
    { -12228.4f, -1977.10f, 132.728f, 1.25495f},
    { -12250.0f, -1964.78f, 135.066f, 0.92901f},
    { -12264.0f, -1953.08f, 134.072f, 0.62663f},
    { -12289.0f, -1924.00f, 132.620f, 5.37829f},
    { -12267.3f, -1902.26f, 131.328f, 5.32724f},
    { -12255.3f, -1893.53f, 134.026f, 5.06413f},
    { -12229.9f, -1891.39f, 134.704f, 4.40047f},
    { -12215.9f, -1889.09f, 137.273f, 4.70285f},
    { -12200.5f, -1890.69f, 135.777f, 4.84422f},
    { -12186.0f, -1890.12f, 134.261f, 4.36513f},
    { -12246.3f, -1890.09f, 135.475f, 4.73427f},
    { -12170.7f, -1894.85f, 133.852f, 3.51690f},
    { -12279.0f, -1931.92f, 136.130f, 0.04151f},
    { -12266.1f, -1940.72f, 132.606f, 0.70910f}
};
}

/*######
## mob_chained_spirit
## When a player dies, a spirit must resurrect him. If the player accepts, the mob must despawn.
## Otherwise the spirit becomes available for other players (they are spawned in limited number).
######*/

struct classic_mob_chained_spirit : public ScriptedAI
{
    classic_mob_chained_spirit(Creature* creature) : ScriptedAI(creature) { }

    ObjectGuid m_uiTargetRezGUID;
    uint32 m_uiRezTimer = 0;

    void Reset() override
    {
        m_uiRezTimer = 0;
    }

    // VMaNGOS GetData(0): ready to revive someone?
    bool IsReadyToRez() const
    {
        return m_uiTargetRezGUID.IsEmpty();
    }

    // VMaNGOS SpellHitTarget(pDead, nullptr) called by Mandokir
    void StartRez(Player* pDead)
    {
        m_uiTargetRezGUID = pDead->GetGUID();
        me->GetMotionMaster()->MovePoint(POINT_CHAINED_SPIRIT_REZ, pDead->GetPositionX(), pDead->GetPositionY(), pDead->GetPositionZ(),
            true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceWalk);
        me->SetHomePosition(pDead->GetPositionX(), pDead->GetPositionY(), pDead->GetPositionZ(), 0.0f);
    }

    void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo && spellInfo->Id == SPELL_CHAINED_SPIRIT_REVIVE)
            me->DespawnOrUnsummon();
    }

    void MovementInform(uint32 mvtType, uint32 moveId) override
    {
        if (mvtType == POINT_MOTION_TYPE && moveId == POINT_CHAINED_SPIRIT_REZ)
            if (ObjectAccessor::GetPlayer(*me, m_uiTargetRezGUID))
                m_uiRezTimer = 2500;
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_uiRezTimer)
        {
            if (m_uiRezTimer <= diff)
            {
                // Attempt to rez player
                Player* target = ObjectAccessor::GetPlayer(*me, m_uiTargetRezGUID);
                if (target && target->getDeathState() == CORPSE && !target->IsResurrectRequested())
                    me->CastSpell(target, SPELL_CHAINED_SPIRIT_REVIVE, true); // Will despawn at SpellHitTarget
                else
                    me->DespawnOrUnsummon();
            }
            else
                m_uiRezTimer -= diff;
        }
    }
};

/*######
## boss_mandokir
######*/

struct classic_boss_mandokir : public ScriptedAI
{
    classic_boss_mandokir(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    // VMaNGOS START_FLAGS = PACIFIED | SPAWNING | NOT_SELECTABLE | IMMUNE_TO_PLAYER
    static constexpr UnitFlags START_FLAGS = UnitFlags(UNIT_FLAG_PACIFIED | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);

    InstanceScript* m_pInstance;

    uint32 m_uiGlobalCooldown = 0;

    uint32 m_uiWatch_Timer = 0;
    uint32 m_uiCharge_Timer = 0;
    uint32 m_uiWhirlwind_Timer = 0;
    uint32 m_uiFear_Timer = 0;
    uint32 m_uiMortalStrike_Timer = 0;

    bool m_bRaptorDead = false;
    ObjectGuid m_uiRaptorGUID;
    ObjectGuid m_uiChargedPlayerGUID;

    bool m_VilebranchDead = false;
    bool m_needVilebranchCheck = false;

    float m_fTargetX = 0.0f;
    float m_fTargetY = 0.0f;
    ObjectGuid m_uiWatchTarget;
    ObjectGuid m_uiTargetToKill;
    bool m_bTargetMoved = false;
    float m_fWatchedTargetAllowedMoveRange = 2.0f;
    bool m_bTargetActed = false;
    float m_fTargetThreat = 0.0f;
    bool m_bFearAfterCharge = false;

    bool m_bChargeCasted = false;
    uint32 m_uiChargeCasted_Timer = 0;

    ObjectGuid m_uiPlayerToRez;

    std::vector<ObjectGuid> m_lSpirits;

    void Reset() override
    {
        m_uiGlobalCooldown = 0;

        m_uiChargedPlayerGUID.Clear();
        m_bFearAfterCharge = false;
        m_bChargeCasted = false;
        m_uiChargeCasted_Timer = 0;

        m_uiWatch_Timer = 33000;
        m_uiCharge_Timer = 15000;
        m_uiWhirlwind_Timer = 20000;
        m_uiFear_Timer = 1000;
        m_uiMortalStrike_Timer = 1000;

        m_bRaptorDead = false;

        m_uiWatchTarget.Clear();
        m_uiTargetToKill.Clear();
        m_bTargetMoved = false;
        m_fWatchedTargetAllowedMoveRange = 2.0f;
        m_bTargetActed = false;
        m_fTargetThreat = 0.0f;

        m_uiPlayerToRez.Clear();

        // VMaNGOS SetLevel(63) + ResetStats()
        me->SetLevel(63);
        me->UpdateLevelDependantStats();

        DoCastSelf(SPELL_MANDOKIR_MOUNT);
        DespawnRaptor();
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_OHGAN, NOT_STARTED);

        DespawnSpirits();

        // VMaNGOS CheckVilebranchState(true). Deferred to the first UpdateAI tick: at creation the grid of the
        // Vilebranch Speaker may not be loaded yet (he would be seen as dead).
        m_needVilebranchCheck = true;
    }

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            DoCastSelf(SPELL_MANDOKIR_LEVEL_UP, true);
            me->SetLevel(uint8(me->GetLevel() + 1));
            m_uiPlayerToRez = pVictim->GetGUID();

            ClassicScriptText(SAY_MANDOKIR_DING_KILL, me);

            if (!m_pInstance)
                return;

            if (!urand(0, 2))
            {
                if (Creature* jTemp = ObjectAccessor::GetCreature(*me, m_pInstance->GetGuidData(CLASSIC_ZG_DATA_JINDO)))
                {
                    if (jTemp->IsAlive())
                        ClassicScriptText(SAY_MANDOKIR_GRATS_JINDO, jTemp);
                }
                else
                    ClassicScriptText(SAY_MANDOKIR_GRATS_JINDO, me);
            }
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_MANDOKIR_AGGRO, me);

        DoZoneInCombat();

        SpawnSpirits();
    }

    void CheckRaptor()
    {
        me->RemoveAurasDueToSpell(SPELL_MANDOKIR_MOUNT);
        if (!m_bRaptorDead)
            SpawnRaptor();

        // Checking if Ohgan is dead. If yes Mandokir will enrage.
        if (!m_bRaptorDead && m_pInstance && m_pInstance->GetData(CLASSIC_ZG_TYPE_OHGAN) == DONE)
        {
            if (DoCastSelf(SPELL_MANDOKIR_ENRAGE) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_MANDOKIR_RAGE, me);
                m_bRaptorDead = true;
            }
        }
    }

    void CheckVilebranchState(bool reset = false)
    {
        // If Vilebranch dies and group wipes, boss should start at the bottom of the stairs
        // Video: https://www.youtube.com/watch?v=joaWY0wjOXI
        Creature* vileBranch = me->FindNearestCreature(NPC_MANDOKIR_VILEBRANCH_SPEAKER, 100.0f, true);
        bool isVilebranchDead = !vileBranch || !vileBranch->IsAlive();
        if (reset || m_VilebranchDead != isVilebranchDead)
        {
            if (isVilebranchDead)
            {
                me->RemoveUnitFlag(START_FLAGS);
                me->SetImmuneToPC(false);
                me->SetHomePosition(-12195.0f, -1948.0f, 130.0f, 3.14f);
            }
            else
            {
                me->SetUnitFlag(START_FLAGS);
                me->SetImmuneToPC(true);
                me->SetHomePosition(me->GetRespawnPosition());   // VMaNGOS ResetHomePosition()
            }
            me->GetMotionMaster()->MoveTargetedHome();

            m_VilebranchDead = isVilebranchDead;
        }
    }

    void CheckWatchedPlayer()
    {
        if (m_uiWatchTarget.IsEmpty())
            return;

        Unit* pWatchTarget = ObjectAccessor::GetUnit(*me, m_uiWatchTarget);
        if (!pWatchTarget || !pWatchTarget->IsAlive())
            return;

        // During the debuff
        if (pWatchTarget->HasAura(SPELL_MANDOKIR_WATCH))
        {
            // The target must not move.
            if (pWatchTarget->HasAura(SPELL_MANDOKIR_FEAR))
            {
                m_fWatchedTargetAllowedMoveRange = 8.0f;
                m_fTargetX = pWatchTarget->GetPositionX();
                m_fTargetY = pWatchTarget->GetPositionY();
            }
            else
            {
                if (!m_bTargetMoved && !pWatchTarget->IsWithinDist2d(m_fTargetX, m_fTargetY, m_fWatchedTargetAllowedMoveRange))
                    m_bTargetMoved = true;
                m_fWatchedTargetAllowedMoveRange = 2.0f;
            }

            // Nor attack.
            if (!m_bTargetActed && GetThreat(pWatchTarget) > m_fTargetThreat)
                m_bTargetActed = true;
        }
        // The debuff is over
        else
        {
            if (m_bTargetMoved || m_bTargetActed)
                m_uiTargetToKill = m_uiWatchTarget;
            else
                m_uiTargetToKill.Clear();
            m_uiWatchTarget.Clear();
        }
    }

    void DespawnSpirits()
    {
        for (ObjectGuid const& guid : m_lSpirits)
            if (Creature* pSpirit = ObjectAccessor::GetCreature(*me, guid))
                if (pSpirit->IsAlive())
                    pSpirit->DespawnOrUnsummon();
        m_lSpirits.clear();
    }

    void SpawnSpirits()
    {
        if (!m_lSpirits.empty())
            return;

        for (ClassicZgSpawnLocation const& loc : aClassicZgSpirits)
            if (Creature* spirit = me->SummonCreature(NPC_MANDOKIR_CHAINED_SPIRIT, loc.fX, loc.fY, loc.fZ, loc.fAng, TEMPSUMMON_CORPSE_DESPAWN))
                m_lSpirits.push_back(spirit->GetGUID());
    }

    void DespawnRaptor()
    {
        if (!m_uiRaptorGUID.IsEmpty())
            if (Creature* pRaptor = ObjectAccessor::GetCreature(*me, m_uiRaptorGUID))
                if (pRaptor->IsAlive())
                    pRaptor->DespawnOrUnsummon();
        m_uiRaptorGUID.Clear();
    }

    void SpawnRaptor()
    {
        // And summon his raptor (VMaNGOS: 0,0,0 = at the summoner's position)
        if (m_uiRaptorGUID.IsEmpty())
            if (Creature* pRaptor = me->SummonCreature(NPC_MANDOKIR_OHGAN, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 35s))
                m_uiRaptorGUID = pRaptor->GetGUID();
    }

    void JustSummoned(Creature* pSummoned) override
    {
        Unit* victim = me->GetVictim();
        if (!victim)
            return;

        if (pSummoned->GetEntry() == NPC_MANDOKIR_OHGAN && pSummoned->AI())
            pSummoned->AI()->AttackStart(victim);
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (me->GetVictim())
            return;

        if (Player* pPlayer = pWho->ToPlayer())
            if (!pPlayer->IsGameMaster())
                if (pPlayer->GetDistance(me) < 40.0f)
                    if (m_VilebranchDead)
                        AttackStart(pWho);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_MANDOKIR_WATCH)
        {
            Unit* pTarget = target->ToUnit();
            if (!pTarget)
                return;

            m_uiWatchTarget = pTarget->GetGUID();
            m_fTargetX      = pTarget->GetPositionX();
            m_fTargetY      = pTarget->GetPositionY();
            m_bTargetMoved = false;
            m_bTargetActed = false;
            m_fTargetThreat = GetThreat(pTarget);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_needVilebranchCheck)
        {
            m_needVilebranchCheck = false;
            CheckVilebranchState(true);
        }
        else if (!m_VilebranchDead)
            CheckVilebranchState();

        if (!m_VilebranchDead || !UpdateVictim())
            return;

        CheckRaptor();
        CheckWatchedPlayer();

        // Check player to rez
        if (!m_uiPlayerToRez.IsEmpty())
        {
            if (Player* killedPlayer = ObjectAccessor::GetPlayer(*me, m_uiPlayerToRez))
            {
                if (killedPlayer->getDeathState() == CORPSE && !killedPlayer->IsResurrectRequested())
                {
                    // Find nearest spirit ready to resurrect
                    classic_mob_chained_spirit* spiritAI = nullptr;
                    Creature* spirit = nullptr;
                    float spiritDist = 0.0f;
                    for (ObjectGuid const& guid : m_lSpirits)
                    {
                        if (Creature* current = ObjectAccessor::GetCreature(*me, guid))
                        {
                            // Ready to resurrect ?
                            classic_mob_chained_spirit* currentAI = dynamic_cast<classic_mob_chained_spirit*>(current->AI());
                            if (currentAI && currentAI->IsReadyToRez())
                            {
                                float currentDist = current->GetDistance(killedPlayer);
                                if (!spirit || currentDist < spiritDist)
                                {
                                    spirit = current;
                                    spiritAI = currentAI;
                                    spiritDist = currentDist;
                                }
                            }
                        }
                    }
                    if (spirit && spiritAI)
                    {
                        // TODO(classic): hardcoded VMaNGOS whisper text (no broadcast text id)
                        spirit->Whisper("I am released through you! Avenge me!", LANG_UNIVERSAL, killedPlayer);
                        spiritAI->StartRez(killedPlayer);
                    }
                }
            }
            m_uiPlayerToRez.Clear();
        }

        // Global cooldown not elapsed.
        if (m_uiGlobalCooldown > diff)
            m_uiGlobalCooldown -= diff;
        else
        {
            if (me->IsNonMeleeSpellCast(false))
                m_uiGlobalCooldown = 1;
            else
                m_uiGlobalCooldown = 0;
        }

        // WATCH
        if (m_uiWatch_Timer < diff)
        {
            if (m_uiGlobalCooldown == 0 && m_uiWatchTarget.IsEmpty())
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                {
                    if (Player* pPlayer = pTarget->GetCharmerOrOwnerPlayerOrPlayerItself())
                    {
                        if (DoCast(pPlayer, SPELL_MANDOKIR_WATCH) == SPELL_CAST_OK)
                        {
                            ClassicScriptText(SAY_MANDOKIR_WATCH, me, pTarget);
                            ClassicScriptText(SAY_MANDOKIR_WATCH_WHISPER, me, pTarget);
                            m_uiWatch_Timer = 20000;
                            m_uiGlobalCooldown = 1;
                            if (m_uiCharge_Timer < 9000)
                                m_uiCharge_Timer = 9000;
                        }
                    }
                }
            }
        }
        else
            m_uiWatch_Timer -= diff;

        // DECAPITATE
        if (!m_uiTargetToKill.IsEmpty())
        {
            if (m_uiGlobalCooldown == 0)
            {
                if (Unit* pTargetToKill = ObjectAccessor::GetUnit(*me, m_uiTargetToKill))
                {
                    if (pTargetToKill->IsAlive())
                    {
                        bool bTargetKilled = false;
                        float addAggro = 0;
                        if (Unit* victim = me->GetVictim())
                        {
                            if (float agro = GetThreat(victim))
                            {
                                ModifyThreatByPercent(pTargetToKill, -100);
                                addAggro = agro;
                            }
                        }
                        me->GetThreatManager().AddThreat(pTargetToKill, addAggro + 2000);
                        // VMaNGOS SelectHostileTarget(): TC re-evaluates the victim on the next UpdateVictim()

                        if (me->GetDistance(pTargetToKill) <= 40.0f)
                        {
                            if (DoCast(pTargetToKill, SPELL_MANDOKIR_DECAPITATE) == SPELL_CAST_OK)
                                bTargetKilled = true;
                            // TODO: Mandokir should also cast a sound explosion, after charging the player
                        }
                        if (!bTargetKilled)
                            Unit::Kill(me, pTargetToKill);
                        if (pTargetToKill->IsAlive())
                            Unit::Kill(me, pTargetToKill);
                        m_uiGlobalCooldown = 1000;
                        m_uiTargetToKill.Clear();
                    }
                }
            }
        }

        // CHARGE
        if (m_uiCharge_Timer < diff)
        {
            if (m_uiGlobalCooldown == 0 && m_uiTargetToKill.IsEmpty() && m_uiWatchTarget.IsEmpty())
            {
                // VMaNGOS GetFarthestVictimInRange(8.0f, 40.0f)
                Unit* pTarget = SelectTarget(SelectTargetMethod::MaxDistance, 0, [this](Unit* unit)
                {
                    float dist = me->GetDistance(unit);
                    return dist >= 8.0f && dist <= 40.0f;
                });
                if (pTarget)
                {
                    if (Player* pPlayer = pTarget->GetCharmerOrOwnerPlayerOrPlayerItself())
                    {
                        if (DoCast(pPlayer, SPELL_MANDOKIR_CHARGE) == SPELL_CAST_OK)
                        {
                            m_uiCharge_Timer = urand(30000, 35000);
                            m_uiGlobalCooldown = 1000;
                            // Fear imminent
                            m_uiFear_Timer = 100;
                            m_bFearAfterCharge = true;
                            m_bChargeCasted = true;
                            m_uiChargeCasted_Timer = 2000;
                            m_uiChargedPlayerGUID = pTarget->GetGUID();
                        }
                    }
                }
                else
                    m_uiCharge_Timer = urand(5000, 10000);
            }
        }
        else
        {
            m_uiCharge_Timer -= diff;
            if (m_bChargeCasted)
            {
                m_uiChargeCasted_Timer = m_uiChargeCasted_Timer > diff ? m_uiChargeCasted_Timer - diff : 0;
                if (m_uiChargeCasted_Timer < diff)
                {
                    m_bChargeCasted = false;
                    if (Unit* victim = me->GetVictim())
                        me->GetMotionMaster()->MoveChase(victim);
                }
            }
        }

        // FEAR
        if (m_uiFear_Timer < diff)
        {
            if (m_bFearAfterCharge)
            {
                if (!m_uiChargedPlayerGUID.IsEmpty())
                {
                    if (Player* player = ObjectAccessor::GetPlayer(*me, m_uiChargedPlayerGUID))
                    {
                        if (player->IsAlive())
                        {
                            if (me->GetDistance(player) < 4.0f)
                            {
                                if (DoCast(player, SPELL_MANDOKIR_FEAR) == SPELL_CAST_OK)
                                {
                                    m_uiFear_Timer = 24000;
                                    m_uiGlobalCooldown = 500;
                                    m_bFearAfterCharge = false;
                                    m_uiChargedPlayerGUID.Clear();
                                }
                            }
                        }
                        else
                        {
                            m_uiFear_Timer = 24000;
                            m_uiGlobalCooldown = 500;
                            m_bFearAfterCharge = false;
                            m_uiChargedPlayerGUID.Clear();
                        }
                    }
                }
            }
        }
        else
            m_uiFear_Timer -= diff;

        // WHIRLWIND
        if (m_uiWhirlwind_Timer < diff)
        {
            if (m_uiGlobalCooldown == 0 && m_uiTargetToKill.IsEmpty() && m_uiWatchTarget.IsEmpty())
            {
                if (DoCastSelf(SPELL_MANDOKIR_WHIRLWIND) == SPELL_CAST_OK)
                {
                    m_uiWhirlwind_Timer = 18000;
                    m_uiGlobalCooldown = 3000;
                }
            }
        }
        else
            m_uiWhirlwind_Timer -= diff;

        // MORTAL STRIKE
        if (m_uiMortalStrike_Timer < diff)
        {
            Unit* victim = me->GetVictim();
            if (m_uiGlobalCooldown == 0 && victim && victim->GetHealthPct() < 50.0f)
            {
                if (DoCast(victim, SPELL_MANDOKIR_MORTAL_STRIKE) == SPELL_CAST_OK)
                {
                    m_uiMortalStrike_Timer = 15000;
                    m_uiGlobalCooldown = 500;
                }
            }
        }
        else
            m_uiMortalStrike_Timer -= diff;

        // melee: TC master auto-melee
    }
};

/*######
## mob_ohgan
######*/

struct classic_mob_ohgan : public ScriptedAI
{
    classic_mob_ohgan(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 SunderArmor_Timer = 0;
    uint32 Thrash_Timer = 0;
    uint32 Execute_Timer = 0;

    void Reset() override
    {
        SunderArmor_Timer = 5000;
        Thrash_Timer = urand(5000, 9000);
        Execute_Timer = 1000;
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_OHGAN, DONE);
    }

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
            if (me->IsInCombat())
                if (Creature* pMandokir = pVictim->FindNearestCreature(NPC_MANDOKIR, 100.0f))
                    if (pMandokir->AI())
                        pMandokir->AI()->KilledUnit(pVictim);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // SunderArmor_Timer
        if (SunderArmor_Timer <= diff)
        {
            DoCastVictim(SPELL_OHGAN_SUNDERARMOR);
            SunderArmor_Timer = 10000 + urand(0, 4999);
        }
        else
            SunderArmor_Timer -= diff;

        // Thrash_Timer
        if (Thrash_Timer <= diff)
        {
            DoCastSelf(SPELL_OHGAN_THRASH);
            Thrash_Timer = urand(5000, 9000);
        }
        else
            Thrash_Timer -= diff;

        // Execute_Timer
        Unit* victim = me->GetVictim();
        if (victim && victim->GetHealth() <= victim->GetMaxHealth() * 0.2f)  // check health first
        {
            if (Execute_Timer <= diff)
            {
                DoCast(victim, SPELL_OHGAN_EXECUTE);
                Execute_Timer = 10000;
            }
            else
                Execute_Timer -= diff;
        }
        else
            // VMaNGOS decrements unconditionally here (uint32 wrap-around); clamped at 0
            Execute_Timer = Execute_Timer > diff ? Execute_Timer - diff : 0;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_mandokir()
{
    RegisterCreatureAI(classic_boss_mandokir);
    RegisterCreatureAI(classic_mob_ohgan);
    RegisterCreatureAI(classic_mob_chained_spirit);
}
