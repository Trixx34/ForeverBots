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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stormwind_city/quest_stormwind_rendezvous.cpp (+ .h)
// Quest support: 6402 (Stormwind Rendezvous) -> 6403 (The Great Masquerade): Squire Rowe signals Marshal Windsor.
//
// TODO(classic): only npc_squire_rowe is ported here. The Great Masquerade event itself is VMaNGOS npc_reginald_windsor
// (entry 12580), which TC master does not have and which was not part of this port batch. Without it the summoned Windsor
// only rides to his first waypoint and stands there. Hooks for a later classic_npc_reginald_windsor port:
//   - Rowe calls Windsor->AI()->SetGUID(playerGuid, WINDSOR_GUID_PLAYER) and SetGUID(roweGuid, WINDSOR_GUID_ROWE)
//     (VMaNGOS sets npc_reginald_windsorAI::playerGUID / m_squireRoweGuid directly);
//   - Windsor must call Rowe->AI()->DoAction(ACTION_ROWE_RESET) where VMaNGOS PokeRowe() calls ResetCreature().
// at_stormwind_gates (pre-1.12 Windsor spawn) is not ported (not requested).

#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

enum StormwindRendezvousData
{
    QUEST_STORMWIND_RENDEZVOUS      = 6402,
    QUEST_THE_GREAT_MASQUERADE      = 6403,

    NPC_REGINALD_WINDSOR            = 12580,

    GOSSIP_ROWE_COMPLETED           = 9066,
    GOSSIP_ROWE_READY               = 9065,
    GOSSIP_ROWE_BUSY                = 9064,
    GOSSIP_ROWE_NOTHING             = 9063,

    GO_FLARE_OF_JUSTICE             = 181987,

    SAY_SIGNAL_SENT                 = 14389,

    MOUNT_WINDSOR                   = 2410,

    // interface with a (future) Windsor port, see TODO above
    WINDSOR_GUID_PLAYER             = 0,
    WINDSOR_GUID_ROWE               = 1,
    ACTION_ROWE_RESET               = 1
};

Position const RoweWaypoints[] =
{
    { -9058.07f, 441.32f, 93.06f, 3.84f },
    { -9084.88f, 419.23f, 92.42f, 3.83f }
};

Position const WindsorFirstWaypoint = { -9050.406250f, 443.974792f, 93.056458f, 0.659825f };
Position const WindsorSummon = { -9148.40f, 371.32f, 91.0f, 0.70f };

// VMaNGOS QUEST_STATUS_COMPLETE also covers rewarded quests; TC reports those as QUEST_STATUS_REWARDED
static bool ClassicIsQuestDone(Player const* player, uint32 questId)
{
    QuestStatus status = player->GetQuestStatus(questId);
    return status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_REWARDED;
}

/*
 * Squire Rowe
 */

struct classic_npc_squire_rowe : public ScriptedAI
{
    classic_npc_squire_rowe(Creature* creature) : ScriptedAI(creature), m_uiTimer(2000), m_uiStep(0), m_bEventProcessed(false), m_bWindsorUp(false) { }

    uint32 m_uiTimer;
    uint32 m_uiStep;
    bool m_bEventProcessed;
    bool m_bWindsorUp;
    ObjectGuid m_playerGuid;

    void Reset() override { }

    void ResetCreature()
    {
        m_playerGuid.Clear();
        m_uiTimer = 2000;
        m_uiStep = 0;
        m_bEventProcessed = false;
        me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        m_bWindsorUp = false;
    }

