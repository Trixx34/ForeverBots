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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/feralas/dire_maul/dreadsteed_ritual.cpp (ScriptDev2 lineage, GPL-2)
// Warlock epic mount ritual (quest 7631 "Dreadsteed of Xoroth").
// Ported: event_dreadsteed_ritual_start, event_dreadsteed_ritual_second_part, go_ritual_wheel, go_ritual_candle,
//         go_ritual_bell, boss_xorothian_dreadsteed, boss_lord_hel_nurath, go_pedestal_of_immol_thar

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_dire_maul.h"
#include "classic_script_text.h"
#include <array>
#include <cmath>
#include <initializer_list>
#include <list>

enum ClassicDMDreadsteedRitual
{
    GOBJ_WHEEL              = 179672,
    GOBJ_CANDLE             = 179673,
    GOBJ_BELL               = 179674,
    GOBJ_FEL_FIRE           = /*179676,*/179681,
    GOBJ_DREADSTEED_PORTAL  = 179681,
    GOBJ_RITUAL_CIRCLE      = 179668,
    GOBJ_PEDESTAL           = 179701,
    GOBJ_DARK_CIRCLE        = 179675,

    GOBJ_RUNE_TYPE_1        = 179669, //99776, 99779, 99782
    GOBJ_RUNE_TYPE_2        = 179670, //99775, 99778, 99781
    GOBJ_RUNE_TYPE_3        = 179671, //99774, 99777, 99780

    // VMaNGOS gameobject guids of the 9 runes (TC spawn id = 30000000 + guid)
    GOBJ_GUID_RUNE_1        = 99774,

    NPC_J_EEVEE             = 14500,
    NPC_XOROTHIAN_IMP       = 14482,
    NPC_DREAD_GUARD         = 14483,
    NPC_XOROTHIAN_DREADSTEED = 14502,
    NPC_LORD_HEL_NURATH     = 14506,
    NPC_DREADSTEED_SPIRIT   = 14504,

    SPELL_WHEEL_AURA        = 23120,
    SPELL_BELL_AURA         = 23117,
    SPELL_CANDLE_AURA       = 23226,

    SAY_HEL_NURATH          = 9727,
    SAY_DEMON_DESPAWN       = 9768,

    // VMaNGOS npc_j_eevee_dreadsteedAI::ShoutFreedom()
    SHOUT_J_EEVEE_FREEDOM   = 9705
};

uint32 const CLASSIC_DM_GO_SPAWN_ID_OFFSET = 30000000;

namespace
{
struct ClassicDMEventLocation
{
    float m_fX, m_fY, m_fZ, m_fO;
    int m_wait;
};

struct ClassicDMNodeInfo
{
    ObjectGuid highGuid;
    bool up = false;
};

// VMaNGOS SetSpawnedByDefault(x) + Refresh(): TC Refresh() does not update the visibility when the GO becomes despawned
void ClassicDMSetGoSpawned(GameObject* go, bool spawned)
{
    go->SetSpawnedByDefault(spawned);
    go->Refresh();
    go->UpdateObjectVisibility(true);
}

void ClassicDMDespawnDemons(WorldObject* source)
{
    for (uint32 entry : { uint32(NPC_XOROTHIAN_IMP), uint32(NPC_DREAD_GUARD) })
    {
        std::list<Creature*> creatures;
        source->GetCreatureListWithEntryInGrid(creatures, entry, 30.0f);
        for (Creature* creature : creatures)
        {
            if (creature->IsAlive())
                ClassicScriptText(SAY_DEMON_DESPAWN, creature);
            creature->DespawnOrUnsummon(); // VMaNGOS DisappearAndDie()
        }
    }
}
}

/*######
## go_pedestal_of_immol_thar
######*/

struct classic_go_pedestal_of_immol_thar : public GameObjectAI
{
    classic_go_pedestal_of_immol_thar(GameObject* go) : GameObjectAI(go)
    {
        //make sure immolthar is dead
        //m_pInstance->SetData(TYPE_IMMOL_THAR, DONE);  ?
        reset();
    }

    void reset()
    {
        guidRitualCircle.Clear();
        eventPhase = 0;
        gobjTimer = 9000;
        gobjStep = 0;

        waveTimer = 0;
        waveStep = 0;

        nodeTimer = urand(15000, 30000);
        nodeTimeTracker = 390000 - nodeTimer;
        nodeNb = 0;
    }

