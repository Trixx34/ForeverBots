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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/ungoro_crater/ungoro_crater.cpp (ScriptDev2 / Nostalrius lineage, GPL-2)
// Quest support: 4245 (Chasing A-Me 01), 4491 (A Little Help From My Friends), 7636 (Stave of the Ancients: Simone)
// Ported: npc_ame01, npc_ringo, npc_simone_the_inconspicuous

#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedFollowerAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

/*######
## npc_ame01
######*/

enum Ame01
{
    SAY_AME_START           = 5062,
    SAY_AME_PROGRESS        = 5063,
    SAY_AME_END             = 5156,
    SAY_AME_AGGRO1          = 5158,
    SAY_AME_AGGRO2          = 5159,
    SAY_AME_AGGRO3          = 5157,

    QUEST_CHASING_AME       = 4245,

    // TODO(classic): import VMaNGOS script_waypoint (entry 9623) as waypoint_path with this id (entry * 8), node id = pointid
    PATH_ESCORT_AME01       = 9623 * 8
};

struct classic_npc_ame01 : public EscortAI
{
    classic_npc_ame01(Creature* creature) : EscortAI(creature) { }

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_CHASING_AME)
            return;

        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_PASSIVE);   // restored on respawn
        me->SetImmuneToNPC(false);

        LoadPath(PATH_ESCORT_AME01);
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_AME_START, me);
                break;
            case 19:
                ClassicScriptText(SAY_AME_PROGRESS, me);
                break;
            case 37:
                ClassicScriptText(SAY_AME_END, me);
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_CHASING_AME, me);
                break;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            return;

        if (Player* player = GetPlayerForEscort())
        {
            if (player->GetVictim() && player->GetVictim() == who)
                return;

            switch (urand(0, 2))
            {
                case 0: ClassicScriptText(SAY_AME_AGGRO1, me); break;
                case 1: ClassicScriptText(SAY_AME_AGGRO2, me); break;
                case 2: ClassicScriptText(SAY_AME_AGGRO3, me); break;
            }
        }
    }
};

/*####
# npc_ringo
####*/

enum Ringo
{
    SAY_RIN_START_1             = 5391,
    SAY_RIN_START_2             = 5392,

    SAY_FAINT_1                 = 5396,
    SAY_FAINT_2                 = 5397,
    SAY_FAINT_3                 = 5394,
    SAY_FAINT_4                 = 5395,

    SAY_WAKE_1                  = 5400,
    SAY_WAKE_2                  = 5399,
    SAY_WAKE_3                  = 5398,
    SAY_WAKE_4                  = 5401,

    SAY_RIN_END_1               = 5402,
    SAY_SPR_END_2               = 5405,
    SAY_RIN_END_3               = 5403,
    EMOTE_RIN_END_4             = 5393,
    SAY_RIN_END_6               = 5404,
    SAY_SPR_END_7               = 5406,

    SPELL_REVIVE_RINGO          = 15591,
    QUEST_A_LITTLE_HELP         = 4491,
    NPC_SPRAGGLE                = 9997
};

