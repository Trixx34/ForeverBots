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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_gothik.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_gothik, spell_anchor (creature AI of the sub-boss triggers 16137) and its dummy spell handler
//          (VMaNGOS pEffectDummyCreature) as spell script classic_spell_gothik_anchor
//          (27892, 27928, 27935, 27893, 27929, 27936, 27915, 27931, 27937)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include <cmath>
#include <iterator>
#include <list>
#include <vector>

namespace
{
enum ClassicNaxxGothikData : uint32
{
    SAY_GOTH_SPEECH_1                = 13029,
    SAY_GOTH_SPEECH_2                = 13031,
    SAY_GOTH_SPEECH_3                = 13032,
    SAY_GOTH_SPEECH_4                = 13033,

    SAY_GOTH_KILL                    = 13027,
    SAY_GOTH_DEATH                   = 13026,
    SAY_GOTH_TELEPORT                = 13028,

    // EMOTE_TO_FRAY = -1533138, EMOTE_GATE = -1533139 (script_texts, no broadcast text): boss emotes, see texts below

    PHASE_GOTH_SPEECH                = 0,
    PHASE_GOTH_BALCONY               = 1,
    PHASE_GOTH_GROUND                = 2,

    MAX_GOTH_WAVES                   = 18,

    SPELL_GOTH_TELEPORT_LEFT         = 28025,                    // guesswork
    SPELL_GOTH_TELEPORT_RIGHT        = 28026,                    // could be defined as dead or live side, left or right facing north

    SPELL_GOTH_HARVESTSOUL           = 28679,
    SPELL_GOTH_SHADOWBOLT            = 29317,

    SPELL_GOTH_IMMUNE_ALL            = 29230,
    GOTH_TELEPORT_PACIFY_TIMER       = 1200
};

char const* const TEXT_GOTH_EMOTE_TO_FRAY = "%s teleports into the fray!";
char const* const TEXT_GOTH_EMOTE_GATE    = "The central gate opens!";

enum ClassicNaxxGothikSpellDummy : uint32
{
    SPELL_GOTH_A_TO_ANCHOR_1     = 27892,
    SPELL_GOTH_B_TO_ANCHOR_1     = 27928,
    SPELL_GOTH_C_TO_ANCHOR_1     = 27935,

    SPELL_GOTH_A_TO_ANCHOR_2     = 27893,
    SPELL_GOTH_B_TO_ANCHOR_2     = 27929,
    SPELL_GOTH_C_TO_ANCHOR_2     = 27936,

    SPELL_GOTH_A_TO_SKULL        = 27915,
    SPELL_GOTH_B_TO_SKULL        = 27931,
    SPELL_GOTH_C_TO_SKULL        = 27937
};

uint32 const ClassicGothikAddEntries[] =
{
    NPC_UNREL_TRAINEE, NPC_UNREL_DEATH_KNIGHT, NPC_UNREL_RIDER, NPC_SPECT_TRAINEE, NPC_SPECT_DEATH_KNIGTH, NPC_SPECT_RIDER, NPC_SPECT_HORSE
};

// npc, npc, npc, timer
uint32 const ClassicGothikSummonData[MAX_GOTH_WAVES][4] =
{
    {NPC_UNREL_TRAINEE, 0, 0, 20000},
    {NPC_UNREL_TRAINEE, 0, 0, 20000},
    {NPC_UNREL_TRAINEE, 0, 0, 10000},
    {NPC_UNREL_DEATH_KNIGHT, 0, 0, 10000},
    {NPC_UNREL_TRAINEE, 0, 0, 15000},
    {NPC_UNREL_DEATH_KNIGHT, 0, 0, 5000},
    {NPC_UNREL_TRAINEE, 0, 0, 20000},
    {NPC_UNREL_DEATH_KNIGHT, NPC_UNREL_TRAINEE, 0, 10000},
    {NPC_UNREL_RIDER, 0, 0, 10000},
    {NPC_UNREL_TRAINEE, 0, 0, 5000},
    {NPC_UNREL_DEATH_KNIGHT, 0, 0, 15000},
    {NPC_UNREL_TRAINEE, NPC_UNREL_RIDER, 0, 10000},
    {NPC_UNREL_DEATH_KNIGHT, 0, 0, 10000},
    {NPC_UNREL_TRAINEE, 0, 0, 10000},
    {NPC_UNREL_RIDER, 0, 0, 5000},
    {NPC_UNREL_DEATH_KNIGHT, 0, 0, 5000},
    {NPC_UNREL_TRAINEE, 0, 0, 20000},
    {NPC_UNREL_RIDER, NPC_UNREL_DEATH_KNIGHT, NPC_UNREL_TRAINEE, 50000},
};
}

