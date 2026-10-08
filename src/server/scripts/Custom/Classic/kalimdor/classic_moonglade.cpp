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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/moonglade/moonglade.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_keeper_remulos (8736 Nightmare Manifests, 8447 Waking Legends)

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
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

// VMaNGOS script_waypoint entry 11832 (pointid 0..32, stored 0-based here)
ClassicEscortPoint const EscortPath11832[] =
{
    { 7848.39f, -2216.36f, 470.888f, 0.0f, 15000 },  // 0: SAY_REMULOS_INTRO_1
    { 7848.39f, -2216.36f, 470.888f, 0.0f, 5000 },  // 1: SAY_REMULOS_INTRO_2
    { 7829.79f, -2244.84f, 463.853f, 4.1338f, 0 },  // 2
    { 7819.01f, -2304.34f, 455.957f, 4.5332f, 0 },  // 3
    { 7931.1f, -2314.35f, 473.054f, 6.1941f, 0 },  // 4
    { 7943.55f, -2324.69f, 477.677f, 5.5901f, 0 },  // 5
    { 7952.02f, -2351.14f, 485.235f, 5.0223f, 0 },  // 6
    { 7963.67f, -2412.99f, 488.953f, 4.8986f, 0 },  // 7
    { 7975.18f, -2551.6f, 490.08f, 4.7952f, 0 },  // 8
    { 7948.05f, -2570.83f, 489.751f, 3.7582f, 0 },  // 9
    { 7947.16f, -2583.4f, 490.066f, 4.6417f, 0 },  // 10
    { 7951.09f, -2596.22f, 489.831f, 5.0098f, 0 },  // 11
    { 7948.27f, -2610.06f, 492.34f, 4.5114f, 0 },  // 12
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 14000 },  // 13: SAY_REMULOS_INTRO_3
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 12000 },  // 14: SAY_REMULOS_INTRO_4
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 5000 },  // 15: SAY_REMULOS_INTRO_5
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 13000 },  // 16: SPELL_CONJURE_RIFT
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 11000 },  // 17: SAY_ERANIKUS_SPAWN
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 5000 },  // 18: SAY_REMULOS_TAUNT_1
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 3000 },  // 19: EMOTE_ERANIKUS_LAUGH
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 10000 },  // 20: SAY_ERANIKUS_TAUNT_2
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 12000 },  // 21: SAY_REMULOS_TAUNT_3
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 6000 },  // 22: SAY_ERANIKUS_TAUNT_4
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 7000 },  // 23: EMOTE_ERANIKUS_ATTACK
    { 7928.52f, -2625.95f, 492.448f, 3.8191f, 0 },  // 24: SAY_REMULOS_DEFEND_1 - eranikus flies up
    { 7948.27f, -2610.06f, 492.34f, 0.6775f, 0 },  // 25
    { 7952.32f, -2594.12f, 490.07f, 1.322f, 0 },  // 26
    { 7913.99f, -2567.0f, 488.331f, 2.5258f, 0 },  // 27
    { 7835.45f, -2571.1f, 489.289f, 3.1937f, 6000 },  // 28: SAY_REMULOS_DEFEND_2
    { 7835.45f, -2571.1f, 489.289f, 3.1937f, 4000 },  // 29: SAY_ERANIKUS_SHADOWS
    { 7835.45f, -2571.1f, 489.289f, 3.1937f, 0 },  // 30: SAY_REMULOS_DEFEND_3 - escort paused
    { 7897.28f, -2560.65f, 487.461f, 0.1674f, 0 },  // 31
    { 7897.28f, -2560.65f, 487.461f, 0.1674f, 0 },  // 32
};
}

/*######
 ## npc_keeper_remulos
 ######*/

enum KeeperRemulosData
{
    SPELL_CONJURE_RIFT           = 25813, // summon Eranikus
    SPELL_HEALING_TOUCH          = 23381,
    SPELL_REGROWTH               = 20665,
    SPELL_REJUVENATION           = 20664,
    SPELL_STARFIRE               = 21668,
    SPELL_ERANIKUS_REDEEMED      = 25846, // transform Eranikus
  //SPELL_MOONGLADE_TRANQUILITY  = unk,   // spell which acts as a spotlight over Eranikus after he is redeemed
    SPELL_THROW_NIGHTMARE_OBJECT = 25004,
    SPELL_ERANIKUS_HOVER         = 17131,
    SPELL_MALFURION_AURA_1       = 10665,
    SPELL_MALFURION_AURA_2       = 24999,

