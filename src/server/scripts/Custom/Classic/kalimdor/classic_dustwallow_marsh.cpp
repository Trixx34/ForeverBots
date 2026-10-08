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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/dustwallow_marsh/dustwallow_marsh.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_archmage_tervosh (1265), npc_lady_jaina_proudmoore (558), npc_stinky_ignatz (1222 / 1270), npc_emberstrife

#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_script_text.h"

namespace
{
struct ClassicEscortPoint
{
    float X, Y, Z, O;
    uint32 WaitMs;
};

// VMaNGOS npc_escortAI reads its path from world.script_waypoint (by creature entry). TC EscortAI needs the path
// supplied by the script, so the VMaNGOS rows are embedded here (node ids are 0-based and contiguous, as TC requires).
template <std::size_t N, typename RunPredicate>
void LoadClassicEscortPath(EscortAI* ai, ClassicEscortPoint const (&points)[N], RunPredicate isRunning)
{
    ai->ResetPath();
    for (uint32 i = 0; i < N; ++i)
    {
        Optional<Milliseconds> waitTime;
        if (points[i].WaitMs)
            waitTime = Milliseconds(points[i].WaitMs);
        ai->AddWaypoint(i, points[i].X, points[i].Y, points[i].Z, points[i].O, waitTime, isRunning(i));
    }
}

// VMaNGOS script_waypoint entry 4880 (pointid 0..24, stored 0-based here)
ClassicEscortPoint const EscortPath4880[] =
{
    { -2646.43f, -3436.07f, 35.3732f, 3.675f, 0 },  // 0
    { -2682.3f, -3457.25f, 34.7593f, 3.675f, 0 },  // 1
    { -2713.53f, -3457.03f, 34.2385f, 3.1345f, 0 },  // 2
    { -2725.97f, -3458.33f, 34.5506f, 3.2457f, 0 },  // 3
    { -2758.55f, -3460.63f, 31.5362f, 3.2121f, 3000 },  // 4
    { -2776.27f, -3484.48f, 32.2331f, 4.0734f, 0 },  // 5
    { -2786.71f, -3492.94f, 31.0815f, 3.8226f, 0 },  // 6
    { -2792.46f, -3492.61f, 30.9467f, 3.0843f, 0 },  // 7
    { -2796.86f, -3487.91f, 30.6505f, 2.3232f, 5000 },  // 8
    { -2788.68f, -3502.16f, 30.8896f, 5.2335f, 0 },  // 9
    { -2788.7f, -3510.47f, 30.8427f, 4.71f, 0 },  // 10
    { -2795.91f, -3521.22f, 31.1912f, 4.1216f, 0 },  // 11
    { -2806.05f, -3515.57f, 29.5638f, 2.6332f, 0 },  // 12
    { -2809.99f, -3513.27f, 30.3906f, 2.6132f, 0 },  // 13
    { -2826.27f, -3531.41f, 33.0932f, 3.981f, 0 },  // 14
    { -2830.66f, -3567.24f, 29.6191f, 4.5905f, 0 },  // 15
    { -2815.27f, -3566.94f, 30.3434f, 0.0195f, 3000 },  // 16
    { -2819.56f, -3591.9f, 30.3798f, 4.5422f, 0 },  // 17
    { -2820.35f, -3594.76f, 31.1295f, 4.4429f, 3000 },  // 18
    { -2838.22f, -3589.1f, 35.6264f, 2.8349f, 0 },  // 19
    { -2848.4f, -3571.17f, 37.6296f, 2.0872f, 0 },  // 20
    { -2855.69f, -3557.87f, 40.0811f, 2.0722f, 0 },  // 21
    { -2864.5f, -3550.97f, 41.4712f, 2.4772f, 0 },  // 22
    { -2885.06f, -3535.7f, 34.445f, 2.5028f, 0 },  // 23
    { -2894.79f, -3540.09f, 34.2761f, 3.5654f, 0 },  // 24: end
};
}

//-----------------------------------------------------------------------------
// Full quest event implementation (Missing Diplomat part 14 id:1265).
// Author: Kampeador
//-----------------------------------------------------------------------------

enum ArchmageTervosh
{
    QUEST_MISSING_DIPLO_PT14        = 1265,
    QUEST_MISSING_DIPLO_PT16        = 1324,
    TERVOSH_SPAWN_DURATION          = 60000, // 60 sec blizzlike value
    TERVOSH_SAY_ON_QUEST_MD_PT14    = 1751,  // Go with grace, and may the Lady's magic protect you.
    NPC_SENTRY_POINT_GUARD          = 5085,  // all guards around Tervosh will salute.
    NPC_TERVOSH                     = 4967,