struct classic_boss_gothik : public ScriptedAI
{
    classic_boss_gothik(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature)) { }

    classic_instance_naxxramas_InstanceScript* m_pInstance;

    uint8 m_uiPhase = PHASE_GOTH_SPEECH;

    uint8 m_uiSpeechCount = 0;
    uint32 m_uiSpeechTimer = 5000;

    uint8 m_uiSummonCount = 0;
    uint32 m_uiSummonTimer = 4000;

    uint32 m_uiTeleportTimer = 15000;
    uint32 m_uiTeleportCastDelay = 0;
    uint32 m_uiShadowboltTimer = 1000;
    uint32 m_uiHarvestSoulTimer = 1000;
    uint32 m_uiNumTP = 0;
    uint32 m_checkAllPlayersOneSideTimer = 1000;
    uint32 m_uiTempPacifyTimer = 0;     // VMaNGOS Unit::SetTempPacified

    bool gatesOpened = false;
    bool m_bRightSide = false;
    bool m_bJustTeleported = false;

    void Reset() override
    {
        m_uiPhase = PHASE_GOTH_SPEECH;

        m_uiSpeechCount = 0;
        m_uiSpeechTimer = 5000;

        m_uiSummonCount = 0;
        m_uiSummonTimer = 4000;

        m_uiTeleportTimer = 15000;
        m_uiShadowboltTimer = 1000;
        m_uiHarvestSoulTimer = 1000;
        m_checkAllPlayersOneSideTimer = 1000;
        m_uiTeleportCastDelay = 0;
        m_uiNumTP = 0;
        gatesOpened = false;
        m_bRightSide = false;
        m_bJustTeleported = false;

        if (m_uiTempPacifyTimer)
        {
            m_uiTempPacifyTimer = 0;
            me->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
        }

        for (uint32 entry : ClassicGothikAddEntries)
        {
            std::list<Creature*> creaturesToDespawn;
            me->GetCreatureListWithEntryInGrid(creaturesToDespawn, entry, 1000.0f);
            for (Creature* pC : creaturesToDespawn)
                pC->DespawnOrUnsummon();
        }
        // TODO(classic): VMaNGOS SetCasterChaseDistance(40) - no TC equivalent (caster chase distance for his shadow bolts)
    }

    void SetTempPacified(uint32 timer)
    {
        m_uiTempPacifyTimer = timer;
        me->SetUnitFlag(UNIT_FLAG_PACIFIED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();

        ClassicScriptText(SAY_GOTH_SPEECH_1, me);

        if (!m_pInstance)
            return;

        m_pInstance->SetData(TYPE_GOTHIK, IN_PROGRESS);

        m_pInstance->SetGothTriggers();

        me->GetMotionMaster()->MoveIdle();
        DoStopAttack();
        if (!me->HasAura(SPELL_GOTH_IMMUNE_ALL))
            DoCastSelf(SPELL_GOTH_IMMUNE_ALL, true);
    }

    void AttackStart(Unit* pWho) override
    {
        if (!me->HasAura(SPELL_GOTH_IMMUNE_ALL))
            ScriptedAI::AttackStart(pWho);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        ScriptedAI::EnterEvadeMode(why);
        // TODO(classic): VMaNGOS also calls Respawn() here (no effect on a living TC creature)
        me->NearTeleportTo(me->GetHomePosition());
    }

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
            ClassicScriptText(SAY_GOTH_KILL, me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_GOTH_DEATH, me);
        OpenTheGate();
        if (m_pInstance)
            m_pInstance->SetData(TYPE_GOTHIK, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_GOTHIK, FAIL);
    }

    void SummonAdd(uint32 entry, float x, float y, float z, float o)
    {
        if (!me->IsInCombat() && me->IsAlive())
            return;

        if (Creature* pCreature = me->SummonCreature(entry, x, y, z, o, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 420000ms))
        {
            pCreature->SetCorpseDelay(10);
            if (gatesOpened)
            {
                CreatureAI::DoZoneInCombat(pCreature);
                return;
            }

            if (!m_pInstance)
                return;

            for (MapReference const& playerRef : me->GetMap()->GetPlayers())
            {
                Player* p = playerRef.GetSource();
                if (!p)
                    continue;

                bool isRightSide = m_pInstance->IsInRightSideGothArea(p);
                switch (entry)
                {
                    case NPC_UNREL_RIDER:
                    case NPC_UNREL_DEATH_KNIGHT:
                    case NPC_UNREL_TRAINEE:
                        if (isRightSide)
                        {
                            pCreature->SetInCombatWith(p);
                            pCreature->GetThreatManager().AddThreat(p, 100.0f);
                        }
                        break;
                    case NPC_SPECT_DEATH_KNIGTH:
                    case NPC_SPECT_HORSE:
                    case NPC_SPECT_RIDER:
                    case NPC_SPECT_TRAINEE:
                        if (!isRightSide)
                        {
                            pCreature->SetInCombatWith(p);
                            pCreature->GetThreatManager().AddThreat(p, 100.0f);
                        }
                        break;
                }
            }

            if (pCreature->AI())
                if (Unit* pTar = pCreature->AI()->SelectTarget(SelectTargetMethod::MinDistance, 0))
                    pCreature->AI()->AttackStart(pTar);
        }
    }

    void SummonAdds(bool bRightSide, uint32 uiSummonEntry)
    {
        if (!m_pInstance)
            return;

        std::list<Creature*> lSummonList;
        m_pInstance->GetGothSummonPointCreatures(lSummonList, bRightSide);

        if (lSummonList.empty())
            return;

        lSummonList.sort(Trinity::ObjectDistanceOrderPred(me));
        std::vector<Creature*> points(lSummonList.begin(), lSummonList.end());

        auto summonAt = [&](size_t idx)
        {
            // VMaNGOS walks the list without bounds checks
            if (idx >= points.size())
                return;
            Creature* c = points[idx];
            SummonAdd(uiSummonEntry, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), c->GetOrientation());
        };

        switch (uiSummonEntry)
        {
            case NPC_UNREL_DEATH_KNIGHT:
                summonAt(0);
                summonAt(points.size() - 1);
                break;
            case NPC_UNREL_TRAINEE:
                summonAt(0);
                summonAt(1);
                summonAt(3);
                break;
            case NPC_UNREL_RIDER:
                summonAt(1);
                break;
        }
    }

    void SummonedCreatureDies(Creature* pSummoned, Unit* /*killer*/) override
    {
        if (!m_pInstance)
            return;

        switch (pSummoned->GetEntry())
        {
            case NPC_SPECT_DEATH_KNIGTH:
            case NPC_SPECT_HORSE:
            case NPC_SPECT_RIDER:
            case NPC_SPECT_TRAINEE:
                return;
        }

        Creature* pAnchor = m_pInstance->GetClosestAnchorForGoth(pSummoned, true);

        if (!pAnchor)
            return;

        Creature* pTempTrigger = me->SummonCreature(
            NPC_SUB_BOSS_TRIGGER,
            pSummoned->GetPositionX(),
            pSummoned->GetPositionY(),
            pSummoned->GetPositionZ(),
            pSummoned->GetOrientation(),
            TEMPSUMMON_TIMED_DESPAWN,
            15000ms);

        if (!pTempTrigger)
            return;

        // Wrong caster, it expected to be pSummoned.
        // Mangos deletes the spell event at caster death, so for delayed spell like this
        // it's just a workaround. Does not affect other than the visual though (+ spell takes longer to "travel")
        // Elysium: we use a temp creature to handle this issue
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.SetOriginalCaster(pSummoned->GetGUID());
        switch (pSummoned->GetEntry())
        {
            case NPC_UNREL_TRAINEE:
                pTempTrigger->CastSpell(pAnchor, SPELL_GOTH_A_TO_ANCHOR_1, args);
                break;
            case NPC_UNREL_DEATH_KNIGHT:
                pTempTrigger->CastSpell(pAnchor, SPELL_GOTH_B_TO_ANCHOR_1, args);
                break;
            case NPC_UNREL_RIDER:
                pTempTrigger->CastSpell(pAnchor, SPELL_GOTH_C_TO_ANCHOR_1, args);
                break;
        }
    }

    void OpenTheGate()
    {
        if (gatesOpened)
            return;

        me->TextEmote(TEXT_GOTH_EMOTE_GATE, nullptr, true);

        gatesOpened = true;
        if (m_pInstance)
            if (GameObject* pGO = m_pInstance->GetSingleGameObjectFromStorage(GO_MILI_GOTH_COMBAT_GATE))
                pGO->SetGoState(GO_STATE_ACTIVE);

        for (uint32 entry : ClassicGothikAddEntries)
        {
            std::list<Creature*> allAdds;
            me->GetCreatureListWithEntryInGrid(allAdds, entry, 300.0f);
            for (Creature* pC : allAdds)
                CreatureAI::DoZoneInCombat(pC);
        }
    }

    bool HasLessPlayersPerSide(uint32 count)
    {
        if (!m_pInstance)
            return true;

        uint32 numLeft = 0;
        uint32 numRight = 0;
        for (MapReference const& playerRef : me->GetMap()->GetPlayers())
        {
            if (Player const* p = playerRef.GetSource())
            {
                // Don't count dead players, including those that are feigned
                // Otherwise we could have a bunch of feigned players sitting on one side
                if (!p->IsAlive() || p->HasAuraType(SPELL_AURA_FEIGN_DEATH))
                    continue;

                if (GameObject* pCombatGate = m_pInstance->GetSingleGameObjectFromStorage(GO_MILI_GOTH_COMBAT_GATE))
                {
                    // Do not count players outside the room.
                    if (!pCombatGate->IsWithinDist(p, 100))
                        continue;

                    // Do not count players stacked inside the gate.
                    if (std::abs(p->GetPositionY() - pCombatGate->GetPositionY()) < 0.5f)
                        continue;

                    if (pCombatGate->GetPositionY() >= p->GetPositionY())
                        ++numRight;
                    else
                        ++numLeft;
                }
            }
        }
        // if there are less than 10 people on one of the sides we consider it as
        // "everyone is on the same side". That to avoid the whole raid afking on spectral
        // side, waiting for gothik to TP down, in which case they have 40 sec to kill him
        // before the gates would ordinarily open.
        return (numLeft < count || numRight < count);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (m_uiTempPacifyTimer)
        {
            if (m_uiTempPacifyTimer <= uiDiff)
            {
                m_uiTempPacifyTimer = 0;
                me->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
            }
            else
                m_uiTempPacifyTimer -= uiDiff;
        }

        if (!me->HasAura(SPELL_GOTH_IMMUNE_ALL))
        {
            if (!UpdateVictim())
                return;
            if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
                return;
        }
        else
        {
            if (!me->IsInCombat())
                return;

            if (me->GetThreatManager().IsThreatListEmpty())
            {
                EnterEvadeMode(EvadeReason::NoHostiles);
                return;
            }
        }

        if (!m_pInstance)
            return;

        switch (m_uiPhase)
        {
            case PHASE_GOTH_SPEECH:
            {
                if (m_uiSpeechTimer < uiDiff)
                {
                    if (HasLessPlayersPerSide(10))
                    {
                        EnterEvadeMode(EvadeReason::Other);
                        return;
                    }

                    m_uiSpeechTimer = 5000;
                    ++m_uiSpeechCount;

                    switch (m_uiSpeechCount)
                    {
                        case 1:
                            ClassicScriptText(SAY_GOTH_SPEECH_2, me);
                            break;
                        case 2:
                            ClassicScriptText(SAY_GOTH_SPEECH_3, me);
                            break;
                        case 3:
                            ClassicScriptText(SAY_GOTH_SPEECH_4, me);
                            break;
                        case 4:
                            m_uiPhase = PHASE_GOTH_BALCONY;
                            break;
                    }
                }
                else
                    m_uiSpeechTimer -= uiDiff;

                break;
            }
            case PHASE_GOTH_BALCONY:
            {
                if (m_uiSummonTimer < uiDiff)
                {
                    if (m_uiSummonCount >= MAX_GOTH_WAVES)
                    {
                        ClassicScriptText(SAY_GOTH_TELEPORT, me);
                        me->TextEmote(TEXT_GOTH_EMOTE_TO_FRAY, nullptr, true);
                        DoCastSelf(SPELL_GOTH_TELEPORT_RIGHT);

                        m_bJustTeleported = true;
                        SetTempPacified(GOTH_TELEPORT_PACIFY_TIMER);

                        // opening the gates when TPing down if all players are considered on the same side
                        if (!gatesOpened && HasLessPlayersPerSide(1))
                            OpenTheGate();

                        if (me->HasAura(SPELL_GOTH_IMMUNE_ALL))
                            me->RemoveAurasDueToSpell(SPELL_GOTH_IMMUNE_ALL);
                        m_uiPhase = PHASE_GOTH_GROUND;
                        return;
                    }

                    SummonAdds(true, ClassicGothikSummonData[m_uiSummonCount][0]);

                    if (ClassicGothikSummonData[m_uiSummonCount][1])
                        SummonAdds(true, ClassicGothikSummonData[m_uiSummonCount][1]);

                    if (ClassicGothikSummonData[m_uiSummonCount][2])
                        SummonAdds(true, ClassicGothikSummonData[m_uiSummonCount][2]);

                    m_uiSummonTimer += ClassicGothikSummonData[m_uiSummonCount][3] - uiDiff;
                    ++m_uiSummonCount;
                }
                else
                    m_uiSummonTimer -= uiDiff;

                break;
            }
            case PHASE_GOTH_GROUND:
            {
                // If we just teleported
                if (m_bJustTeleported)
                {
                    m_bRightSide = m_pInstance->IsInRightSideGothArea(me);
                    ResetThreatAndAttackNearestTarget();
                    m_bJustTeleported = false;
                }

                // Prevent units in the other side of the room getting aggro from dots
                if (!gatesOpened)
                {
                    if (Unit* victim = me->GetVictim())
                    {
                        bool unitIsRight = m_pInstance->IsInRightSideGothArea(victim);
                        if (m_bRightSide != unitIsRight)
                        {
                            me->GetThreatManager().ResetThreat(victim);
                            // VMaNGOS SelectHostileTarget(): the new victim is picked on the next UpdateVictim()
                        }
                    }
                }

                if (!gatesOpened && me->GetHealthPct() < 30.0f)
                {
                    OpenTheGate();
                }

                // We check if a side has wiped every 1 sec. If it's the case, we open the gates
                if (!gatesOpened && m_checkAllPlayersOneSideTimer < uiDiff)
                {
                    if (HasLessPlayersPerSide(1))
                        OpenTheGate();
                    m_checkAllPlayersOneSideTimer = 1000;
                }
                else
                    m_checkAllPlayersOneSideTimer -= std::min(m_checkAllPlayersOneSideTimer, uiDiff);

                if (m_uiTeleportTimer < uiDiff && !gatesOpened) // stop teleporting after gates open
                {
                    uint32 uiTeleportSpell = m_bRightSide ? SPELL_GOTH_TELEPORT_LEFT : SPELL_GOTH_TELEPORT_RIGHT;

                    if (DoCastSelf(uiTeleportSpell) == SPELL_CAST_OK)
                    {
                        m_uiTeleportTimer = urand(15000, 20000);
                        m_uiShadowboltTimer = 1000;
                        m_uiTeleportCastDelay = 300; // delay spell timers for ~2s after teleport (inc pacify)
                        if (++m_uiNumTP >= 4 && !gatesOpened)
                            OpenTheGate();

                        // Clear the target and temporarily pacify after the teleport
                        me->SetTarget(ObjectGuid::Empty);
                        me->StopMoving();
                        me->GetMotionMaster()->Clear();
                        SetTempPacified(GOTH_TELEPORT_PACIFY_TIMER);
                        m_bJustTeleported = true;
                        return;
                    }
                }
                else
                {
                    m_uiTeleportTimer -= std::min(m_uiTeleportTimer, uiDiff);
                }

                // Delay any other casts if they will occur within 3 seconds of the teleport.
                // We need this to avoid a client issue where the teleport animation will
                // break and Gothik will slow walk to the teleport location. This is in place
                // of having proper recovery times on spells to prevent casts occuring too
                // close to one another
                if (!gatesOpened && m_uiTeleportTimer <= 3000)
                    m_uiTeleportCastDelay = 3000;

                if (m_uiTeleportCastDelay <= uiDiff)
                {
                    if (m_uiShadowboltTimer <= uiDiff)
                    {
                        if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* u) { return me->IsWithinLOSInMap(u); }))
                        {
                            if (DoCast(pTarget, SPELL_GOTH_SHADOWBOLT) == SPELL_CAST_OK)
                            {
                                m_uiShadowboltTimer = urand(1600, 2000);
                            }
                        }
                    }
                    else
                        m_uiShadowboltTimer -= uiDiff;

                    if (m_uiHarvestSoulTimer <= uiDiff)
                    {
                        if (DoCastSelf(SPELL_GOTH_HARVESTSOUL) == SPELL_CAST_OK)
                        {
                            m_uiHarvestSoulTimer = 20000;
                            if (m_uiTeleportTimer <= 1000)
                                m_uiTeleportTimer += 1200;
                        }
                    }
                    else
                        m_uiHarvestSoulTimer -= uiDiff;
                }
                else
                    m_uiTeleportCastDelay -= uiDiff;

                break;
            }
        }
    }

    void ResetThreatAndAttackNearestTarget()
    {
        ResetThreatList();
        if (Unit* pNearest = SelectTarget(SelectTargetMethod::MinDistance, 0, [this](Unit* u) { return me->IsWithinLOSInMap(u); }))
        {
            AttackStart(pNearest);
            AddThreat(pNearest, 300.0f);
        }
    }
};

