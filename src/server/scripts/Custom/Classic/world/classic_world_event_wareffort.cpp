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

// Classic 1.60 port of VMaNGOS src/scripts/world/world_event_wareffort.cpp (MaNGOS / Nostalrius / Light's Hope lineage, GPL-2)
// Only npc_aqwar_saurfang (High Overlord Saurfang during the Ahn'Qiraj war effort, Cenarion Hold) is ported here.
// TODO(classic): the rest of the VMaNGOS war effort system (resource counters in saved variables, world states,
// npc_AQwar_collector, transport/transition game events, npc_aqwar_ch_attack waves, infantry followers) does not exist
// in TC and is not ported. npc_aqwar_ch_attack used to set m_lastWave on this AI (public member kept for that).

#include "ScriptMgr.h"
#include "GameEventMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"
#include <array>
#include <iterator>

enum AQWarSaurfang
{
    // TODO(classic): these are VMaNGOS game_event ids (world_event_wareffort.h). TC's game_event table has other ids;
    // map them to the imported war effort events (Cenarion Hold attack / final battle) before relying on this script.
    AQWAR_EVENT_WAR_EFFORT_CH_ATTACK    = 59,
    AQWAR_EVENT_WAR_EFFORT_FINALBATTLE  = 61,

    AQWAR_ZONE_SILITHUS                 = 1377,

    AQWAR_BCT_SAURFANG_SPEECH1          = 11620,
    AQWAR_BCT_SAURFANG_SPEECH2          = 11621,
    AQWAR_BCT_SAURFANG_SPEECH3          = 11622,
    AQWAR_BCT_SAURFANG_SPEECH4          = 11623,
    AQWAR_BCT_SAURFANG_SPEECH5          = 11624,
    AQWAR_BCT_SAURFANG_SPEECH6          = 11625,
    AQWAR_BCT_SAURFANG_SPEECH7          = 11626,
    AQWAR_BCT_SAURFANG_SPEECH8          = 11627,
    AQWAR_BCT_SAURFANG_SPEECH9          = 11628,
    AQWAR_BCT_SAURFANG_SPEECH10         = 11629,
    AQWAR_BCT_SAURFANG_SPEECH11         = 11630,
    AQWAR_BCT_SAURFANG_SPEECH12         = 11631,
    AQWAR_BCT_SAURFANG_SPEECH13         = 11646,
    AQWAR_BCT_SAURFANG_SPEECH14         = 11647,
    AQWAR_BCT_SAURFANG_FINAL_BATTLE     = 11619,

    AQWAR_BCT_SAURFANG_AGGRO1           = 11527, // guessed
    AQWAR_BCT_SAURFANG_AGGRO2           = 11528, // guessed
    AQWAR_BCT_SAURFANG_AGGRO3           = 11538, // sniffed
    AQWAR_BCT_SAURFANG_AGGRO4           = 11540, // sniffed
    AQWAR_BCT_SAURFANG_AGGRO5           = 11541, // guessed
    AQWAR_BCT_SAURFANG_AGGRO6           = 11614, // sniffed
    AQWAR_BCT_SAURFANG_AGGRO7           = 11615, // sniffed
    AQWAR_BCT_SAURFANG_AGGRO8           = 11616, // sniffed
    AQWAR_BCT_SAURFANG_AGGRO9           = 11648, // sniffed

    AQWAR_BCT_SAURFANG_KILLED_UNIT      = 7237,  // sniffed
    AQWAR_BCT_SAURFANG_RAGE             = 11563, // sniffed
    AQWAR_BCT_SAURFANG_BATTLE_WON       = 11651, // sniffed

    AQWAR_SPELL_SF_TERRIFYING_ROAR      = 14100,
    AQWAR_SPELL_SF_CLEAVE               = 16044,
    AQWAR_SPELL_SF_CHARGE               = 15749,
    AQWAR_SPELL_SF_MORTAL_STRIKE        = 24573,
    AQWAR_SPELL_SF_SAURFANG_RAGE        = 26341,

