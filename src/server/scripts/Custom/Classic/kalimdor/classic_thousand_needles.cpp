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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/thousand_needles/thousand_needles.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1950 (Get the Scoop), 4770 (Homeward Bound), 4904 (Free at Last), 5151 (Hypercapacitor Gizmo)
// Ported: npc_lakota_windsong, npc_paoka_swiftmountain, npc_plucky_johnson, go_panther_cage

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "classic_script_text.h"

/*######
# npc_lakota_windsong
######*/

enum LakotaWindsong
{
    SAY_LAKO_START              = 5926,
    SAY_LAKO_LOOK_OUT           = 5927,
    SAY_LAKO_HERE_COME          = 5928,
    SAY_LAKO_MORE               = 5929,
    SAY_LAKO_END                = 5930,

    QUEST_FREE_AT_LAST          = 4904,
    NPC_GRIM_BANDIT             = 10758,

    ID_AMBUSH_1                 = 0,
    ID_AMBUSH_2                 = 2,
    ID_AMBUSH_3                 = 4,

    // TODO(classic): import VMaNGOS script_waypoint (entry 10646) as waypoint_path with this id (entry * 8), node id = pointid
    PATH_ESCORT_LAKOTA          = 10646 * 8
};

Position const BanditLoc[6] =
{
    { -4905.479492f, -2062.732666f, 84.352f, 0.0f },
    { -4915.201172f, -2073.528320f, 84.733f, 0.0f },
    { -4878.883301f, -1986.947876f, 91.966f, 0.0f },
    { -4877.503906f, -1966.113403f, 91.859f, 0.0f },
    { -4767.985352f, -1873.169189f, 90.192f, 0.0f },
    { -4788.861328f, -1888.007813f, 89.888f, 0.0f }
};

struct classic_npc_lakota_windsong : public EscortAI
{
    classic_npc_lakota_windsong(Creature* creature) : EscortAI(creature) { }

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_FREE_AT_LAST)
            return;

        ClassicScriptText(SAY_LAKO_START, me, player);
        me->SetFaction(FACTION_ESCORTEE_H_NEUTRAL_PASSIVE);   // VMaNGOS FACTION_ESCORTEE 33, restored on respawn
        me->SetImmuneToNPC(false);

        LoadPath(PATH_ESCORT_LAKOTA);
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 8:
                ClassicScriptText(SAY_LAKO_LOOK_OUT, me);
                DoSpawnBandits(ID_AMBUSH_1);
                break;
            case 14:
                ClassicScriptText(SAY_LAKO_HERE_COME, me);
                DoSpawnBandits(ID_AMBUSH_2);
                break;
            case 21:
                ClassicScriptText(SAY_LAKO_MORE, me);
                DoSpawnBandits(ID_AMBUSH_3);
                break;
            case 45:
                if (Player* player = GetPlayerForEscort())
                {
                    ClassicScriptText(SAY_LAKO_END, me);
                    player->GroupEventHappens(QUEST_FREE_AT_LAST, me);
                }
                break;
        }
    }

    void DoSpawnBandits(uint32 ambushId)
    {
        for (uint32 i = 0; i < 2; ++i)
            me->SummonCreature(NPC_GRIM_BANDIT, BanditLoc[i + ambushId], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
    }
};

/*######
# npc_paoka_swiftmountain
######*/

enum PaokaSwiftmountain
{
    SAY_START                   = 5648,
    SAY_WYVERN                  = 5654,
    SAY_COMPLETE                = 5683,

    QUEST_HOMEWARD              = 4770,
    NPC_WYVERN                  = 4107,

    // TODO(classic): import VMaNGOS script_waypoint (entry 10427) as waypoint_path with this id (entry * 8), node id = pointid
    PATH_ESCORT_PAOKA           = 10427 * 8
};

Position const WyvernLoc[3] =
{
    { -4990.606f, -906.057f, -5.343f, 0.0f },
    { -4970.241f, -927.378f, -4.951f, 0.0f },
    { -4985.364f, -952.528f, -5.199f, 0.0f }
};

