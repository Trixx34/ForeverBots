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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/silithus.cpp (ScriptDev2 / Nostalrius lineage, GPL-2)
// Ported: npc_solenor (Nelson the Nice / Solenor the Slayer, Stave of the Ancients), npc_Geologist_Larksbane (quest 8315),
//         npc_Krug_SkullSplit (Field Duty Papers event), npc_Shai

#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuaternionData.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "World.h"
#include "classic_script_text.h"
#include <list>

namespace
{
// VMaNGOS World.h: dynamic respawn scaling ("DRSS") reference population
// TODO(classic): value taken from VMaNGOS World.h (not in the VMaNGOS files available here) - verify.
constexpr uint32 BLIZZLIKE_REALM_POPULATION = 2500;
}

/*#####
 ## npc_solenor (Nelson the Nice -> Solenor the Slayer)
 ######*/

enum Solenor
{
    SPELL_SOUL_FLAME                = 23272,
    SPELL_DREADFUL_FRIGHT           = 23275,
    SPELL_CREEPING_DOOM             = 23589,
    SPELL_CRIPPLING_CLIP            = 23279,
    SPELL_WING_CLIP_RANK_3          = 14268,
    SPELL_FROST_TRAP                = 13810,

    EMOTE_IMMOBILIZED               = 9785,

    NPC_NELSON_THE_NICE             = 14536,
    NPC_SOLENOR_THE_SLAYER          = 14530,
    NPC_THE_CLEANER                 = 14503
};

Position const NelsonHomePos = { -7724.21f, 1676.43f, 7.0571f, 4.80044f };

struct classic_npc_solenor : public ScriptedAI
{
    classic_npc_solenor(Creature* creature) : ScriptedAI(creature)
    {
        _transform = false;
        _despawnTimer = 0;
        _transformTimer = 10000;
        _transformEmoteTimer = 5000;
        _dreadfulFrightTimer = 0;
        _creepingDoomTimer = 0;
        _castSoulFlameTimer = 0;
    }

    void Reset() override
    {
        switch (me->GetEntry())
        {
            case NPC_NELSON_THE_NICE:
                me->SetRespawnDelay(35 * MINUTE);
                me->SetRespawnTime(35 * MINUTE);
                me->SetHomePosition(NelsonHomePos);
                if (me->GetDistance(NelsonHomePos) > 1.0f)   // VMaNGOS always teleports; skip when already there (initial spawn)
                    me->NearTeleportTo(NelsonHomePos);
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != WAYPOINT_MOTION_TYPE)
                {
                    me->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
                    me->GetMotionMaster()->Initialize();
                }

                me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_GOSSIP);

                _transformTimer      = 10000;
                _transformEmoteTimer = 5000;
                _transform           = false;
                _despawnTimer        = 0;
                _castSoulFlameTimer  = 0;
                break;
            case NPC_SOLENOR_THE_SLAYER:
                if (!_despawnTimer)
                {
                    _despawnTimer = 20 * MINUTE * IN_MILLISECONDS;
                    _castSoulFlameTimer = 150;
                    me->AddAura(SPELL_SOUL_FLAME, me); // apply on spawn in case of instant Freezing Trap
                }

