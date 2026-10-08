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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stormwind_city/stormwind_city.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1640 (Beat Bartleby), 1447 (The Missing Diplomat part 8)

#include "ScriptMgr.h"
#include "Group.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_script_text.h"

/*######
## npc_bartleby
######*/

enum BartlebyData
{
    CLASSIC_FACTION_ENEMY       = 168,
    QUEST_BEAT          = 1640
};

struct classic_npc_bartleby : public ScriptedAI
{
    classic_npc_bartleby(Creature* creature) : ScriptedAI(creature)
    {
        _normalFaction = creature->GetFaction();
    }

    void Reset() override
    {
        if (me->GetFaction() != _normalFaction)
            me->SetFaction(_normalFaction);
    }

    // VMaNGOS AttackedBy override (attack back unless friendly) is TC's default behaviour; nothing to port.

    void DamageTaken(Unit* doneBy, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!doneBy)
            return;

        if (damage > me->GetHealth() || ((me->GetHealth() - damage) * 100 / me->GetMaxHealth() < 15))
        {
            damage = 0;

            if (Player* player = doneBy->ToPlayer())
                player->AreaExploredOrEventHappens(QUEST_BEAT);

            EnterEvadeMode();
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_BEAT)
        {
            me->SetFaction(CLASSIC_FACTION_ENEMY);
            AttackStart(player);
        }
    }

private:
    uint32 _normalFaction;
};

/*######
## npc_dashel_stonefist
######*/

//-----------------------------------------------------------------------------
// Full quest event implementation (Missing Diplomat part 8 id:1447).
// Author: Kampeador
//-----------------------------------------------------------------------------
enum DashelStonefistData
{
    // ids from "broadcast_text" table
    SAY_PROGRESS_1_DAS = 1961, // Now you're gonna get it good, "PlayerName".
    SAY_PROGRESS_2_DAS = 1712, // Okay, okay! Enough fighting. No one else needs to get hurt.
    SAY_PROGRESS_3_DAS = 1713, // It's okay, boys. Back off. You've done enough. I'll meet up with you later.
    SAY_PROGRESS_4_THU = 1716, // All right, boss. You sure though? Just seems like a waste of good practice.
    SAY_PROGRESS_5_THU = 1715, // Yeah, okay, boss. No problem.
    // quest id
    QUEST_MISSING_DIPLO_PT8 = 1447,
    // NPCs that helps Dashel
    NPC_OLD_TOWN_THUG = 4969,
    // factions
    FACTION_NEUTRAL = 189,
    FACTION_IRONFORGE = 122, // original faction taken from DB
    FACTION_FRIENDLY_TO_ALL = 35,
    // quest phases
    MDQP_NONE = 0, // Dashel returns his spawn point

    // Occur only if thugs are alive
    MDQP_SAY1 = 1,
    MDQP_SAY2 = 2,
    MDQP_SAY3 = 4,
    MDQP_THUG_WALK_AWAY_1 = 5,
    MDQP_THUG_WALK_AWAY_2 = 6,

    MDQP_QUEST_COMPLETE = 7 // Triggers quest complete
};

Position const DashelThugResetPosition[] =
{
    { -8669.338867f, 448.362976f, 99.740005f },
    { -8686.397461f, 447.595703f, 99.994408f }
};