    AQWAR_FACTION_MIGHT_OF_KALIMDOR     = 777,

    AQWAR_MOUNT_DISPLAY_SAURFANG        = 10278
};

static constexpr uint32 AQWarSaurfangSpeech[] =
{
    AQWAR_BCT_SAURFANG_SPEECH1, AQWAR_BCT_SAURFANG_SPEECH2, AQWAR_BCT_SAURFANG_SPEECH3, AQWAR_BCT_SAURFANG_SPEECH4,
    AQWAR_BCT_SAURFANG_SPEECH5, AQWAR_BCT_SAURFANG_SPEECH6, AQWAR_BCT_SAURFANG_SPEECH7, AQWAR_BCT_SAURFANG_SPEECH8,
    AQWAR_BCT_SAURFANG_SPEECH9, AQWAR_BCT_SAURFANG_SPEECH10, AQWAR_BCT_SAURFANG_SPEECH11, AQWAR_BCT_SAURFANG_SPEECH12,
    AQWAR_BCT_SAURFANG_SPEECH13, AQWAR_BCT_SAURFANG_SPEECH14
};

static constexpr uint32 AQWarSaurfangAggro[] =
{
    AQWAR_BCT_SAURFANG_AGGRO1, AQWAR_BCT_SAURFANG_AGGRO2, AQWAR_BCT_SAURFANG_AGGRO3, AQWAR_BCT_SAURFANG_AGGRO4,
    AQWAR_BCT_SAURFANG_AGGRO5, AQWAR_BCT_SAURFANG_AGGRO6, AQWAR_BCT_SAURFANG_AGGRO7, AQWAR_BCT_SAURFANG_AGGRO8,
    AQWAR_BCT_SAURFANG_AGGRO9
};

static constexpr uint32 AQWarSaurfangSpeechCount = 14;

Position const AQWarSaurfangWaveIncomingPosition = { -6985.67f, 956.06f, 10.21f, 2.6f };

std::array<Position, 12> const AQWarSaurfangGatePath
{{
    { -7002.48f, 967.38f, 6.70f, 3.15f },
    { -7205.49f, 967.08f, 0.95f, 2.9f },
    { -7265.48f, 995.34f, 2.55f, 3.16f },
    { -7418.73f, 1000.99f, 0.91f, 2.91f },
    { -7661.01f, 1052.23f, 4.82f, 2.32f },
    { -7759.05f, 1164.64f, 0.02f, 2.22f },
    { -7810.24f, 1275.49f, -11.08f, 2.72f },
    { -7909.35f, 1319.05f, -7.79f, 2.32f },
    { -7952.77f, 1377.95f, 2.94f, 1.38f },
    { -7933.09f, 1490.65f, -6.62f, 2.68f },
    { -8014.01f, 1532.97f, 2.81f, 3.10f },
    { -8079.99f, 1523.19f, 2.61f, 3.15f }
}};

struct classic_npc_aqwar_saurfang : public ScriptedAI
{
    bool m_lastWave;    // set by the (not ported) Cenarion Hold attack controller before the last wave

    classic_npc_aqwar_saurfang(Creature* creature) : ScriptedAI(creature)
    {
        m_lastWave = false;
        _cenarionHoldAttackWarn = false;
        _finalBattle = false;
        _speechDelay = 120000; // 2 minutes on the first delay to give people time to reach CH
        _speechStep = 0;
        _inSpeech = false;
        _movingToGate = false;

        _movePoint = 0;
        _movePointReached = true;
        _movementPaused = false;

        _mortalStrikeTimer = 0;
        _terrifyingRoarTimer = 0;
        _chargeTimer = 0;
        _cleaveTimer = 0;

        me->SetFaction(AQWAR_FACTION_MIGHT_OF_KALIMDOR);
    }

