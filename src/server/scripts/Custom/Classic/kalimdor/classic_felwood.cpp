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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/felwood/felwood.cpp (ScriptDev2 lineage, GPL-2)
// Ported: npc_cursed_ooze, npc_tainted_ooze (4101/4102 jars), npc_captured_arkonarin (5203), go_corrupted_plant

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuaternionData.h"
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

// VMaNGOS script_waypoint entry 11016 (pointid 0..111, stored 0-based here)
ClassicEscortPoint const EscortPath11016[] =
{
    { 5004.98f, -440.237f, 319.059f, 3.7929f, 4000 },  // 0: SAY_ESCORT_START
    { 4992.22f, -449.964f, 317.057f, 3.7929f, 0 },  // 1
    { 4988.55f, -457.438f, 316.289f, 4.2559f, 0 },  // 2
    { 4989.98f, -464.297f, 316.846f, 4.9179f, 0 },  // 3
    { 4994.04f, -467.754f, 318.055f, 5.5778f, 0 },  // 4
    { 5002.31f, -466.318f, 319.965f, 0.1719f, 0 },  // 5
    { 5011.8f, -462.889f, 321.501f, 0.3467f, 0 },  // 6
    { 5020.53f, -460.797f, 321.97f, 0.2352f, 0 },  // 7
    { 5026.84f, -463.171f, 321.345f, 5.9233f, 0 },  // 8
    { 5028.66f, -476.805f, 318.726f, 4.8451f, 0 },  // 9
    { 5029.5f, -487.131f, 318.179f, 4.7936f, 0 },  // 10
    { 5031.18f, -497.678f, 316.533f, 4.8703f, 0 },  // 11
    { 5032.72f, -504.748f, 314.744f, 4.9269f, 0 },  // 12
    { 5035.0f, -513.138f, 314.372f, 4.9777f, 0 },  // 13
    { 5037.49f, -521.733f, 313.221f, 4.9944f, 6000 },  // 14: SAY_FIRST_STOP
    { 5049.06f, -519.546f, 313.221f, 0.1868f, 0 },  // 15
    { 5059.17f, -522.93f, 313.221f, 5.9602f, 0 },  // 16
    { 5062.75f, -529.933f, 313.221f, 5.185f, 0 },  // 17
    { 5063.9f, -538.827f, 313.221f, 4.841f, 0 },  // 18
    { 5062.22f, -545.635f, 313.221f, 4.4705f, 0 },  // 19
    { 5061.69f, -552.333f, 313.101f, 4.6334f, 0 },  // 20
    { 5060.33f, -560.349f, 310.873f, 4.5443f, 0 },  // 21
    { 5055.62f, -565.541f, 308.737f, 3.9756f, 0 },  // 22
    { 5049.8f, -567.604f, 306.537f, 3.4822f, 0 },  // 23
    { 5043.01f, -564.946f, 303.682f, 2.7685f, 0 },  // 24
    { 5038.22f, -559.823f, 301.463f, 2.3226f, 0 },  // 25
    { 5039.46f, -548.675f, 297.824f, 1.46f, 0 },  // 26
    { 5043.44f, -538.807f, 297.801f, 1.1874f, 0 },  // 27
    { 5056.4f, -528.954f, 297.801f, 0.65f, 0 },  // 28
    { 5064.4f, -521.904f, 297.801f, 0.7224f, 0 },  // 29
    { 5067.62f, -512.999f, 297.196f, 1.2238f, 0 },  // 30
    { 5065.99f, -505.329f, 297.214f, 1.7802f, 0 },  // 31
    { 5062.24f, -499.086f, 297.448f, 2.1117f, 0 },  // 32
    { 5065.09f, -492.069f, 298.054f, 1.185f, 0 },  // 33
    { 5071.19f, -491.173f, 297.666f, 0.1458f, 5000 },  // 34: SAY_SECOND_STOP
    { 5087.47f, -496.478f, 296.677f, 5.9682f, 0 },  // 35
    { 5095.55f, -508.639f, 296.677f, 5.2988f, 0 },  // 36
    { 5104.3f, -521.014f, 296.677f, 5.3278f, 0 },  // 37
    { 5110.13f, -532.123f, 296.677f, 5.1957f, 4000 },  // 38: open equipment chest
    { 5110.13f, -532.123f, 296.677f, 5.1957f, 4000 },  // 39: cast SPELL_STRENGHT_ARKONARIN
    { 5110.13f, -532.123f, 296.677f, 5.1957f, 4000 },  // 40: SAY_EQUIPMENT
    { 5110.13f, -532.123f, 296.677f, 5.1957f, 0 },  // 41: SAY_ESCAPE
    { 5099.75f, -510.823f, 296.677f, 2.0243f, 0 },  // 42
    { 5091.94f, -497.516f, 296.677f, 2.1015f, 0 },  // 43
    { 5079.38f, -486.811f, 297.638f, 2.4358f, 0 },  // 44
    { 5069.21f, -488.77f, 298.082f, 3.3319f, 0 },  // 45
    { 5064.24f, -496.051f, 297.275f, 4.1134f, 0 },  // 46
    { 5065.08f, -505.239f, 297.361f, 4.8036f, 0 },  // 47
    { 5067.82f, -515.245f, 297.125f, 4.9797f, 0 },  // 48
    { 5064.62f, -521.17f, 297.801f, 4.2172f, 0 },  // 49
    { 5053.22f, -530.739f, 297.801f, 3.8399f, 0 },  // 50
    { 5045.73f, -538.311f, 297.801f, 3.9324f, 0 },  // 51
    { 5039.69f, -548.112f, 297.801f, 4.1601f, 0 },  // 52
    { 5038.78f, -557.588f, 300.787f, 4.6167f, 0 },  // 53
    { 5042.01f, -566.749f, 303.838f, 5.0514f, 0 },  // 54
    { 5050.56f, -568.149f, 306.782f, 6.1209f, 0 },  // 55
    { 5056.98f, -564.674f, 309.342f, 0.4961f, 0 },  // 56
    { 5060.79f, -556.801f, 311.936f, 1.1201f, 0 },  // 57
    { 5059.58f, -551.626f, 313.221f, 1.8005f, 0 },  // 58
    { 5062.83f, -541.994f, 313.221f, 1.2454f, 0 },  // 59
    { 5063.55f, -531.288f, 313.221f, 1.5036f, 0 },  // 60
    { 5057.93f, -523.088f, 313.221f, 2.1716f, 0 },  // 61
    { 5049.47f, -519.36f, 313.221f, 2.7265f, 0 },  // 62
    { 5040.79f, -519.809f, 313.221f, 3.1933f, 0 },  // 63
    { 5034.3f, -515.361f, 313.948f, 2.5408f, 0 },  // 64
    { 5032.0f, -505.532f, 314.663f, 1.8007f, 0 },  // 65
    { 5029.92f, -495.645f, 316.821f, 1.7781f, 0 },  // 66
    { 5028.87f, -487.0f, 318.179f, 1.6917f, 0 },  // 67
    { 5028.11f, -475.531f, 318.839f, 1.637f, 0 },  // 68
    { 5027.76f, -465.442f, 320.643f, 1.6055f, 0 },  // 69
    { 5019.96f, -460.892f, 321.969f, 2.6135f, 0 },  // 70
    { 5009.43f, -464.793f, 321.248f, 3.4964f, 0 },  // 71
    { 4999.57f, -468.062f, 319.426f, 3.4617f, 0 },  // 72
    { 4992.03f, -468.128f, 317.894f, 3.1503f, 0 },  // 73
    { 4988.17f, -461.293f, 316.369f, 2.0849f, 0 },  // 74
    { 4990.62f, -447.459f, 317.104f, 1.3955f, 0 },  // 75
    { 4993.48f, -438.643f, 318.272f, 1.2571f, 0 },  // 76
    { 4995.45f, -430.178f, 318.462f, 1.3421f, 0 },  // 77
    { 4993.56f, -422.876f, 318.864f, 1.8241f, 0 },  // 78
    { 4985.4f, -420.864f, 320.205f, 2.8998f, 0 },  // 79
    { 4976.52f, -426.168f, 323.112f, 3.68f, 0 },  // 80
    { 4969.83f, -429.755f, 325.029f, 3.6338f, 0 },  // 81
    { 4960.7f, -425.44f, 325.834f, 2.7001f, 0 },  // 82
    { 4955.45f, -418.765f, 327.433f, 2.2373f, 0 },  // 83
    { 4949.7f, -408.796f, 328.004f, 2.094f, 0 },  // 84
    { 4940.02f, -403.222f, 329.956f, 2.6191f, 0 },  // 85
    { 4934.98f, -401.475f, 330.898f, 2.8079f, 0 },  // 86
    { 4928.69f, -399.302f, 331.744f, 2.809f, 0 },  // 87
    { 4926.94f, -398.436f, 333.079f, 2.6821f, 0 },  // 88
    { 4916.16f, -393.822f, 333.729f, 2.7372f, 0 },  // 89
    { 4908.39f, -396.217f, 333.217f, 3.4406f, 0 },  // 90
    { 4905.61f, -396.535f, 335.05f, 3.2555f, 0 },  // 91
    { 4897.88f, -395.245f, 337.346f, 2.9762f, 0 },  // 92
    { 4895.21f, -388.203f, 339.295f, 1.9332f, 0 },  // 93
    { 4896.94f, -382.429f, 341.04f, 1.2797f, 0 },  // 94
    { 4901.88f, -378.799f, 342.771f, 0.6337f, 0 },  // 95
    { 4908.09f, -380.635f, 344.597f, 5.9957f, 0 },  // 96
    { 4911.91f, -385.818f, 346.491f, 5.3475f, 0 },  // 97
    { 4910.1f, -393.444f, 348.798f, 4.4794f, 0 },  // 98
    { 4903.5f, -396.947f, 350.812f, 3.6295f, 0 },  // 99
    { 4898.08f, -394.226f, 351.821f, 2.6763f, 0 },  // 100
    { 4891.33f, -393.436f, 351.801f, 3.0251f, 0 },  // 101
    { 4881.2f, -395.211f, 351.59f, 3.3151f, 0 },  // 102
    { 4877.84f, -395.536f, 349.713f, 3.238f, 0 },  // 103
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 5000 },  // 104: SAY_FRESH_AIR
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 3000 },  // 105: SAY_BETRAYER
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 2000 },  // 106: SAY_TREY
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 0 },  // 107: SAY_ATTACK_TREY
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 5000 },  // 108: SAY_ESCORT_COMPLETE
    { 4873.97f, -394.919f, 349.844f, 2.9835f, 1000 },  // 109
    { 4863.02f, -394.521f, 350.65f, 3.1053f, 0 },  // 110
    { 4848.7f, -397.612f, 351.215f, 3.3542f, 0 },  // 111
};
}