    void GenerateGlyphAndNodeGuids() //glyphs=runes
    {
        for (uint32 i = 0; i < 9; ++i)
        {
            // VMaNGOS ObjectGuid(HIGHGUID_GAMEOBJECT, GOBJ_RUNE_TYPE_3 - (i % 3), GOBJ_GUID_RUNE_1 + i)
            ObjectGuid::LowType const spawnId = CLASSIC_DM_GO_SPAWN_ID_OFFSET + GOBJ_GUID_RUNE_1 + i;
            auto bounds = me->GetMap()->GetGameObjectBySpawnIdStore().equal_range(spawnId);
            if (bounds.first != bounds.second)
                guidGlyphTab[i] = bounds.first->second->GetGUID();
            else
                TC_LOG_DEBUG("scripts", "Dreadsteed Ritual : cannot find Rune {} (spawn id {})", i, spawnId);
        }
        for (uint32 i = 0; i < 3; ++i)
        {
            if (GameObject* gobj = me->FindNearestGameObject(GOBJ_WHEEL + i, 30.0f, false))
            {
                nodes[i].highGuid = gobj->GetGUID();
                nodes[i].up = false;
            }
            else
                TC_LOG_DEBUG("scripts", "Dreadsteed Ritual : cannot find Node {}", i);
        }
        if (GameObject* gobj = me->FindNearestGameObject(GOBJ_RITUAL_CIRCLE, 30.0f, false))
            guidRitualCircle = gobj->GetGUID();
    }

    std::array<ClassicDMNodeInfo, 3> nodes;
    std::array<ObjectGuid, 9> guidGlyphTab;
    std::array<ObjectGuid, 18> guidFlameTab;
    ObjectGuid guidRitualCircle;
    uint32 gobjTimer = 0;
    uint32 gobjStep = 0;
    uint32 waveTimer = 0;
    uint32 waveStep = 0;
    uint32 nodeTimer = 0;
    uint32 nodeNb = 0;
    uint32 nodeTimeTracker = 0;
    uint8 eventPhase = 0; //0: nothing happened, default. 1: phase with jeevee setting the gobjs 2: phase with demon waves 3: waiting for item use for next 4: dreadsteed&owner

    std::array<ClassicDMEventLocation, 18> spawnPoints = { }; //not using m_wait though

    bool EventStart(ObjectGuid playerGuid)
    {
        if (eventPhase != 0)
            return false;
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetGuidData(DATA_DREADSTEED_RITUAL_PLAYER, playerGuid);
        GenerateGlyphAndNodeGuids();
        eventPhase = 1;
        if (Creature* jeevee = me->SummonCreature(NPC_J_EEVEE, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 420s))
        {
            // VMaNGOS: dynamic_cast<npc_j_eevee_dreadsteedAI*>(jeevee->AI()) -> ShoutFreedom(); SetPlayerGuid(playerGuid);
            // TODO(classic): npc_j_eevee lives in VMaNGOS world/npc_j_eevee.cpp (shared with Scholomance) and is not part of this
            // port; its Classic port must accept the ritual player via SetGUID(guid, DM_JEEVEE_GUID_PLAYER).
            ClassicScriptText(SHOUT_J_EEVEE_FREEDOM, jeevee);
            if (jeevee->AI())
                jeevee->AI()->SetGUID(playerGuid, DM_JEEVEE_GUID_PLAYER);
        }
        return true;
    }

    void EventSecondPartStart()
    {
        if (eventPhase != 3)
            return;
        eventPhase = 4;
        waveTimer = 8000;
        waveStep = 0;
        if (GameObject* gobj = me->GetMap()->GetGameObject(guidRitualCircle))
        {
            gobj->SetGoState(GO_STATE_READY);
            gobj->SetSpawnedByDefault(false);
            gobj->SetLootState(GO_READY);
            gobj->SetRespawnTime(1);
        }
        if (GameObject* gobj = me->FindNearestGameObject(GOBJ_DREADSTEED_PORTAL, 10.0f, false))
            ClassicDMSetGoSpawned(gobj, true);
        gobjTimer = 10000;
    }

    void PhaseTwoEndedSuccess()
    {
        if (GameObject* gobj = me->GetMap()->GetGameObject(guidRitualCircle))
            gobj->SetGoState(GO_STATE_ACTIVE);

        //write it in m_pInstance?
        for (ObjectGuid const& guid : guidFlameTab)
        {
            if (GameObject* gobj = me->GetMap()->GetGameObject(guid))
                gobj->DespawnOrUnsummon();
        }
        ++eventPhase;

        ClassicDMDespawnDemons(me);
    }