                _hunterGuid.Clear();
                _dreadfulFrightTimer = urand(10000, 15000);
                _creepingDoomTimer   = urand(3000, 6000);
                break;
        }
    }

    /** Nelson the Nice */
    void Transform()
    {
        me->UpdateEntry(NPC_SOLENOR_THE_SLAYER);
        me->SetHomePosition(me->GetPosition());
        me->SetDefaultMovementType(IDLE_MOTION_TYPE);
        me->GetMotionMaster()->Initialize();
        Reset();
    }

    void BeginEvent(ObjectGuid playerGuid)
    {
        _hunterGuid = playerGuid;
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
        _transform = true;
    }

    // VMaNGOS starts the event with a DB gossip script (SCRIPT_COMMAND_SEND_SCRIPT_EVENT -> OnScriptEventHappened).
    // TODO(classic): TC has no script-event command; the event is started from the gossip option instead
    // (any option of Nelson's menu), or by SetGUID(playerGuid, 0) from another script.
    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        if (me->GetEntry() != NPC_NELSON_THE_NICE || _transform)
            return false;

        CloseGossipMenuFor(player);
        BeginEvent(player->GetGUID());
        return true;
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (guid.IsPlayer() && me->GetEntry() == NPC_NELSON_THE_NICE && !_transform)
            BeginEvent(guid);
    }

    /** Solenor the Slayer */
    void JustEngagedWith(Unit* who) override
    {
        if (who->GetClass() == CLASS_HUNTER && (_hunterGuid.IsEmpty() || _hunterGuid == who->GetGUID()))
            _hunterGuid = who->GetGUID();
        else
            DemonDespawn();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        me->RemoveAllControlled();   // VMaNGOS RemoveGuardians()
        ScriptedAI::EnterEvadeMode(why);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (Unit* victim = me->GetVictim())
            summoned->AI()->AttackStart(victim);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->SetHomePosition(NelsonHomePos);
        // DRSS
        uint32 respawnDelay = 3 * HOUR;
        uint32 sessions = sWorld->GetActiveSessionCount();
        if (sessions > BLIZZLIKE_REALM_POPULATION)
            respawnDelay = uint32(respawnDelay * (float(BLIZZLIKE_REALM_POPULATION) / float(sessions)));

        me->SetRespawnDelay(respawnDelay);
        me->SetRespawnTime(respawnDelay);
        me->SaveRespawnTime();
    }

    void DemonDespawn(bool triggered = true)
    {
        me->RemoveAllControlled();
        me->SetHomePosition(NelsonHomePos);
        me->SetRespawnDelay(15 * MINUTE);
        me->SetRespawnTime(15 * MINUTE);
        me->SaveRespawnTime();

        if (triggered)
        {
            if (Creature* cleaner = me->SummonCreature(NPC_THE_CLEANER, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, Milliseconds(20 * MINUTE * IN_MILLISECONDS)))
            {
                std::list<Unit*> targets;
                for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                    if (Unit* target = ref->GetVictim())
                        targets.push_back(target);

                for (Unit* target : targets)
                {
                    if (target->IsAlive())
                    {
                        cleaner->EngageWithTarget(target);
                        cleaner->AI()->AttackStart(target);
                    }
                }
            }
        }

        me->DespawnOrUnsummon(0s, Seconds(15 * MINUTE));
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_WING_CLIP_RANK_3)
        {
            if (DoCastSelf(SPELL_CRIPPLING_CLIP, true) == SPELL_CAST_OK)
                ClassicScriptText(EMOTE_IMMOBILIZED, me);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        /** Nelson the Nice */
        if (_transform)
        {
            if (_transformEmoteTimer)
            {
                if (_transformEmoteTimer <= diff)
                {
                    me->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
                    _transformEmoteTimer = 0;
                }
                else
                    _transformEmoteTimer -= diff;
            }

            if (_transformTimer < diff)
            {
                _transform = false;
                Transform();
            }
            else
                _transformTimer -= diff;
        }

        /** Solenor the Slayer */
        if (_despawnTimer)
        {
            if (_despawnTimer <= diff)
            {
                if (me->IsAlive() && !me->IsInCombat())
                {
                    DemonDespawn(false);
                    return;
                }
            }
            else
                _despawnTimer -= diff;
        }

        if (_castSoulFlameTimer)
        {
            if (_castSoulFlameTimer <= diff)
            {
                // delay this cast so spell animation is visible to the player
                me->CastSpell(me, SPELL_SOUL_FLAME, false);
                _castSoulFlameTimer = 0;
            }
            else
                _castSoulFlameTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (me->HasAura(SPELL_SOUL_FLAME) && me->HasAura(SPELL_FROST_TRAP))
            me->RemoveAurasDueToSpell(SPELL_SOUL_FLAME);

        if (me->GetThreatManager().GetThreatListSize() > 1)
        {
            DemonDespawn();
            return;
        }

        if (_creepingDoomTimer < diff)
        {
            DoCastSelf(SPELL_CREEPING_DOOM);
            _creepingDoomTimer = 15000;
        }
        else
            _creepingDoomTimer -= diff;

        if (_dreadfulFrightTimer < diff)
        {
            if (Unit* victim = me->GetVictim())
            {
                if (me->GetDistance2d(victim) > 5.0f)
                {
                    if (DoCast(victim, SPELL_DREADFUL_FRIGHT) == SPELL_CAST_OK)
                        _dreadfulFrightTimer = urand(15000, 20000);
                }
            }
        }
        else
            _dreadfulFrightTimer -= diff;
    }

private:
    uint32 _transformTimer;
    uint32 _transformEmoteTimer;
    bool _transform;

    ObjectGuid _hunterGuid;
    uint32 _dreadfulFrightTimer;
    uint32 _creepingDoomTimer;
    uint32 _castSoulFlameTimer;
    uint32 _despawnTimer;
};

