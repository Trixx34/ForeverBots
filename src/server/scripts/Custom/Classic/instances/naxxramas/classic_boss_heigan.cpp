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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_heigan.cpp (GPL-2)
// Scripts: boss_heigan, spell_heigan_mana_burn

/*
Full rewrite by Gemt

Current semi-unknowns:
The aoe manaburn; not seen it used in videos, despite all original vanilla guides mentioning it.
Probably because its pretty much a wipe if ranged/healers are hit by it.
While Decrepit fever is very aggressive on its cooldown after a dance phase, the mana burn does not seem to be,
so we should give "plenty" of time for casters to get to the platform, and tank to move the boss away.

Chain pull radius for adds in encounter gauntlet is a bit of a guesswork with some hacks to override
the default, static, callForHelp radius.

*/

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "EventMap.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include <algorithm>
#include <vector>

namespace
{
enum ClassicNaxxHeiganData : uint32
{
    // BroadcastText ids
    SAY_HEIGAN_AGGRO1              = 13041,
    SAY_HEIGAN_AGGRO2              = 13042,
    SAY_HEIGAN_AGGRO3              = 13043,
    SAY_HEIGAN_SLAY                = 13045,

    SAY_HEIGAN_TAUNT1              = 13046,
    SAY_HEIGAN_TAUNT2              = 13047,
    SAY_HEIGAN_TAUNT3              = 13048,
    SAY_HEIGAN_TAUNT4              = 13050,
    SAY_HEIGAN_CHANNELING          = 13049,
    // SAY_DEATH      = -1533118 (VMaNGOS script_texts id, "need find correct bct id!"; no text data available)
    // EMOTE_TELEPORT = -1533136 (VMaNGOS script_texts id, unused by the script)
    // EMOTE_RETURN   = -1533137 (VMaNGOS script_texts id, unused by the script)

    SPELL_HEIGAN_ERUPTION          = 29371,
    SPELL_HEIGAN_PLAGUE_WAVE       = 30243,

    //Spells by boss
    SPELL_HEIGAN_DECREPIT_FEVER    = 29998,
    SPELL_HEIGAN_PLAGUE_CLOUD      = 29350,
    SPELL_HEIGAN_TELEPORT_SELF     = 30211,
    SPELL_HEIGAN_MANABURN          = 29310,

    NPC_HEIGAN_PLAGUE_WAVE         = 17293
};

enum ClassicNaxxHeiganEvents : uint32
{
    EVENT_HEIGAN_FEVER = 1,
    EVENT_HEIGAN_ERUPT,
    EVENT_HEIGAN_DANCE,
    EVENT_HEIGAN_DANCE_END,
    EVENT_HEIGAN_TAUNT,
    EVENT_HEIGAN_DOOR_CLOSE,
    EVENT_HEIGAN_MANABURN,
    EVENT_HEIGAN_PORT_PLAYER
};

enum ClassicNaxxHeiganPhases : uint8
{
    PHASE_HEIGAN_FIGHT = 1,
    PHASE_HEIGAN_DANCE
};

//static uint32 const firstEruptionDBGUID = 533048;
uint8 const naxxHeiganNumSections = 4;
/*static uint8 const numEruptions[numSections] = { // count of sequential GO DBGUIDs in the respective section of the room
    15,
    25,
    23,
    13
};*/

// in tunnel
constexpr float naxxHeiganSafespotFissures[3][3] =
{
    {2747.0f, -3754.0f, 274.0f},
    {2805.8f, -3695.88f, 273.61f},
    {2812.95f, -3703.52f, 273.61f},
};

constexpr float naxxHeiganSect1SafeSpot[3][3] =
{
    { 2799.5f, -3691.0f, 273.62f },
    { 2810.67f, -3706.06f, 275.0f },
    { 2803.51f, -3697.42f, 274.1f }
};

constexpr float naxxHeiganSect2SafeSpot[3] = { 2790.51f, -3690.45f, 273.622f };
constexpr float naxxHeiganSect3SafeSpot[3] = { 2778.40f, -3702.645f, 273.621f };
constexpr float naxxHeiganSect4SafeSpot[3][3] =
{
    { 2777.2f, -3712.41f, 273.63f },
    { 2783.06f, -3717.7f, 273.63f },
    { 2791.62f, -3726.04f, 273.63f },
};
}