// VMaNGOS Player::GroupEventFailHappens (not in TC): fail the quest for the player and his group members on the map
static void ClassicGroupEventFailHappens(Player* player, uint32 questId)
{
    if (Group* group = player->GetGroup())
    {
        for (GroupReference const& itr : group->GetMembers())
        {
            Player* member = itr.GetSource();
            if (member && member->IsInMap(player) && member->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                member->FailQuest(questId);
        }
    }
    else if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
        player->FailQuest(questId);
}

struct classic_npc_dashel_stonefist : public ScriptedAI
{
    // old town thugs
    ObjectGuid m_thugs[2];
    // current event phase
    uint32 m_eventPhase;
    // check if an event has been started.
    bool m_dialogStarted;
    // player guid to trigger: quest completed
    ObjectGuid m_playerGuid;
    // timer to switch between phases
    uint32 m_nextPhaseDelayTimer;
    // used to check if 1 or more thugs are alive
    bool m_thugsAlive;
    // used to check if its a quest fight or not.
    bool m_questFightStarted;

    // if quest fight is active, then dialog event will be triggered
    inline void startQuestFight() { m_questFightStarted = true; }

    classic_npc_dashel_stonefist(Creature* creature) : ScriptedAI(creature), m_eventPhase(MDQP_NONE), m_dialogStarted(false),
        m_nextPhaseDelayTimer(3000), m_thugsAlive(false), m_questFightStarted(false) { }

    void Reset() override
    {
        if (m_questFightStarted)
        {
            // Reset() during quest fight -> quest failed.
            if (Player* player = ObjectAccessor::GetPlayer(*me, m_playerGuid))
                ClassicGroupEventFailHappens(player, QUEST_MISSING_DIPLO_PT8);

            // remove thugs
            for (ObjectGuid const& guid : m_thugs)
            {
                if (Creature* thug = ObjectAccessor::GetCreature(*me, guid))
                    if (thug->IsAlive())
                        if (TempSummon* summon = thug->ToTempSummon())
                            summon->UnSummon();
            }
        }

        // clear thug guids
        for (ObjectGuid& guid : m_thugs)
            guid.Clear();

        m_questFightStarted = false;
        m_eventPhase = MDQP_NONE;
        m_dialogStarted = false;
        m_nextPhaseDelayTimer = 3000; // MDQP_SAY1 phase delay
        m_thugsAlive = false;
        // restore some flags
        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        // restore faction
        me->SetFaction(FACTION_IRONFORGE);
        // reset player guid
        m_playerGuid.Clear();
    }

    // VMaNGOS: after the surrender Dashel only drops combat and walks home (no Reset()); TC's default evade always calls
    // Reset(), which would end the dialog, so skip it while the dialog is running.
    void EnterEvadeMode(EvadeReason why) override
    {
        if (!m_dialogStarted)
        {
            ScriptedAI::EnterEvadeMode(why);
            return;
        }

        if (!_EnterEvadeMode(why))
            return;

        me->AddUnitState(UNIT_STATE_EVADE);
        me->GetMotionMaster()->MoveTargetedHome();
    }

    void DamageTaken(Unit* /*doneBy*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (m_questFightStarted)
        {
            if (damage > me->GetHealth() || me->GetHealthPct() < 20.0f)
            {
                damage = 0;

                // Dashel says: Okay, okay! Enough fighting. No one else needs to get hurt.
                ClassicScriptText(SAY_PROGRESS_2_DAS, me);

                me->RemoveAllAuras();
                me->SetFaction(FACTION_FRIENDLY_TO_ALL);

                // check if thugs are alive
                for (ObjectGuid const& guid : m_thugs)
                {
                    if (Creature* thug = ObjectAccessor::GetCreature(*me, guid))
                    {
                        if (!thug->IsAlive())
                            continue;

                        thug->RemoveAllAuras();
                        thug->GetThreatManager().ClearAllThreat();
                        thug->CombatStop();

                        thug->SetFaction(FACTION_FRIENDLY_TO_ALL);

                        thug->GetMotionMaster()->MoveTargetedHome();

                        m_thugsAlive = true;
                    }
                }

                m_questFightStarted = false;
                m_dialogStarted = true;
                m_eventPhase = MDQP_NONE;

                // DeleteThreatList + CombatStop + MoveTargetedHome (Reset() is skipped, see EnterEvadeMode)
                me->GetThreatManager().ClearAllThreat();
                EnterEvadeMode(EvadeReason::Other);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        switch (m_eventPhase)
        {
            case MDQP_SAY1: // Occurs only if thugs are alive
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    // Dashel says: It's okay, boys. Back off. You've done enough.I'll meet up with you later.
                    ClassicScriptText(SAY_PROGRESS_3_DAS, me);
                    // switch phase
                    m_nextPhaseDelayTimer = 3000;
                    m_eventPhase = MDQP_SAY2;
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            case MDQP_SAY2: // Occurs only if thugs are alive
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    // recheck for safety, thugs can be killed by gm command, etc.
                    if (Creature* thug = ObjectAccessor::GetCreature(*me, m_thugs[0]))
                        if (thug->IsAlive())
                            ClassicScriptText(SAY_PROGRESS_4_THU, thug);

                    // switch phase
                    m_nextPhaseDelayTimer = 1500;
                    m_eventPhase = MDQP_SAY3;
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            case MDQP_SAY3: // Occurs only if thugs are alive
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    // recheck for safety, thugs can be killed by gm command, etc.
                    if (Creature* thug = ObjectAccessor::GetCreature(*me, m_thugs[1]))
                        if (thug->IsAlive())
                            ClassicScriptText(SAY_PROGRESS_5_THU, thug);

                    // switch phase
                    m_nextPhaseDelayTimer = 1000;
                    m_eventPhase = MDQP_THUG_WALK_AWAY_1;
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            case MDQP_THUG_WALK_AWAY_1: // Occurs only if thugs are alive
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    ResetThug(0);

                    m_nextPhaseDelayTimer = 1500;
                    m_eventPhase = MDQP_THUG_WALK_AWAY_2;
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            case MDQP_THUG_WALK_AWAY_2: // Occurs only if thugs are alive
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    // Second thug goes away
                    ResetThug(1);

                    m_nextPhaseDelayTimer = 1000;
                    m_eventPhase = MDQP_QUEST_COMPLETE;
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            case MDQP_QUEST_COMPLETE:
            {
                if (m_nextPhaseDelayTimer < diff)
                {
                    // Set quest completed
                    if (Player* player = ObjectAccessor::GetPlayer(*me, m_playerGuid))
                        player->GroupEventHappens(QUEST_MISSING_DIPLO_PT8, me);

                    Reset();
                }
                else
                    m_nextPhaseDelayTimer -= diff;
                break;
            }
            default: // MDQP_NONE
                UpdateVictim();
                break;
        }
    }

    void ResetThug(int thug)
    {
        if (thug >= 2)
            return;

        if (Creature* pThug = ObjectAccessor::GetCreature(*me, m_thugs[thug]))
        {
            if (pThug->IsAlive())
            {
                Position const& pos = DashelThugResetPosition[thug];
                pThug->GetMotionMaster()->MovePoint(0, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), true, {}, {},
                    MovementWalkRunSpeedSelectionMode::ForceWalk);
                pThug->DespawnOrUnsummon(3s);
            }
        }
    }

    // Dashel returns to his spawn point
    void JustReachedHome() override
    {
        // switch to correct dialog phase, depends if thugs are alive.
        if (m_dialogStarted)
        {
            if (m_thugsAlive)
                m_eventPhase = MDQP_SAY1;
            else
                m_eventPhase = MDQP_QUEST_COMPLETE;
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        // case: something weird happened, killed by GM command, etc.
        if (m_dialogStarted || m_questFightStarted)
        {
            // remove thugs
            for (ObjectGuid& guid : m_thugs)
            {
                if (Creature* thug = ObjectAccessor::GetCreature(*me, guid))
                    if (TempSummon* summon = thug->ToTempSummon())
                        summon->UnSummon();
                guid.Clear();
            }
        }
    }

    void SummonedCreatureDies(Creature* creature, Unit* /*killer*/) override
    {
        // If the thug died for whatever reason, clear the guid.
        for (ObjectGuid& guid : m_thugs)
        {
            if (guid == creature->GetGUID())
                guid.Clear();
        }
    }

    void SummonedCreatureDespawn(Creature* creature) override
    {
        for (ObjectGuid& guid : m_thugs)
        {
            if (guid == creature->GetGUID())
                guid.Clear();
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_MISSING_DIPLO_PT8)
            return;

        // On official in some cases: he didn't say this phrase. I am not sure if it was a bug or a random feature.
        // Dashel says: Now you're gonna get it good, "PlayerName".
        ClassicScriptText(SAY_PROGRESS_1_DAS, me, player);

        me->SetFaction(FACTION_NEUTRAL);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);

        m_playerGuid = player->GetGUID();

        // spawn thugs and make them focus player.
        Creature* thug1 = me->SummonCreature(NPC_OLD_TOWN_THUG, -8676.075195f, 443.744019f, 99.632210f, 3.981758f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30s);
        if (!thug1 || !thug1->AI())
            return;

        m_thugs[0] = thug1->GetGUID();

        thug1->AI()->AttackStart(player);
        thug1->SetImmuneToNPC(false);

        // thug 2
        if (Creature* thug2 = me->SummonCreature(NPC_OLD_TOWN_THUG, -8685.416992f, 443.130829f, 99.526917f, 5.759635f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30s))
        {
            m_thugs[1] = thug2->GetGUID();
            if (thug2->AI())
                thug2->AI()->AttackStart(player);
            thug2->SetImmuneToNPC(false);
        }
        // start quest fight.
        startQuestFight();
        // make Dashel focus player.
        AttackStart(player);
    }
};

void AddSC_classic_stormwind_city()
{
    RegisterCreatureAI(classic_npc_bartleby);
    RegisterCreatureAI(classic_npc_dashel_stonefist);
}
