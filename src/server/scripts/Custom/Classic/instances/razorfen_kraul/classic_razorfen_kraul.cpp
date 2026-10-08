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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/the_barrens/razorfen_kraul/razorfen_kraul.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1144 (Willix the Importer), 1221 (Blueleaf Tubers)
// Ported: npc_willix_the_importer, npc_snufflenose_gopher

#include "ScriptMgr.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedFollowerAI.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "classic_razorfen_kraul.h"
#include "classic_script_text.h"
#include <cmath>
#include <list>

/*######
## npc_willix_the_importer
######*/

enum WillixTheImporter
{
    QUEST_WILLIX_THE_IMPORTER  = 1144,

    SAY_WILLIX_READY           = 1482,
    SAY_WILLIX_1               = 1483,
    SAY_WILLIX_2               = 1484,
    SAY_WILLIX_3               = 1485,
    SAY_WILLIX_4               = 1486,
    SAY_WILLIX_5               = 1487,
    SAY_WILLIX_6               = 1488,
    SAY_WILLIX_7               = 1490,
    SAY_WILLIX_END             = 1493,

    SAY_WILLIX_AGGRO_1         = 1546,
    SAY_WILLIX_AGGRO_2         = 1544,
    SAY_WILLIX_AGGRO_3         = 1545,
    SAY_WILLIX_AGGRO_4         = 1547,

    NPC_RAGING_AGAMAR          = 4514
};

namespace
{
float const RFKBoarSpawn[4][3] =
{
    {2151.420f, 1733.18f, 52.10f},
    {2144.463f, 1726.89f, 51.93f},
    {1956.433f, 1597.97f, 81.75f},
    {1958.971f, 1599.01f, 81.44f}
};

// VMaNGOS script_waypoint entry 4508 (pointid, x, y, z, waittime ms)
struct ClassicRFKEscortPoint
{
    uint32 Id;
    float X, Y, Z;
    uint32 WaitMs;
};

ClassicRFKEscortPoint const WillixPath[] =
{
    {  0, 2194.38f,  1791.65f, 65.48f,  5000 },
    {  1, 2188.56f,  1805.87f, 64.45f,     0 },
    {  2, 2186.2f,   1836.28f, 59.859f, 5000 },
    {  3, 2163.27f,  1851.67f, 56.73f,     0 },
    {  4, 2140.22f,  1845.02f, 48.32f,     0 },
    {  5, 2131.5f,   1804.29f, 46.85f,     0 },
    {  6, 2096.18f,  1789.03f, 51.13f,  3000 },
    {  7, 2074.46f,  1780.09f, 55.64f,     0 },
    {  8, 2055.12f,  1768.67f, 58.46f,     0 },
    {  9, 2037.83f,  1748.62f, 60.27f,  5000 },
    { 10, 2037.51f,  1728.94f, 60.85f,     0 },
    { 11, 2044.7f,   1711.71f, 59.71f,     0 },
    { 12, 2067.66f,  1701.84f, 57.77f,     0 },
    { 13, 2078.91f,  1704.54f, 56.77f,     0 },
    { 14, 2097.65f,  1715.24f, 54.74f,  3000 },
    { 15, 2106.44f,  1720.98f, 54.41f,     0 },
    { 16, 2123.96f,  1732.56f, 52.27f,     0 },
    { 17, 2153.82f,  1728.73f, 51.92f,     0 },
    { 18, 2163.49f,  1706.33f, 54.42f,     0 },
    { 19, 2158.75f,  1695.98f, 55.7f,      0 },
    { 20, 2142.6f,   1680.72f, 58.24f,     0 },
    { 21, 2118.31f,  1671.54f, 59.21f,     0 },
    { 22, 2086.02f,  1672.04f, 61.24f,     0 },
    { 23, 2068.81f,  1658.93f, 61.24f,     0 },
    { 24, 2062.82f,  1633.31f, 64.35f,     0 },
    { 25, 2060.92f,  1600.11f, 62.41f,  3000 },
    { 26, 2063.05f,  1589.16f, 63.26f,     0 },
    { 27, 2063.67f,  1577.22f, 65.89f,     0 },
    { 28, 2057.94f,  1560.68f, 68.4f,      0 },
    { 29, 2052.56f,  1548.05f, 73.35f,     0 },
    { 30, 2045.22f,  1543.4f,  76.65f,     0 },
    { 31, 2034.35f,  1543.01f, 79.7f,      0 },
    { 32, 2029.95f,  1542.94f, 80.79f,     0 },
    { 33, 2021.34f,  1538.67f, 80.8f,      0 },
    { 34, 2012.45f,  1549.48f, 79.93f,     0 },
    { 35, 2008.05f,  1554.92f, 80.44f,     0 },
    { 36, 2006.54f,  1562.72f, 81.11f,     0 },
    { 37, 2003.8f,   1576.43f, 81.57f,     0 },
    { 38, 2000.57f,  1590.06f, 80.62f,     0 },
    { 39, 1998.96f,  1596.87f, 80.22f,     0 },
    { 40, 1991.19f,  1600.82f, 79.39f,     0 },
    { 41, 1980.71f,  1601.44f, 79.77f,     0 },
    { 42, 1967.22f,  1600.18f, 80.62f,     0 },
    { 43, 1956.43f,  1596.97f, 81.75f,     0 },
    { 44, 1954.87f,  1592.02f, 82.18f,  3000 },
    { 45, 1948.35f,  1571.35f, 80.96f, 30000 },
    { 46, 1947.02f,  1566.42f, 81.8f,  30000 }
};
}