// VMaNGOS EffectDummyCreature_spell_anchor
class classic_spell_gothik_anchor : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Creature* pCreatureTarget = GetHitCreature();
        if (!pCreatureTarget || pCreatureTarget->GetEntry() != NPC_SUB_BOSS_TRIGGER)
            return;

        classic_instance_naxxramas_InstanceScript* pInstance = GetClassicNaxxInstance(pCreatureTarget);

        if (!pInstance)
            return;

        uint32 const uiSpellId = GetSpellInfo()->Id;
        switch (uiSpellId)
        {
            case SPELL_GOTH_A_TO_ANCHOR_1:                           // trigger mobs at high right side
            case SPELL_GOTH_B_TO_ANCHOR_1:
            case SPELL_GOTH_C_TO_ANCHOR_1:
            {
                if (Creature* pAnchor2 = pInstance->GetClosestAnchorForGoth(pCreatureTarget, false))
                {
                    uint32 uiTriggered = SPELL_GOTH_A_TO_ANCHOR_2;

                    if (uiSpellId == SPELL_GOTH_B_TO_ANCHOR_1)
                        uiTriggered = SPELL_GOTH_B_TO_ANCHOR_2;
                    else if (uiSpellId == SPELL_GOTH_C_TO_ANCHOR_1)
                        uiTriggered = SPELL_GOTH_C_TO_ANCHOR_2;

                    pCreatureTarget->CastSpell(pAnchor2, uiTriggered, true);
                }
                break;
            }
            case SPELL_GOTH_A_TO_ANCHOR_2:                           // trigger mobs at high left side
            case SPELL_GOTH_B_TO_ANCHOR_2:
            case SPELL_GOTH_C_TO_ANCHOR_2:
            {
                std::list<Creature*> lTargets;
                pInstance->GetGothSummonPointCreatures(lTargets, false);

                if (!lTargets.empty())
                {
                    std::list<Creature*>::iterator itr = lTargets.begin();
                    uint32 uiPosition = urand(0, lTargets.size() - 1);
                    std::advance(itr, uiPosition);

                    if (Creature* pTarget = (*itr))
                    {
                        uint32 uiTriggered = SPELL_GOTH_A_TO_SKULL;

                        if (uiSpellId == SPELL_GOTH_B_TO_ANCHOR_2)
                            uiTriggered = SPELL_GOTH_B_TO_SKULL;
                        else if (uiSpellId == SPELL_GOTH_C_TO_ANCHOR_2)
                            uiTriggered = SPELL_GOTH_C_TO_SKULL;

                        pCreatureTarget->CastSpell(pTarget, uiTriggered, true);
                    }
                }
                break;
            }
            case SPELL_GOTH_A_TO_SKULL:                              // final destination trigger mob
            case SPELL_GOTH_B_TO_SKULL:
            case SPELL_GOTH_C_TO_SKULL:
            {
                if (Creature* pGoth = pInstance->GetSingleCreatureFromStorage(NPC_GOTHIK))
                {
                    classic_boss_gothik* gothAI = dynamic_cast<classic_boss_gothik*>(pGoth->AI());
                    if (!gothAI)
                        break;

                    uint32 uiNpcEntry = NPC_SPECT_TRAINEE;

                    if (uiSpellId == SPELL_GOTH_B_TO_SKULL)
                        uiNpcEntry = NPC_SPECT_DEATH_KNIGTH;
                    else if (uiSpellId == SPELL_GOTH_C_TO_SKULL)
                        uiNpcEntry = NPC_SPECT_RIDER;

                    gothAI->SummonAdd(uiNpcEntry, pCreatureTarget->GetPositionX(), pCreatureTarget->GetPositionY(), pCreatureTarget->GetPositionZ(), pCreatureTarget->GetOrientation());

                    if (uiNpcEntry == NPC_SPECT_RIDER)
                        gothAI->SummonAdd(NPC_SPECT_HORSE, pCreatureTarget->GetPositionX(), pCreatureTarget->GetPositionY(), pCreatureTarget->GetPositionZ(), pCreatureTarget->GetOrientation());
                }
                break;
            }
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(classic_spell_gothik_anchor::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// VMaNGOS gothikTriggerAI (script name spell_anchor, creature 16137)
struct classic_spell_anchor : public ScriptedAI
{
    classic_spell_anchor(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetWanderDistance(0.01f);
        me->SetDefaultMovementType(RANDOM_MOTION_TYPE);
        me->GetMotionMaster()->Initialize();
    }

    void MoveInLineOfSight(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void AttackStart(Unit* /*who*/) override { }
    void UpdateAI(uint32 /*diff*/) override { }
};

void AddSC_classic_boss_gothik()
{
    RegisterCreatureAI(classic_boss_gothik);
    RegisterCreatureAI(classic_spell_anchor);
    RegisterSpellScript(classic_spell_gothik_anchor);
}