    void EventEndedFail()
    {
        eventPhase = 0;
        for (ObjectGuid const& guid : guidFlameTab)
        {
            if (GameObject* gobj = me->GetMap()->GetGameObject(guid))
                gobj->DespawnOrUnsummon();
        }
        for (ObjectGuid const& guid : guidGlyphTab)
        {
            if (GameObject* gobj = me->GetMap()->GetGameObject(guid))
                ClassicDMSetGoSpawned(gobj, false);
        }
        if (GameObject* gobj = me->GetMap()->GetGameObject(guidRitualCircle))
        {
            gobj->SetGoState(GO_STATE_READY);
            ClassicDMSetGoSpawned(gobj, false);
        }
        for (ClassicDMNodeInfo const& node : nodes)
        {
            if (GameObject* gobj = me->GetMap()->GetGameObject(node.highGuid))
                ClassicDMSetGoSpawned(gobj, false);
        }
        ClassicDMDespawnDemons(me);
        reset();
    }

    void SpawnNode(uint32 index)
    {
        if (GameObject* gobj = me->GetMap()->GetGameObject(nodes[index].highGuid))
        {
            ClassicDMSetGoSpawned(gobj, true);
            gobj->SetGoState(GO_STATE_ACTIVE);
            gobj->SetFlag(GO_FLAG_IN_USE);
            nodes[index].up = true;
        }
    }

    void gobjNextStep()
    {
        //switch event phase
        switch (eventPhase)
        {
            case 1:
                switch (gobjStep)
                {
                    case 0:
                        //spawn Bell.
                        SpawnNode(2);
                        gobjTimer = 5000;
                        ++gobjStep;
                        break;
                    case 1:
                        //spawn Wheel
                        SpawnNode(0);
                        gobjTimer = 6000;
                        ++gobjStep;
                        break;
                    case 2:
                        //spawn Candle
                        SpawnNode(1);
                        gobjTimer = 7000;
                        ++gobjStep;
                        break;
                    case 3:
                        gobjTimer = 0;
                        ++eventPhase;
                        gobjStep = 0;
                        break;
                    default:
                        break;
                }
                break;
            case 2:
                switch (gobjStep)
                {
                    case 0:
                    {
                        //spawn flames & circle
                        for (int i = 1; i <= 18; ++i)
                        {
                            float angle = float(i * 2 * M_PI / 18) + 0.2f; //+0.2 to see
                            float x = me->GetPositionX() + 56.7f * std::cos(angle);
                            float y = me->GetPositionY() + 56.7f * std::sin(angle);
                            // VMaNGOS respawn time 480000 (ms)
                            if (GameObject* gobj = me->SummonGameObject(GOBJ_FEL_FIRE, x, y, -29.8f, 0.0f, QuaternionData::fromEulerAnglesZYX(0.0f, 0.0f, 0.0f), 480s, GO_SUMMON_TIMED_DESPAWN))
                                guidFlameTab[i - 1] = gobj->GetGUID();
                        }
                        if (GameObject* gobj = me->GetMap()->GetGameObject(guidRitualCircle))
                        {
                            gobj->SetGoState(GO_STATE_READY);
                            ClassicDMSetGoSpawned(gobj, true); // circle
                        }
                        gobjTimer = 45000;
                        ++gobjStep;
                        break;
                    }
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                        if (GameObject* gobj = me->GetMap()->GetGameObject(guidGlyphTab[gobjStep - 1]))
                            ClassicDMSetGoSpawned(gobj, true);
                        gobjTimer = 45000;
                        ++gobjStep;
                        break;
                    case 9:
                        //last glyph
                        if (GameObject* gobj = me->GetMap()->GetGameObject(guidGlyphTab[8]))
                        {
                            ClassicDMSetGoSpawned(gobj, true);
                            gobj->SendCustomAnim(0);
                        }
                        gobjTimer = 0;
                        ++gobjStep;
                        break;
                    case 10:
                    {
                        //well basicaly pause before p3. wait for 3 nodes to be back up
                        uint8 nbOkNodes = 0;
                        for (ClassicDMNodeInfo const& node : nodes)
                        {
                            if (node.up)
                                ++nbOkNodes;
                        }
                        if (nbOkNodes == 3)
                            PhaseTwoEndedSuccess();
                        gobjTimer = 20000;
                        break;
                    }
                    default:
                        break;
                }
                break;
            case 4:
                if (GameObject* gobj = me->FindNearestGameObject(GOBJ_DREADSTEED_PORTAL, 10.0f, false))
                    ClassicDMSetGoSpawned(gobj, false);
                ++gobjStep;
                break;
            default:
                break;
        }
    }

