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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/burning_steppes.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 4121 (Precarious Predicament), 7636 (Stave of the Ancients - Klinfran the Crazed)
// Escort waypoints are the VMaNGOS script_waypoint rows, added inline (TC master has no script_waypoint table).

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include <iterator>

/*######
## npc_grark_lorkrub
######*/

enum GrarkLorkrub
{
    SAY_START                   = 4903,
    SAY_PAY                     = 4904,
    SAY_FIRST_AMBUSH_START      = 4905,
    SAY_FIRST_AMBUSH_END        = 4906,
    SAY_SEC_AMBUSH_START        = 4907,
    SAY_SEC_AMBUSH_END          = 4908,
    SAY_THIRD_AMBUSH_START      = 4909,
    SAY_THIRD_AMBUSH_END        = 4911,
    EMOTE_LAUGH                 = 4912,
    SAY_LAST_STAND              = 4913,
    SAY_LEXLORT_1               = 4928,
    SAY_LEXLORT_2               = 4929,
    EMOTE_RAISE_AXE             = 4930,
    EMOTE_LOWER_HAND            = 4932,
    SAY_LEXLORT_3               = 4931,
    SAY_LEXLORT_4               = 4933,

    EMOTE_SUBMIT                = 4918,
    SAY_AGGRO                   = 4927,

    SPELL_CAPTURE_GRARK             = 14250,

    NPC_BLACKROCK_AMBUSHER          = 9522,
    NPC_BLACKROCK_RAIDER            = 9605,
    NPC_FLAMESCALE_DRAGONSPAWN      = 7042,
    NPC_SEARSCALE_DRAKE             = 7046,

    NPC_GRARK_LORKRUB               = 9520,
    NPC_HIGH_EXECUTIONER_NUZARK     = 9538,
    NPC_SHADOW_OF_LEXLORT           = 9539,

    FACTION_GRARK_CAPTURED          = 85,   // VMaNGOS: guesswork faction (Orgrimmar)

    QUEST_ID_PRECARIOUS_PREDICAMENT = 4121
};

struct GrarkDialogueEntry
{
    int32 action;
    int32 who;
    uint32 timer;
};

GrarkDialogueEntry const GrarkOutroDialogue[] =
{
    {SAY_LAST_STAND,    NPC_GRARK_LORKRUB,              5000},
    {SAY_LEXLORT_1,     NPC_SHADOW_OF_LEXLORT,          3000},
    {SAY_LEXLORT_2,     NPC_SHADOW_OF_LEXLORT,          5000},
    {EMOTE_RAISE_AXE,   NPC_HIGH_EXECUTIONER_NUZARK,    4000},
    {EMOTE_LOWER_HAND,  NPC_SHADOW_OF_LEXLORT,          3000},
    {SAY_LEXLORT_3,     NPC_SHADOW_OF_LEXLORT,          3000},
    {NPC_GRARK_LORKRUB, 0,                              5000},
    {SAY_LEXLORT_4,     NPC_SHADOW_OF_LEXLORT,          0},
    {0, 0, 0},
};

struct GrarkEscortPoint
{
    float x, y, z;
    uint32 waitMs;
};