    void Reset() override
    {
        if (_movingToGate)
            me->Mount(AQWAR_MOUNT_DISPLAY_SAURFANG);

        _mortalStrikeTimer = urand(1, 15) * IN_MILLISECONDS;
        _chargeTimer = urand(0, 4) * IN_MILLISECONDS;
        _cleaveTimer = urand(3, 9) * IN_MILLISECONDS;
        _terrifyingRoarTimer = urand(4, 12) * IN_MILLISECONDS;
    }

    void JustEngagedWith(Unit* who) override
    {
        me->Dismount();
        _movementPaused = true;
        ClassicScriptText(AQWarSaurfangAggro[urand(0, uint32(std::size(AQWarSaurfangAggro)) - 1)], me, who);

        if (me->CastSpell(me, AQWAR_SPELL_SF_SAURFANG_RAGE, false) == SPELL_CAST_OK)
            ClassicScriptText(AQWAR_BCT_SAURFANG_RAGE, me);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (roll_chance(10))
            ClassicScriptText(AQWAR_BCT_SAURFANG_KILLED_UNIT, me);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        if (m_lastWave)
        {
            ClassicScriptText(AQWAR_BCT_SAURFANG_BATTLE_WON, me);
            m_lastWave = false;
        }
        ScriptedAI::EnterEvadeMode(why);
    }

    // VMaNGOS: engages any hostile unit in line of sight (ignores aggro radius)
    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || me->IsEngaged())
            return;

        if (me->HasReactState(REACT_PASSIVE) || me->IsImmuneToPC() || me->IsImmuneToNPC())
            return;

        if (me->IsValidAttackTarget(who) && me->IsHostileTo(who))
        {
            if (who->isInAccessiblePlaceFor(me) && me->IsWithinLOSInMap(who))
                me->EngageWithTarget(who);
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (_movingToGate && pointId < AQWarSaurfangGatePath.size())
        {
            me->SetFacingTo(AQWarSaurfangGatePath[pointId].GetOrientation());
            _movePointReached = true;
        }

        if (_movePoint == AQWarSaurfangGatePath.size())
        {
            _movingToGate = false;
            me->Dismount();
            Position const& last = AQWarSaurfangGatePath[_movePoint - 1];
            me->SetHomePosition(last.GetPositionX(), last.GetPositionY(), last.GetPositionZ(), 2.6f);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsAlive())
            return;

        if (!_cenarionHoldAttackWarn && sGameEventMgr->IsActiveEvent(AQWAR_EVENT_WAR_EFFORT_CH_ATTACK))
        {
            MoveToWaveBattlePosition();
            _cenarionHoldAttackWarn = true;
            return;
        }

        if (!_finalBattle && sGameEventMgr->IsActiveEvent(AQWAR_EVENT_WAR_EFFORT_FINALBATTLE))
        {
            me->SetImmuneToPC(true);
            me->SetImmuneToNPC(true);
            _inSpeech = true;
            _finalBattle = true;
            me->Mount(AQWAR_MOUNT_DISPLAY_SAURFANG);

            // Recover from crash/restart
            if (!_cenarionHoldAttackWarn)
            {
                MoveToWaveBattlePosition();
                _cenarionHoldAttackWarn = true;
            }
        }

        if (_finalBattle)
        {
            if (_inSpeech)
            {
                if (_speechDelay <= diff)
                {
                    if (_speechStep < AQWarSaurfangSpeechCount)
                        ClassicScriptText(AQWarSaurfangSpeech[_speechStep], me);
                    ++_speechStep;
                    _speechDelay = 10000;
                }
                else
                    _speechDelay -= diff;

                if (_speechStep == AQWarSaurfangSpeechCount)
                {
                    // TODO(classic): VMaNGOS sWorld.SendBroadcastTextToWorld(BCT_SAURFANG_FINAL_BATTLE) sends this text to every
                    // player online; TC has no equivalent helper, so it is said by Saurfang with its own chat type instead.
                    ClassicScriptText(AQWAR_BCT_SAURFANG_FINAL_BATTLE, me);
                    _inSpeech = false;
                    _movingToGate = true;
                    me->SetImmuneToPC(false);
                    me->SetImmuneToNPC(false);
                    me->SetWalk(false);
                }
            }
            else if (_movingToGate && _movePointReached && !_movementPaused && _movePoint < AQWarSaurfangGatePath.size())
            {
                Position const& point = AQWarSaurfangGatePath[_movePoint];
                me->GetMotionMaster()->MovePoint(_movePoint, point.GetPositionX(), point.GetPositionY(), point.GetPositionZ(), true, {}, {},
                    MovementWalkRunSpeedSelectionMode::ForceRun);
                me->SetHomePosition(point);
                ++_movePoint;

                _movePointReached = false;
            }
        }

        if (!UpdateVictim())
            return;

        if (_mortalStrikeTimer < diff)
        {
            if (DoCastVictim(AQWAR_SPELL_SF_MORTAL_STRIKE) == SPELL_CAST_OK)
                _mortalStrikeTimer = urand(11, 20) * IN_MILLISECONDS;
        }
        else
            _mortalStrikeTimer -= diff;

        if (_cleaveTimer < diff)
        {
            if (DoCastVictim(AQWAR_SPELL_SF_CLEAVE) == SPELL_CAST_OK)
                _cleaveTimer = urand(9, 21) * IN_MILLISECONDS;
        }
        else
            _cleaveTimer -= diff;

        if (_chargeTimer < diff)
        {
            if (DoCastVictim(AQWAR_SPELL_SF_CHARGE) == SPELL_CAST_OK)
                _chargeTimer = urand(4, 12) * IN_MILLISECONDS;
        }
        else
            _chargeTimer -= diff;

        if (_terrifyingRoarTimer < diff)
        {
            if (DoCastVictim(AQWAR_SPELL_SF_TERRIFYING_ROAR) == SPELL_CAST_OK)
                _terrifyingRoarTimer = urand(30, 40) * IN_MILLISECONDS;
        }
        else
            _terrifyingRoarTimer -= diff;
    }

    // VMaNGOS JustRespawned(): respawned back at CH, keep running to the gate.
    // TODO(classic): TC recreates the AI on respawn, so the war-effort state below is lost there and is re-derived from
    // the active game events in UpdateAI (the final battle speech would restart after a respawn).
    void JustAppeared() override
    {
        if (_movingToGate)
        {
            _movePoint = 0;
            _movePointReached = true;
            _movementPaused = false;
        }

        ScriptedAI::JustAppeared();
    }

    void MoveToWaveBattlePosition()
    {
        me->SetHomePosition(AQWarSaurfangWaveIncomingPosition);
        me->GetMotionMaster()->MoveTargetedHome();
    }

    void JustReachedHome() override
    {
        _movementPaused = false;
    }