enum FelwoodOozes
{
    SPELL_CURSED            = 13483,
    SPELL_TAINTED           = 3335,
    SPELL_QUEST_CURSED_JAR  = 15698,
    SPELL_QUEST_TAINTED_JAR = 15699
};

/*###############
# Cursed Oose
################*/

struct classic_npc_cursed_ooze : public ScriptedAI
{
    classic_npc_cursed_ooze(Creature* creature) : ScriptedAI(creature), _spellTimer(3000) { }

    void Reset() override
    {
        _spellTimer = 3000;
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo && spellInfo->Id == SPELL_QUEST_CURSED_JAR)
            me->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_spellTimer < diff)
        {
            if (DoCastSelf(SPELL_CURSED) == SPELL_CAST_OK)
                _spellTimer = 60000;
        }
        else
            _spellTimer -= diff;
    }

private:
    uint32 _spellTimer;
};

/*###############
# Tainted Oose
################*/

struct classic_npc_tainted_ooze : public ScriptedAI
{
    classic_npc_tainted_ooze(Creature* creature) : ScriptedAI(creature), _spellTimer(3000) { }

    void Reset() override
    {
        _spellTimer = 3000;
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo && spellInfo->Id == SPELL_QUEST_TAINTED_JAR)
            me->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_spellTimer < diff)
        {
            if (DoCastSelf(SPELL_TAINTED) == SPELL_CAST_OK)
                _spellTimer = 60000;
        }
        else
            _spellTimer -= diff;
    }