    // VMaNGOS constructor / JustRespawned -> ResetCreature()
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        ResetCreature();
    }

    // VMaNGOS npc_reginald_windsorAI::PokeRowe()
    void DoAction(int32 action) override
    {
        if (action == ACTION_ROWE_RESET)
            ResetCreature();
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (!m_bEventProcessed || type != POINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case 1:
                me->GetMotionMaster()->MovePoint(2, RoweWaypoints[1].GetPositionX(), RoweWaypoints[1].GetPositionY(), RoweWaypoints[1].GetPositionZ());
                break;
            case 2:
                me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
                m_uiTimer = 5000;
                ++m_uiStep;
                break;
            case 4:
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                ClassicScriptText(SAY_SIGNAL_SENT, me);
                m_bEventProcessed = false;
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_bEventProcessed)
        {
            if (m_uiTimer < diff)
            {
                switch (m_uiStep)
                {
                    case 0:
                        me->SetSpeedRate(MOVE_RUN, 1.1f);
                        me->GetMotionMaster()->MovePoint(1, RoweWaypoints[0].GetPositionX(), RoweWaypoints[0].GetPositionY(), RoweWaypoints[0].GetPositionZ());
                        m_uiTimer = 1000;
                        ++m_uiStep;
                        break;
                    case 2:
                        me->SummonGameObject(GO_FLARE_OF_JUSTICE, -9095.839844f, 411.178986f, 92.244499f, 2.303830f,
                            QuaternionData(0.0f, 0.0f, 0.913545f, 0.406738f), 10s);
                        me->GetMotionMaster()->MovePoint(3, RoweWaypoints[0].GetPositionX(), RoweWaypoints[0].GetPositionY(), RoweWaypoints[0].GetPositionZ());
                        m_uiTimer = 1500;
                        ++m_uiStep;
                        break;
                    case 3:
                        if (Creature* windsor = me->SummonCreature(NPC_REGINALD_WINDSOR, WindsorSummon, TEMPSUMMON_MANUAL_DESPAWN, 90min))
                        {
                            windsor->setActive(true); // VMaNGOS summons him as an active object

                            if (CreatureAI* windsorAI = windsor->AI())
                            {
                                if (!m_playerGuid.IsEmpty())
                                    windsorAI->SetGUID(m_playerGuid, WINDSOR_GUID_PLAYER);

                                windsorAI->SetGUID(me->GetGUID(), WINDSOR_GUID_ROWE);
                            }

                            m_bWindsorUp = true;
                            windsor->Mount(MOUNT_WINDSOR);
                            windsor->SetWalk(false);
                            windsor->SetSpeedRate(MOVE_RUN, 1.0f);
                            windsor->GetMotionMaster()->MovePoint(0, WindsorFirstWaypoint.GetPositionX(), WindsorFirstWaypoint.GetPositionY(), WindsorFirstWaypoint.GetPositionZ());
                            windsor->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                        }
                        ++m_uiStep;
                        break;
                    case 4:
                    {
                        Position home = me->GetRespawnPosition();
                        me->GetMotionMaster()->MovePoint(4, home.GetPositionX(), home.GetPositionY(), home.GetPositionZ(), true, home.GetOrientation());
                        ++m_uiStep;
                        break;
                    }
                }
            }
            else
                m_uiTimer -= diff;
        }
        else
            ScriptedAI::UpdateAI(diff);
    }

    bool OnGossipHello(Player* player) override
    {
        if (ClassicIsQuestDone(player, QUEST_STORMWIND_RENDEZVOUS) && !ClassicIsQuestDone(player, QUEST_THE_GREAT_MASQUERADE))
        {
            if (!m_bWindsorUp)
            {
                AddGossipItemFor(player, GossipOptionNpc::None, "Let Marshal Windsor know that I am ready.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
                SendGossipMenuFor(player, GOSSIP_ROWE_READY, me->GetGUID());
            }
            else
                SendGossipMenuFor(player, GOSSIP_ROWE_BUSY, me->GetGUID());
        }
        else if (ClassicIsQuestDone(player, QUEST_THE_GREAT_MASQUERADE))
            SendGossipMenuFor(player, GOSSIP_ROWE_COMPLETED, me->GetGUID());
        else
            SendGossipMenuFor(player, GOSSIP_ROWE_NOTHING, me->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);

        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF:
                if (ClassicIsQuestDone(player, QUEST_STORMWIND_RENDEZVOUS))
                {
                    m_bEventProcessed = true;
                    m_playerGuid = player->GetGUID();
                    me->SetWalk(false);
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                }

                CloseGossipMenuFor(player);
                break;
        }

        return true;
    }
};

void AddSC_classic_stormwind_rendezvous()
{
    RegisterCreatureAI(classic_npc_squire_rowe);
}