/*###
 ## npc_Geologist_Larksbane
 ###*/

enum Larksbane
{
    GO_GLYPHED_CRYSTAL              = 180514,
    NPC_BARISTOLTH                  = 15180,     // Baristolth of the Shifting Sands
    QUEST_THE_CALLING               = 8315
};

struct classic_npc_Geologist_Larksbane : public ScriptedAI
{
    classic_npc_Geologist_Larksbane(Creature* creature) : ScriptedAI(creature), _nextActionTimer(0), _currAction(0) { }

    void Reset() override
    {
        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        _crystalGUIDs.clear();
        _currAction = 0;
        _nextActionTimer = 0;
    }

    void OnQuestReward(Player* /*player*/, Quest const* quest, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_THE_CALLING)
            return;

        me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);

        if (GameObject* go = me->SummonGameObject(GO_GLYPHED_CRYSTAL, -6825.29f, 809.125f, 51.8699f, 0.349065f, QuaternionData(0.0f, 0.0f, 0.173648f, 0.984808f), 0s))
            _crystalGUIDs.push_back(go->GetGUID());
        if (GameObject* go = me->SummonGameObject(GO_GLYPHED_CRYSTAL, -6822.21f, 808.584f, 51.5885f, 2.77507f, QuaternionData(0.0f, 0.0f, 0.983254f, 0.182238f), 0s))
            _crystalGUIDs.push_back(go->GetGUID());
        if (GameObject* go = me->SummonGameObject(GO_GLYPHED_CRYSTAL, -6823.57f, 811.977f, 51.4426f, 4.41568f, QuaternionData(0.0f, 0.0f, -0.803857f, 0.594823f), 0s))
            _crystalGUIDs.push_back(go->GetGUID());

        _currAction = 1;
        _nextActionTimer = 4000;
    }

    void TalkLine(uint32 broadcastTextId, Emote emote = EMOTE_ONESHOT_TALK)
    {
        me->HandleEmoteCommand(emote);
        me->Say(broadcastTextId);     // VMaNGOS MonsterSay
    }

    void DoLarksbaneAction()
    {
        switch (_currAction)
        {
            case 1:  TalkLine(10762); _nextActionTimer = 4000; break;
            case 2:
                for (ObjectGuid const& guid : _crystalGUIDs)
                    if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                        crystal->Use(me);
                _nextActionTimer = 5000;
                break;
            case 3:  TalkLine(10829); _nextActionTimer = 7000; break;
            case 4:  TalkLine(10830); _nextActionTimer = 11000; break;
            case 5:  TalkLine(10831); _nextActionTimer = 9000; break;
            case 6:  TalkLine(10832); _nextActionTimer = 11000; break;
            case 7:  TalkLine(10833); _nextActionTimer = 11000; break;
            case 8:  TalkLine(10836, EMOTE_ONESHOT_EXCLAMATION); _nextActionTimer = 9000; break;
            case 9:
                me->TextEmote(10837);  // VMaNGOS MonsterTextEmote
                _nextActionTimer = 3000;
                break;
            case 10: TalkLine(10838); _nextActionTimer = 4000; break;
            case 11: TalkLine(10839); _nextActionTimer = 12000; break;
            case 12: TalkLine(10840, EMOTE_ONESHOT_EXCLAMATION); _nextActionTimer = 9000; break;
            case 13: TalkLine(10841); _nextActionTimer = 9000; break;
            case 14: TalkLine(10842); _nextActionTimer = 12000; break;
            case 15: TalkLine(10843); _nextActionTimer = 12000; break;
            case 16: TalkLine(10844); _nextActionTimer = 12000; break;
            case 17: TalkLine(10845); _nextActionTimer = 12000; break;
            case 18: TalkLine(10846); _nextActionTimer = 12000; break;
            case 19: TalkLine(10847); _nextActionTimer = 12000; break;
            case 20: TalkLine(10848); _nextActionTimer = 9000; break;
            case 21: TalkLine(10849); _nextActionTimer = 3000; break;
            case 22: TalkLine(10850); _nextActionTimer = 12000; break;
            case 23: TalkLine(10851); _nextActionTimer = 9000; break;
            case 24: TalkLine(10852); _nextActionTimer = 3000; break;
            case 25:
                if (Creature* baristolth = me->FindNearestCreature(NPC_BARISTOLTH, 50.0f))
                    baristolth->TextEmote(10853);
                _nextActionTimer = 4000;
                break;
            case 26:
                if (Creature* baristolth = me->FindNearestCreature(NPC_BARISTOLTH, 50.0f))
                    baristolth->Say(10854);
                _nextActionTimer = 5000;
                break;
            case 27:
                for (ObjectGuid const& guid : _crystalGUIDs)
                    if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                        crystal->Delete();
                _crystalGUIDs.clear();
                _nextActionTimer = 5000;
                break;
            case 28:
                me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                _currAction = 0;
                _nextActionTimer = 0;
                return;
            default:
                _nextActionTimer = 4000;
                break;
        }
        ++_currAction;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_currAction)
        {
            if (_nextActionTimer < diff)
                DoLarksbaneAction();
            else
                _nextActionTimer -= diff;
        }
    }

