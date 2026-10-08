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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/westfall/westfall.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1651 (The Legend of Stalvan / Tome of Valor escort, Daphne Stilwell)

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedEscortAI.h"
#include "classic_script_text.h"
#include <iterator>

/*######
## npc_daphne_stilwell
######*/

enum DaphneStilwellData
{
    DAPHNE_SAY_DS_START         = 2360,
    DAPHNE_SAY_DS_DOWN_1        = 5269,
    DAPHNE_SAY_DS_DOWN_2        = 2369,
    DAPHNE_SAY_DS_DOWN_3        = 2358,
    DAPHNE_SAY_DS_PROLOGUE      = 3090,

    DAPHNE_SPELL_SHOOT          = 6660,
    DAPHNE_QUEST_TOME_VALOR     = 1651,
    DAPHNE_NPC_DEFIAS_RAIDER    = 6180,
    DAPHNE_ITEM_RIFLE_DISPLAY   = 6946     // VMaNGOS SetVirtualItem(BASE_ATTACK, 6946)
};

// VMaNGOS script_waypoint entry 6182 (x, y, z, waittime ms).
// VMaNGOS Start(bRun = true) and SetRun(false) when point 10 is reached: nodes 0-10 run, 11-18 walk.
struct DaphneEscortPoint { float x, y, z; uint32 waitMs; };
static DaphneEscortPoint const DaphnePath[] =
{
    { -11480.7f, 1545.09f, 49.8986f, 0 },
    { -11466.8f, 1530.15f, 50.2636f, 0 },
    { -11465.2f, 1528.34f, 50.9544f, 0 },
    { -11463.0f, 1525.24f, 50.9377f, 0 },
    { -11461.0f, 1526.61f, 50.9377f, 5000 },
    { -11463.0f, 1525.24f, 50.9377f, 0 },
    { -11465.2f, 1528.34f, 50.9544f, 0 },
    { -11468.4f, 1535.08f, 50.4009f, 15000 },
    { -11468.4f, 1535.08f, 50.4009f, 15000 },
    { -11468.4f, 1535.08f, 50.4009f, 10000 },
    { -11467.9f, 1532.46f, 50.3489f, 0 },
    { -11466.1f, 1529.86f, 50.2094f, 0 },
    { -11463.0f, 1525.24f, 50.9377f, 0 },
    { -11461.0f, 1526.61f, 50.9377f, 5000 },
    { -11463.0f, 1525.24f, 50.9377f, 0 },
    { -11465.2f, 1528.34f, 50.9544f, 0 },
    { -11470.3f, 1537.28f, 50.3785f, 0 },
    { -11475.6f, 1548.68f, 50.1844f, 0 },
    { -11482.3f, 1557.41f, 48.6245f, 0 }
};

Position const DaphneRaiderSpawns[5] =
{
    { -11450.836f, 1569.755f, 54.267f, 4.230f },
    { -11449.697f, 1569.124f, 54.421f, 4.206f },
    { -11448.237f, 1568.307f, 54.620f, 4.206f },
    { -11448.037f, 1570.213f, 54.961f, 4.283f },
    { -11449.018f, 1570.738f, 54.828f, 4.220f }
};

struct classic_npc_daphne_stilwell : public EscortAI
{
    classic_npc_daphne_stilwell(Creature* creature) : EscortAI(creature), _wpHolder(0), _shootTimer(0)
    {
        for (uint32 i = 0; i < std::size(DaphnePath); ++i)
        {
            Optional<Milliseconds> wait;
            if (DaphnePath[i].waitMs)
                wait = Milliseconds(DaphnePath[i].waitMs);
            AddWaypoint(i, DaphnePath[i].x, DaphnePath[i].y, DaphnePath[i].z, 0.0f, wait, i <= 10);
        }
    }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            _wpHolder = 0;

        _shootTimer = 0;
    }

    // VMaNGOS calls Reset() on every evade (also while escorting) and says the "wave down" texts there.
    // TC EscortAI only calls Reset() on evade when not escorting, so the texts are said here.
    void EnterEvadeMode(EvadeReason why) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))
        {
            switch (_wpHolder)
            {
                case 7:
                    ClassicScriptText(DAPHNE_SAY_DS_DOWN_1, me);
                    break;
                case 8:
                    ClassicScriptText(DAPHNE_SAY_DS_DOWN_2, me);
                    break;
                case 9:
                    ClassicScriptText(DAPHNE_SAY_DS_DOWN_3, me);
                    break;
                default:
                    break;
            }
            _shootTimer = 0;
        }

        EscortAI::EnterEvadeMode(why);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != DAPHNE_QUEST_TOME_VALOR)
            return;

        ClassicScriptText(DAPHNE_SAY_DS_START, me);
        // VMaNGOS Start(bRun = true, player, quest, bInstantRespawn = true)
        Start(true, player->GetGUID(), quest, true);
    }

    void SummonRaiders(uint32 count)
    {
        for (uint32 i = 0; i < count; ++i)
            me->SummonCreature(DAPHNE_NPC_DEFIAS_RAIDER, DaphneRaiderSpawns[i], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        _wpHolder = waypointId;

        switch (waypointId)
        {
            case 4:
                me->SetVirtualItem(0, DAPHNE_ITEM_RIFLE_DISPLAY);
                me->SetSheath(SHEATH_STATE_RANGED);
                me->HandleEmoteCommand(EMOTE_STATE_USE_STANDING_NO_SHEATHE);
                break;
            case 7:
                SummonRaiders(3);
                break;
            case 8:
                me->SetSheath(SHEATH_STATE_RANGED);
                SummonRaiders(4);
                break;
            case 9:
                me->SetSheath(SHEATH_STATE_RANGED);
                SummonRaiders(5);
                break;
            case 10:
                // SetRun(false): nodes 11+ are added as walk
                break;
            case 11:
                ClassicScriptText(DAPHNE_SAY_DS_PROLOGUE, me);
                break;
            case 13:
                SetEquipmentSlots(true);
                me->SetSheath(SHEATH_STATE_UNARMED);
                me->HandleEmoteCommand(EMOTE_STATE_USE_STANDING_NO_SHEATHE);
                break;
            case 17:
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(DAPHNE_QUEST_TOME_VALOR, me);
                break;
            default:
                break;
        }
    }

    // ranged attacker: no melee, chase at 30 yards
    void AttackStart(Unit* who) override
    {
        if (!who)
            return;

        AttackStartCaster(who, 30.0f);
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(me);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_shootTimer < diff)
        {
            _shootTimer = 1000;

            if (!me->IsWithinMeleeRange(me->GetVictim()))
                DoCastVictim(DAPHNE_SPELL_SHOOT);
        }
        else
            _shootTimer -= diff;
    }

private:
    uint32 _wpHolder;
    uint32 _shootTimer;
};

void AddSC_classic_westfall()
{
    RegisterCreatureAI(classic_npc_daphne_stilwell);
}
