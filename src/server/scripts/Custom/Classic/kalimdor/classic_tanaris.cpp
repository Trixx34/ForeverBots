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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/tanaris/tanaris.cpp (ScriptDev2 / Nostalrius lineage, GPL-2)
// Quest support: 1560 (Tooga's Quest), 2882 (Cuergo's Gold), 8181 (Confront Yeh'kinya - Hakkar event)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedFollowerAI.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

/*####
# npc_tooga
####*/

enum Tooga
{
    SAY_TOOGA_RANDOM_START      = 2221,
    SAY_TOOGA_RANDOM_END        = 2228,

    SAY_TOOG_POST_1             = 2137,
    SAY_TORT_POST_2             = 2138,
    SAY_TOOG_POST_3             = 2139,
    SAY_TORT_POST_4             = 2140,
    SAY_TOOG_POST_5             = 2141,
    SAY_TORT_POST_6             = 2145,

    QUEST_TOOGA                 = 1560,
    NPC_TORTA                   = 6015,

    POINT_ID_TO_WATER           = 1
};

Position const ToogaToWaterLoc = { -7032.664551f, -4906.199219f, -1.606446f, 0.0f };

struct classic_npc_tooga : public FollowerAI
{
    classic_npc_tooga(Creature* creature) : FollowerAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _checkSpeechTimer = urand(30000, 60000);
        _postEventTimer = 1000;
        _phasePostEvent = 0;
        _tortaGuid.Clear();
    }

    void Reset() override
    {
        Initialize();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        FollowerAI::MoveInLineOfSight(who);

        if (!me->GetVictim() && !HasFollowState(STATE_FOLLOW_COMPLETE | STATE_FOLLOW_POSTEVENT) && who->GetEntry() == NPC_TORTA)
        {
            if (me->IsWithinDistInMap(who, INTERACTION_DISTANCE))
            {
                if (Player* player = GetLeaderForFollower())
                {
                    if (player->GetQuestStatus(QUEST_TOOGA) == QUEST_STATUS_INCOMPLETE)
                        player->GroupEventHappens(QUEST_TOOGA, me);
                }

                _tortaGuid = who->GetGUID();
                SetFollowComplete(true);
            }
        }
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        FollowerAI::MovementInform(motionType, pointId);

        if (motionType != POINT_MOTION_TYPE)
            return;

        if (pointId == POINT_ID_TO_WATER)
            SetFollowComplete();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_TOOGA)
            StartFollow(player, FACTION_ESCORTEE_N_FRIEND_PASSIVE, QUEST_TOOGA);
    }

    void UpdateFollowerAI(uint32 diff) override
    {
        if (UpdateVictim())
            return;

        //we are doing the post-event, or...
        if (HasFollowState(STATE_FOLLOW_POSTEVENT))
        {
            if (_postEventTimer < diff)
            {
                _postEventTimer = 5000;
                Creature* torta = ObjectAccessor::GetCreature(*me, _tortaGuid);

                if (!torta || !torta->IsAlive())
                {
                    //something happened, so just complete
                    SetFollowComplete();
                    return;
                }

                switch (_phasePostEvent)
                {
                    case 1:
                        ClassicScriptText(SAY_TOOG_POST_1, me);
                        break;
                    case 2:
                        ClassicScriptText(SAY_TORT_POST_2, torta);
                        break;
                    case 3:
                        ClassicScriptText(SAY_TOOG_POST_3, me);
                        break;
                    case 4:
                        ClassicScriptText(SAY_TORT_POST_4, torta);
                        break;
                    case 5:
                        ClassicScriptText(SAY_TOOG_POST_5, me);
                        break;
                    case 6:
                        ClassicScriptText(SAY_TORT_POST_6, torta);
                        me->GetMotionMaster()->MovePoint(POINT_ID_TO_WATER, ToogaToWaterLoc);
                        break;
                }

                ++_phasePostEvent;
            }
            else
                _postEventTimer -= diff;
        }
        //...we are doing regular speech check
        else if (HasFollowState(STATE_FOLLOW_INPROGRESS))
        {
            if (_checkSpeechTimer < diff)
            {
                _checkSpeechTimer = urand(30000, 60000);

                if (Player* player = GetLeaderForFollower())
                    ClassicScriptText(urand(SAY_TOOGA_RANDOM_START, SAY_TOOGA_RANDOM_END), me, player);
            }
            else
                _checkSpeechTimer -= diff;
        }
    }

private:
    uint32 _checkSpeechTimer;
    uint32 _postEventTimer;
    uint32 _phasePostEvent;
    ObjectGuid _tortaGuid;
};

/*####
# go_inconspicuous_landmark (quest 2882 "Cuergo's Gold")
####*/

enum InconspicuousLandmark
{
    NPC_PIRATES_1           = 7899,
    NPC_PIRATES_2           = 7901,
    NPC_PIRATES_3           = 7902,
    QUEST_CUERGOS_GOLD      = 2882
};

struct classic_go_inconspicuous_landmark : public GameObjectAI
{
    classic_go_inconspicuous_landmark(GameObject* go) : GameObjectAI(go), _timer(0), _inUse(false) { }