struct classic_npc_paoka_swiftmountain : public EscortAI
{
    classic_npc_paoka_swiftmountain(Creature* creature) : EscortAI(creature) { }

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_HOMEWARD)
            return;

        ClassicScriptText(SAY_START, me, player);
        me->SetFaction(FACTION_ESCORTEE_H_NEUTRAL_ACTIVE);
        me->SetImmuneToNPC(false);

        LoadPath(PATH_ESCORT_PAOKA);
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 15:
                ClassicScriptText(SAY_WYVERN, me);
                DoSpawnWyvern();
                break;
            case 26:
                ClassicScriptText(SAY_COMPLETE, me);
                break;
            case 27:
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_HOMEWARD, me);
                break;
        }
    }

    void DoSpawnWyvern()
    {
        for (Position const& pos : WyvernLoc)
            me->SummonCreature(NPC_WYVERN, pos, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20s);
    }
};

/*######
# "Plucky" Johnson
######*/

enum PluckyJohnson
{
    FACTION_PLUCKY_FRIENDLY     = 35,
    QUEST_SCOOP                 = 1950,
    SPELL_PLUCKY_HUMAN          = 9192,
    SPELL_PLUCKY_CHICKEN        = 9220,

    GOSSIP_TEXT_PLUCKY_1        = 720,
    GOSSIP_TEXT_PLUCKY_2        = 738
};

#define GOSSIP_ITEM_QUEST   "Please tell me the Phrase.."

struct classic_npc_plucky_johnson : public ScriptedAI
{
    classic_npc_plucky_johnson(Creature* creature) : ScriptedAI(creature)
    {
        _normFaction = creature->GetFaction();
        _resetTimer = 120000;
    }

    void Reset() override
    {
        _resetTimer = 120000;

        if (me->GetFaction() != _normFaction)
            me->SetFaction(_normFaction);

        if (me->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
            me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);

        me->CastSpell(me, SPELL_PLUCKY_CHICKEN, false);
    }

    void ReceiveEmote(Player* player, uint32 textEmote) override
    {
        if (player->GetQuestStatus(QUEST_SCOOP) == QUEST_STATUS_INCOMPLETE)
        {
            if (textEmote == TEXT_EMOTE_BECKON)
            {
                me->SetFaction(FACTION_PLUCKY_FRIENDLY);
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                me->CastSpell(me, SPELL_PLUCKY_HUMAN, false);
            }
        }

        if (textEmote == TEXT_EMOTE_CHICKEN)
        {
            if (me->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
                return;

            me->SetFaction(FACTION_PLUCKY_FRIENDLY);
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            me->CastSpell(me, SPELL_PLUCKY_HUMAN, false);
            me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
        }
    }

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (player->GetQuestStatus(QUEST_SCOOP) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_QUEST, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, GOSSIP_TEXT_PLUCKY_1, me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);

        if (action == GOSSIP_ACTION_INFO_DEF)
        {
            SendGossipMenuFor(player, GOSSIP_TEXT_PLUCKY_2, me->GetGUID());
            player->AreaExploredOrEventHappens(QUEST_SCOOP);
        }
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (me->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
        {
            if (_resetTimer < diff)
            {
                if (!me->GetVictim())
                    EnterEvadeMode();
                else
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);

                return;
            }
            else
                _resetTimer -= diff;
        }

        UpdateVictim();
    }

private:
    uint32 _normFaction;
    uint32 _resetTimer;
};

/*#####
# go_panther_cage (quest 5151)
######*/

enum PantherCage
{
    NPC_ENRAGED_PANTHER         = 10992,
    QUEST_HYPERCAPACITOR_GIZMO  = 5151
};

struct classic_go_panther_cage : public GameObjectAI
{
    classic_go_panther_cage(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (player->GetQuestStatus(QUEST_HYPERCAPACITOR_GIZMO) == QUEST_STATUS_INCOMPLETE)
        {
            if (Creature* panther = me->FindNearestCreature(NPC_ENRAGED_PANTHER, 5.0f, true))
            {
                panther->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);    // VMaNGOS UNIT_FLAG_SPAWNING
                panther->SetImmuneToPC(false);                        // VMaNGOS UNIT_FLAG_IMMUNE_TO_PLAYER
                panther->AI()->AttackStart(player);
            }
        }

        // Must return false for the cage to open.
        return false;
    }
};

void AddSC_classic_thousand_needles()
{
    RegisterCreatureAI(classic_npc_lakota_windsong);
    RegisterCreatureAI(classic_npc_paoka_swiftmountain);
    RegisterCreatureAI(classic_npc_plucky_johnson);
    RegisterGameObjectAI(classic_go_panther_cage);
}