private:
    uint32 _spellTimer;
};

/*####
# npc_captured_arkonarin
####*/

enum CapturedArkonarin
{
    SAY_ESCORT_START                = 6433,
    SAY_FIRST_STOP                  = 6456,
    SAY_SECOND_STOP                 = 6457,
    SAY_AGGRO                       = 6801,
    SAY_FOUND_EQUIPMENT             = 6458,
    SAY_ESCAPE_DEMONS               = 6460,
    SAY_FRESH_AIR                   = 6461,
    SAY_TREY_BETRAYER               = 6466,
    SAY_TREY                        = 6463,
    SAY_TREY_ATTACK                 = 6802,
    SAY_ESCORT_COMPLETE             = 6468,

    SPELL_STRENGTH_ARKONARIN        = 18163,
    SPELL_MORTAL_STRIKE             = 16856,
    SPELL_CLEAVE                    = 15496,

    QUEST_ID_RESCUE_JAEDENAR        = 5203,
    NPC_JAEDENAR_LEGIONNAIRE        = 9862,
    NPC_SPIRT_TREY                  = 11141,
    GO_ARKONARIN_CHEST              = 176225,
    GO_ARKONARIN_CAGE               = 176306,

    NPC_ARKO_NARIN                  = 11018,

    FACTION_ESCORT_N_NEUTRAL_ACTIVE = 250
};