struct classic_npc_willix_the_importer : public EscortAI
{
    classic_npc_willix_the_importer(Creature* creature) : EscortAI(creature) { }

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        EscortAI::JustAppeared();
    }

    // Exact use of these texts remains unknown, it seems that he should only talk when he initiates the attack or he is the first who is attacked by a npc
    void JustEngagedWith(Unit* who) override
    {
        switch (urand(0, 6))                                // Not always said
        {
            case 0:
                ClassicScriptText(SAY_WILLIX_AGGRO_1, me, who);
                break;
            case 1:
                ClassicScriptText(SAY_WILLIX_AGGRO_2, me, who);
                break;
            case 2:
                ClassicScriptText(SAY_WILLIX_AGGRO_3, me, who);
                break;
            case 3:
                ClassicScriptText(SAY_WILLIX_AGGRO_4, me, who);
                break;
            default:
                break;
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(me);
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_WILLIX_THE_IMPORTER)
            return;

        // After 4.0.1 set run = true (VMaNGOS starts walking)
        ResetPath();
        for (ClassicRFKEscortPoint const& point : WillixPath)
            AddWaypoint(point.Id, point.X, point.Y, point.Z, 0.0f, point.WaitMs ? Optional<Milliseconds>(Milliseconds(point.WaitMs)) : Optional<Milliseconds>(), false);

        Start(true, player->GetGUID(), quest);
        ClassicScriptText(SAY_WILLIX_READY, me, player);
        me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_PASSIVE);   // VMaNGOS SetFactionTemporary(..., TEMPFACTION_RESTORE_RESPAWN)
        me->SetImmuneToNPC(false);
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 2:
                ClassicScriptText(SAY_WILLIX_1, me);
                break;
            case 6:
                ClassicScriptText(SAY_WILLIX_2, me);
                break;
            case 9:
                ClassicScriptText(SAY_WILLIX_3, me);
                break;
            case 14:
                ClassicScriptText(SAY_WILLIX_4, me);
                // Summon 2 boars on the pathway
                me->SummonCreature(NPC_RAGING_AGAMAR, RFKBoarSpawn[0][0], RFKBoarSpawn[0][1], RFKBoarSpawn[0][2], 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 25s);
                me->SummonCreature(NPC_RAGING_AGAMAR, RFKBoarSpawn[1][0], RFKBoarSpawn[1][1], RFKBoarSpawn[1][2], 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 25s);
                break;
            case 25:
                ClassicScriptText(SAY_WILLIX_5, me);
                break;
            case 33:
                ClassicScriptText(SAY_WILLIX_6, me);
                break;
            case 44:
                ClassicScriptText(SAY_WILLIX_7, me);
                // Summon 2 boars at the end
                me->SummonCreature(NPC_RAGING_AGAMAR, RFKBoarSpawn[2][0], RFKBoarSpawn[2][1], RFKBoarSpawn[2][2], 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 25s);
                me->SummonCreature(NPC_RAGING_AGAMAR, RFKBoarSpawn[3][0], RFKBoarSpawn[3][1], RFKBoarSpawn[3][2], 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 25s);
                break;
            case 45:
                ClassicScriptText(SAY_WILLIX_END, me);
                me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                // Complete event
                if (Player* player = GetPlayerForEscort())
                    player->GroupEventHappens(QUEST_WILLIX_THE_IMPORTER, me);
                SetEscortPaused(true);
                break;
            default:
                break;
        }
    }
};

/*######
## npc_snufflenose_gopher
######*/

enum SnufflenoseGopher
{
    SPELL_SNUFFLENOSE_COMMAND   = 8283,
    NPC_SNUFFLENOSE_GOPHER      = 4781,
    GO_BLUELEAF_TUBER           = 20920,

    SAY_GOPHER_SPAWN            = 1638,
    SAY_GOPHER_COMMAND          = 1591,
    SAY_GOPHER_FOUND            = 1592,

    CLASSIC_FACTION_GOPHER      = 35
};

struct classic_npc_snufflenose_gopher : public FollowerAI
{
    classic_npc_snufflenose_gopher(Creature* creature) : FollowerAI(creature), _isMovementActive(false), _followPausedTimer(3000), _spawnHandled(false) { }