    void MoveDemonToPedestal(Creature* crea)
    {
        crea->SetFacingToObject(me);
        float x = me->GetPositionX();
        float y = me->GetPositionY();
        float z = me->GetPositionZ();
        crea->SetHomePosition(x, y, z, 0);
        crea->GetMotionMaster()->Clear();
        crea->GetMotionMaster()->Initialize();
        crea->GetMotionMaster()->MovePoint(1, x, y, z);
    }

    void SummonImp()
    {
        uint8 i = uint8(urand(0, 17));
        if (Creature* crea = me->SummonCreature(NPC_XOROTHIAN_IMP, spawnPoints[i].m_fX, spawnPoints[i].m_fY, spawnPoints[i].m_fZ, 0, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600s))
        {
            crea->SetWalk(false);
            MoveDemonToPedestal(crea);
        }
    }

    void SummonGuard()
    {
        uint8 i = uint8(urand(0, 17));
        if (Creature* crea = me->SummonCreature(NPC_DREAD_GUARD, spawnPoints[i].m_fX, spawnPoints[i].m_fY, spawnPoints[i].m_fZ, 0, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600s))
        {
            //crea->SetWalk(false);
            MoveDemonToPedestal(crea);
        }
    }

    void WaveSpawn()
    {
        float x = me->GetPositionX();
        float y = me->GetPositionY();
        if (eventPhase == 2)
        {
            switch (waveStep) //TODO
            {
                case 0:
                    for (int i = 1; i <= 18; ++i)
                    {
                        float angle = float(i * 2 * M_PI / 18) + 0.2f;
                        // VMaNGOS accumulates the offset on x/y (reset to the pedestal position only after a successful summon)
                        x += 63.0f * std::cos(angle);
                        y += 63.0f * std::sin(angle);

                        spawnPoints[i - 1].m_fX = x;
                        spawnPoints[i - 1].m_fY = y;
                        spawnPoints[i - 1].m_fZ = -28; //need to check z.
                        if (Creature* crea = me->SummonCreature(NPC_XOROTHIAN_IMP, x, y, -28, 0, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600s))
                        {
                            MoveDemonToPedestal(crea);
                            x = me->GetPositionX();
                            y = me->GetPositionY();
                        }
                    }
                    waveTimer = 60000;
                    ++waveStep;
                    break;
                case 1:
                    SummonImp();
                    SummonImp();
                    SummonGuard();
                    //+1 +1
                    waveTimer = 11000;
                    ++waveStep;
                    break;
                case 2:
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    waveTimer = 9000;
                    ++waveStep;
                    break;
                case 3:
                    SummonImp();
                    SummonGuard();
                    waveTimer = 10000;
                    ++waveStep;
                    break;
                case 4:
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    waveTimer = 28000;
                    ++waveStep;
                    break;
                case 5:
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    waveTimer = 20000;
                    ++waveStep;
                    break;
                case 6:
                    SummonImp();
                    SummonGuard();
                    waveTimer = 8000;
                    ++waveStep;
                    break;
                case 7:
                    SummonImp();
                    SummonImp();
                    waveTimer = 19000;
                    ++waveStep;
                    break;
                case 8: //2:51
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    waveTimer = 12000;
                    ++waveStep;
                    break;
                case 9: //3:42
                    SummonGuard();
                    waveTimer = 20000;
                    ++waveStep;
                    break;
                case 10:
                    SummonImp();
                    waveTimer = 23000;
                    ++waveStep;
                    break;
                case 11:
                    SummonImp();
                    waveTimer = 5000;
                    ++waveStep;
                    break;
                case 12:
                    SummonGuard();
                    //+1guard+1+1+x+1+1
                    waveTimer = 25000;
                    ++waveStep;
                    break;
                case 13:
                    SummonImp();
                    SummonImp();
                    SummonImp();
                    //+1+1
                    waveTimer = 28000;
                    ++waveStep;
                    break;
                case 14:
                    SummonGuard();
                    //+1+1+3
                    waveTimer = 39000;
                    ++waveStep;
                    break;
                case 15:
                    SummonImp();
                    waveTimer = 4000;
                    ++waveStep;
                    break;
                case 16: //5:23
                    SummonGuard();
                    //+1+2+1
                    waveTimer = 20000;
                    ++waveStep;
                    break;
                case 17:
                    SummonImp();
                    //+1
                    waveTimer = 20000;
                    ++waveStep;
                    break;
                case 18: //6:19
                    SummonImp();
                    waveTimer = 12000;
                    ++waveStep;
                    break;
                case 19:
                    SummonImp();
                    SummonGuard();
                    waveTimer = 50000;
                    ++waveStep;
                    break;
                case 20: //6:29
                    SummonImp();
                    SummonGuard();
                    waveTimer = 20000;
                    ++waveStep;
                    break;
                case 21:
                    SummonImp();
                    waveTimer = 4000;
                    ++waveStep;
                    break;
                case 22:
                    SummonImp();
                    waveTimer = 8000;
                    ++waveStep;
                    break;
                case 23:
                    waveTimer = 0;
                    break;
                default:
                    break;
            }
        }
        else if (eventPhase == 4)
        {
            switch (waveStep)
            {
                case 0:
                    //pop horse
                    me->SummonCreature(NPC_XOROTHIAN_DREADSTEED, -39.0447f, 812.591f, -29.4525f, 1.39626f, TEMPSUMMON_DEAD_DESPAWN);
                    waveTimer = 10000;
                    ++waveStep;
                    break;
                case 1:
                    if (Creature* crea = me->SummonCreature(NPC_LORD_HEL_NURATH, -39.5763f, 812.786f, -29.4525f, 2.26893f, TEMPSUMMON_DEAD_DESPAWN))
                    {
                        ClassicScriptText(SAY_HEL_NURATH, crea);
                        crea->m_Events.AddEventAtOffset([crea]()
                        {
                            crea->SetImmuneToPC(false); // VMaNGOS RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER)
                        }, 5s);
                    }
                    waveTimer = 210000;
                    ++waveStep;
                    break;
                default:
                    break;
            }
        }
    }

