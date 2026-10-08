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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/silverpine_forest/silverpine_forest.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 435 (Escorting Erland)

#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "classic_script_text.h"

/*#####
## npc_deathstalker_erland
#####*/

enum DeathstalkerErlandData
{
    SAY_START_1         = 481,
    SAY_START_2         = 482,
    SAY_AGGRO_1         = 543,
    SAY_AGGRO_2         = 544,
    SAY_AGGRO_3         = 541,
    SAY_PROGRESS        = 483,
    SAY_END             = 484,
    SAY_RANE            = 534,
    SAY_RANE_REPLY      = 535,
    SAY_CHECK_NEXT      = 536,
    SAY_QUINN           = 537,
    SAY_QUINN_REPLY     = 539,
    SAY_BYE             = 538,

    QUEST_ERLAND        = 435,
    NPC_RANE            = 1950,
    NPC_QUINN           = 1951,
    FACTION_ESCORTEE    = 232
};

namespace
{
struct ClassicEscortPoint
{
    uint32 Id;
    float X, Y, Z;
    uint32 WaitTime;
};
}

// VMaNGOS script_waypoint entry 1978 (pointid, x, y, z, waittime ms)
ClassicEscortPoint const ErlandPath[] =
{
    {  0, 1406.32f, 1083.1f,  52.55f,     0 },
    {  1, 1400.49f, 1080.42f, 52.5f,      0 }, // SAY_START_2
    {  2, 1388.48f, 1083.1f,  52.52f,     0 },
    {  3, 1370.16f, 1084.02f, 52.3f,      0 },
    {  4, 1359.02f, 1080.85f, 52.46f,     0 },
    {  5, 1341.43f, 1087.39f, 52.69f,     0 },
    {  6, 1321.93f, 1090.51f, 50.66f,     0 },
    {  7, 1312.98f, 1095.91f, 47.49f,     0 },
    {  8, 1301.09f, 1102.94f, 47.76f,     0 },
    {  9, 1297.73f, 1106.35f, 50.18f,     0 },
    { 10, 1295.49f, 1124.32f, 50.49f,     0 },
    { 11, 1294.84f, 1137.25f, 51.75f,     0 },
    { 12, 1292.89f, 1158.99f, 52.65f,     0 },
    { 13, 1290.75f, 1168.67f, 52.56f,  2000 }, // quest complete SAY_END
    { 14, 1287.12f, 1203.49f, 52.66f,  5000 }, // SAY_RANE
    { 15, 1288.3f,  1203.89f, 52.68f,  5000 }, // SAY_RANE_REPLY
    { 16, 1288.3f,  1203.89f, 52.68f,  5000 }, // SAY_CHECK_NEXT
    { 17, 1290.72f, 1207.44f, 52.69f,     0 },
    { 18, 1297.5f,  1207.18f, 53.74f,     0 },
    { 19, 1301.32f, 1220.9f,  53.74f,     0 },
    { 20, 1298.55f, 1220.43f, 53.74f,     0 },
    { 21, 1297.38f, 1212.87f, 58.51f,     0 },
    { 22, 1297.8f,  1210.04f, 58.51f,     0 },
    { 23, 1305.01f, 1206.1f,  58.51f,     0 },
    { 24, 1310.51f, 1207.36f, 58.51f,  5000 }, // SAY_QUINN
    { 25, 1312.59f, 1207.21f, 58.51f,  5000 }, // SAY_QUINN_REPLY
    { 26, 1312.59f, 1207.21f, 58.51f, 30000 }  // SAY_BYE
};

struct classic_npc_deathstalker_erland : public EscortAI
{
    classic_npc_deathstalker_erland(Creature* creature) : EscortAI(creature) { }

    void MoveInLineOfSight(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))
        {
            if (_raneGUID.IsEmpty() && who->GetEntry() == NPC_RANE)
            {
                if (me->IsWithinDistInMap(who, 30.0f))
                    _raneGUID = who->GetGUID();
            }
            if (_quinnGUID.IsEmpty() && who->GetEntry() == NPC_QUINN)
            {
                if (me->IsWithinDistInMap(who, 30.0f))
                    _quinnGUID = who->GetGUID();
            }
        }

        EscortAI::MoveInLineOfSight(who);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        Player* player = GetPlayerForEscort();
        if (!player)
            return;

        switch (waypointId)
        {
            case 0:
                ClassicScriptText(SAY_START_2, me, player);
                break;
            case 13:
                ClassicScriptText(SAY_END, me, player);
                player->GroupEventHappens(QUEST_ERLAND, me);
                break;
            case 14:
                if (Unit* rane = ObjectAccessor::GetUnit(*me, _raneGUID))
                    ClassicScriptText(SAY_RANE, rane, me);
                break;
            case 15:
                ClassicScriptText(SAY_RANE_REPLY, me);
                break;
            case 16:
                ClassicScriptText(SAY_CHECK_NEXT, me);
                break;
            case 24:
                ClassicScriptText(SAY_QUINN, me);
                break;
            case 25:
                if (Unit* quinn = ObjectAccessor::GetUnit(*me, _quinnGUID))
                    ClassicScriptText(SAY_QUINN_REPLY, quinn, me);
                break;
            case 26:
                ClassicScriptText(SAY_BYE, me);
                break;
        }
    }

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            _raneGUID.Clear();
            _quinnGUID.Clear();
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        switch (urand(0, 2))
        {
            case 0:
                ClassicScriptText(SAY_AGGRO_1, me, who);
                break;
            case 1:
                ClassicScriptText(SAY_AGGRO_2, me, who);
                break;
            case 2:
                ClassicScriptText(SAY_AGGRO_3, me, who);
                break;
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_ERLAND)
        {
            ClassicScriptText(SAY_START_1, me);
            me->SetFaction(FACTION_ESCORTEE); // VMaNGOS TEMPFACTION_RESTORE_RESPAWN: TC restores the template faction on respawn

            ResetPath();
            for (ClassicEscortPoint const& point : ErlandPath)
            {
                Optional<Milliseconds> wait;
                if (point.WaitTime)
                    wait = Milliseconds(point.WaitTime);
                AddWaypoint(point.Id, point.X, point.Y, point.Z, 0.0f, wait, false);
            }
            Start(false, player->GetGUID(), quest);
        }
    }

private:
    ObjectGuid _raneGUID;
    ObjectGuid _quinnGUID;
};

void AddSC_classic_silverpine_forest()
{
    RegisterCreatureAI(classic_npc_deathstalker_erland);
}