    SPELL_TELEPORT_VISUAL1          = 7141,  // A visual spell effect when Tervosh arrives
    SPELL_TELEPORT_VISUAL2          = 7077,  // A visual spell effect when Tervosh returns back
    SPELL_PROUDMOORES_DEFENSE       = 7120,  // player characters will receive a buff, once quest is completed

    // script phases:
    MDQP_PREPARE_TO_ARRIVE          = 0, // Very rare. It occurs when a new player arrives during MDQP_TELEPORT_BACK in previous event.
    MDQP_ARRIVE                     = 1, // Tervosh arrives.
    MDQP_GUARDS_SALUTE              = 2, // All Sentry Point guards withing 10 yards start facing Tervosh and use EMOTE_ONESHOT_SALUTE emote
    MDQP_GUARDS_RESTORE_MOVEMENT    = 3, // Currently unused, core does this automatically.
    MDQP_WAITING                    = 4, // Wait for despawn
    MDQP_TELEPORT_BACK              = 5, // Tervosh despawn during this phase

    // Interface for the VMaNGOS area trigger script "at_sentry_point" (not part of this port):
    ACTION_TERVOSH_START_EVENT          = 1, // m_eventStarted = true
    ACTION_TERVOSH_START_EVENT_DELAYED  = 2, // invisible, phase MDQP_PREPARE_TO_ARRIVE after 3 s, m_eventStarted = true
    ACTION_TERVOSH_RESET_DESPAWN_DELAY  = 3, // resetDespawnDelay()
    DATA_TERVOSH_PHASE                  = 1  // getCurrentPhase()
};

// TODO(classic): the event itself is started by the VMaNGOS area trigger script "at_sentry_point" (completes quest 1265,
// summons Tervosh at (-3476.860840, -4106.740723, 17.107151, 5.420159) with UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_IMMUNE_TO_NPC). That AT script is not
// part of this port; it can drive this AI through DoAction()/GetData() with the ACTION_TERVOSH_* / DATA_TERVOSH_PHASE ids.
struct classic_npc_archmage_tervosh : public ScriptedAI
{
    classic_npc_archmage_tervosh(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _eventStarted = false;
        _eventPhase = MDQP_ARRIVE;
        _nextPhaseDelayTimer = 1000; // delay for the visual effect of the arrival
        _despawnDelayTimer = TERVOSH_SPAWN_DURATION;
    }

    void Reset() override
    {
        Initialize();
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_TERVOSH_START_EVENT:
                _eventStarted = true;
                break;
            case ACTION_TERVOSH_START_EVENT_DELAYED:
                me->SetVisible(false);
                // set inital phase to MDQP_PREPARE_TO_ARRIVE
                _nextPhaseDelayTimer = 3000;
                _eventPhase = MDQP_PREPARE_TO_ARRIVE;
                _eventStarted = true;
                break;
            case ACTION_TERVOSH_RESET_DESPAWN_DELAY:
                // used on area-trigger: if a new player arrives, reset event duration.
                _despawnDelayTimer = TERVOSH_SPAWN_DURATION;
                break;
            default:
                break;
        }
    }

    uint32 GetData(uint32 id) const override
    {
        if (id == DATA_TERVOSH_PHASE)
            return _eventPhase;
        return 0;
    }

    void OnQuestReward(Player* player, Quest const* quest, LootItemType /*type*/, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() == QUEST_MISSING_DIPLO_PT14)
        {
            // Tervosh says: Go with grace, and may the Lady's magic protect you.
            ClassicScriptText(TERVOSH_SAY_ON_QUEST_MD_PT14, me);

            // rare case: if two players are completing this quest at the same time, then only the last one will receive a buff.
            // i am pretty sure that, this bug existed on retail, so handle this case, by making cast triggered/instant.
            me->CastSpell(player, SPELL_PROUDMOORES_DEFENSE, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_eventStarted)
        {
            ScriptedAI::UpdateAI(diff);
            return;
        }

        switch (_eventPhase)
        {
            case MDQP_PREPARE_TO_ARRIVE:
                if (_nextPhaseDelayTimer < diff)
                {
                    me->SetVisible(true);

                    // switch phase
                    _nextPhaseDelayTimer = 1000; // delay for the visual effect of the arrival
                    _eventPhase = MDQP_ARRIVE;
                }
                else
                    _nextPhaseDelayTimer -= diff;
                break;
            case MDQP_ARRIVE:
                // use a visual effect with 1 sec delay
                if (_nextPhaseDelayTimer < diff)
                {
                    DoCastSelf(SPELL_TELEPORT_VISUAL1);

                    // switch phase
                    _nextPhaseDelayTimer = 2000; // salute guards 2 sec delay
                    _eventPhase = MDQP_GUARDS_SALUTE;
                }
                else
                    _nextPhaseDelayTimer -= diff;
                break;
            case MDQP_GUARDS_SALUTE:
                if (_nextPhaseDelayTimer < diff)
                {
                    // All guards salute withing 10 yards.
                    std::list<Creature*> guards;
                    me->GetCreatureListWithEntryInGrid(guards, NPC_SENTRY_POINT_GUARD, 10.0f);

                    for (Creature* guard : guards)
                    {
                        if (!guard->IsInCombat() && guard->IsAlive())
                        {
                            guard->StopMoving(); // Movement will be restored automatically in the core
                            guard->SetFacingToObject(me);
                            guard->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
                        }
                    }

                    _eventPhase = MDQP_WAITING;
                }
                else
                    _nextPhaseDelayTimer -= diff;
                break;
            case MDQP_WAITING:
                // possible add a visual effect
                if (_despawnDelayTimer < diff)
                {
                    // cast a visual spell effect
                    DoCastSelf(SPELL_TELEPORT_VISUAL2);

                    // switch phase, delay despawn by 2 seconds, 1 sec teleport visual, 1 sec latency.
                    _nextPhaseDelayTimer = 2000;
                    _eventPhase = MDQP_TELEPORT_BACK;
                }
                else
                    _despawnDelayTimer -= diff;
                break;
            case MDQP_TELEPORT_BACK:
                if (_nextPhaseDelayTimer < diff)
                    me->DespawnOrUnsummon(); // VMaNGOS TemporarySummon::UnSummon()
                else
                    _nextPhaseDelayTimer -= diff;
                break;
            default:
                break;
        }
    }