// VMaNGOS script_waypoint entry 9520, points 1..46 (TC node id = VMaNGOS point id - 1; TC requires node id == index)
GrarkEscortPoint const GrarkPath[] =
{
    { -7699.62f, -1444.29f, 139.87f, 4000 }, // 1  SAY_START
    { -7670.67f, -1458.25f, 140.74f,    0 }, // 2
    { -7675.26f, -1465.58f, 140.74f,    0 }, // 3
    { -7685.84f, -1472.66f, 140.75f,    0 }, // 4
    { -7700.08f, -1473.41f, 140.79f,    0 }, // 5
    { -7712.55f, -1470.19f, 140.79f,    0 }, // 6
    { -7717.27f, -1481.70f, 140.72f, 5000 }, // 7  SAY_PAY
    { -7726.23f, -1500.78f, 132.99f,    0 }, // 8
    { -7744.61f, -1531.61f, 132.69f,    0 }, // 9
    { -7763.08f, -1536.22f, 131.93f,    0 }, // 10
    { -7815.32f, -1522.61f, 134.16f,    0 }, // 11
    { -7850.26f, -1516.87f, 138.17f,    0 }, // 12 SAY_FIRST_AMBUSH_START
    { -7850.26f, -1516.87f, 138.17f, 3000 }, // 13 SAY_FIRST_AMBUSH_END
    { -7881.01f, -1508.49f, 142.37f,    0 }, // 14
    { -7888.91f, -1458.09f, 144.79f,    0 }, // 15
    { -7889.18f, -1430.21f, 145.31f,    0 }, // 16
    { -7900.53f, -1427.01f, 150.26f,    0 }, // 17
    { -7904.15f, -1429.91f, 150.27f,    0 }, // 18
    { -7921.48f, -1425.47f, 140.54f,    0 }, // 19
    { -7941.43f, -1413.10f, 134.35f,    0 }, // 20
    { -7964.85f, -1367.45f, 132.99f,    0 }, // 21
    { -7989.95f, -1319.12f, 133.71f,    0 }, // 22
    { -8010.43f, -1270.23f, 133.42f,    0 }, // 23
    { -8025.62f, -1243.78f, 133.91f,    0 }, // 24 SAY_SEC_AMBUSH_START
    { -8025.62f, -1243.78f, 133.91f, 3000 }, // 25 SAY_SEC_AMBUSH_END
    { -8015.22f, -1196.98f, 146.76f,    0 }, // 26
    { -7994.68f, -1151.38f, 160.70f,    0 }, // 27
    { -7970.91f, -1132.81f, 170.16f,    0 }, // 28 summon Searscale Drakes
    { -7927.59f, -1122.79f, 185.86f,    0 }, // 29
    { -7897.67f, -1126.67f, 194.32f,    0 }, // 30 SAY_THIRD_AMBUSH_START
    { -7897.67f, -1126.67f, 194.32f, 3000 }, // 31 SAY_THIRD_AMBUSH_END
    { -7864.11f, -1135.98f, 203.29f,    0 }, // 32
    { -7837.31f, -1137.73f, 209.63f,    0 }, // 33
    { -7808.72f, -1134.90f, 214.84f,    0 }, // 34
    { -7786.85f, -1127.24f, 214.84f,    0 }, // 35
    { -7746.58f, -1125.16f, 215.08f, 5000 }, // 36 EMOTE_LAUGH
    { -7746.41f, -1103.62f, 215.62f,    0 }, // 37
    { -7740.25f, -1090.51f, 216.69f,    0 }, // 38
    { -7730.97f, -1085.55f, 217.12f,    0 }, // 39
    { -7697.89f, -1089.43f, 217.62f,    0 }, // 40
    { -7679.30f, -1059.15f, 220.09f,    0 }, // 41
    { -7661.39f, -1038.24f, 226.24f,    0 }, // 42
    { -7634.49f, -1020.96f, 234.30f,    0 }, // 43
    { -7596.22f, -1013.16f, 244.03f,    0 }, // 44
    { -7556.53f, -1021.74f, 253.21f, 2000 }, // 45 SAY_LAST_STAND
    { -7556.53f, -1021.74f, 253.21f,    0 }, // 46
};

struct classic_npc_grark_lorkrub : public EscortAI
{
    classic_npc_grark_lorkrub(Creature* creature) : EscortAI(creature),
        _somethingWentWrongTimer(400000), _killedCreatures(0), _isFirstSearScale(true), _dialogueTimer(0), _dialogueStep(-1)
    {
        for (uint32 i = 0; i < std::size(GrarkPath); ++i)
        {
            GrarkEscortPoint const& p = GrarkPath[i];
            AddWaypoint(i, p.x, p.y, p.z, 0.0f, p.waitMs ? Optional<Milliseconds>(Milliseconds(p.waitMs)) : Optional<Milliseconds>(), false);
        }
    }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            _somethingWentWrongTimer = 400000; // ~6 minutes
            _killedCreatures = 0;
            _isFirstSearScale = true;
            _dialogueStep = -1;

            _searscaleGuids.clear();

            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            ClassicScriptText(SAY_AGGRO, me);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // No combat during escort
        if (HasEscortState(STATE_ESCORT_ESCORTING))
            return;

