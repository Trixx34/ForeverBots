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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/feralas.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_shay_leafrunner (2845), npc_kindal_moonweaver + npc_captured_sprite_darter (2969)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedFollowerAI.h"
#include "SpellInfo.h"
#include "classic_script_text.h"

enum FeralasFactions
{
    FACTION_ESCORT_A_NEUTRAL_PASSIVE = 10
};

/*######
## npc_shay_leafrunner
######*/

enum ShayLeafrunner
{
    SAY_ESCORT_START            = 3921,
    SAY_WANDER_1                = 3912,
    SAY_WANDER_2                = 3907,
    SAY_WANDER_3                = 3909,
    SAY_WANDER_4                = 3911,
    SAY_WANDER_DONE_1           = 3914,
    SAY_WANDER_DONE_2           = 3913,
    SAY_WANDER_DONE_3           = 3916,
    EMOTE_WANDER                = 3918,
    SAY_EVENT_COMPLETE_1        = 3917,
    SAY_EVENT_COMPLETE_2        = 3922,

    SPELL_SHAYS_BELL            = 11402,
    NPC_ROCKBITER               = 7765,
    QUEST_ID_WANDERING_SHAY     = 2845
};

struct classic_npc_shay_leafrunner : public FollowerAI
{
    classic_npc_shay_leafrunner(Creature* creature) : FollowerAI(creature), _wanderTimer(0), _despawnTimer(0), _isRecalled(false), _isComplete(false) { }

    void Reset() override
    {
        _isRecalled = false;
        _isComplete = false;
    }

    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        FollowerAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_ID_WANDERING_SHAY)
            return;

        ClassicScriptText(SAY_ESCORT_START, me);
        me->SetImmuneToNPC(false);
        BeforeStartFollow(player, FACTION_ESCORT_A_NEUTRAL_PASSIVE, quest);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        FollowerAI::MoveInLineOfSight(who);

        if (!_isComplete && who->GetEntry() == NPC_ROCKBITER && me->IsWithinDistInMap(who, 20.0f))
        {
            Player* player = GetLeaderForFollower();
            if (!player)
                return;

            ClassicScriptText(SAY_EVENT_COMPLETE_1, me);
            ClassicScriptText(SAY_EVENT_COMPLETE_2, who);

            // complete quest
            player->GroupEventHappens(QUEST_ID_WANDERING_SHAY, me);
            SetFollowComplete(true);
            me->DespawnOrUnsummon(30s);
            _isComplete = true;
            _wanderTimer = 0;
            _despawnTimer = 0;

            // move to Rockbiter
            float x, y, z;
            who->GetContactPoint(me, x, y, z, INTERACTION_DISTANCE);
            me->GetMotionMaster()->MovePoint(0, x, y, z);
        }
        // VMaNGOS checks pWho->IsWithinDistInMap(pWho, ...) (always true), i.e. any player in line of sight ends the recall
        else if (_isRecalled && who->GetTypeId() == TYPEID_PLAYER)
        {
            if (HasFollowState(STATE_FOLLOW_INPROGRESS) || HasFollowState(STATE_FOLLOW_PAUSED))
                _wanderTimer = 60000;

            _isRecalled = false;

            switch (urand(0, 2))
            {
                case 0: ClassicScriptText(SAY_WANDER_DONE_1, me); break;
                case 1: ClassicScriptText(SAY_WANDER_DONE_2, me); break;
                case 2: ClassicScriptText(SAY_WANDER_DONE_3, me); break;
            }
        }
    }

    void JustDied(Unit* killer) override
    {
        _wanderTimer = 0;
        _despawnTimer = 0;
        FollowerAI::JustDied(killer);
    }

    void BeforeStartFollow(Player* player, uint32 factionForFollower, Quest const* quest)
    {
        // TODO(classic): VMaNGOS passes a follow distance of 5 yards; TC FollowerAI always uses PET_FOLLOW_DIST
        StartFollow(player, factionForFollower, quest->GetQuestId());
        _wanderTimer = 30000;
        _despawnTimer = 910000;
    }

    void ResumeFollowing()
    {
        _isRecalled = true;
        me->SetWalk(false);
        SetFollowPaused(false);
    }

    // VMaNGOS EffectDummyCreature_npc_shay_leafrunner (SPELL_SHAYS_BELL, effect 0)
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_SHAYS_BELL)
            return;

        if (!caster || caster->GetTypeId() != TYPEID_PLAYER)
            return;

        ResumeFollowing();
    }

    void UpdateFollowerAI(uint32 diff) override
    {
        if (_despawnTimer)
        {
            if (_despawnTimer <= diff)
            {
                me->DisappearAndDie();
                _wanderTimer = 0;
                _despawnTimer = 0;
            }
            else
                _despawnTimer -= diff;
        }

        if (!UpdateVictim())
        {
            if (_wanderTimer)
            {
                if (_wanderTimer <= diff)
                {
                    // set follow paused and wander in a random point
                    SetFollowPaused(true);
                    ClassicScriptText(EMOTE_WANDER, me);
                    _wanderTimer = 0;

                    switch (urand(0, 3))
                    {
                        case 0: ClassicScriptText(SAY_WANDER_1, me); break;
                        case 1: ClassicScriptText(SAY_WANDER_2, me); break;
                        case 2: ClassicScriptText(SAY_WANDER_3, me); break;
                        case 3: ClassicScriptText(SAY_WANDER_4, me); break;
                    }

                    float x, y, z;
                    me->SetWalk(true);
                    me->GetNearPoint(me, x, y, z, frand(25.0f, 40.0f), frand(0.0f, 2.0f * float(M_PI)));
                    me->GetMotionMaster()->MovePoint(1, x, y, z);
                }
                else
                    _wanderTimer -= diff;
            }

            return;
        }
    }