    NPC_ERANIKUS_TYRANT     = 15491,
    NPC_NIGHTMARE_PHANTASM  = 15629, // shadows summoned during the event - should cast 17228 and 21307
    NPC_REMULOS             = 11832,
    NPC_TYRANDE_WHISPERWIND = 15633, // appears with the priestess during the event to help the players - should cast healing spells
    NPC_ELUNE_PRIESTESS     = 15634,
    NPC_MALFURION           = 15362,

    QUEST_NIGHTMARE_MANIFESTS = 8736,
    QUEST_WAKING_LEGENDS      = 8447,

    FACTION_REMULOS_EVENT     = 1254, // Alita: stop Remulos from healing shades (cenarion circle, enemies with group 8)

    // yells -> in cronological order
    SAY_REMULOS_INTRO_1    = 11282, // remulos intro
    SAY_REMULOS_INTRO_2    = 11283,
    SAY_REMULOS_INTRO_3    = 11290,
    SAY_REMULOS_INTRO_4    = 11291,
    SAY_REMULOS_INTRO_5    = 11292,
    EMOTE_SUMMON_ERANIKUS  = 11277, // eranikus spawn - world emote
    SAY_ERANIKUS_SPAWN     = 11030,
    SAY_REMULOS_TAUNT_1    = 11293, // eranikus and remulos chat
    EMOTE_ERANIKUS_LAUGH   = 11296,
    SAY_ERANIKUS_TAUNT_2   = 11294,
    SAY_REMULOS_TAUNT_3    = 11295,
    SAY_ERANIKUS_TAUNT_4   = 11297,
    EMOTE_ERANIKUS_ATTACK  = 11298, // start attack
    SAY_REMULOS_DEFEND_1   = 11300,
    SAY_REMULOS_DEFEND_2   = 11301,
    SAY_ERANIKUS_SHADOWS   = 11299,
    SAY_REMULOS_DEFEND_3   = 11302,
    SAY_ERANIKUS_ATTACK_1  = 11304,
    SAY_ERANIKUS_ATTACK_2  = 11305,
    SAY_ERANIKUS_ATTACK_3  = 11306,
    SAY_REMULOS_OUTRO_1    = 11303, // remulos outro
    SAY_REMULOS_OUTRO_2    = 11329,
    // Texts Waking_Legends quest
    SAY_REMULOS_1          = 10866,
    SAY_REMULOS_2          = 10867,
    SAY_REMULOS_3          = 10868,
    SAY_REMULOS_4          = 10870,
    SAY_REMULOS_5          = 10872,
    SAY_REMULOS_6          = 10874,
    SAY_REMULOS_7          = 10877,
    SAY_REMULOS_8          = 10879,
    SAY_MALFURION_1        = 10869,
    SAY_MALFURION_2        = 10871,
    SAY_MALFURION_3        = 10873,
    SAY_MALFURION_4        = 10876,
    SAY_MALFURION_5        = 10878,

    POINT_ID_ERANIKUS_FLIGHT   = 0,
    POINT_ID_ERANIKUS_COMBAT   = 1,
    POINT_ID_ERANIKUS_REDEEMED = 2,

    MAX_SHADOWS      = 4, // the max shadows summoned per turn
    MAX_SUMMON_TURNS = 10, // There are about 10 summoned shade waves

    // Called by a (future) boss_eranikus port when Eranikus is redeemed (VMaNGOS npc_keeper_remulosAI::DoHandleOutro)
    ACTION_REMULOS_OUTRO = 1
};

struct EventLocations
{
    float m_fX, m_fY, m_fZ, m_fO;
};

// Waking Legends Quest
static EventLocations const RemulosLocations[] =
{
    { 7828.177246f, -2246.510010f, 463.565979f, 4.57f },
    { 7817.905762f, -2303.467529f, 456.028320f, 3.58f },
    { 7772.034668f, -2325.162354f, 454.414551f, 3.58f },
    { 7753.641113f, -2305.021973f, 456.996155f, 3.41f },
    { 7749.481934f, -2304.861328f, 455.891174f, 3.49f }, // Remulos final lake location
    { 7848.299805f, -2216.350098f, 470.888000f, 4.05f }, // Remulos initial location
};
// End Waking Legends Quest