struct classic_npc_ringo : public FollowerAI
{
    classic_npc_ringo(Creature* creature) : FollowerAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _faintTimer = urand(30000, 60000);
        _endEventProgress = 0;
        _endEventTimer = 1000;
        _spraggleGuid.Clear();
    }

    void Reset() override
    {
        Initialize();
    }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        FollowerAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_A_LITTLE_HELP)
            return;

        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetImmuneToNPC(false);
        StartFollow(player, FACTION_ESCORTEE_N_NEUTRAL_PASSIVE, QUEST_A_LITTLE_HELP);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        FollowerAI::MoveInLineOfSight(who);

        if (!me->GetVictim() && !HasFollowState(STATE_FOLLOW_COMPLETE) && who->GetEntry() == NPC_SPRAGGLE)
        {
            if (me->IsWithinDistInMap(who, INTERACTION_DISTANCE))
            {
                if (Player* player = GetLeaderForFollower())
                {
                    if (player->GetQuestStatus(QUEST_A_LITTLE_HELP) == QUEST_STATUS_INCOMPLETE)
                        player->GroupEventHappens(QUEST_A_LITTLE_HELP, me);
                }

                _spraggleGuid = who->GetGUID();
                SetFollowComplete(true);
            }
        }
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (HasFollowState(STATE_FOLLOW_INPROGRESS | STATE_FOLLOW_PAUSED) && spellInfo->Id == SPELL_REVIVE_RINGO)
            ClearFaint();
    }

    void SetFaint()
    {
        if (!HasFollowState(STATE_FOLLOW_POSTEVENT))
        {
            SetFollowPaused(true);
            ClassicScriptText(RAND(SAY_FAINT_1, SAY_FAINT_2, SAY_FAINT_3, SAY_FAINT_4), me);
        }

        //what does actually happen here? Emote? Aura?
        me->SetStandState(UNIT_STAND_STATE_SLEEP);
    }

    void ClearFaint()
    {
        me->SetStandState(UNIT_STAND_STATE_STAND);

        if (HasFollowState(STATE_FOLLOW_POSTEVENT))
            return;

        ClassicScriptText(RAND(SAY_WAKE_1, SAY_WAKE_2, SAY_WAKE_3, SAY_WAKE_4), me);

        SetFollowPaused(false);
    }

    void UpdateFollowerAI(uint32 diff) override
    {
        if (UpdateVictim())
            return;

        if (HasFollowState(STATE_FOLLOW_POSTEVENT))
        {
            if (_endEventTimer < diff)
            {
                Creature* spraggle = ObjectAccessor::GetCreature(*me, _spraggleGuid);
                if (!spraggle || !spraggle->IsAlive())
                {
                    SetFollowComplete();
                    return;
                }

                switch (_endEventProgress)
                {
                    case 1:
                        ClassicScriptText(SAY_RIN_END_1, me);
                        _endEventTimer = 3000;
                        break;
                    case 2:
                        ClassicScriptText(SAY_SPR_END_2, spraggle);
                        _endEventTimer = 5000;
                        break;
                    case 3:
                        ClassicScriptText(SAY_RIN_END_3, me);
                        _endEventTimer = 1000;
                        break;
                    case 4:
                        ClassicScriptText(EMOTE_RIN_END_4, me);
                        SetFaint();
                        _endEventTimer = 9000;
                        break;
                    case 5:
                        ClearFaint();
                        _endEventTimer = 1000;
                        break;
                    case 6:
                        ClassicScriptText(SAY_RIN_END_6, me);
                        _endEventTimer = 3000;
                        break;
                    case 7:
                        ClassicScriptText(SAY_SPR_END_7, spraggle);
                        _endEventTimer = 10000;
                        break;
                    case 8:
                        _endEventTimer = 5000;
                        break;
                    case 9:
                        SetFollowComplete();
                        break;
                }

                ++_endEventProgress;
            }
            else
                _endEventTimer -= diff;
        }
        else if (HasFollowState(STATE_FOLLOW_INPROGRESS))
        {
            if (!HasFollowState(STATE_FOLLOW_PAUSED))
            {
                if (_faintTimer < diff)
                {
                    SetFaint();
                    _faintTimer = urand(60000, 120000);
                }
                else
                    _faintTimer -= diff;
            }
        }
    }

private:
    uint32 _faintTimer;
    uint32 _endEventProgress;
    uint32 _endEventTimer;
    ObjectGuid _spraggleGuid;
};

/*######
## npc_simone_the_inconspicuous 14527
######*/

enum SimoneTheInconspicuous
{
    SPELL_FOOLS_PLIGHT              = 23504,

    NPC_SIMONE_THE_SEDUCTRESS       = 14533,
    NPC_PRECIOUS                    = 14528,
    NPC_PRECIOUS_THE_DEVOURER       = 14538,

    QUEST_STAVE_OF_THE_ANCIENTS     = 7636,

    // SetGUID ids used to link the summoned demons (VMaNGOS sets npc_simone_seductressAI::m_simoneGuid / m_preciousGuid and
    // npc_precious_the_devourerAI::m_simoneGuid directly).
    // TODO(classic): npc_simone_seductress (14533) and npc_precious_the_devourer (14538) are not part of this port; their
    // ports must accept these SetGUID ids.
    DATA_SIMONE_GUID                = 1,
    DATA_PRECIOUS_GUID              = 2
};