    void UpdateAI(uint32 diff) override
    {
        if (_inUse)
        {
            if (_timer < diff)
            {
                _inUse = false;
                me->SetGoState(GO_STATE_READY);
                me->RemoveFlag(GO_FLAG_IN_USE);
            }
            else
                _timer -= diff;
        }
    }

    void SetInUse()
    {
        me->SetGoState(GO_STATE_ACTIVE);
        me->SetFlag(GO_FLAG_IN_USE);
        _inUse = true;
        _timer = 600000;
    }

    void SummonPirate(uint32 entry, float x, float y, float z, float o, Player* player)
    {
        if (Creature* pirate = me->SummonCreature(entry, x, y, z, o, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 310s))
            pirate->AI()->AttackStart(player);
    }

    bool OnGossipHello(Player* player) override
    {
        if (!_inUse)
        {
            SetInUse();

            if (me->GetGoType() == GAMEOBJECT_TYPE_GOOBER && player->GetQuestStatus(QUEST_CUERGOS_GOLD) == QUEST_STATUS_INCOMPLETE)
            {
                uint32 extraPirateType[2] = { NPC_PIRATES_1, NPC_PIRATES_2 };

                SummonPirate(NPC_PIRATES_1, -10119.85f, -4068.36f, 4.55f, 1.35f, player);
                SummonPirate(NPC_PIRATES_2, -10109.80f, -4054.45f, 5.64f, 3.17f, player);
                SummonPirate(NPC_PIRATES_3, -10127.80f, -4047.04f, 4.50f, 5.07f, player);

                for (uint32& pirateEntry : extraPirateType)
                {
                    switch (urand(0, 2))
                    {
                        case 0: pirateEntry = NPC_PIRATES_1; break;
                        case 1: pirateEntry = NPC_PIRATES_2; break;
                        case 2: pirateEntry = NPC_PIRATES_3; break;
                    }
                }

                SummonPirate(extraPirateType[0], -10113.952148f, -4040.484375f, 5.174251f, 4.300828f, player);
                SummonPirate(extraPirateType[1], -10136.779297f, -4063.175049f, 4.787039f, 0.526417f, player);
            }
        }
        return true;
    }

private:
    uint32 _timer;
    bool _inUse;
};

/*######
## npc_yehkinya
######*/

enum Yehkinya : uint32
{
    SAY_HAKKAR_EVENT_1          = 10456,
    SAY_HAKKAR_EVENT_2          = 10457,

    SPELL_TRANSFORM_VISUAL      = 24085,

    QUEST_HAKKAR_EVENT          = 8181,

    DISPLAY_YEHKINYA            = 7902,
    DISPLAY_YEHKINYA_BIRD       = 1336,

    // TODO(classic): VMaNGOS script_waypoint (entry 8579) must be imported as waypoint_path with this id
    // (TC convention entry * 8, node ids = VMaNGOS pointid). VMaNGOS starts this escort running.
    PATH_ESCORT_YEHKINYA        = 8579 * 8
};

struct classic_npc_yehkinya : public EscortAI
{
    classic_npc_yehkinya(Creature* creature) : EscortAI(creature), _eventTimer(0), _isEventStarted(false) { }

    void Reset() override
    {
        _isEventStarted = false;
        // TODO(classic): VMaNGOS LoadEquipment(1315) = creature_equip_template 1315; TC equipment is per creature entry,
        // so the default equipment set (id 1) is used instead.
        me->LoadEquipment(1, true);
        me->SetDisplayId(DISPLAY_YEHKINYA);
        _eventTimer = 0;
        me->SetCanFly(false);
        me->SetDisableGravity(false);
        me->SetWalk(false);
    }

    void OnQuestReward(Player* player, Quest const* quest, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_HAKKAR_EVENT)
            return;

        ClassicScriptText(SAY_HAKKAR_EVENT_1, me, player);

        LoadPath(PATH_ESCORT_YEHKINYA);
        Start(true, ObjectGuid::Empty, nullptr, true);
        me->SetWalk(false);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 1:
                me->SetWalk(false);
                _isEventStarted = true;
                me->LoadEquipment(0, true);
                _eventTimer = 3000;
                DoCastSelf(SPELL_TRANSFORM_VISUAL);
                me->SetDisplayId(DISPLAY_YEHKINYA_BIRD);
                me->SetCanFly(true);
                me->SetDisableGravity(true);
                SetEscortPaused(true);
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (_eventTimer <= diff)
        {
            if (_isEventStarted)
            {
                SetEscortPaused(false);
                ClassicScriptText(SAY_HAKKAR_EVENT_2, me);
                me->SetWalk(false);
                _eventTimer = 15000;
            }
        }
        else
            _eventTimer -= diff;
    }

private:
    uint32 _eventTimer;
    bool _isEventStarted;
};

void AddSC_classic_tanaris()
{
    RegisterCreatureAI(classic_npc_tooga);
    RegisterCreatureAI(classic_npc_yehkinya);
    RegisterGameObjectAI(classic_go_inconspicuous_landmark);
}