private:
    uint32 _wanderTimer;
    uint32 _despawnTimer;
    bool _isRecalled;
    bool _isComplete;
};

/*######
## npc_kindal_moonweaver
######*/

enum KindalMoonweaver
{
    NPC_CAPTURED_SPRITE_DARTER      = 7997,

    GO_CAGE_DOOR                    = 143979,

    QUEST_FREEDOM_FOR_ALL_CREATURES = 2969,

    REQUEST_SAVED_SPRITE_DARTER     = 6,

    SPELL_MANA_BURN                 = 11981,

    SAY_KINDAL_BEGIN                = 4079,
    SAY_KINDAL_SUCCESS              = 4080,
    SAY_KINDAL_FAIL_SPRITES         = 4081,
    SAY_KINDAL_FAIL_TIMER           = 5285,
    SAY_KINDAL_AGGRO1               = 4122,
    SAY_KINDAL_AGGRO2               = 4123,
    SAY_KINDAL_AGGRO3               = 4124,
    SAY_KINDAL_AGGRO4               = 4125,

    FACTION_ESCORTEE_SPRITE         = 10,
    FACTION_ESCORTEE_KINDAL         = 231
};

struct sMovementInformation
{
    uint8 uiSPoint, uiEPoint;
};

static sMovementInformation const KindalMovementInfo[11] =
{
    { 2, 3 },
    { 2, 4 },
    { 2, 5 },
    { 0, 6 },
    { 0, 7 },
    { 0, 8 },
    { 1, 6 },
    { 1, 7 },
    { 1, 8 },
    { 1, 9 },
    { 1, 10 }
};

static float const KindalMovePoints[11][3] =
{
    { -4531.78f, 807.50f, 59.92f },
    { -4513.14f, 765.45f, 60.72f },
    { -4529.44f, 825.49f, 60.51f },
    { -4563.52f, 877.13f, 61.07f },
    { -4578.42f, 891.02f, 65.79f },
    { -4592.71f, 890.61f, 69.11f },
    { -4582.46f, 751.20f, 49.65f },
    { -4572.83f, 741.11f, 45.69f },
    { -4557.85f, 730.01f, 45.57f },
    { -4529.02f, 706.96f, 60.70f },
    { -4515.88f, 696.60f, 64.38f }
};