private:
    bool _eventStarted;
    uint32 _despawnDelayTimer;  // once this timer expires, NPC will despawn.
    uint32 _eventPhase;
    uint32 _nextPhaseDelayTimer;
};

/*######
## npc_lady_jaina_proudmoore
######*/

enum LadyJainaProudmoore
{
    QUEST_JAINAS_AUTOGRAPH          = 558,
    QUEST_MISSING_DIPLO_PT17        = 1267,
    NPC_SUMMONED_WATER_ELEMENTAL    = 10955,
    SPELL_JAINA_FIREBALL            = 20678,
    SPELL_JAINA_FIREBLAST           = 20679,
    SPELL_JAINA_BLIZZARD            = 20680,
    SPELL_JAINA_WATER_ELEMENTAL     = 20681,
    SPELL_JAINA_TELEPORT            = 20682,
    SPELL_JAINAS_AUTOGRAPH          = 23122,

    SOUND_JAINA_AGGRO               = 5882,

    NPC_TEXT_JAINA_HENDEL_CUSTODY   = 3158, // Hendel is in our custody now...
    NPC_TEXT_JAINA_WELCOME          = 3157, // I welcome you to Theramore...
    NPC_TEXT_JAINA_AUTOGRAPH        = 7012
};

#define GOSSIP_ITEM_JAINA "I know this is rather silly but i have a young ward who is a bit shy and would like your autograph."

struct classic_npc_lady_jaina_proudmoore : public ScriptedAI
{
    classic_npc_lady_jaina_proudmoore(Creature* creature) : ScriptedAI(creature), _spellTimer(3000), _specialTimer(15000) { }

    void Reset() override
    {
        _spellTimer = 3000;
        _specialTimer = 15000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->PlayDistanceSound(SOUND_JAINA_AGGRO);
    }

    bool HasWaterElemental()
    {
        std::list<TempSummon*> elementals;
        me->GetAllMinionsByEntry(elementals, NPC_SUMMONED_WATER_ELEMENTAL);
        return !elementals.empty();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || me->IsNonMeleeSpellCast(false))
            return;

        if (_specialTimer < diff)
        {
            if (!urand(0, 4) && !HasWaterElemental())
            {
                if (DoCastSelf(SPELL_JAINA_WATER_ELEMENTAL) == SPELL_CAST_OK)
                    _specialTimer = urand(10, 20) * IN_MILLISECONDS;
            }
            else
            {
                Unit* oldVictim = me->GetVictim();
                if (DoCastVictim(SPELL_JAINA_TELEPORT) == SPELL_CAST_OK)
                {
                    _specialTimer = urand(10, 30) * IN_MILLISECONDS;

                    if (me->GetDistance2d(-4018.1f, -4525.24f) > 40.0f && oldVictim)
                    {
                        // If we don't remove target from threat list after teleporting,
                        // Jaina will try to chase him and evade despite having other targets.
                        me->GetThreatManager().ClearThreat(oldVictim);
                    }
                }
            }
        }
        else
            _specialTimer -= diff;