    void BreakNode()
    {
        uint8 nbOkNodes = 0;
        for (ClassicDMNodeInfo const& node : nodes)
        {
            if (node.up)
                ++nbOkNodes;
        }
        if (nbOkNodes < 2)
        {
            EventEndedFail();
            return;
        }
        uint8 nodeToBreak = uint8(urand(0, nbOkNodes - 1));
        if (nbOkNodes == 2)
        {
            uint8 count = 0;
            for (uint8 i = 0; i < 3; ++i)
            {
                if (nodes[i].up)
                {
                    if (nodeToBreak == count)
                    {
                        nodeToBreak = i;
                        break;
                    }
                    ++count;
                }
            }
        }

        switch (nodeNb)
        {
            case 0:
            case 1:
            case 2:
            case 3:
                nodeTimer = urand(35000, 45000);
                nodeTimeTracker -= nodeTimer;
                break;
            case 4:
            case 5:
            case 6:
            case 7:
                if ((nodeTimeTracker > (7 - nodeNb) * 41000 + 71000) && urand(0, 1))
                    nodeTimer = 70000;
                else
                    nodeTimer = 41000;
                nodeTimeTracker -= nodeTimer;
                break;
            default:
                nodeTimer = 41000;
                break;
        }

        nodes[nodeToBreak].up = false;
        if (GameObject* gobj = me->GetMap()->GetGameObject(nodes[nodeToBreak].highGuid))
        {
            gobj->SetGoState(GO_STATE_READY);
            gobj->RemoveFlag(GO_FLAG_IN_USE);
        }
        ++nodeNb;
    }

    void UpdateAI(uint32 diff) override
    {
        if (eventPhase == 1 || eventPhase == 2 || eventPhase == 4) //and phase < ended
        {
            if (gobjTimer < diff)
                gobjNextStep();
            else
                gobjTimer -= diff;
        }
        if (eventPhase == 2 || eventPhase == 4) //and phase < ended
        {
            if (waveTimer < diff)
                WaveSpawn();
            else
                waveTimer -= diff;
        }
        if (eventPhase == 2) //and phase < ended
        {
            if (nodeTimer < diff)
                BreakNode();
            else
                nodeTimer -= diff;
        }
    }

