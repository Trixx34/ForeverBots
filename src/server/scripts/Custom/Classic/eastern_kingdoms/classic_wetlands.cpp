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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/wetlands/wetlands.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1249 (The Missing Diplomat part 11). Original VMaNGOS implementation by Kampeador.

#include "ScriptMgr.h"
#include "Group.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include <iterator>

enum MissingDiplomat
{
    // ids from "broadcast_text" table
    DIPLOMAT_SAY_PROGRESS_1_TAP     = 5827, // Oh, it's on now! But you thought I'd be alone too, huh?!
    DIPLOMAT_SAY_PROGRESS_2_FRI     = 5828, // Whoa! This is way more than what I bargained for, you're on your own, Slim!
    DIPLOMAT_SAY_PROGRESS_3_TAP     = 1743, // Okay, okay! No need to get all violent. I'll talk. I'll talk!
    DIPLOMAT_SAY_PROGRESS_4_TAP     = 1744, // I have a few notes from the job back at my place. I'll get them and then meet you back in the inn.
    DIPLOMAT_SAY_PROGRESS_5_MIC     = 4169, // I'm glad the commotions died down some around here. The last thing this place needs is another brawl.

    DIPLOMAT_QUEST_PART10           = 1248,
    DIPLOMAT_QUEST_PART11           = 1249,

    DIPLOMAT_FACTION_FRIENDLY_TO_ALL = 35,
    // Blizzlike number will probably be 34, but there are some collisions in current db with other NPCs in this area,
    // they will attack Tapoke Slim Jahn during the event.
    DIPLOMAT_FACTION_NEUTRAL        = 189,

    // spells used by Tapoke "Slim" Jahn
    DIPLOMAT_SPELL_STEALTH          = 6634,  // used during escape from the inn
    DIPLOMAT_SPELL_CALL_FRIENDS     = 16457, // summons 1x friend
    DIPLOMAT_SPELL_PUMMEL           = 12555, // used to interrupt enemy spells

    DIPLOMAT_NPC_MIKHAIL            = 4963,  // quest giver, starts the event
    DIPLOMAT_NPC_SLIMS_FRIEND       = 4971,  // NPC that helps Tapoke Slim
    DIPLOMAT_NPC_TAPOKE_SLIM_JAHN   = 4962,

    DIPLOMAT_WAYPOINT_MAILBOX       = 3,
    DIPLOMAT_WAYPOINT_GATE          = 9,

    DIPLOMAT_NPC_TEXT_BUSY          = 1713   // npc_text shown by Mikhail when the event cannot start
};

// VMaNGOS script_waypoint entry 4962. Walks, SetRun() at the mailbox (point 3): nodes 4-9 run.
Position const TapokePath[] =
{
    { -3804.22f, -830.536f, 10.0931f },
    { -3803.93f, -833.711f, 10.0823f },
    { -3802.19f, -836.116f, 10.0776f },
    { -3795.98f, -836.212f, 10.0776f },
    { -3766.63f, -838.984f, 11.0411f },
    { -3730.40f, -840.066f, 12.0595f },
    { -3707.90f, -851.156f, 10.7113f },
    { -3685.95f, -846.704f, 10.1290f },
    { -3663.17f, -837.297f, 9.89928f },
    { -3659.13f, -839.593f, 9.89778f }
};

/*######
## npc_tapoke_slim_jahn
######*/

struct classic_npc_tapoke_slim_jahn : public EscortAI
{
    classic_npc_tapoke_slim_jahn(Creature* creature) : EscortAI(creature)
    {
        // TODO(classic): VMaNGOS disables pathfinding between these waypoints (SetPathfindingEnabledBetweenWaypoints(false));
        // TC EscortAI always uses MovePath(_path) with the generator defaults.
        for (uint32 i = 0; i < std::size(TapokePath); ++i)
            AddWaypoint(i, TapokePath[i].GetPositionX(), TapokePath[i].GetPositionY(), TapokePath[i].GetPositionZ(), i > DIPLOMAT_WAYPOINT_MAILBOX);

        Initialize();
        _announcePending = true;
    }

    void Initialize()
    {
        _nextPhaseDelay = 0;
        _dialogPhase = 0;
        _isBeaten = false;
        _pummelTimer = urand(1400, 2700);
    }

    void Reset() override
    {
        Initialize();
    }

    // called by Mikhail on quest accept
    void StartEvent(Player* player, Quest const* quest)
    {
        Start(true, player->GetGUID(), quest);
        // VMaNGOS SetDelayBeforeTheFirstWaypoint(750)
        SetPauseTimer(750ms);
        // VMaNGOS JustStartedEscort(): once the event starts, NPC uses his Stealth spell
        me->CastSpell(me, DIPLOMAT_SPELL_STEALTH, false);
    }