// VMaNGOS walks, SetRun() at point 34, SetRun(false) at 43, SetRun() at 109
static bool ArkonarinIsRunNode(uint32 node)
{
    return (node > 34 && node <= 43) || node > 109;
}

struct classic_npc_captured_arkonarin : public EscortAI
{
    classic_npc_captured_arkonarin(Creature* creature) : EscortAI(creature), _canAttack(false)
    {
        Initialize();
    }

    void Initialize()
    {
        _mortalStrikeTimer = urand(5000, 7000);
        _cleaveTimer = urand(1000, 4000);
    }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            _canAttack = false;

        Initialize();
    }

    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        me->RestoreFaction(); // VMaNGOS TEMPFACTION_RESTORE_RESPAWN
        EscortAI::JustAppeared();
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_ID_RESCUE_JAEDENAR)
            return;

        LoadClassicEscortPath(this, EscortPath11016, ArkonarinIsRunNode);
        Start(true, player->GetGUID(), quest);

        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetImmuneToNPC(false);

        if (GameObject* cage = GetClosestGameObjectWithEntry(me, GO_ARKONARIN_CAGE, 5.0f))
            cage->Use(me);
    }

    void JustEngagedWith(Unit* who) override
    {
        if (who->GetEntry() == NPC_SPIRT_TREY)
            ClassicScriptText(SAY_TREY_ATTACK, me);
        else if (roll_chance(25))
            ClassicScriptText(SAY_AGGRO, me, who);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_JAEDENAR_LEGIONNAIRE)
            summoned->AI()->AttackStart(me);
        else if (summoned->GetEntry() == NPC_SPIRT_TREY)
        {
            ClassicScriptText(SAY_TREY_BETRAYER, summoned);
            _treyGuid = summoned->GetGUID();
        }
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 0:
                if (Player* player = GetPlayerForEscort())
                    ClassicScriptText(SAY_ESCORT_START, me, player);
                break;
            case 14:
                ClassicScriptText(SAY_FIRST_STOP, me);
                break;
            case 34:
                ClassicScriptText(SAY_SECOND_STOP, me);
                // SetRun(): see ArkonarinIsRunNode
                break;
            case 38:
                if (GameObject* chest = GetClosestGameObjectWithEntry(me, GO_ARKONARIN_CHEST, 5.0f))
                    chest->Use(me);
                me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
                break;
            case 39:
                DoCastSelf(SPELL_STRENGTH_ARKONARIN);
                break;
            case 40:
                if (Player* player = GetPlayerForEscort())
                    me->SetFacingToObject(player);
                _canAttack = true;
                ClassicScriptText(SAY_FOUND_EQUIPMENT, me);
                me->UpdateEntry(NPC_ARKO_NARIN);
                me->SetFaction(FACTION_ESCORT_N_NEUTRAL_ACTIVE);
                me->SetImmuneToNPC(false);
                break;
            case 41:
                ClassicScriptText(SAY_ESCAPE_DEMONS, me);
                me->SummonCreature(NPC_JAEDENAR_LEGIONNAIRE, 5082.068f, -490.084f, 296.856f, 5.15f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2min);
                me->SummonCreature(NPC_JAEDENAR_LEGIONNAIRE, 5084.135f, -489.187f, 296.832f, 5.15f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2min);
                me->SummonCreature(NPC_JAEDENAR_LEGIONNAIRE, 5085.676f, -488.518f, 296.824f, 5.15f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2min);
                break;
            case 43:
                // SetRun(false): see ArkonarinIsRunNode
                break;
            case 104:
                ClassicScriptText(SAY_FRESH_AIR, me);
                break;
            case 105:
                me->SummonCreature(NPC_SPIRT_TREY, 4844.839f, -395.763f, 350.603f, 6.25f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2min);
                break;
            case 106:
                ClassicScriptText(SAY_TREY, me);
                break;
            case 107:
                if (Creature* trey = ObjectAccessor::GetCreature(*me, _treyGuid))
                    AttackStart(trey);
                break;
            case 108:
                if (Player* player = GetPlayerForEscort())
                    me->SetFacingToObject(player);
                ClassicScriptText(SAY_ESCORT_COMPLETE, me);
                break;
            case 109:
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_ID_RESCUE_JAEDENAR, me);
                // SetRun(): see ArkonarinIsRunNode
                break;
            default:
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_canAttack)
        {
            if (_mortalStrikeTimer < diff)
            {
                if (DoCastVictim(SPELL_MORTAL_STRIKE) == SPELL_CAST_OK)
                    _mortalStrikeTimer = urand(7000, 10000);
            }
            else
                _mortalStrikeTimer -= diff;

            if (_cleaveTimer < diff)
            {
                if (DoCastVictim(SPELL_CLEAVE) == SPELL_CAST_OK)
                    _cleaveTimer = urand(3000, 6000);
            }
            else
                _cleaveTimer -= diff;
        }
    }