static EventLocations const EranikusLocations[] =
{
    { 7881.72f, -2651.23f, 493.29f, 0.40f }, // eranikus spawn loc
    { 7929.86f, -2574.88f, 505.35f, 0.0f },  // eranikus flight move loc
    { 7912.98f, -2568.99f, 488.71f, 0.0f },  // eranikus combat move loc
    { 7906.57f, -2565.63f, 488.39f, 0.0f },  // eranikus redeemed loc
};

static EventLocations const ShadowsLocations[] =
{
    // Inside the house shades - first wave only
    { 7832.78f, -2604.57f, 489.29f, 0.0f },
    { 7826.68f, -2538.46f, 489.30f, 0.0f },
    { 7811.48f, -2573.20f, 488.49f, 0.0f },
    // Outside shade points - basically only the first set of coords is used for the summoning; there is no solid proof of using the other coords
    { 7888.32f, -2566.25f, 487.02f, 0.0f },
    { 7946.12f, -2577.10f, 489.97f, 0.0f }, // saw this in a vid... Alita. My theory : 3 spawn points, + one spawn point being near the player(or Remulos, in the vids they stick together..).
    { 7963.00f, -2492.03f, 487.84f, 0.0f }  // Alita, actually yes.
};

// TODO(classic): boss_eranikus (15491) is a separate VMaNGOS script and not part of this port. It drives the second half of
// the Nightmare Manifests event (Tyrande, priestesses, redemption) and calls DoHandleOutro() -> here DoAction(ACTION_REMULOS_OUTRO).
// VMaNGOS SummonedMovementInform (Eranikus reaching his flight/combat points) has no TC hook: it is emulated by polling the
// summoned Eranikus' movement in UpdateEscortAI().
struct classic_npc_keeper_remulos : public EscortAI
{
    classic_npc_keeper_remulos(Creature* creature) : EscortAI(creature), _questActive(0)
    {
        Initialize();
    }

    void Initialize()
    {
        _outroTimer = 0;
        _outroPhase = 0;
        _summonCount = 0;

        _eranikusGUID.Clear();
        _eranikusPendingPoint = -1;

        _shadeSummonTimer = 0;
        _healTimer = 10000;
        _starfireTimer = 25000;

        _isFirstWave = true;

        _transitionTimer = 0;

        _eventWLStarted = false;
        _sayingTimeTimer = 0;
        _castSpellTimer = 0;
        _summonMalfurionTimer = 0;
        _returnInitialPosition = false;
        _initialPositionTimer = 0;
        _questCompleteTimer = 0;

        _malfurionGUID.Clear();

        for (uint32& timer : _tabMovementsTimer)
            timer = 0;
        for (uint32& timer : _tabDialogsTimer)
            timer = 0;
    }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            Initialize();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        _questActive = quest->GetQuestId();