    //keep an eye on nodes
    void NodeUpped(GameObject* go)
    {
        for (ClassicDMNodeInfo& node : nodes)
        {
            if (node.highGuid == go->GetGUID())
            {
                node.up = true;
                break;
            }
        }
    }
};

/*######
## event_dreadsteed_ritual_start
######*/

class classic_event_dreadsteed_ritual_start : public EventScript
{
public:
    classic_event_dreadsteed_ritual_start() : EventScript("classic_event_dreadsteed_ritual_start") { }

    // object = VMaNGOS target (the pedestal), invoker = VMaNGOS source (the player)
    void OnTrigger(WorldObject* object, WorldObject* invoker, uint32 /*eventId*/) override
    {
        if (!object || !invoker)
            return;

        if (GameObject* go = object->ToGameObject())
            if (classic_go_pedestal_of_immol_thar* pedestalAI = dynamic_cast<classic_go_pedestal_of_immol_thar*>(go->AI()))
                pedestalAI->EventStart(invoker->GetGUID());
    }
};

/*######
## go_ritual_wheel / go_ritual_candle / go_ritual_bell
######*/

struct classic_dm_go_ritual_node_base : public GameObjectAI
{
    classic_dm_go_ritual_node_base(GameObject* go, uint32 refreshTimer, uint32 spellId) : GameObjectAI(go),
        timer(0), refreshTime(refreshTimer), spell(spellId) { }

    uint32 timer;
    uint32 refreshTime;
    uint32 spell;

    // VMaNGOS GOHello_go_ritual_node
    bool OnGossipHello(Player* /*player*/) override
    {
        me->SetGoState(GO_STATE_ACTIVE);
        me->SetFlag(GO_FLAG_IN_USE);
        if (GameObject* pedestal = me->FindNearestGameObject(GOBJ_PEDESTAL, 30.0f))
        {
            if (classic_go_pedestal_of_immol_thar* pedestalAI = dynamic_cast<classic_go_pedestal_of_immol_thar*>(pedestal->AI()))
                pedestalAI->NodeUpped(me);
        }
        else
            TC_LOG_DEBUG("scripts", "Dreadsteed Ritual : GOBJ_PEDESTAL not found");
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (me->GetGoState() == GO_STATE_ACTIVE && me->isSpawned())
        {
            if (timer < diff)
            {
                if (InstanceScript* instance = me->GetInstanceScript())
                {
                    if (GameObject* candleAura = me->GetMap()->GetGameObject(instance->GetGuidData(GO_RITUAL_CANDLE_AURA)))
                    {
                        if (Player* target = me->GetMap()->GetPlayer(instance->GetGuidData(DATA_DREADSTEED_RITUAL_PLAYER)))
                        {
                            if (spell == SPELL_CANDLE_AURA)
                                candleAura->CastSpell(target, spell, true);
                            else
                                me->CastSpell(target, spell, true);
                        }
                    }
                }

                timer = refreshTime/*5000*/;
            }
            else
                timer -= diff;
        }
        else
            timer = 0;
    }
};

struct classic_go_ritual_wheel : public classic_dm_go_ritual_node_base
{
    classic_go_ritual_wheel(GameObject* go) : classic_dm_go_ritual_node_base(go, 5000, SPELL_WHEEL_AURA) { }
};

struct classic_go_ritual_candle : public classic_dm_go_ritual_node_base
{
    classic_go_ritual_candle(GameObject* go) : classic_dm_go_ritual_node_base(go, 5000, SPELL_CANDLE_AURA) { }
};

struct classic_go_ritual_bell : public classic_dm_go_ritual_node_base
{
    classic_go_ritual_bell(GameObject* go) : classic_dm_go_ritual_node_base(go, 5000, SPELL_BELL_AURA) { }
};

/*######
## event_dreadsteed_ritual_second_part
######*/

class classic_event_dreadsteed_ritual_second_part : public EventScript
{
public:
    classic_event_dreadsteed_ritual_second_part() : EventScript("classic_event_dreadsteed_ritual_second_part") { }

    void OnTrigger(WorldObject* object, WorldObject* /*invoker*/, uint32 /*eventId*/) override
    {
        if (!object)
            return;

        if (GameObject* pedestal = object->FindNearestGameObject(GOBJ_PEDESTAL, 10.0f))
            if (classic_go_pedestal_of_immol_thar* pedestalAI = dynamic_cast<classic_go_pedestal_of_immol_thar*>(pedestal->AI()))
                pedestalAI->EventSecondPartStart();
    }
};