private:
    bool _cenarionHoldAttackWarn;
    bool _finalBattle;
    uint32 _speechDelay;
    uint32 _speechStep;
    bool _inSpeech;
    bool _movingToGate;
    uint32 _movePoint;
    bool _movePointReached;
    bool _movementPaused;

    uint32 _mortalStrikeTimer;
    uint32 _terrifyingRoarTimer;
    uint32 _chargeTimer;
    uint32 _cleaveTimer;
};

// VMaNGOS uses this AI only in Silithus; elsewhere (e.g. Orgrimmar) the creature keeps CreatureEventAI.
// Returning nullptr lets TC pick the default AI for other zones.
// TODO(classic): the non-Silithus Saurfang loses its EventAI/SmartAI because the generated SQL clears AIName for
// every creature using this script name.
static classic_npc_aqwar_saurfang* GetAI_classic_npc_aqwar_saurfang(Creature* creature)
{
    if (creature->GetMap()->GetZoneId(creature->GetPhaseShift(), creature->GetPosition()) == AQWAR_ZONE_SILITHUS)
        return new classic_npc_aqwar_saurfang(creature);

    return nullptr;
}

void AddSC_classic_world_event_wareffort()
{
    RegisterCreatureAIWithFactory(classic_npc_aqwar_saurfang, GetAI_classic_npc_aqwar_saurfang);
}