        if (quest->GetQuestId() == QUEST_NIGHTMARE_MANIFESTS)
        {
            // avoid starting the escort twice
            me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
            LoadClassicEscortPath(this, EscortPath11832, [](uint32) { return false; });
            Start(true, player->GetGUID(), quest);
            SetDespawnAtFar(false); // VMaNGOS SetMaxPlayerDistance(0) disables the distance check
        }
        if (quest->GetQuestId() == QUEST_WAKING_LEGENDS)
        {
            me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
            LoadClassicEscortPath(this, EscortPath11832, [](uint32) { return false; });
            Start(true, player->GetGUID(), quest);
        }
    }

    // Remulos follows player
    void EnterEvadeMode(EvadeReason why) override
    {
        EscortAI::EnterEvadeMode(why);
        if (HasEscortState(STATE_ESCORT_ESCORTING) && !_isFirstWave)
        {
            if (Player* player = GetPlayerForEscort())
            {
                if (_questActive == QUEST_NIGHTMARE_MANIFESTS && me->IsWithinDistInMap(player, 200.0f))
                    me->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
            }
        }
    }

    // VMaNGOS EffectDummyCreature_conjure_rift (SPELL_CONJURE_RIFT, effect 0)
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_CONJURE_RIFT && caster)
            caster->SummonCreature(NPC_ERANIKUS_TYRANT, EranikusLocations[0].m_fX, EranikusLocations[0].m_fY, EranikusLocations[0].m_fZ, EranikusLocations[0].m_fO, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600s);
    }

    void JustSummoned(Creature* summoned) override
    {
        switch (summoned->GetEntry())
        {
            case NPC_ERANIKUS_TYRANT:
                _eranikusGUID = summoned->GetGUID();
                // Make Eranikus unattackable first
                summoned->AddAura(SPELL_ERANIKUS_HOVER, summoned); // hover
                summoned->SetCanFly(true);
                summoned->SetDisableGravity(true);
                summoned->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE); // VMaNGOS UNIT_FLAG_SPAWNING
                summoned->SetRespawnDelay(DAY);
                break;
            case NPC_NIGHTMARE_PHANTASM:
                summoned->AI()->AttackStart(me);
                summoned->SetRespawnDelay(DAY);
                break;
            case NPC_MALFURION:
                _malfurionGUID = summoned->GetGUID();
                summoned->AddAura(SPELL_MALFURION_AURA_1, summoned);
                summoned->AddAura(SPELL_MALFURION_AURA_2, summoned);
                break;
            default:
                break;
        }
    }

    void MoveEranikus(Creature* eranikus, uint32 pointId, EventLocations const& loc)
    {
        eranikus->GetMotionMaster()->MovePoint(pointId, loc.m_fX, loc.m_fY, loc.m_fZ);
        _eranikusPendingPoint = int32(pointId);
        _eranikusPendingPos = Position(loc.m_fX, loc.m_fY, loc.m_fZ);
    }

    // VMaNGOS SummonedMovementInform for Eranikus
    void EranikusMovementInform(Creature* summoned, uint32 pointId)
    {
        if (_questActive != QUEST_NIGHTMARE_MANIFESTS)
            return;

        switch (pointId)
        {
            case POINT_ID_ERANIKUS_FLIGHT:
                // Set Eranikus to face Remulos
                summoned->SetFacingToObject(me);
                break;
            case POINT_ID_ERANIKUS_COMBAT:
                // Start attack
                summoned->SetCanFly(false);
                summoned->SetDisableGravity(false);
                summoned->HandleEmoteCommand(EMOTE_ONESHOT_LAND);
                ClassicScriptText(SAY_ERANIKUS_ATTACK_2, summoned);
                _transitionTimer = 1000;
                break;
            default:
                break;
        }
    }

    void PollEranikusMovement()
    {
        if (_eranikusPendingPoint < 0)
            return;

        Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID);
        if (!eranikus)
        {
            _eranikusPendingPoint = -1;
            return;
        }

        if (eranikus->GetExactDist(&_eranikusPendingPos) < 2.0f || eranikus->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        {
            uint32 pointId = uint32(_eranikusPendingPoint);
            _eranikusPendingPoint = -1;
            EranikusMovementInform(eranikus, pointId);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_questActive == QUEST_NIGHTMARE_MANIFESTS)
        {
            // set quest to failed
            if (Player* player = GetPlayerForEscort())
                player->FailQuest(QUEST_NIGHTMARE_MANIFESTS);

            // despawn the summons
            if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                eranikus->AI()->EnterEvadeMode();
            _questActive = 0;
        }
        if (_questActive == QUEST_WAKING_LEGENDS)
        {
            // set quest to failed
            if (Player* player = GetPlayerForEscort())
                player->FailQuest(QUEST_WAKING_LEGENDS);
            _questActive = 0;
        }

        // Remulos is only targetable for friendly player spells during Eranikus event so reset on death
        me->SetPvP(false);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        if (_questActive != QUEST_NIGHTMARE_MANIFESTS)
            return;

        switch (waypointId)
        {
            case 0:
                if (Player* player = GetPlayerForEscort())
                    ClassicScriptText(SAY_REMULOS_INTRO_1, me, player);
                // Remulos is only targetable for friendly player spells during Eranikus event
                me->SetPvP(true);
                me->SetSpeedRate(MOVE_WALK, 2.2f);
                me->SetWalk(true);
                break;
            case 1:
                me->SetFaction(FACTION_REMULOS_EVENT);
                ClassicScriptText(SAY_REMULOS_INTRO_2, me);
                break;
            case 13:
                if (Player* player = GetPlayerForEscort())
                    ClassicScriptText(SAY_REMULOS_INTRO_3, me, player);
                break;
            case 14:
                ClassicScriptText(SAY_REMULOS_INTRO_4, me);
                break;
            case 15:
                ClassicScriptText(SAY_REMULOS_INTRO_5, me);
                break;
            case 16:
                // Summon ERANIKUS
                DoCastSelf(SPELL_CONJURE_RIFT);
                break;
            case 17:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                {
                    // This big yellow emote was removed at some point in WotLK
                    //ClassicScriptText(EMOTE_SUMMON_ERANIKUS, eranikus);
                    ClassicScriptText(SAY_ERANIKUS_SPAWN, eranikus);
                }
                break;
            case 18:
                ClassicScriptText(SAY_REMULOS_TAUNT_1, me);
                break;
            case 19:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    ClassicScriptText(EMOTE_ERANIKUS_LAUGH, eranikus);
                break;
            case 20:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    ClassicScriptText(SAY_ERANIKUS_TAUNT_2, eranikus);
                break;
            case 21:
                ClassicScriptText(SAY_REMULOS_TAUNT_3, me);
                break;
            case 22:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    ClassicScriptText(SAY_ERANIKUS_TAUNT_4, eranikus);
                break;
            case 23:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    ClassicScriptText(EMOTE_ERANIKUS_ATTACK, eranikus);
                break;
            case 24:
                if (Player* player = GetPlayerForEscort())
                    ClassicScriptText(SAY_REMULOS_DEFEND_1, me, player);
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    MoveEranikus(eranikus, POINT_ID_ERANIKUS_FLIGHT, EranikusLocations[1]);
                break;
            case 28:
                ClassicScriptText(SAY_REMULOS_DEFEND_2, me);
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                {
                    me->SetFacingToObject(eranikus);
                    eranikus->SetFacingToObject(me);
                }
                break;
            case 29:
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    ClassicScriptText(SAY_ERANIKUS_SHADOWS, eranikus);
                break;
            case 30:
                ClassicScriptText(SAY_REMULOS_DEFEND_3, me);
                SetEscortPaused(true);
                _shadeSummonTimer = 5000;
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    me->SetFacingToObject(eranikus);
                break;
            case 31:
                SetEscortPaused(true);
                break;
            default:
                break;
        }
    }

    void DoHandleOutro(WorldObject* target)
    {
        if (Player* player = GetPlayerForEscort())
            player->GroupEventHappens(QUEST_NIGHTMARE_MANIFESTS, target);

        // Remulos is only targetable for friendly player spells during Eranikus event: remove flag on quest completion
        me->SetPvP(false);

        _outroTimer = 3000;
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_REMULOS_OUTRO)
        {
            Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID);
            DoHandleOutro(eranikus ? static_cast<WorldObject*>(eranikus) : static_cast<WorldObject*>(me));
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (_questActive != QUEST_NIGHTMARE_MANIFESTS)
            return;

        PollEranikusMovement();

        if (_outroTimer)
        {
            if (_outroTimer <= diff)
            {
                switch (_outroPhase)
                {
                    case 0:
                        ClassicScriptText(SAY_REMULOS_OUTRO_1, me);
                        _outroTimer = 3000;
                        break;
                    case 1:
                        // Despawn Remulos after the outro is finished - he will respawn automatically at his home position after a few min
                        ClassicScriptText(SAY_REMULOS_OUTRO_2, me);
                        me->NearTeleportTo(me->GetHomePosition());
                        _outroTimer = 0;
                        me->DespawnOrUnsummon();
                        break;
                    default:
                        break;
                }
                ++_outroPhase;
            }
            else
                _outroTimer -= diff;
        }

        // during the battle
        if (_shadeSummonTimer)
        {
            if (_shadeSummonTimer <= diff)
            {
                // do this yell only first time
                if (_isFirstWave)
                {
                    // summon 3 shades inside the house
                    for (uint8 i = 0; i < MAX_SHADOWS; ++i)
                        me->SummonCreature(NPC_NIGHTMARE_PHANTASM, ShadowsLocations[i].m_fX, ShadowsLocations[i].m_fY, ShadowsLocations[i].m_fZ, 0.0f, TEMPSUMMON_DEAD_DESPAWN);

                    if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                        ClassicScriptText(SAY_ERANIKUS_ATTACK_1, eranikus);

                    ++_summonCount;
                    SetEscortPaused(false);
                    _isFirstWave = false;
                }

                uint8 randomShadowNumber = urand(3, MAX_SHADOWS);

                // Alita : four possible zones : in front of the building, accross the bridge(not far from the platform), another farther away, and next to player(well since the player is supposed to be with Remulos).
                if (_summonCount < MAX_SUMMON_TURNS)
                {
                    switch (_summonCount % 2)
                    {
                        case 0:
                            if (Player* player = GetPlayerForEscort())
                            {
                                for (uint8 i = 0; i < randomShadowNumber; ++i)
                                {
                                    Position pos = me->GetRandomPoint(player->GetPosition(), 20.0f);
                                    me->SummonCreature(NPC_NIGHTMARE_PHANTASM, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() + 2.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                                }
                            }
                            break;
                        case 1:
                        {
                            uint8 randomSummonPoint = urand(3, 5);
                            for (uint8 i = 0; i < randomShadowNumber; ++i)
                            {
                                Position pos = me->GetRandomPoint(Position(ShadowsLocations[randomSummonPoint].m_fX, ShadowsLocations[randomSummonPoint].m_fY, ShadowsLocations[randomSummonPoint].m_fZ), 10.0f);
                                me->SummonCreature(NPC_NIGHTMARE_PHANTASM, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 50s);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    ++_summonCount;
                }

                // If all the shades were summoned then set Eranikus in combat
                // We don't count the dead shades, because the boss is usually set in combat before all shades are dead
                if (_summonCount == MAX_SUMMON_TURNS)
                {
                    _shadeSummonTimer = 0;

                    if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                    {
                        MoveEranikus(eranikus, POINT_ID_ERANIKUS_COMBAT, EranikusLocations[2]);
                        eranikus->RemoveAurasDueToSpell(SPELL_ERANIKUS_HOVER);
                    }
                }
                else
                    _shadeSummonTimer = urand(20000, 30000);
            }
            else
                _shadeSummonTimer -= diff;
        }

        if (_transitionTimer)
        {
            if (_transitionTimer <= diff)
            {
                if (Creature* eranikus = ObjectAccessor::GetCreature(*me, _eranikusGUID))
                {
                    _transitionTimer = 0;

                    eranikus->SetWalk(true);
                    eranikus->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    eranikus->AI()->AttackStart(me);
                }
            }
            else
                _transitionTimer -= diff;
        }

        // Combat spells
        if (!UpdateVictim())
            return;

        if (_healTimer < diff)
        {
            if (Unit* target = DoSelectLowestHpFriendly(DEFAULT_VISIBILITY_DISTANCE))
            {
                switch (urand(0, 2))
                {
                    case 0: DoCast(target, SPELL_HEALING_TOUCH); break;
                    case 1: DoCast(target, SPELL_REJUVENATION); break;
                    case 2: DoCast(target, SPELL_REGROWTH); break;
                }
            }
            _healTimer = 10000;
        }
        else
            _healTimer -= diff;

        if (_starfireTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                if (DoCast(target, SPELL_STARFIRE) == SPELL_CAST_OK)
                    _starfireTimer = 20000;
            }
        }
        else
            _starfireTimer -= diff;
    }

    // Waking Legends helper: VMaNGOS uses MovePoint without MOVE_PATHFINDING except for the first move
    void MoveRemulos(uint32 index, bool pathfinding = false)
    {
        me->GetMotionMaster()->MovePoint(index, RemulosLocations[index].m_fX, RemulosLocations[index].m_fY, RemulosLocations[index].m_fZ, pathfinding);
    }

    void UpdateAI(uint32 diff) override
    {
        EscortAI::UpdateAI(diff);

        if (_questActive != QUEST_WAKING_LEGENDS)
            return;

        Player* player = GetPlayerForEscort();
        if (!player || player->GetQuestStatus(QUEST_WAKING_LEGENDS) != QUEST_STATUS_INCOMPLETE)
            return;

        if (!_eventWLStarted)
        {
            SetEscortPaused(true);
            ClassicScriptText(SAY_REMULOS_1, me, player);
            _sayingTimeTimer = 3000;
            _eventWLStarted = true;
        }

        if (_sayingTimeTimer)
        {
            if (_sayingTimeTimer <= diff)
            {
                MoveRemulos(0, true);
                if (!_returnInitialPosition)
                    _tabMovementsTimer[0] = 7000;
                else
                    _initialPositionTimer = 6000;
                _sayingTimeTimer = 0;
            }
            else
                _sayingTimeTimer -= diff;
        }

        if (_tabMovementsTimer[0])
        {
            if (_tabMovementsTimer[0] <= diff)
            {
                MoveRemulos(1);
                if (!_returnInitialPosition)
                    _tabMovementsTimer[1] = 9000;
                else
                    _sayingTimeTimer = 7000;
                _tabMovementsTimer[0] = 0;
            }
            else
                _tabMovementsTimer[0] -= diff;
        }

        if (_tabMovementsTimer[1])
        {
            if (_tabMovementsTimer[1] <= diff)
            {
                MoveRemulos(2);
                if (!_returnInitialPosition)
                    _tabMovementsTimer[2] = 8000;
                else
                    _tabMovementsTimer[0] = 8000;
                _tabMovementsTimer[1] = 0;
            }
            else
                _tabMovementsTimer[1] -= diff;
        }

        if (_tabMovementsTimer[2])
        {
            if (_tabMovementsTimer[2] <= diff)
            {
                MoveRemulos(3);
                if (!_returnInitialPosition)
                    _tabMovementsTimer[3] = 5000;
                else
                    _tabMovementsTimer[1] = 1000;
                _tabMovementsTimer[2] = 0;
            }
            else
                _tabMovementsTimer[2] -= diff;
        }

        if (_tabMovementsTimer[3])
        {
            if (_tabMovementsTimer[3] <= diff)
            {
                MoveRemulos(4);
                _tabMovementsTimer[4] = 4000;
                _tabMovementsTimer[3] = 0;
            }
            else
                _tabMovementsTimer[3] -= diff;
        }

        if (_tabMovementsTimer[4])
        {
            if (_tabMovementsTimer[4] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_2, me, player);
                _castSpellTimer = 7000;
                _tabMovementsTimer[4] = 0;
            }
            else
                _tabMovementsTimer[4] -= diff;
        }

        if (_castSpellTimer)
        {
            if (_castSpellTimer <= diff)
            {
                DoCastSelf(SPELL_THROW_NIGHTMARE_OBJECT);
                _summonMalfurionTimer = 8000;
                _castSpellTimer = 0;
            }
            else
                _castSpellTimer -= diff;
        }

        if (_summonMalfurionTimer)
        {
            if (_summonMalfurionTimer <= diff)
            {
                me->SummonCreature(NPC_MALFURION, 7734.575684f, -2312.118652f, 452.679504f, 0.068726f, TEMPSUMMON_TIMED_DESPAWN, 96s);
                _summonMalfurionTimer = 0;
                _tabDialogsTimer[0] = 3000;
            }
            else
                _summonMalfurionTimer -= diff;
        }

        if (_tabDialogsTimer[0])
        {
            if (_tabDialogsTimer[0] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_3, me);
                _tabDialogsTimer[0] = 0;
                _tabDialogsTimer[1] = 3000;
            }
            else
                _tabDialogsTimer[0] -= diff;
        }

        if (_tabDialogsTimer[1])
        {
            if (_tabDialogsTimer[1] <= diff)
            {
                if (Creature* malfurion = ObjectAccessor::GetCreature(*me, _malfurionGUID))
                {
                    ClassicScriptText(SAY_MALFURION_1, malfurion);
                    _tabDialogsTimer[1] = 0;
                    _tabDialogsTimer[2] = 8000;
                }
            }
            else
                _tabDialogsTimer[1] -= diff;
        }

        if (_tabDialogsTimer[2])
        {
            if (_tabDialogsTimer[2] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_4, me);
                _tabDialogsTimer[2] = 0;
                _tabDialogsTimer[3] = 13000;
            }
            else
                _tabDialogsTimer[2] -= diff;
        }

        if (_tabDialogsTimer[3])
        {
            if (_tabDialogsTimer[3] <= diff)
            {
                if (Creature* malfurion = ObjectAccessor::GetCreature(*me, _malfurionGUID))
                {
                    ClassicScriptText(SAY_MALFURION_2, malfurion);
                    _tabDialogsTimer[3] = 0;
                    _tabDialogsTimer[4] = 16000;
                }
            }
            else
                _tabDialogsTimer[3] -= diff;
        }

        if (_tabDialogsTimer[4])
        {
            if (_tabDialogsTimer[4] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_5, me);
                _tabDialogsTimer[4] = 0;
                _tabDialogsTimer[5] = 10000;
            }
            else
                _tabDialogsTimer[4] -= diff;
        }

        if (_tabDialogsTimer[5])
        {
            if (_tabDialogsTimer[5] <= diff)
            {
                if (Creature* malfurion = ObjectAccessor::GetCreature(*me, _malfurionGUID))
                {
                    ClassicScriptText(SAY_MALFURION_3, malfurion);
                    _tabDialogsTimer[5] = 0;
                    _tabDialogsTimer[6] = 11000;
                }
            }
            else
                _tabDialogsTimer[5] -= diff;
        }

        // Kept as in VMaNGOS: this block reuses timer [5] (already cleared above when it fires), so SAY_REMULOS_6 is never said.
        if (_tabDialogsTimer[5])
        {
            if (_tabDialogsTimer[5] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_6, me);
                _tabDialogsTimer[5] = 0;
                _tabDialogsTimer[6] = 15000;
            }
            else
                _tabDialogsTimer[5] -= diff;
        }

        if (_tabDialogsTimer[6])
        {
            if (_tabDialogsTimer[6] <= diff)
            {
                if (Creature* malfurion = ObjectAccessor::GetCreature(*me, _malfurionGUID))
                {
                    ClassicScriptText(SAY_MALFURION_4, malfurion);
                    _tabDialogsTimer[6] = 0;
                    _tabDialogsTimer[7] = 21000;
                }
            }
            else
                _tabDialogsTimer[6] -= diff;
        }

        if (_tabDialogsTimer[7])
        {
            if (_tabDialogsTimer[7] <= diff)
            {
                if (Creature* malfurion = ObjectAccessor::GetCreature(*me, _malfurionGUID))
                {
                    ClassicScriptText(SAY_MALFURION_5, malfurion);
                    _tabDialogsTimer[7] = 0;
                    _tabDialogsTimer[8] = 12000;
                }
            }
            else
                _tabDialogsTimer[7] -= diff;
        }

        if (_tabDialogsTimer[8])
        {
            if (_tabDialogsTimer[8] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_7, me);
                _tabDialogsTimer[8] = 0;
                _tabDialogsTimer[9] = 9000;
            }
            else
                _tabDialogsTimer[8] -= diff;
        }

        if (_tabDialogsTimer[9])
        {
            if (_tabDialogsTimer[9] <= diff)
            {
                ClassicScriptText(SAY_REMULOS_8, me);
                _tabDialogsTimer[9] = 0;
                _returnInitialPosition = true;
                _tabMovementsTimer[2] = 7000;
            }
            else
                _tabDialogsTimer[9] -= diff;
        }

        if (_initialPositionTimer)
        {
            if (_initialPositionTimer <= diff)
            {
                MoveRemulos(5);
                _initialPositionTimer = 0;
                _questCompleteTimer = 11000;
            }
            else
                _initialPositionTimer -= diff;
        }

        if (_questCompleteTimer)
        {
            if (_questCompleteTimer <= diff)
            {
                me->NearTeleportTo(me->GetHomePosition());
                if (player->GetQuestStatus(QUEST_WAKING_LEGENDS) == QUEST_STATUS_INCOMPLETE)
                    player->GroupEventHappens(QUEST_WAKING_LEGENDS, me);
                me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                _questCompleteTimer = 0;
                _questActive = 0;
                // VMaNGOS ForcedDespawn() + Respawn(): despawn and respawn right away
                me->DespawnOrUnsummon(0s, 1s);
            }
            else
                _questCompleteTimer -= diff;
        }
    }

private:
    uint32 _questActive; // VMaNGOS global m_idQuestActive

    uint32 _healTimer;
    uint32 _starfireTimer;
    uint32 _shadeSummonTimer;
    uint32 _outroTimer;
    uint32 _transitionTimer;

    bool _eventWLStarted;
    uint32 _sayingTimeTimer;
    uint32 _tabMovementsTimer[5];
    uint32 _castSpellTimer;
    uint32 _summonMalfurionTimer;
    uint32 _tabDialogsTimer[10];
    bool _returnInitialPosition;
    uint32 _initialPositionTimer;
    uint32 _questCompleteTimer;

    ObjectGuid _eranikusGUID;
    ObjectGuid _malfurionGUID;
    int32 _eranikusPendingPoint;
    Position _eranikusPendingPos;

    uint8 _outroPhase;
    uint8 _summonCount;

    bool _isFirstWave;
};

void AddSC_classic_moonglade()
{
    RegisterCreatureAI(classic_npc_keeper_remulos);
}