    void Reset() override
    {
        me->SetFaction(CLASSIC_FACTION_GOPHER);
        _isMovementActive = false;
        _followPausedTimer = 3000;
    }

    // VMaNGOS does this in the AI constructor
    void JustAppeared() override
    {
        FollowerAI::JustAppeared();

        if (_spawnHandled)
            return;
        _spawnHandled = true;

        ClassicScriptText(SAY_GOPHER_SPAWN, me);

        // Follow player by default
        if (Unit* unitOwner = me->GetOwner())
            if (Player* owner = unitOwner->ToPlayer())
                StartFollow(owner);

        SetFollowPaused(true);
    }

    void MovementInform(uint32 moveType, uint32 pointId) override
    {
        if (moveType != POINT_MOTION_TYPE || !pointId)
            return;

        if (!HasFollowState(STATE_FOLLOW_PAUSED))
            return;

        if (GameObject* go = me->GetMap()->GetGameObject(_targetTuberGuid))
        {
            go->SetRespawnTime(3 * MINUTE);
            go->Refresh();

            go->RemoveFlag(GO_FLAG_INTERACT_COND);
            _foundTubers.push_back(_targetTuberGuid);
        }

        // Wait for 5 seconds after uncovering tuber before following again
        _followPausedTimer = 5000;
        _isMovementActive = false;
    }

    // Function to search for new tuber in range
    void DoFindNewTuber()
    {
        std::list<GameObject*> tubersInRange;
        me->GetGameObjectListWithEntryInGrid(tubersInRange, GO_BLUELEAF_TUBER, 60.0f);

        if (tubersInRange.empty())
            return;

        tubersInRange.sort(Trinity::ObjectDistanceOrderPred(me));
        GameObject* nearestTuber = nullptr;

        // Always need to find new ones
        for (GameObject* tuber : tubersInRange)
        {
            if (IsValidTuber(tuber))
            {
                nearestTuber = tuber;
                break;
            }
        }

        if (!nearestTuber)
            return;

        ClassicScriptText(SAY_GOPHER_FOUND, me);

        _targetTuberGuid = nearestTuber->GetGUID();

        float x, y, z;
        nearestTuber->GetContactPoint(me, x, y, z);
        me->GetMotionMaster()->MovePoint(1, x, y, z);
        _isMovementActive = true;
        SetFollowPaused(true);
    }

    bool IsValidTuber(GameObject* tuber)
    {
        Unit* viewPoint = me;

        // Do LOS checks from Player if exists
        if (Unit* owner = me->GetOwner())
            viewPoint = owner;

        if (tuber->isSpawned() || !tuber->HasFlag(GO_FLAG_INTERACT_COND) || !tuber->IsWithinLOSInMap(viewPoint))
            return false;

        // Check if tuber is in list of already found tubers
        for (ObjectGuid const& guid : _foundTubers)
            if (tuber->GetGUID() == guid)
                return false;

        // Check that tuber is not more than 15 yards above or below current position
        return std::fabs(viewPoint->GetPositionZ() - tuber->GetPositionZ()) <= 15.0f;
    }

    // VMaNGOS EffectDummyCreature_npc_snufflenose_gopher (spell 8283 effect 0 on the gopher)
    // TODO(classic): VMaNGOS answers SPELL_FAILED_BAD_TARGETS when the caster has not targeted the gopher; that needs a
    // SpellScript CheckCast. Spell 8283 uses TARGET_UNIT_NEARBY_ENTRY, so a conditions row (SourceTypeOrReferenceId 13,
    // ConditionTypeOrReference 31, entry 4781) is needed in TC for the spell to find the gopher.
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_SNUFFLENOSE_COMMAND)
            return;

        Unit* unit = caster ? caster->ToUnit() : nullptr;
        if (!unit)
            return;

        // Do nothing if player has not targeted gopher
        if (unit->GetTarget() != me->GetGUID())
            return;

        ClassicScriptText(SAY_GOPHER_COMMAND, me, unit);

        if (HasFollowState(STATE_FOLLOW_PAUSED))
        {
            SetFollowPaused(false);
            _isMovementActive = false;
            _targetTuberGuid.Clear();
        }
        else
            DoFindNewTuber();
    }

    void UpdateAI(uint32 diff) override
    {
        if (_isMovementActive)
            return;

        if (_followPausedTimer < diff)
            SetFollowPaused(false);
        else
            _followPausedTimer -= diff;

        FollowerAI::UpdateAI(diff);
    }

private:
    bool _isMovementActive;
    ObjectGuid _targetTuberGuid;
    std::list<ObjectGuid> _foundTubers;
    uint32 _followPausedTimer;
    bool _spawnHandled;
};

void AddSC_classic_razorfen_kraul()
{
    RegisterCreatureAI(classic_npc_willix_the_importer);
    RegisterCreatureAI(classic_npc_snufflenose_gopher);
}