        EscortAI::MoveInLineOfSight(who);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_ID_PRECARIOUS_PREDICAMENT)
            Start(true, player->GetGUID(), quest);
    }

    // VMaNGOS EffectDummyCreature_spell_capture_grark (effect 0 of the dummy spell)
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_CAPTURE_GRARK)
            return;

        // Note: this implementation needs additional research! There is a lot of guesswork involved in this!
        if (me->GetHealthPct() > 25.0f)
            return;

        // The faction is guesswork - needs more research (VMaNGOS TEMPFACTION_RESTORE_RESPAWN: TC restores the template faction on respawn)
        ClassicScriptText(EMOTE_SUBMIT, me);
        me->SetFaction(FACTION_GRARK_CAPTURED);
        EnterEvadeMode(EvadeReason::Other);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        uint32 const pointId = waypointId + 1; // VMaNGOS point ids start at 1

        switch (pointId)
        {
            case 1:
                ClassicScriptText(SAY_START, me);
                break;
            case 7:
                ClassicScriptText(SAY_PAY, me);
                break;
            case 12:
                ClassicScriptText(SAY_FIRST_AMBUSH_START, me);
                SetEscortPaused(true);

                me->SummonCreature(NPC_BLACKROCK_AMBUSHER, -7844.3f, -1521.6f, 139.2f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_BLACKROCK_AMBUSHER, -7860.4f, -1507.8f, 141.0f, 6.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_BLACKROCK_RAIDER,   -7845.6f, -1508.1f, 138.8f, 6.1f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_BLACKROCK_RAIDER,   -7859.8f, -1521.8f, 139.2f, 6.2f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                break;
            case 24:
                ClassicScriptText(SAY_SEC_AMBUSH_START, me);
                SetEscortPaused(true);

                me->SummonCreature(NPC_BLACKROCK_AMBUSHER,     -8035.3f, -1222.2f, 135.5f, 5.1f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_FLAMESCALE_DRAGONSPAWN, -8037.5f, -1216.9f, 135.8f, 5.1f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_BLACKROCK_AMBUSHER,     -8009.5f, -1222.1f, 139.2f, 3.9f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_FLAMESCALE_DRAGONSPAWN, -8007.1f, -1219.4f, 140.1f, 3.9f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                break;
            case 28:
                me->SummonCreature(NPC_SEARSCALE_DRAKE, -7900.1f, -1133.14f, 193.98f, 3.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_SEARSCALE_DRAKE, -7898.8f, -1125.1f, 193.9f, 3.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                me->SummonCreature(NPC_SEARSCALE_DRAKE, -7895.6f, -1119.5f, 194.5f, 3.1f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 200000ms);
                break;
            case 30:
            {
                if (_killedCreatures < 11)
                    SetEscortPaused(true);
                ClassicScriptText(SAY_THIRD_AMBUSH_START, me);

                Player* player = GetPlayerForEscort();
                if (!player)
                    return;

                // Set all the dragons in combat
                for (ObjectGuid const& guid : _searscaleGuids)
                    if (Creature* drake = ObjectAccessor::GetCreature(*me, guid))
                        drake->AI()->AttackStart(player);
                break;
            }
            case 36:
                ClassicScriptText(EMOTE_LAUGH, me);
                break;
            case 45:
                StartNextDialogueText(SAY_LAST_STAND);
                SetEscortPaused(true);

                me->SummonCreature(NPC_HIGH_EXECUTIONER_NUZARK, -7532.3f, -1029.4f, 258.0f, 2.7f, TEMPSUMMON_TIMED_DESPAWN, 40000ms);
                me->SummonCreature(NPC_SHADOW_OF_LEXLORT,       -7532.8f, -1032.9f, 258.2f, 2.5f, TEMPSUMMON_TIMED_DESPAWN, 40000ms);
                break;
        }
    }

    void StartNextDialogueText(int32 firstAction)
    {
        for (int32 i = 0; i < int32(std::size(GrarkOutroDialogue)); ++i)
        {
            if (GrarkOutroDialogue[i].action == firstAction)
            {
                _dialogueStep = i;
                _dialogueTimer = 0;
                break;
            }
        }
    }

    void DialogueUpdate(uint32 diff)
    {
        if (_dialogueStep < 0)
            return;

        if (_dialogueTimer < diff)
        {
            if (GrarkOutroDialogue[_dialogueStep].action != 0)
            {
                if (GrarkOutroDialogue[_dialogueStep].who != 0)
                {
                    if (Creature* speaker = GetSpeakerByEntry(GrarkOutroDialogue[_dialogueStep].who))
                        ClassicScriptText(GrarkOutroDialogue[_dialogueStep].action, speaker);
                }
                JustDidDialogueStep(GrarkOutroDialogue[_dialogueStep].action);
                ++_dialogueStep;
                _dialogueTimer = GrarkOutroDialogue[_dialogueStep].timer;
            }
            else
                _dialogueStep = -1;
        }
        else
            _dialogueTimer -= diff;
    }

    void JustDidDialogueStep(int32 entry)
    {
        switch (entry)
        {
            case SAY_LEXLORT_1:
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
                break;
            case SAY_LEXLORT_3:
                // Note: this part isn't very clear. Should he just simply attack him, or charge him?
                if (Creature* nuzark = ObjectAccessor::GetCreature(*me, _nuzarkGuid))
                    nuzark->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK2HTIGHT);
                break;
            case NPC_GRARK_LORKRUB:
                // Fake death creature when the axe is lowered. This will allow us to finish the event
                me->InterruptNonMeleeSpells(true);
                me->SetHealth(1);
                me->StopMoving();
                me->RemoveAllAurasOnDeath();
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveIdle();
                me->SetStandState(UNIT_STAND_STATE_DEAD);
                break;
            case SAY_LEXLORT_4:
                // Finish the quest
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_ID_PRECARIOUS_PREDICAMENT, me);
                // Kill self
                me->KillSelf();
                break;
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        switch (summoned->GetEntry())
        {
            case NPC_HIGH_EXECUTIONER_NUZARK:
                _nuzarkGuid = summoned->GetGUID();
                break;
            case NPC_SHADOW_OF_LEXLORT:
                _lexlortGuid = summoned->GetGUID();
                break;
            case NPC_SEARSCALE_DRAKE:
                // If it's the flying drake allow him to move in circles
                if (_isFirstSearScale)
                {
                    _isFirstSearScale = false;
                    // ToDo (VMaNGOS): this guy should fly in circles above the creature
                }
                _searscaleGuids.push_back(summoned->GetGUID());
                break;
            default:
                // The hostile mobs should attack the player only
                if (Player* player = GetPlayerForEscort())
                    summoned->AI()->AttackStart(player);
                break;
        }
    }

    void SummonedCreatureDies(Creature* /*summoned*/, Unit* /*killer*/) override
    {
        ++_killedCreatures;
        switch (_killedCreatures)
        {
            case 4:
                ClassicScriptText(SAY_FIRST_AMBUSH_END, me);
                _somethingWentWrongTimer = 400000;
                SetEscortPaused(false);
                break;
            case 8:
                ClassicScriptText(SAY_SEC_AMBUSH_END, me);
                _somethingWentWrongTimer = 400000;
                SetEscortPaused(false);
                break;
            case 11:
                ClassicScriptText(SAY_THIRD_AMBUSH_END, me);
                _somethingWentWrongTimer = 400000;
                SetEscortPaused(false);
                break;
        }
    }

    Creature* GetSpeakerByEntry(int32 entry)
    {
        switch (entry)
        {
            case NPC_GRARK_LORKRUB:
                return me;
            case NPC_HIGH_EXECUTIONER_NUZARK:
                return ObjectAccessor::GetCreature(*me, _nuzarkGuid);
            case NPC_SHADOW_OF_LEXLORT:
                return ObjectAccessor::GetCreature(*me, _lexlortGuid);
            default:
                return nullptr;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        DialogueUpdate(diff);

        if (HasEscortState(STATE_ESCORT_PAUSED) && HasEscortState(STATE_ESCORT_ESCORTING))
        {
            if (_somethingWentWrongTimer < diff) // basically if players avoid killing adds, he will stay stuck.
            {
                me->DespawnOrUnsummon();
                return;
            }
            else
                _somethingWentWrongTimer -= diff;
        }

        UpdateVictim();
    }

private:
    ObjectGuid _nuzarkGuid;
    ObjectGuid _lexlortGuid;
    GuidList _searscaleGuids;

    uint32 _somethingWentWrongTimer;
    uint8 _killedCreatures;
    bool _isFirstSearScale;
    uint32 _dialogueTimer;
    int32 _dialogueStep;
};

/*######
## npc_klinfran (Franklin the Friendly 14529 -> Klinfran the Crazed 14534)
######*/

enum Klinfran
{
    SPELL_DEMONIC_FRENZY            = 23257,
    SPELL_ENTROPIC_STING            = 23260,
    SPELL_SCORPID_STING_RANK_4      = 14277,

    EMOTE_FRENZY                    = 7797,

    NPC_NELSON_THE_NICE             = 14529,
    NPC_KLINFRAN_THE_CRAZED         = 14534,
    NPC_THE_CLEANER                 = 14503,

    QUEST_STAVE_OF_THE_ANCIENTS     = 7636
};

#define GOSSIP_ITEM_KLINFRAN "Show me your real face, demon."

Position const KlinfranHomePos = { -8318.19f, -993.662f, 176.956f, 5.65024f };

struct classic_npc_klinfran : public ScriptedAI
{
    classic_npc_klinfran(Creature* creature) : ScriptedAI(creature),
        _transformTimer(10000), _transformEmoteTimer(5000), _transform(false), _demonicFrenzyTimer(5000), _despawnTimer(0) { }

    void Reset() override
    {
        switch (me->GetEntry())
        {
            case NPC_NELSON_THE_NICE:
                me->SetRespawnDelay(35 * MINUTE);
                me->SetRespawnTime(35 * MINUTE);
                me->SetHomePosition(KlinfranHomePos);
                me->NearTeleportTo(KlinfranHomePos);
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
                break;
            case NPC_KLINFRAN_THE_CRAZED:
                if (!_despawnTimer)
                    _despawnTimer = 20 * MINUTE * IN_MILLISECONDS;

                _hunterGuid.Clear();
                _demonicFrenzyTimer = 5000;
                break;
        }
    }

    bool OnGossipHello(Player* player) override
    {
        if (player->GetQuestStatus(QUEST_STAVE_OF_THE_ANCIENTS) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_KLINFRAN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        CloseGossipMenuFor(player);
        BeginEvent(player->GetGUID());
        return true;
    }

    /** Franklin the Friendly */
    void Transform()
    {
        me->UpdateEntry(NPC_KLINFRAN_THE_CRAZED);
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

    /** Klinfran the Crazed */
    void JustEngagedWith(Unit* who) override
    {
        if (who->GetClass() == CLASS_HUNTER && (_hunterGuid.IsEmpty() || _hunterGuid == who->GetGUID()))
            _hunterGuid = who->GetGUID();
        else
            DemonDespawn();
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->SetHomePosition(KlinfranHomePos);

        // TODO(classic): VMaNGOS scales this 3 hour respawn down when the active session count exceeds BLIZZLIKE_REALM_POPULATION
        // (dynamic respawn scaling). TC master has its own dynamic respawn system (worldserver.conf Respawn.Dynamic*); fixed 3h here.
        uint32 const respawnDelay = 3 * HOUR;

        me->SetRespawnDelay(respawnDelay);
        me->SetRespawnTime(respawnDelay);
        me->SaveRespawnTime();
    }

    void DemonDespawn(bool triggered = true)
    {
        me->SetHomePosition(KlinfranHomePos);
        me->SetRespawnDelay(15 * MINUTE);
        me->SetRespawnTime(15 * MINUTE);
        me->SaveRespawnTime();

        if (triggered)
        {
            if (Creature* cleaner = me->SummonCreature(NPC_THE_CLEANER, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 20min))
            {
                for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                {
                    if (Unit* unit = ref->GetVictim())
                    {
                        if (unit->IsAlive())
                        {
                            cleaner->SetInCombatWith(unit);
                            cleaner->GetThreatManager().AddThreat(unit, 0.0f);
                            cleaner->AI()->AttackStart(unit);
                        }
                    }
                }
            }
        }

        me->DespawnOrUnsummon();
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_SCORPID_STING_RANK_4)
        {
            me->RemoveAurasDueToSpell(SPELL_DEMONIC_FRENZY);
            DoCastSelf(SPELL_ENTROPIC_STING, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        /** Franklin the Friendly */
        if (_transform)
        {
            if (_transformEmoteTimer)
            {
                if (_transformEmoteTimer <= diff)
                {
                    me->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
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

        /** Klinfran the Crazed */
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

        if (!UpdateVictim())
            return;

        if (me->GetThreatManager().GetThreatListSize() > 1)
        {
            DemonDespawn();
            return;
        }

        if (_demonicFrenzyTimer < diff)
        {
            if (DoCastSelf(SPELL_DEMONIC_FRENZY) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_FRENZY, me);
                _demonicFrenzyTimer = 15000;
            }
        }
        else
            _demonicFrenzyTimer -= diff;
    }

private:
    uint32 _transformTimer;
    uint32 _transformEmoteTimer;
    bool _transform;

    ObjectGuid _hunterGuid;
    uint32 _demonicFrenzyTimer;
    uint32 _despawnTimer;
};

void AddSC_classic_burning_steppes()
{
    RegisterCreatureAI(classic_npc_grark_lorkrub);
    RegisterCreatureAI(classic_npc_klinfran);
}
