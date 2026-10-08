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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/swamp_of_sorrows/swamp_of_sorrows.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1393 (Galen's Escape)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedEscortAI.h"
#include "classic_script_text.h"
#include <iterator>

/*######
## npc_galen_goodward
######*/

enum GalenGoodwardData
{
    GALEN_QUEST_GALENS_ESCAPE   = 1393,

    GALEN_GO_GALENS_CAGE        = 37118,

    GALEN_SAY_PERIODIC          = 7124,
    GALEN_SAY_QUEST_ACCEPTED    = 1854,
    GALEN_SAY_ATTACKED_1        = 1628,
    GALEN_SAY_ATTACKED_2        = 1629,
    GALEN_SAY_QUEST_COMPLETE    = 1855,
    GALEN_EMOTE_WHISPER         = 2076,
    GALEN_EMOTE_DISAPPEAR       = 1856,

    GALEN_FACTION_ESCORT        = 495
};

// VMaNGOS script_waypoint entry 5391 (x, y, z, waittime ms). VMaNGOS starts walking and SetRun(true) at point 20.
struct GalenEscortPoint { float x, y, z; uint32 waitMs; };
static GalenEscortPoint const GalenPath[] =
{
    { -9901.12f, -3727.29f, 22.11f, 3000 },
    { -9909.27f, -3727.81f, 23.25f, 0 },
    { -9935.25f, -3729.02f, 22.11f, 0 },
    { -9945.83f, -3719.34f, 21.68f, 0 },
    { -9963.41f, -3710.18f, 21.71f, 0 },
    { -9972.75f, -3690.13f, 21.68f, 0 },
    { -9989.70f, -3669.67f, 21.67f, 0 },
    { -9989.21f, -3647.76f, 23.00f, 0 },
    { -9992.27f, -3633.74f, 21.67f, 0 },
    { -10002.3f, -3611.67f, 22.26f, 0 },
    { -9999.25f, -3586.33f, 21.85f, 0 },
    { -10006.5f, -3571.99f, 21.67f, 0 },
    { -10014.3f, -3545.24f, 21.67f, 0 },
    { -10018.9f, -3525.03f, 21.68f, 0 },
    { -10030.2f, -3514.77f, 21.67f, 0 },
    { -10045.1f, -3501.49f, 21.67f, 0 },
    { -10052.9f, -3479.13f, 21.67f, 0 },
    { -10060.7f, -3460.31f, 21.67f, 0 },
    { -10074.7f, -3436.85f, 20.97f, 0 },
    { -10074.7f, -3436.85f, 20.97f, 0 },
    { -10072.9f, -3408.92f, 20.43f, 15000 },
    { -10108.0f, -3406.05f, 22.06f, 0 }
};

struct classic_npc_galen_goodward : public EscortAI
{
    classic_npc_galen_goodward(Creature* creature) : EscortAI(creature), _periodicSayTimer(6000)
    {
        for (uint32 i = 0; i < std::size(GalenPath); ++i)
        {
            Optional<Milliseconds> wait;
            if (GalenPath[i].waitMs)
                wait = Milliseconds(GalenPath[i].waitMs);
            AddWaypoint(i, GalenPath[i].x, GalenPath[i].y, GalenPath[i].z, 0.0f, wait, i > 20);
        }
    }

    void Reset() override
    {
        _periodicSayTimer = 6000;
    }

    void JustEngagedWith(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))
            ClassicScriptText(urand(0, 1) ? GALEN_SAY_ATTACKED_1 : GALEN_SAY_ATTACKED_2, me, who);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != GALEN_QUEST_GALENS_ESCAPE)
            return;

        // VMaNGOS Start(bRun = false, ...): walking path; faction restored on respawn by EscortAI::InitializeAI
        Start(true, player->GetGUID(), quest);
        me->SetFaction(GALEN_FACTION_ESCORT);
        ClassicScriptText(GALEN_SAY_QUEST_ACCEPTED, me);
    }

    void WaypointStarted(uint32 nodeId, uint32 /*pathId*/) override
    {
        switch (nodeId)
        {
            case 0:
            {
                me->SetImmuneToNPC(false);
                GameObject* cage = nullptr;
                if (!_cageGUID.IsEmpty())
                    cage = ObjectAccessor::GetGameObject(*me, _cageGUID);
                else
                    cage = me->FindNearestGameObject(GALEN_GO_GALENS_CAGE, INTERACTION_DISTANCE);
                if (cage)
                {
                    cage->UseDoorOrButton();
                    _cageGUID = cage->GetGUID();
                }
                break;
            }
            case 21:
                me->SetImmuneToNPC(true);
                ClassicScriptText(GALEN_EMOTE_DISAPPEAR, me);
                break;
            default:
                break;
        }
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 0:
                if (GameObject* cage = ObjectAccessor::GetGameObject(*me, _cageGUID))
                    cage->ResetDoorOrButton();
                break;
            case 20:
                if (Player* player = GetPlayerForEscort())
                {
                    me->SetFacingToObject(player);
                    ClassicScriptText(GALEN_SAY_QUEST_COMPLETE, me, player);
                    ClassicScriptText(GALEN_EMOTE_WHISPER, me, player);
                    player->GroupEventHappens(GALEN_QUEST_GALENS_ESCAPE, me);
                }
                // SetRun(true): node 21 is added with run = true
                break;
            default:
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (_periodicSayTimer < diff)
        {
            // VMaNGOS checks HasEscortState(STATE_ESCORT_NONE), which is always false (state & 0), so the periodic say
            // never fires there. Kept identical (disabled); to enable:
            // if (!HasEscortState(STATE_ESCORT_ESCORTING)) ClassicScriptText(GALEN_SAY_PERIODIC, me);
            _periodicSayTimer = 6000;
        }
        else
            _periodicSayTimer -= diff;

        UpdateVictim();
    }

private:
    ObjectGuid _cageGUID;
    uint32 _periodicSayTimer;
};

void AddSC_classic_swamp_of_sorrows()
{
    RegisterCreatureAI(classic_npc_galen_goodward);
}