    Creature* GetFriend() const
    {
        if (_friendGUID.IsEmpty())
            return nullptr;
        return ObjectAccessor::GetCreature(*me, _friendGUID);
    }

    void DespawnFriendIfExists()
    {
        if (Creature* slimsFriend = GetFriend())
            slimsFriend->DespawnOrUnsummon();
        _friendGUID.Clear();
    }

    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == DIPLOMAT_NPC_SLIMS_FRIEND)
            _friendGUID = summon->GetGUID();
    }

    // This function is also called when NPC runs away from player/group range.
    void JustDied(Unit* killer) override
    {
        DespawnFriendIfExists();
        // Let escort ai do all checks for players and quests.
        EscortAI::JustDied(killer);
    }

    // VMaNGOS JustRespawned(): restore faction and let Mikhail announce that Slim is back (done on the next update,
    // like VMaNGOS m_justCreated, so Mikhail is loaded). Respawn delay does not need restoring: TC's forced respawn time
    // in DespawnOrUnsummon() does not change the stored delay.
    void JustAppeared() override
    {
        EscortAI::JustAppeared();
        me->RestoreFaction();
        _announcePending = true;
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case DIPLOMAT_WAYPOINT_MAILBOX:
                // SetRun(): nodes after the mailbox are added with run = true
                // change faction, which makes him attackable
                me->SetFaction(DIPLOMAT_FACTION_NEUTRAL);
                break;
            case DIPLOMAT_WAYPOINT_GATE:
            {
                // set quest failed if tapoke slim escaped (VMaNGOS GroupEventFailHappens)
                if (Player* player = GetPlayerForEscort())
                {
                    if (Group* group = player->GetGroup())
                    {
                        for (GroupReference const& groupRef : group->GetMembers())
                            if (groupRef.GetSource()->IsInMap(player))
                                groupRef.GetSource()->FailQuest(DIPLOMAT_QUEST_PART11);
                    }
                    else
                        player->FailQuest(DIPLOMAT_QUEST_PART11);
                }

                DespawnFriendIfExists();
                break;
            }
            default:
                break;
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // This function is also called when Tapoke Slim Jahn has been defeated!
        if (Creature* slimsFriend = GetFriend())
            if (slimsFriend->IsAlive())
                return;

        // calls a friend
        SpellCastResult castResult = DoCastSelf(DIPLOMAT_SPELL_CALL_FRIENDS);
        // He says this phrase only during the event
        if (HasEscortState(STATE_ESCORT_ESCORTING) && castResult == SPELL_CAST_OK)
            ClassicScriptText(DIPLOMAT_SAY_PROGRESS_1_TAP, me);
    }

    // TODO(classic): VMaNGOS AttackedBy() override (attack back unless friendly) has no TC master hook;
    // TC engages attackers through the threat system by default.

    void UpdateEscortAI(uint32 diff) override
    {
        if (_announcePending)
        {
            _announcePending = false;
            // distance between Mikhail and Tapoke "Slim" Jahn is about 16 yards, 20 used for "safety".
            if (Creature* mikhail = me->FindNearestCreature(DIPLOMAT_NPC_MIKHAIL, 20.0f))
                ClassicScriptText(DIPLOMAT_SAY_PROGRESS_5_MIC, mikhail);
        }

        if (_isBeaten)
        {
            if (_nextPhaseDelay < diff)
            {
                switch (_dialogPhase)
                {
                    case 0:
                    {
                        // Set Tapoke Slim Jahn and his friend facing to player character.
                        if (Player* player = GetPlayerForEscort())
                        {
                            me->SetFacingToObject(player);

                            if (Creature* slimsFriend = GetFriend())
                                if (slimsFriend->IsAlive())
                                    slimsFriend->SetFacingToObject(player);
                        }
                        _nextPhaseDelay = 2000;
                        break;
                    }
                    case 1:
                        // despawn Slim's friend
                        DespawnFriendIfExists();

                        me->HandleEmoteCommand(EMOTE_ONESHOT_BEG);
                        ClassicScriptText(DIPLOMAT_SAY_PROGRESS_3_TAP, me);
                        _nextPhaseDelay = 4000;
                        break;
                    case 2:
                        me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        ClassicScriptText(DIPLOMAT_SAY_PROGRESS_4_TAP, me);
                        _nextPhaseDelay = 6000;
                        break;
                    case 3:
                    {
                        if (Player* player = GetPlayerForEscort())
                            player->GroupEventHappens(DIPLOMAT_QUEST_PART11, me);

                        // make an illusion returning him back to the inn: despawn in 1s, respawn 2s later
                        me->DespawnOrUnsummon(1s, 2s);

                        _nextPhaseDelay = 0;
                        _dialogPhase = 0;
                        _isBeaten = false;
                        break;
                    }
                    default:
                        break;
                }
                // move to the next phase
                ++_dialogPhase;
            }
            else
                _nextPhaseDelay -= diff;
        }
        else
        {
            if (!UpdateVictim())
                return;

            // Pummel timer
            if (_pummelTimer < diff)
            {
                if (DoCastVictim(DIPLOMAT_SPELL_PUMMEL) == SPELL_CAST_OK)
                    _pummelTimer = urand(7300, 15000);
            }
            else
                _pummelTimer -= diff;
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            return;

        if (_isBeaten)
        {
            damage = 0;
            return;
        }

        if (damage >= me->GetHealth() || me->GetHealthPct() < 20.0f)
        {
            damage = 0;

            _isBeaten = true;

            if (Creature* slimsFriend = GetFriend())
            {
                slimsFriend->CombatStop(true);
                slimsFriend->RemoveAllAuras();
                slimsFriend->GetThreatManager().ClearAllThreat();
                slimsFriend->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);    // VMaNGOS UNIT_FLAG_SPAWNING (0x2)
                slimsFriend->SetImmuneToNPC(true);

                ClassicScriptText(DIPLOMAT_SAY_PROGRESS_2_FRI, slimsFriend);
            }

            SetEscortPaused(true);

            me->SetFaction(DIPLOMAT_FACTION_FRIENDLY_TO_ALL);
            me->RemoveAllAuras();
            me->GetThreatManager().ClearAllThreat();
            me->CombatStop(true);
            EngagementOver();
        }
    }