// TODO(classic): VMaNGOS FollowerAI::OnEscortFailed(false) (SAY_KINDAL_FAIL_TIMER when the leader is lost/too far) has no
// hook in TC FollowerAI, which despawns the follower silently.
struct classic_npc_kindal_moonweaver : public FollowerAI
{
    classic_npc_kindal_moonweaver(Creature* creature) : FollowerAI(creature), _savedSpriteDarter(0), _diedSpriteDarter(0), _eventStarted(false)
    {
        me->SetSheath(SHEATH_STATE_UNARMED);
    }

    void Reset() override
    {
        if (!HasFollowState(STATE_FOLLOW_INPROGRESS))
            _eventStarted = false;
    }

    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        FollowerAI::JustAppeared();
    }

    void JustDied(Unit* killer) override
    {
        FollowerAI::JustDied(killer);

        me->SetRespawnTime(10);
    }

    void JustEngagedWith(Unit* victim) override
    {
        ClassicScriptText(urand(SAY_KINDAL_AGGRO1, SAY_KINDAL_AGGRO4), me, victim);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_FREEDOM_FOR_ALL_CREATURES)
            return;

        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetFacingToObject(player);
        me->SetImmuneToNPC(false);
        ClassicScriptText(SAY_KINDAL_BEGIN, me, player);
        StartFollow(player, FACTION_ESCORTEE_KINDAL, quest->GetQuestId());
        BeginEvent();
        SetFollowPaused(true);

        Creature* creature = me;
        me->m_Events.AddEventAtOffset([creature]()
        {
            if (!creature->IsAlive())
                return;

            if (classic_npc_kindal_moonweaver* kindalAI = dynamic_cast<classic_npc_kindal_moonweaver*>(creature->AI()))
                kindalAI->SetFollowPaused(false);
        }, 3s);
    }

    void BeginEvent();
    void SpriteSaved();
    void SpriteDied();
    void EndEvent();

private:
    uint8 _savedSpriteDarter;
    uint8 _diedSpriteDarter;
    bool _eventStarted;
};

struct classic_npc_captured_sprite_darter : public ScriptedAI
{
    classic_npc_captured_sprite_darter(Creature* creature) : ScriptedAI(creature)
    {
        me->setActive(true);
        ResetState();
    }

    void ResetState()
    {
        _run = false;
        _eventStart = false;
        _kindalGuid.Clear();
        _gateGuid.Clear();
        _movePoint = 0;
        _runPath = urand(0, 10);
        _runStartTimer = urand(0, 3000);
        _manaBurnTimer = urand(3000, 6000);
    }

    void Reset() override
    {
        ResetState();
    }

    void JustAppeared() override
    {
        me->RestoreFaction(); // VMaNGOS TEMPFACTION_RESTORE_RESPAWN
        ScriptedAI::JustAppeared();
    }

    void StartEvent(ObjectGuid const& kindalGuid, ObjectGuid const& gateGuid)
    {
        ResetState();
        _kindalGuid = kindalGuid;
        _gateGuid = gateGuid;
        _eventStart = true;
        me->SetImmuneToNPC(false);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // VMaNGOS: drop combat without returning home, then resume the run from the previous point
        if (!_EnterEvadeMode(why))
            return;

        if (_movePoint)
            --_movePoint;

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            me->GetMotionMaster()->Clear();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* kindal = ObjectAccessor::GetCreature(*me, _kindalGuid))
            if (classic_npc_kindal_moonweaver* kindalAI = dynamic_cast<classic_npc_kindal_moonweaver*>(kindal->AI()))
                kindalAI->SpriteDied();