private:
    std::list<ObjectGuid> _crystalGUIDs;
    uint32 _nextActionTimer;
    uint32 _currAction;
};

/*########################
 ## npc_Krug_SkullSplit ##
 ########################*/

#define GOSSIP_ITEM_KRUG_SKULLSPLIT_1 "Continue."
#define GOSSIP_ITEM_KRUG_SKULLSPLIT_2 "Very well, let's go!"

Position const HunterKillerSpawnPos = { -7765.0f, 536.0f, -43.0f, 0.8f };
Position const ShaiSpawnPos         = { -7556.600098f, 749.007019f, -17.578800f, 0.0f };
Position const ShaiDestPos          = { -7553.46f, 720.924f, -16.715f, 0.0f };
Position const MerokSpawnPos        = { -7537.149902f, 731.184021f, -16.418100f, 0.0f };
Position const MerokDestPos         = { -7542.45f, 720.155f, -15.6801f, 0.0f };

enum KrugSkullSplit
{
    QUEST_FIELD_DUTY                = 8731,

    NPC_HUNTERKILLER                = 15620,
    NPC_MEROK_LONGSTRIDE            = 15613,
    NPC_SHADOW_PRIESTESS_SHAI       = 15615,
    NPC_ORGRIMMAR_LEGION_GRUNT      = 15616,

    FACTION_FRIENDLY_KRUG           = 35,

    // TODO(classic): VMaNGOS uses script_texts -1780131..-1780139 ("NO SNIFFED BROADCAST_TEXT DATA FOR THESE EXISTS").
    // No broadcast text ids are known, so these lines are silent (ClassicScriptText ignores id 0).
    SAY_KRUG_LINE_1                 = 0,
    SAY_KRUG_LINE_2                 = 0,
    SAY_KRUG_LINE_3                 = 0,
    SAY_KRUG_LINE_4                 = 0,
    SAY_KRUG_LINE_5                 = 0,
    SAY_KRUG_LINE_6                 = 0,
    SAY_KRUG_LINE_7                 = 0,
    SAY_KRUG_LINE_8                 = 0,
    SAY_GRUNT_LINE_9                = 0
};

enum FieldDutyPaperEventStatus
{
    EVENT_NOT_STARTED,
    EVENT_STARTED,
    EVENT_COMPLETE
};

/**
 * Originally written by Ivina & Malkins < Nostalrius >
 * Various improvements by additional authors
 */