private:
    ObjectGuid _friendGUID;
    bool _isBeaten;
    bool _announcePending;
    uint32 _nextPhaseDelay;
    uint32 _dialogPhase;
    uint32 _pummelTimer;
};

/*######
## npc_mikhail
######*/

struct classic_npc_mikhail : public ScriptedAI
{
    classic_npc_mikhail(Creature* creature) : ScriptedAI(creature) { }

    void SendQuestMenu(Player* player)
    {
        player->PrepareQuestMenu(me->GetGUID());
        player->SendPreparedQuest(me);
    }

    void SendBusyText(Player* player)
    {
        // id: 1713 was taken from the npc_text table.
        ClearGossipMenuFor(player);
        SendGossipMenuFor(player, DIPLOMAT_NPC_TEXT_BUSY, me->GetGUID());
    }

    bool OnGossipHello(Player* player) override
    {
        // if player has completed a previous The Missing Diplomat part 10
        // (VMaNGOS QUEST_STATUS_COMPLETE covers both "complete" and "rewarded")
        QuestStatus part10 = player->GetQuestStatus(DIPLOMAT_QUEST_PART10);
        if ((part10 == QUEST_STATUS_COMPLETE || part10 == QUEST_STATUS_REWARDED)
            && player->GetQuestStatus(DIPLOMAT_QUEST_PART11) == QUEST_STATUS_NONE)
        {
            // check if a quest event can be started: Tapoke Slim Jahn is alive and in range
            Creature* slim = me->FindNearestCreature(DIPLOMAT_NPC_TAPOKE_SLIM_JAHN, 20.0f);
            if (!slim)
            {
                // happens if a player from the opposite faction pulled Tapoke "Slim" Jahn out of the inn
                SendBusyText(player);
                return true;
            }

            classic_npc_tapoke_slim_jahn* slimAI = dynamic_cast<classic_npc_tapoke_slim_jahn*>(slim->AI());
            if (!slimAI || slimAI->HasEscortState(STATE_ESCORT_ESCORTING))
            {
                // event has already started (or something weird happened)
                SendBusyText(player);
                return true;
            }
        }

        // show quest menu as usual
        SendQuestMenu(player);
        return true;
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != DIPLOMAT_QUEST_PART11)
            return;

        // distance between Mikhail and Tapoke "Slim" Jahn is about 16 yards, 20 used for "safety".
        Creature* slim = me->FindNearestCreature(DIPLOMAT_NPC_TAPOKE_SLIM_JAHN, 20.0f);
        if (!slim)
            return; // Rare case: Tapoke "Slim" Jahn became unavailable while the quest text was read.

        if (classic_npc_tapoke_slim_jahn* slimAI = dynamic_cast<classic_npc_tapoke_slim_jahn*>(slim->AI()))
        {
            // despawn Slim's friend if he was summoned previously (attacked by the opposite faction)
            slimAI->DespawnFriendIfExists();
            // start escort (VMaNGOS Start(bRun = false, ...))
            slimAI->StartEvent(player, quest);
        }
    }
};

void AddSC_classic_wetlands()
{
    RegisterCreatureAI(classic_npc_tapoke_slim_jahn);
    RegisterCreatureAI(classic_npc_mikhail);
}