struct classic_boss_heigan : public ScriptedAI
{
    classic_boss_heigan(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature))
    {
        currentPhase = PHASE_HEIGAN_FIGHT;
        eruptionPhase = 0;
        killCooldown = 10000;
    }

    classic_instance_naxxramas_InstanceScript* m_pInstance;

    ClassicNaxxHeiganPhases currentPhase;
    EventMap m_events;
    uint8 eruptionPhase;
    std::vector<ObjectGuid> _eruptTiles[naxxHeiganNumSections];
    uint32 killCooldown;
    std::vector<ObjectGuid> portedPlayersThisPhase;

    void Reset() override
    {
        portedPlayersThisPhase.clear();

        m_events.Reset();
        killCooldown = 10000;
        currentPhase = PHASE_HEIGAN_FIGHT;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();

        eruptionPhase = 0;
        currentPhase = PHASE_HEIGAN_FIGHT;
        m_events.ScheduleEvent(EVENT_HEIGAN_FEVER,       Seconds(30), 0, PHASE_HEIGAN_FIGHT);
        m_events.ScheduleEvent(EVENT_HEIGAN_DANCE,       Seconds(90), 0, PHASE_HEIGAN_FIGHT);
        m_events.ScheduleEvent(EVENT_HEIGAN_ERUPT,       Seconds(15), 0, PHASE_HEIGAN_FIGHT);
        m_events.ScheduleEvent(EVENT_HEIGAN_MANABURN,    Seconds(15), 0, PHASE_HEIGAN_FIGHT);
        m_events.ScheduleEvent(EVENT_HEIGAN_TAUNT,       randtime(Seconds(20), Seconds(70)));
        m_events.ScheduleEvent(EVENT_HEIGAN_DOOR_CLOSE,  Seconds(15));
        m_events.ScheduleEvent(EVENT_HEIGAN_PORT_PLAYER, Seconds(40));

        ClassicScriptText(urand(SAY_HEIGAN_AGGRO1, SAY_HEIGAN_AGGRO3), me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_HEIGAN, IN_PROGRESS);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (currentPhase == PHASE_HEIGAN_DANCE)
            return;
        else
        {
            if (who->GetPositionX() > 2825.0f)
                return;
            // VMaNGOS: CanInitiateAttack() && IsTargetableBy() && IsHostileTo() && IsInAccessablePlaceFor() && IsWithinLOSInMap()
            // (no aggro-radius check). Both VMaNGOS branches (AttackStart when no victim, otherwise SetInCombatWith + AddThreat)
            // map to EngageWithTarget in TC.
            if (!me->HasReactState(REACT_PASSIVE) && me->IsValidAttackTarget(who) && me->IsHostileTo(who))
            {
                if (me->CanCreatureAttack(who) && me->IsWithinLOSInMap(who))
                {
                    if (!me->GetVictim())
                        me->EngageWithTarget(who);
                    else if (me->GetMap()->IsDungeon())
                        me->EngageWithTarget(who);
                }
            }
        }
    }

    void AttackStart(Unit* who) override
    {
        if (currentPhase == PHASE_HEIGAN_DANCE)
            return;
        else
            ScriptedAI::AttackStart(who);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (!killCooldown)
            ClassicScriptText(SAY_HEIGAN_SLAY, me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        // TODO(classic): VMaNGOS DoScriptText(SAY_DEATH = -1533118) - no broadcast text id known (VMaNGOS: "need find correct bct id!");
        // the text is not in the imported data, so nothing is said (VMaNGOS says nothing either when the script text is missing).

        if (m_pInstance)
        {
            m_pInstance->SetData(TYPE_HEIGAN, DONE);
            m_pInstance->UpdateAutomaticBossEntranceDoor(GO_PLAG_HEIG_ENTRY_DOOR, DONE);
        }
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
        {
            m_pInstance->SetData(TYPE_HEIGAN, FAIL);
            m_pInstance->UpdateAutomaticBossEntranceDoor(GO_PLAG_HEIG_ENTRY_DOOR, FAIL);
        }
    }

    void SendEruptCustomLocation(float x, float y, float z)
    {
        if (Creature* fissureCreature = me->SummonCreature(NPC_HEIGAN_PLAGUE_WAVE, x, y, z, 0, TEMPSUMMON_TIMED_DESPAWN, Milliseconds(50)))
        {
            fissureCreature->CastSpell(fissureCreature, SPELL_HEIGAN_ERUPTION, true);
        }
    }

    void UpdateEruption()
    {
        if (!m_pInstance)
            return;
        Creature* fissureCreature = me->SummonCreature(NPC_HEIGAN_PLAGUE_WAVE, 2773.0f, -3684.0f, 292.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN, Milliseconds(1000));
        if (!fissureCreature)
        {
            TC_LOG_ERROR("scripts", "Heigan: failed spawning fissure creature");
            return;
        }

        for (uint8 uiArea = 0; uiArea < 4; ++uiArea)
        {
            // Actually this is correct :P
            if (uiArea == (eruptionPhase % 6) || uiArea == 6 - (eruptionPhase % 6))
                continue;

            for (ObjectGuid const& guid : m_pInstance->m_alHeiganTrapGuids[uiArea])
            {
                if (GameObject* pTrap = m_pInstance->GetGameObject(guid))
                {
                    pTrap->Use(fissureCreature);
                    pTrap->SendCustomAnim(0); // VMaNGOS SendGameObjectCustomAnim()
                }
            }

            switch (uiArea)
            {
                case 0:
                    for (auto const& i : naxxHeiganSect1SafeSpot)
                        SendEruptCustomLocation(i[0], i[1], i[2]);
                    break;
                case 1:
                    SendEruptCustomLocation(naxxHeiganSect2SafeSpot[0], naxxHeiganSect2SafeSpot[1], naxxHeiganSect2SafeSpot[2]);
                    break;
                case 2:
                    SendEruptCustomLocation(naxxHeiganSect3SafeSpot[0], naxxHeiganSect3SafeSpot[1], naxxHeiganSect3SafeSpot[2]);
                    break;
                case 3:
                    for (auto const& i : naxxHeiganSect4SafeSpot)
                        SendEruptCustomLocation(i[0], i[1], i[2]);
                    break;
            }
        }

        // safespot avoidance in tunnel
        if (currentPhase == PHASE_HEIGAN_DANCE)
        {
            for (auto const& safespotFissure : naxxHeiganSafespotFissures)
                SendEruptCustomLocation(safespotFissure[0], safespotFissure[1], safespotFissure[2]);
        }

        ++eruptionPhase;
    }

    void SummmonPlagueWave(float x, float y, float z, float o)
    {
        if (Creature* pCloud = me->SummonCreature(NPC_HEIGAN_PLAGUE_WAVE, x, y, z, o, TEMPSUMMON_TIMED_DESPAWN, Milliseconds(45000)))
        {
            pCloud->CastSpell(pCloud, SPELL_HEIGAN_PLAGUE_WAVE, true);
        }
    }

    // VMaNGOS GetTimeUntilEvent result for an event that must survive m_events.Reset()
    Milliseconds StashNaxxHeiganTaunt() const
    {
        Milliseconds tauntStash = m_events.GetTimeUntilEvent(EVENT_HEIGAN_TAUNT);
        if (tauntStash == Milliseconds::max()) // not scheduled (should not happen); avoid overflowing the event time
            tauntStash = randtime(Seconds(20), Seconds(70));
        return tauntStash;
    }

    void EventStartDance()
    {
        portedPlayersThisPhase.clear();

        if (DoCast(me, SPELL_HEIGAN_TELEPORT_SELF, true) != SPELL_CAST_OK)
        {
            return;
        }
        currentPhase = PHASE_HEIGAN_DANCE;

        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->StopMoving();
        me->GetMotionMaster()->MoveIdle();
        DoStopAttack();
        DoCastAOE(SPELL_HEIGAN_PLAGUE_CLOUD);

        Milliseconds tauntStash = StashNaxxHeiganTaunt();
        m_events.Reset();
        m_events.ScheduleEvent(EVENT_HEIGAN_TAUNT, tauntStash);
        m_events.ScheduleEvent(EVENT_HEIGAN_DANCE_END, Seconds(45), 0, PHASE_HEIGAN_DANCE);
        m_events.ScheduleEvent(EVENT_HEIGAN_ERUPT, Seconds(4));

        // the regular ones
        for (auto const& eyeStalkPossition : eyeStalkPossitions)
        {
            SummmonPlagueWave(eyeStalkPossition[0], eyeStalkPossition[1], eyeStalkPossition[2], eyeStalkPossition[3]);
        }

        ClassicScriptText(SAY_HEIGAN_CHANNELING, me);
        eruptionPhase = 0;
    }

    void EventDanceEnd()
    {
        currentPhase = PHASE_HEIGAN_FIGHT;

        Milliseconds tauntStash = StashNaxxHeiganTaunt();
        m_events.Reset();
        m_events.ScheduleEvent(EVENT_HEIGAN_TAUNT,       tauntStash);
        m_events.ScheduleEvent(EVENT_HEIGAN_FEVER,       Seconds(5)); // videos confirm this, unless raid moves perfectly, more or less everyone is hit.
        m_events.ScheduleEvent(EVENT_HEIGAN_DANCE,       Seconds(90));
        m_events.ScheduleEvent(EVENT_HEIGAN_ERUPT,       Seconds(10));
        m_events.ScheduleEvent(EVENT_HEIGAN_MANABURN,    Seconds(10));
        m_events.ScheduleEvent(EVENT_HEIGAN_PORT_PLAYER, Seconds(18));
        m_events.ScheduleEvent(EVENT_HEIGAN_PORT_PLAYER, Seconds(48));
        me->InterruptNonMeleeSpells(false);
        me->SetReactState(REACT_AGGRESSIVE);
        eruptionPhase = 0;

        if (!UpdateVictim())
            return;
        me->GetMotionMaster()->MoveChase(me->GetVictim());
    }

    void EventPortPlayer()
    {
        // VMaNGOS iterates the (sorted) threat list and skips its first entry (the tank)
        std::vector<Unit*> threatVictims;
        for (ThreatReference const* ref : me->GetThreatManager().GetSortedThreatList())
            threatVictims.push_back(ref->GetVictim());

        std::vector<Unit*> candidates;
        for (size_t i = 1; i < threatVictims.size(); ++i) // skip the tank
        {
            if (Player* pUnit = threatVictims[i]->ToPlayer())
            {
                // Candidates are only alive players who have not yet been ported during this phase rotation
                if (pUnit->IsAlive()
                    && std::find(portedPlayersThisPhase.begin(), portedPlayersThisPhase.end(), pUnit->GetGUID()) == portedPlayersThisPhase.end())
                {
                    candidates.push_back(pUnit);
                }
            }
        }

        for (int i = 0; i < 3; i++)
        {
            if (candidates.empty())
                break;

            uint32 idx = urand(0, uint32(candidates.size()) - 1);
            Unit* target = candidates[idx];
            candidates.erase(candidates.begin() + idx);
            portedPlayersThisPhase.push_back(target->GetGUID());
            // getting the spell visual to show both where you were TPed from and where you are TPed too
            if (Creature* pCreature = me->SummonCreature(NPC_HEIGAN_PLAGUE_WAVE,
                target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), target->GetOrientation(),
                TEMPSUMMON_TIMED_DESPAWN, Milliseconds(2000)))
            {
                // TODO(classic): VMaNGOS pCreature->SendSpellGo(pCreature, 30211) (visual-only SMSG_SPELL_GO of spell 30211,
                // nothing is cast); TC has no visual-only SpellGo helper. The summon is kept so the despawn timing is identical.
                (void)pCreature;
            }
            // TODO(classic): VMaNGOS target->SendSpellGo(target, 30211) (visual only), not ported.
            target->NearTeleportTo(2917.43f, -3769.18f, 273.62f, 3.1415f);
        }
    }

    void EventTaunt()
    {
        // Taunt
        static uint32 const naxxHeiganTaunts[4] = { SAY_HEIGAN_TAUNT1, SAY_HEIGAN_TAUNT2, SAY_HEIGAN_TAUNT3, SAY_HEIGAN_TAUNT4 };
        ClassicScriptText(naxxHeiganTaunts[urand(0, 3)], me);  // VMaNGOS PickRandomValue(...)
        m_events.Repeat(randtime(Seconds(20), Seconds(70)));
    }

    void CheckManausersAndRepeat()
    {
        // Looking for anyone with a manabar, currently excluded pets and totems
        // within 25yd range (radius of SPELL_MANABURN). If there is one we cast SPELL_MANABURN
        bool found_mana_in_range = false;
        for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
        {
            if (Player* pTarget = ref->GetVictim()->ToPlayer())
            {
                if (pTarget->GetPowerType() == POWER_MANA && pTarget->IsAlive())
                {
                    // VMaNGOS GetDistance3dToCenter
                    if (me->GetExactDist(pTarget) < 28.0f)
                    {
                        found_mana_in_range = true;
                        break;
                    }
                }
            }
        }

        if (found_mana_in_range && DoCast(me, SPELL_HEIGAN_MANABURN) == SPELL_CAST_OK)
            m_events.Repeat(Seconds(3));
        else
            m_events.Repeat(Seconds(1));
    }

    void UpdateAI(uint32 diff) override
    {
        // This will avoid him running off the platform during dance phase.
        if (currentPhase == PHASE_HEIGAN_FIGHT)
        {
            if (!UpdateVictim())
                return;
            if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
                return;
        }
        else
        {
            // If wipe, we force the dance phase to end so above code runs and he evades.
            if (me->GetThreatManager().IsThreatListEmpty())
                EventDanceEnd();
        }

        m_events.Update(diff);
        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_HEIGAN_FEVER:
                    DoCastAOE(SPELL_HEIGAN_DECREPIT_FEVER);
                    m_events.Repeat(randtime(Seconds(20), Seconds(25)));
                    break;
                case EVENT_HEIGAN_DANCE:
                    EventStartDance();
                    break;
                case EVENT_HEIGAN_DANCE_END:
                    EventDanceEnd();
                    break;
                case EVENT_HEIGAN_ERUPT:
                    UpdateEruption();
                    m_events.Repeat(currentPhase == PHASE_HEIGAN_DANCE ? Seconds(3) : Seconds(10));
                    break;
                case EVENT_HEIGAN_TAUNT:
                    EventTaunt();
                    break;
                case EVENT_HEIGAN_DOOR_CLOSE:
                    if (m_pInstance)
                        m_pInstance->UpdateAutomaticBossEntranceDoor(GO_PLAG_HEIG_ENTRY_DOOR, IN_PROGRESS);
                    break;
                case EVENT_HEIGAN_MANABURN:
                    CheckManausersAndRepeat();
                    break;
                case EVENT_HEIGAN_PORT_PLAYER:
                    EventPortPlayer();
                    break;
            }
        }

        if (killCooldown < diff)
            killCooldown = 0;
        else
            killCooldown -= diff;

        // melee: TC master auto-melee (no victim during the dance phase)
    }
};

// 29310 - Mana Burn (Heigan, naxxramas)
// VMaNGOS OnSetTargetMap: radius = 28.0f. 1.60 client data: effect 0 POWER_BURN, targets 22/15, radius index 20.
// TC has no radius hook in SpellScript: the spell's RadiusMod is set in OnPrecast (before target selection) so that
// the effective radius becomes 28 yd.
class classic_spell_heigan_mana_burn : public SpellScript
{
    void OnPrecast() override
    {
        // Without a bigger raidus its possible to tank heigan in one corner of the platform, and have ranged stay in the other corner
        SpellRange const radius = GetEffectInfo(EFFECT_0).CalcRadius(GetCaster());
        if (radius.Max > 0.0f)
            GetSpell()->SetSpellValue(CastSpellExtraArgsInit::SpellValueOverride(SPELLVALUE_RADIUS_MOD, SpellEffectValue(28.0f / radius.Max)));
    }

    void Register() override { }
};

void AddSC_classic_boss_heigan()
{
    RegisterCreatureAI(classic_boss_heigan);
    RegisterSpellScript(classic_spell_heigan_mana_burn);
}