#define GOSSIP_ITEM_SIMONE "Show me your real face, demon."

struct classic_npc_simone_the_inconspicuous : public ScriptedAI
{
    classic_npc_simone_the_inconspicuous(Creature* creature) : ScriptedAI(creature)
    {
        _foolsPlightTimer = urand(5000, 10000);
        _transformTimer = 10000;
        _transformEmoteTimer = 5000;
        _transform = false;
    }

    void Reset() override
    {
        me->SetRespawnDelay(35 * MINUTE);
        me->SetRespawnTime(35 * MINUTE);

        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_GOSSIP);
        me->SetVisible(true);

        _foolsPlightTimer    = urand(5000, 10000);
        _transformTimer      = 10000;
        _transformEmoteTimer = 5000;
        _transform           = false;

        if (Creature* precious = me->FindNearestCreature(NPC_PRECIOUS, 100.0f))
        {
            precious->SetVisible(true);
            precious->GetMotionMaster()->MoveFollow(me, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
        }
        else if (Creature* summoned = me->SummonCreature(NPC_PRECIOUS, me->GetPosition(), TEMPSUMMON_DEAD_DESPAWN))
            summoned->GetMotionMaster()->MoveFollow(me, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }

    void Transform()
    {
        if (!ObjectAccessor::GetPlayer(*me, _playerGuid))
            return;

        Creature* demon = me->SummonCreature(NPC_SIMONE_THE_SEDUCTRESS, me->GetPosition(), TEMPSUMMON_DEAD_DESPAWN);
        Creature* precious = me->FindNearestCreature(NPC_PRECIOUS, 100.0f);

        if (demon)
        {
            demon->AI()->SetGUID(me->GetGUID(), DATA_SIMONE_GUID);

            me->SetVisible(false);
            me->DespawnOrUnsummon();
        }

        if (demon && precious)
        {
            if (Creature* preciousDevourer = me->SummonCreature(NPC_PRECIOUS_THE_DEVOURER, precious->GetPosition(), TEMPSUMMON_DEAD_DESPAWN))
            {
                preciousDevourer->setActive(true);
                demon->AI()->SetGUID(preciousDevourer->GetGUID(), DATA_PRECIOUS_GUID);
                preciousDevourer->AI()->SetGUID(demon->GetGUID(), DATA_SIMONE_GUID);
            }

            precious->SetVisible(false);
            precious->DespawnOrUnsummon();
        }
    }

    void BeginEvent(ObjectGuid playerGuid)
    {
        _playerGuid = playerGuid;
        me->GetMotionMaster()->MoveIdle();
        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
        _transform = true;
    }

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (player->GetQuestStatus(QUEST_STAVE_OF_THE_ANCIENTS) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_SIMONE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        CloseGossipMenuFor(player);
        BeginEvent(player->GetGUID());
        me->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_transform)
        {
            if (_transformEmoteTimer)
            {
                if (_transformEmoteTimer <= diff)
                {
                    me->HandleEmoteCommand(EMOTE_ONESHOT_SHOUT);
                    _transformEmoteTimer = 0;
                }
                else
                    _transformEmoteTimer -= diff;
            }

            if (_transformTimer < diff)
            {
                _transform = false;
                Transform();
                return;
            }
            else
                _transformTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (_foolsPlightTimer < diff)
        {
            if (DoCastVictim(SPELL_FOOLS_PLIGHT) == SPELL_CAST_OK)
                _foolsPlightTimer = urand(5000, 10000);
        }
        else
            _foolsPlightTimer -= diff;
    }

private:
    uint32 _foolsPlightTimer;
    uint32 _transformTimer;
    uint32 _transformEmoteTimer;
    bool _transform;
    ObjectGuid _playerGuid;
};

void AddSC_classic_ungoro_crater()
{
    RegisterCreatureAI(classic_npc_ame01);
    RegisterCreatureAI(classic_npc_ringo);
    RegisterCreatureAI(classic_npc_simone_the_inconspicuous);
}