        me->DespawnOrUnsummon(10s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (_kindalGuid.IsEmpty() || _gateGuid.IsEmpty())
                return;

            if (_eventStart && !_run)
            {
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, _gateGuid))
                {
                    if (gate->GetGoState() == GO_STATE_ACTIVE)
                    {
                        if (_runStartTimer < diff)
                        {
                            me->SetFaction(FACTION_ESCORTEE_SPRITE);
                            _run = true;
                        }
                        else
                            _runStartTimer -= diff;
                    }
                }
            }

            if (_run)
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                {
                    switch (_movePoint)
                    {
                        case 0:
                        {
                            float const* pos = KindalMovePoints[KindalMovementInfo[_runPath].uiSPoint];
                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MovePoint(0, pos[0], pos[1], pos[2]);
                            ++_movePoint;
                            break;
                        }
                        case 1:
                        {
                            float const* pos = KindalMovePoints[KindalMovementInfo[_runPath].uiEPoint];
                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MovePoint(1, pos[0], pos[1], pos[2]);
                            ++_movePoint;
                            break;
                        }
                        case 2:
                            if (Creature* kindal = ObjectAccessor::GetCreature(*me, _kindalGuid))
                            {
                                if (classic_npc_kindal_moonweaver* kindalAI = dynamic_cast<classic_npc_kindal_moonweaver*>(kindal->AI()))
                                    kindalAI->SpriteSaved();
                                me->DespawnOrUnsummon();
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

            return;
        }

        if (me->GetVictim()->GetPowerType() == POWER_MANA)
        {
            if (_manaBurnTimer < diff)
            {
                if (DoCastVictim(SPELL_MANA_BURN) == SPELL_CAST_OK)
                    _manaBurnTimer = urand(7000, 10000);
            }
            else
                _manaBurnTimer -= diff;
        }
    }

private:
    bool _run;
    bool _eventStart;
    uint8 _runPath;
    uint8 _movePoint;
    uint32 _runStartTimer;
    uint32 _manaBurnTimer;
    ObjectGuid _kindalGuid;
    ObjectGuid _gateGuid;
};

void classic_npc_kindal_moonweaver::BeginEvent()
{
    GameObject* gate = GetClosestGameObjectWithEntry(me, GO_CAGE_DOOR, 100.0f);
    if (!gate)
        return;

    _savedSpriteDarter = 0;
    _diedSpriteDarter = 0;
    _eventStarted = true;

    gate->SetGoState(GO_STATE_READY);

    std::list<Creature*> sprites;
    gate->GetCreatureListWithEntryInGrid(sprites, NPC_CAPTURED_SPRITE_DARTER, 40.0f);

    for (Creature* sprite : sprites)
        if (classic_npc_captured_sprite_darter* spriteAI = dynamic_cast<classic_npc_captured_sprite_darter*>(sprite->AI()))
            spriteAI->StartEvent(me->GetGUID(), gate->GetGUID());
}

void classic_npc_kindal_moonweaver::SpriteSaved()
{
    ++_savedSpriteDarter;

    if (_savedSpriteDarter >= REQUEST_SAVED_SPRITE_DARTER)
    {
        if (Player* player = GetLeaderForFollower())
        {
            if (player->GetQuestStatus(QUEST_FREEDOM_FOR_ALL_CREATURES) == QUEST_STATUS_INCOMPLETE)
            {
                player->GroupEventHappens(QUEST_FREEDOM_FOR_ALL_CREATURES, me);
                ClassicScriptText(SAY_KINDAL_SUCCESS, me, player);
            }
        }

        SetFollowComplete(true);
        EndEvent();
    }
}

void classic_npc_kindal_moonweaver::SpriteDied()
{
    if (!_eventStarted)
        return;

    ++_diedSpriteDarter;

    if (_diedSpriteDarter > 5)
    {
        if (Player* player = GetLeaderForFollower())
        {
            if (player->GetQuestStatus(QUEST_FREEDOM_FOR_ALL_CREATURES) == QUEST_STATUS_INCOMPLETE)
            {
                player->FailQuest(QUEST_FREEDOM_FOR_ALL_CREATURES);
                ClassicScriptText(SAY_KINDAL_FAIL_SPRITES, me, player);
            }
        }

        SetFollowComplete(false);
    }
}

void classic_npc_kindal_moonweaver::EndEvent()
{
    if (HasFollowState(STATE_FOLLOW_POSTEVENT))
    {
        SetFollowComplete();
        me->SetRespawnTime(10);
    }
}

void AddSC_classic_feralas()
{
    RegisterCreatureAI(classic_npc_shay_leafrunner);
    RegisterCreatureAI(classic_npc_kindal_moonweaver);
    RegisterCreatureAI(classic_npc_captured_sprite_darter);
}