private:
    ObjectGuid _treyGuid;
    bool _canAttack;
    uint32 _mortalStrikeTimer;
    uint32 _cleaveTimer;
};

/*####
# go_corrupted_plant
####*/

// corrupted plant entry -> cleansed plant entry
static uint32 const CorruptedPlantEntries[][2] =
{
    { 164885, 164881 },
    { 173324, 173325 },
    { 174608, 174609 },
    { 174684, 174685 },
    { 164886, 164882 },
    { 171939, 171940 },
    { 171942, 171943 },
    { 174594, 174612 },
    { 174595, 174613 },
    { 174596, 174614 },
    { 174598, 174615 },
    { 174712, 174714 },
    { 174713, 174715 },
    { 164888, 164883 },
    { 173284, 174622 },
    { 174605, 174623 },
    { 174606, 174624 },
    { 174607, 174625 },
    { 174686, 174687 },
    { 164887, 164884 },
    { 173327, 173326 },
    { 174599, 174616 },
    { 174600, 174617 },
    { 174601, 174618 },
    { 174602, 174619 },
    { 174603, 174620 },
    { 174604, 174621 },
    { 174708, 174710 },
    { 174709, 174711 }
};

struct classic_go_corrupted_plant : public GameObjectAI
{
    static constexpr uint32 CLEANSED_PLANT_RESPAWN_TIMER = 25 * 60; // 25 minutes

    classic_go_corrupted_plant(GameObject* go) : GameObjectAI(go), _cleansedEntry(0)
    {
        for (auto const& pair : CorruptedPlantEntries)
        {
            if (pair[0] == me->GetEntry() || pair[1] == me->GetEntry())
            {
                _cleansedEntry = pair[1];
                break;
            }
        }
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (me->isSpawned() && !_cleansedGuid.IsEmpty())
        {
            if (GameObject* cleansed = ObjectAccessor::GetGameObject(*me, _cleansedGuid))
                cleansed->DespawnOrUnsummon();
            _cleansedGuid.Clear();
        }
    }

    void OnQuestReward(Player* /*player*/, Quest const* /*quest*/, LootItemType /*type*/, uint32 /*opt*/) override
    {
        PlantQuestRewarded();
    }

    void PlantQuestRewarded()
    {
        // VMaNGOS (patch >= 1.9) keeps the cleansed plant until the corrupted one respawns (SetSpawnedByDefault + UpdateAI cleanup).
        // TC may recreate the corrupted plant object on respawn (dynamic spawning), losing _cleansedGuid, so the cleansed plant
        // is summoned for the corrupted plant's respawn delay instead (falls back to the VMaNGOS 25 minutes).
        uint32 despawnTime = me->GetRespawnDelay() ? me->GetRespawnDelay() : uint32(CLEANSED_PLANT_RESPAWN_TIMER);
        if (_cleansedEntry)
            if (GameObject* cleansed = me->SummonGameObject(_cleansedEntry, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, QuaternionData::fromEulerAnglesZYX(0.0f, 0.0f, 0.0f), Seconds(despawnTime)))
                _cleansedGuid = cleansed->GetGUID();

        me->DespawnOrUnsummon();
    }

private:
    uint32 _cleansedEntry;
    ObjectGuid _cleansedGuid;
};

void AddSC_classic_felwood()
{
    RegisterCreatureAI(classic_npc_cursed_ooze);
    RegisterCreatureAI(classic_npc_tainted_ooze);
    RegisterCreatureAI(classic_npc_captured_arkonarin);
    RegisterGameObjectAI(classic_go_corrupted_plant);
}