struct classic_npc_Krug_SkullSplit : public ScriptedAI
{
    classic_npc_Krug_SkullSplit(Creature* creature) : ScriptedAI(creature)
    {
        _gruntSpeechTimer = 0;
        ResetEvent();
    }

    void Reset() override { }

    FieldDutyPaperEventStatus GetEventStatus() const { return _eventStatus; }

    void ResetEvent()
    {
        _eventStatus = EVENT_NOT_STARTED;
        if (Creature* hunterKiller = ObjectAccessor::GetCreature(*me, _hunterKillerGUID))
            hunterKiller->DespawnOrUnsummon();
        _speechTimer = 0;
        _speechNum = 0;
        _hunterKillerGUID.Clear();
        _eventResetTimer = 120000;
        _isDoingSpeech = false;
        _gruntSpeech = false;
        ResetOtherNPCsPosition();
    }

    void StartEvent()
    {
        if (_eventStatus != EVENT_NOT_STARTED)
            return;

        ClassicScriptText(SAY_KRUG_LINE_1, me);

        _gruntSpeechTimer = 3000;
        _speechTimer = 10000;
        _speechNum = 0;
        _isDoingSpeech = true;
        _gruntSpeech = false;

        if (Creature* hunterKiller = me->SummonCreature(NPC_HUNTERKILLER, HunterKillerSpawnPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 450s))
        {
            hunterKiller->setActive(true);
            hunterKiller->SetRespawnDelay(460);
            _eventStatus = EVENT_STARTED;
            _hunterKillerGUID = hunterKiller->GetGUID();
            InitOtherNPCsGuids();
            hunterKiller->SetFaction(FACTION_FRIENDLY_KRUG);
        }

        me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
    }

    void CompleteEvent()
    {
        _eventResetTimer = 1800000;
        _eventStatus = EVENT_COMPLETE;
        _isDoingSpeech = false;
        _gruntSpeech = true;

        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
    }

    void InitOtherNPCsGuids()
    {
        _merokGUID.Clear();
        _shaiGUID.Clear();

        if (Creature* merok = me->FindNearestCreature(NPC_MEROK_LONGSTRIDE, 100.0f, true))
            _merokGUID = merok->GetGUID();

        if (Creature* shai = me->FindNearestCreature(NPC_SHADOW_PRIESTESS_SHAI, 100.0f, true))
            _shaiGUID = shai->GetGUID();
    }

    void ResetOtherNPCsPosition()
    {
        if (Creature* merok = ObjectAccessor::GetCreature(*me, _merokGUID))
            merok->GetMotionMaster()->MovePoint(0, MerokSpawnPos);

        if (Creature* shai = ObjectAccessor::GetCreature(*me, _shaiGUID))
            shai->GetMotionMaster()->MovePoint(0, ShaiSpawnPos);
    }

    void SummonedCreatureDies(Creature* summoned, Unit* /*killer*/) override
    {
        if (summoned->GetGUID() == _hunterKillerGUID)
            CompleteEvent();
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        if (summoned->GetGUID() == _hunterKillerGUID && _eventStatus != EVENT_COMPLETE)
            ResetEvent();
    }