/*######
## boss_lord_hel_nurath
######*/

enum ClassicDMHelNurath
{
    //spells are absolutely certain.
    SPELL_BERSERKER_CHARGE          = 16636, //OK
    SPELL_FLAME_BUFFET              = 22713, //OK
    SPELL_SUMMON_DREADSTEED_SPIRIT  = 23159, //works while dead?
    SPELL_SHADOW_WORD               = 17146, //OK
    SPELL_VEIL_OF_SHADOW            = 23224, //OK
    SPELL_SLEEP                     = 20989, //OK
    SPELL_KNOCK_AWAY                = 18670  //OK
};

struct classic_boss_lord_hel_nurath : public ScriptedAI
{
    classic_boss_lord_hel_nurath(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiShadowWord_Timer = 0;
    uint32 m_uiVielOfShadow_Timer = 0;
    uint32 m_uiSleep_Timer = 0;
    uint32 m_uiKnockAway_Timer = 0;

    void Reset() override
    {
        m_uiShadowWord_Timer = 28000;
        m_uiVielOfShadow_Timer = 16000;
        m_uiSleep_Timer = 21000;
        m_uiKnockAway_Timer = 20000; //less than 20s
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiShadowWord_Timer < diff)
        {
            // remember to check that it changes target if the target is asleep
            if (DoCastVictim(SPELL_SHADOW_WORD) == SPELL_CAST_OK)
                m_uiShadowWord_Timer = urand(10000, 30000);
        }
        else
            m_uiShadowWord_Timer -= diff;

        if (m_uiVielOfShadow_Timer < diff)
        {
            if (DoCastVictim(SPELL_VEIL_OF_SHADOW) == SPELL_CAST_OK)
                m_uiVielOfShadow_Timer = urand(20000, 85000);
        }
        else
            m_uiVielOfShadow_Timer -= diff;

        if (m_uiSleep_Timer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                if (DoCast(target, SPELL_SLEEP) == SPELL_CAST_OK)
                    m_uiSleep_Timer = urand(15000, 36000);
        }
        else
            m_uiSleep_Timer -= diff;

        if (m_uiKnockAway_Timer < diff)
        {
            if (DoCastVictim(SPELL_KNOCK_AWAY) == SPELL_CAST_OK)
                m_uiKnockAway_Timer = urand(6000, 10000);
        }
        else
            m_uiKnockAway_Timer -= diff;
    }
};

/*######
## boss_xorothian_dreadsteed
######*/

struct classic_boss_xorothian_dreadsteed : public ScriptedAI
{
    classic_boss_xorothian_dreadsteed(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiCharge_Timer = 0;
    uint32 m_uiFlameBuffet_Timer = 0;

    void Reset() override
    {
        m_uiCharge_Timer = 8000;
        m_uiFlameBuffet_Timer = 10000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (m_uiCharge_Timer < diff)
        {
            // remember to check that it changes target if the target is asleep
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                if (DoCast(target, SPELL_BERSERKER_CHARGE) == SPELL_CAST_OK)
                    m_uiCharge_Timer = urand(10000, 18000);
        }
        else
            m_uiCharge_Timer -= diff;

        if (m_uiFlameBuffet_Timer < diff)
        {
            if (DoCastVictim(SPELL_FLAME_BUFFET) == SPELL_CAST_OK)
                m_uiFlameBuffet_Timer = urand(7000, 12000);
        }
        else
            m_uiFlameBuffet_Timer -= diff;
    }

    void JustDied(Unit* /*killer*/) override
    {
        // TODO(classic): TC may refuse a cast from a dead caster unless the spell allows it (VMaNGOS: "works while dead?")
        me->CastSpell(me, SPELL_SUMMON_DREADSTEED_SPIRIT, true);
    }
};

void AddSC_classic_dreadsteed_ritual()
{
    new classic_event_dreadsteed_ritual_start();
    new classic_event_dreadsteed_ritual_second_part();
    RegisterGameObjectAI(classic_go_ritual_wheel);
    RegisterGameObjectAI(classic_go_ritual_candle);
    RegisterGameObjectAI(classic_go_ritual_bell);
    RegisterCreatureAI(classic_boss_xorothian_dreadsteed);
    RegisterCreatureAI(classic_boss_lord_hel_nurath);
    RegisterGameObjectAI(classic_go_pedestal_of_immol_thar);
}