        if (_spellTimer < diff)
        {
            switch (urand(0, 4))
            {
                case 0:
                case 1:
                    if (DoCastVictim(SPELL_JAINA_FIREBALL) == SPELL_CAST_OK)
                        _spellTimer = urand(3, 10) * IN_MILLISECONDS;
                    break;
                case 2:
                case 3:
                    if (DoCastVictim(SPELL_JAINA_FIREBLAST) == SPELL_CAST_OK)
                        _spellTimer = urand(3, 10) * IN_MILLISECONDS;
                    break;
                case 4:
                    // VMaNGOS SelectRandomUnfriendlyTarget(nullptr, 25.0f); TC picks from the threat list
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 25.0f))
                    {
                        if (DoCast(target, SPELL_JAINA_BLIZZARD) == SPELL_CAST_OK)
                            _spellTimer = urand(1, 3) * IN_MILLISECONDS;
                    }
                    break;
                default:
                    break;
            }
        }
        else
            _spellTimer -= diff;
    }

    bool OnGossipHello(Player* player) override
    {
        if (me->IsQuestGiver())
            player->PrepareQuestMenu(me->GetGUID());

        if (player->GetQuestStatus(QUEST_JAINAS_AUTOGRAPH) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ITEM_JAINA, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        // Correct gossip text depends on The Missing Diplomat quest chain progression.
        if (player->GetQuestStatus(QUEST_MISSING_DIPLO_PT17) == QUEST_STATUS_COMPLETE)
            SendGossipMenuFor(player, NPC_TEXT_JAINA_HENDEL_CUSTODY, me->GetGUID());
        else if (player->GetQuestStatus(QUEST_MISSING_DIPLO_PT16) == QUEST_STATUS_COMPLETE)
            player->SendPreparedQuest(me);
        else
            SendGossipMenuFor(player, NPC_TEXT_JAINA_WELCOME, me->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        if (action == GOSSIP_ACTION_INFO_DEF)
        {
            SendGossipMenuFor(player, NPC_TEXT_JAINA_AUTOGRAPH, me->GetGUID());
            player->CastSpell(player, SPELL_JAINAS_AUTOGRAPH, false);
        }
        return true;
    }

private:
    uint32 _spellTimer;
    uint32 _specialTimer;
};

/*####
# Alita
# npc_stinky_ignatz
####*/

enum StinkyIgnatz
{
    QUEST_STINKYS_ESCAPE_A      = 1222,
    QUEST_STINKYS_ESCAPE_H      = 1270,
    SAY_IGNATZ_START            = 1610,
    SAY_IGNATZ_0                = 1611,
    SAY_IGNATZ_1                = 1612,
    SAY_IGNATZ_2                = 1614,
    SAY_IGNATZ_3                = 1615,
    SAY_IGNATZ_4                = 1617,
    SAY_IGNATZ_END              = 1618,
    SAY_IGNATZ_AGGRO_1          = 1630,
    SAY_IGNATZ_AGGRO_2          = 1631,
    GOBJ_BOGBEAN_PLANT          = 20939,

    FACTION_STINKY_ESCORT       = 113
};

struct classic_npc_stinky_ignatz : public EscortAI
{
    classic_npc_stinky_ignatz(Creature* creature) : EscortAI(creature), _currWaypoint(0), _timer(21000) { }

    void Reset() override { }

    void JustAppeared() override
    {
        _currWaypoint = 0;
        _timer = 21000;
        me->RestoreFaction();
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_STINKYS_ESCAPE_A && quest->GetQuestId() != QUEST_STINKYS_ESCAPE_H)
            return;

        me->SetFaction(FACTION_STINKY_ESCORT);
        me->SetStandState(UNIT_STAND_STATE_STAND);
        LoadClassicEscortPath(this, EscortPath4880, [](uint32) { return false; });
        Start(true, player->GetGUID(), quest);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        _currWaypoint = waypointId;

        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_IGNATZ_START, me);
                break;
            case 4:
                ClassicScriptText(SAY_IGNATZ_0, me);
                break;
            case 8:
                ClassicScriptText(SAY_IGNATZ_1, me);
                break;
            case 16:
                _timer = 4000;
                if (GameObject* go = me->FindNearestGameObject(GOBJ_BOGBEAN_PLANT, 40.0f, false))
                    if (!go->isSpawned())
                        go->Respawn();
                break;
            case 18:
                _timer = 2000;
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
                break;
            case 24:
                if (Player* player = GetPlayerForEscort())
                {
                    ClassicScriptText(SAY_IGNATZ_END, me, player);
                    if (player->GetQuestStatus(QUEST_STINKYS_ESCAPE_A) == QUEST_STATUS_INCOMPLETE)
                        player->GroupEventHappens(QUEST_STINKYS_ESCAPE_A, me);
                    else if (player->GetQuestStatus(QUEST_STINKYS_ESCAPE_H) == QUEST_STATUS_INCOMPLETE)
                        player->GroupEventHappens(QUEST_STINKYS_ESCAPE_H, me);
                }
                break;
            default:
                break;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        // not always
        if (urand(0, 2))
            return;

        if (_currWaypoint < 15)
            ClassicScriptText(SAY_IGNATZ_AGGRO_1, me, who);
        else
            ClassicScriptText(SAY_IGNATZ_AGGRO_2, me, who);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->GetVictim())
        {
            if (_timer < 20000)
            {
                switch (_currWaypoint)
                {
                    case 16:
                        if (_timer < diff)
                        {
                            if (Player* player = GetPlayerForEscort())
                                ClassicScriptText(SAY_IGNATZ_3, me, player);
                            _timer = 21000;
                        }
                        else
                        {
                            if (_timer >= 1000 && _timer < 1000 + diff)
                                ClassicScriptText(SAY_IGNATZ_2, me);
                            _timer -= diff;
                        }
                        break;
                    case 18:
                        if (_timer < diff)
                        {
                            ClassicScriptText(SAY_IGNATZ_4, me);
                            _timer = 21000;
                        }
                        else
                        {
                            if (_timer >= 1000 && _timer < 1000 + diff)
                            {
                                if (GameObject* go = me->FindNearestGameObject(GOBJ_BOGBEAN_PLANT, 10.0f))
                                    go->DespawnOrUnsummon();
                                me->SetStandState(UNIT_STAND_STATE_STAND);
                            }
                            _timer -= diff;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        EscortAI::UpdateAI(diff);
    }

private:
    uint32 _currWaypoint;
    uint32 _timer;
};

/*
 * Emberstrife
 */

enum Emberstrife
{
    EMOTE_GENERIC_FRENZY_KILL   = 7797,
    EMOTE_GENERIC_IS_WEAKENED   = 11476,

    SPELL_FRENZY                = 8269,
    SPELL_CLEAVE                = 19983,
    SPELL_FLAME_BREATH          = 9573
};

struct classic_npc_emberstrife : public ScriptedAI
{
    classic_npc_emberstrife(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _cleaveTimer = urand(6000, 8000);
        _frenzyTimer = 0;
        _flameBreathTimer = urand(8000, 12000);
        _weakened = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        if (!_weakened && me->GetHealthPct() < 11.0f)
        {
            _weakened = true;
            ClassicScriptText(EMOTE_GENERIC_IS_WEAKENED, me);
        }

        // Cleave
        if (_cleaveTimer < diff)
        {
            if (DoCastVictim(SPELL_CLEAVE) == SPELL_CAST_OK)
                _cleaveTimer = urand(6000, 8000);
        }
        else
            _cleaveTimer -= diff;

        // Flame Breath
        if (_flameBreathTimer < diff)
        {
            if (DoCastVictim(SPELL_FLAME_BREATH) == SPELL_CAST_OK)
                _flameBreathTimer = urand(8000, 12000);
        }
        else
            _flameBreathTimer -= diff;

        // Frenzy
        if (me->GetHealthPct() < 60.0f)
        {
            if (_frenzyTimer < diff)
            {
                if (DoCastSelf(SPELL_FRENZY) == SPELL_CAST_OK)
                {
                    ClassicScriptText(EMOTE_GENERIC_FRENZY_KILL, me);
                    _frenzyTimer = 2 * MINUTE * IN_MILLISECONDS + 500;
                }
            }
            else
                _frenzyTimer -= diff;
        }
    }

private:
    uint32 _cleaveTimer;
    uint32 _frenzyTimer;
    uint32 _flameBreathTimer;
    bool _weakened;
};

void AddSC_classic_dustwallow_marsh()
{
    RegisterCreatureAI(classic_npc_archmage_tervosh);
    RegisterCreatureAI(classic_npc_lady_jaina_proudmoore);
    RegisterCreatureAI(classic_npc_stinky_ignatz);
    RegisterCreatureAI(classic_npc_emberstrife);
}