    void JustDied(Unit* /*killer*/) override
    {
        ResetEvent();
    }

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);

        if (player->GetQuestStatus(QUEST_FIELD_DUTY) == QUEST_STATUS_INCOMPLETE && _eventStatus == EVENT_NOT_STARTED)
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_KRUG_SKULLSPLIT_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        else if (player->GetQuestStatus(QUEST_FIELD_DUTY) == QUEST_STATUS_INCOMPLETE && _eventStatus == EVENT_COMPLETE)
        {
            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());
        }

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);

        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_KRUG_SKULLSPLIT_2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 2)
        {
            StartEvent();
            CloseGossipMenuFor(player);
        }
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        /**
         * Note: The blizzlike behaviour here is probably to require the Hunter-Killer
         * to be killed much more often than our current timers. It'd be pretty aids
         * killing it every run though or requiring many players to wait each attempt.
         */
        if (_eventStatus == EVENT_COMPLETE)
        {
            if (_eventResetTimer <= diff)
                ResetEvent();
            else
                _eventResetTimer -= diff;
        }

        /* Speech */
        if (_isDoingSpeech)
        {
            if (_speechTimer < diff)
            {
                switch (_speechNum)
                {
                    case 0:
                        ClassicScriptText(SAY_KRUG_LINE_2, me);
                        _speechTimer = 10000;
                        ++_speechNum;
                        break;
                    case 1:
                        ClassicScriptText(SAY_KRUG_LINE_3, me);
                        _speechTimer = 10000;
                        ++_speechNum;
                        break;
                    case 2:
                        ClassicScriptText(SAY_KRUG_LINE_4, me);
                        _speechTimer = 10000;
                        ++_speechNum;
                        break;
                    case 3:
                        ClassicScriptText(SAY_KRUG_LINE_5, me);
                        _speechTimer = 6500;
                        ++_speechNum;
                        break;
                    case 4:
                        ClassicScriptText(SAY_KRUG_LINE_6, me);
                        _speechTimer = 1000;
                        ++_speechNum;
                        break;
                    case 5:
                        if (Creature* merok = ObjectAccessor::GetCreature(*me, _merokGUID))
                            merok->GetMotionMaster()->MovePoint(0, MerokDestPos);

                        if (Creature* shai = ObjectAccessor::GetCreature(*me, _shaiGUID))
                            shai->GetMotionMaster()->MovePoint(0, ShaiDestPos);
                        ClassicScriptText(SAY_KRUG_LINE_7, me);
                        _speechTimer = 6000;
                        ++_speechNum;
                        break;
                    case 6:
                        ClassicScriptText(SAY_KRUG_LINE_8, me);
                        ++_speechNum;
                        break;
                }
            }
            else
                _speechTimer -= diff;
        }

        /* Grunt */
        if (_gruntSpeech)
        {
            if (_gruntSpeechTimer < diff)
            {
                std::list<Creature*> gruntList;
                me->GetCreatureListWithEntryInGrid(gruntList, NPC_ORGRIMMAR_LEGION_GRUNT, 100.0f);
                for (Creature* grunt : gruntList)
                    if (grunt->IsAlive())
                        ClassicScriptText(SAY_GRUNT_LINE_9, grunt);

                _gruntSpeech = false;
            }
            else
                _gruntSpeechTimer -= diff;
        }

        /* Clean unwanted states */
        if (Creature* hunterKiller = ObjectAccessor::GetCreature(*me, _hunterKillerGUID))
        {
            if (_eventStatus == EVENT_NOT_STARTED)
                hunterKiller->DespawnOrUnsummon();
        }
        else if (_eventStatus == EVENT_STARTED)
            ResetEvent();

        /* Start of combat script */
        UpdateVictim();
    }

private:
    FieldDutyPaperEventStatus _eventStatus;
    uint32 _eventResetTimer;
    uint32 _speechNum;
    uint32 _gruntSpeechTimer;
    uint32 _speechTimer;
    bool _isDoingSpeech;
    bool _gruntSpeech;
    ObjectGuid _shaiGUID;
    ObjectGuid _merokGUID;
    ObjectGuid _hunterKillerGUID;
};

/*#####
 ## npc_Shai
 ######*/

enum Shai
{
    SPELL_FLASH_HEAL = 17138
};

struct classic_npc_Shai : public ScriptedAI
{
    classic_npc_Shai(Creature* creature) : ScriptedAI(creature), _flashHealTimer(12000) { }

    void Reset() override
    {
        _flashHealTimer = 12000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (me->IsNonMeleeSpellCast(false))
            return;

        // FLASH HEAL
        if (_flashHealTimer <= diff)
        {
            Unit* target = DoSelectLowestHpFriendly(60.0f, 1);
            if (!target)
                return;

            if (DoCast(target, SPELL_FLASH_HEAL) == SPELL_CAST_OK)
                _flashHealTimer = 12000;
        }
        else
            _flashHealTimer -= diff;
    }

private:
    uint32 _flashHealTimer;
};

void AddSC_classic_silithus()
{
    RegisterCreatureAI(classic_npc_solenor);
    RegisterCreatureAI(classic_npc_Geologist_Larksbane);
    RegisterCreatureAI(classic_npc_Krug_SkullSplit);
    RegisterCreatureAI(classic_npc_Shai);
}
