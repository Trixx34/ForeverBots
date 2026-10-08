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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/tanaris/zulfarrak/zulfarrak.cpp (TrinityCore/ScriptDev2 lineage, GPL-2)
// Ported: npc_sergeant_bly (7604), npc_weegli_blastfuse (7607), go_shallow_grave (128308), go_troll_cage (141070-141074),
//         go_table_theka (142715), ward_zumrah (7785), at_zumrah, at_antusul

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DB2Structure.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_zulfarrak.h"

namespace
{
enum ClassicZFBlyAndCrewFactions
{
    FACTION_ZF_HOSTILE          = 14,
    FACTION_ZF_FRIENDLY         = 35,   // while in cages (so the trolls won't attack them while they're caged)
    FACTION_ZF_FREED            = 250   // after release (so they'll be hostile towards trolls)
};

enum ClassicZFActions
{
    ACTION_ZF_BLY_START_FIGHT   = 1,    // VMaNGOS npc_sergeant_blyAI::OnScriptEventHappened
    ACTION_ZF_WEEGLI_DOOR       = 2     // VMaNGOS npc_weegli_blastfuseAI::OnScriptEventHappened
};

void ClassicZFSwitchFactionIfAlive(InstanceScript* instance, Creature* source, uint32 entry)
{
    if (Creature* crew = ObjectAccessor::GetCreature(*source, instance->GetGuidData(entry)))
        if (crew->IsAlive())
            crew->SetFaction(FACTION_ZF_HOSTILE);
}
}

/*######
## npc_sergeant_bly
######*/

enum ClassicZFBly
{
    SAY_ZF_BLY_1                = 3882,
    SAY_ZF_BLY_2                = 3884,
    SAY_ZF_BLY_WEEGLI           = 3811,

    SPELL_ZF_SHIELD_BASH        = 11972,
    SPELL_ZF_REVENGE            = 12170,

    GOSSIP_ZF_BLY_NOT_STARTED   = 1515,
    GOSSIP_ZF_BLY_IN_PROGRESS   = 1516,
    GOSSIP_ZF_BLY_FIGHT         = 1517
};

#define GOSSIP_ZF_BLY_TEXT "That's it! I'm tired of helping you out.  It's time we settled things on the battlefield!"

struct classic_npc_sergeant_bly : public ScriptedAI
{
    classic_npc_sergeant_bly(Creature* creature) : ScriptedAI(creature), _postGossipStep(0), _textTimer(0),
        _shieldBashTimer(5000), _revengeTimer(8000)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override
    {
        _shieldBashTimer = 5000;
        _revengeTimer = 8000;
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_ZF_BLY_START_FIGHT)
            return;

        _postGossipStep = 1;
        _textTimer = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_instance)
            return;

        if (_postGossipStep > 0 && _postGossipStep < 4)
        {
            if (_textTimer < diff)
            {
                switch (_postGossipStep)
                {
                    case 1:
                        ClassicScriptText(SAY_ZF_BLY_1, me);
                        _textTimer = 5000;
                        break;
                    case 2:
                        ClassicScriptText(SAY_ZF_BLY_2, me);
                        _textTimer = 5000;
                        break;
                    case 3:
                        me->SetFaction(FACTION_ZF_HOSTILE);
                        if (Player* target = ObjectAccessor::GetPlayer(*me, _playerGUID))
                            AttackStart(target);
                        // weegli doesn't fight - he goes & blows up the door
                        if (Creature* weegli = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(ENTRY_ZF_WEEGLI)))
                        {
                            if (weegli->AI())
                                weegli->AI()->DoAction(ACTION_ZF_WEEGLI_DOOR);
                            ClassicScriptText(SAY_ZF_BLY_WEEGLI, weegli);
                        }

                        ClassicZFSwitchFactionIfAlive(_instance, me, ENTRY_ZF_RAVEN);
                        ClassicZFSwitchFactionIfAlive(_instance, me, ENTRY_ZF_ORO);
                        ClassicZFSwitchFactionIfAlive(_instance, me, ENTRY_ZF_MURTA);
                        break;
                    default:
                        break;
                }
                ++_postGossipStep;
            }
            else
                _textTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (_shieldBashTimer <= diff)
        {
            DoCastVictim(SPELL_ZF_SHIELD_BASH);
            _shieldBashTimer = 15000;
        }
        else
            _shieldBashTimer -= diff;

        // this is wrong, spell should never be used unless the victim dodge, parry or block attack (VMaNGOS comment)
        if (_revengeTimer <= diff)
        {
            DoCastVictim(SPELL_ZF_REVENGE);
            _revengeTimer = 10000;
        }
        else
            _revengeTimer -= diff;
    }

    bool OnGossipHello(Player* player) override
    {
        if (!_instance)
            return false;

        uint32 const phase = _instance->GetData(EVENT_ZF_PYRAMID);
        if (phase == PYRAMID_ZF_KILLED_ALL_TROLLS)
        {
            AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ZF_BLY_TEXT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            SendGossipMenuFor(player, GOSSIP_ZF_BLY_FIGHT, me->GetGUID());
        }
        else if (phase == PYRAMID_ZF_NOT_STARTED)
            SendGossipMenuFor(player, GOSSIP_ZF_BLY_NOT_STARTED, me->GetGUID());
        else
            SendGossipMenuFor(player, GOSSIP_ZF_BLY_IN_PROGRESS, me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            CloseGossipMenuFor(player);
            _playerGUID = player->GetGUID();
            DoAction(ACTION_ZF_BLY_START_FIGHT);
        }
        return true;
    }

private:
    InstanceScript* _instance;
    uint32 _postGossipStep;
    uint32 _textTimer;
    uint32 _shieldBashTimer;
    uint32 _revengeTimer;
    ObjectGuid _playerGUID;
};

/*######
## go_troll_cage
######*/

namespace
{
void ClassicZFInitBlyCrewMember(InstanceScript* instance, GameObject* source, uint32 entry, float x, float y, float z)
{
    if (Creature* crew = ObjectAccessor::GetCreature(*source, instance->GetGuidData(entry)))
    {
        // (VMaNGOS SetCombatStartPosition(x, y, z): no TC equivalent, the home position covers it)
        crew->SetHomePosition(x, y, z, 4.7f);
        crew->GetMotionMaster()->MovePoint(1, x, y, z, true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceWalk);
        crew->SetFaction(FACTION_ZF_FREED);
    }
}
}

struct classic_go_troll_cage : public GameObjectAI
{
    classic_go_troll_cage(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
        {
            instance->SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_CAGES_OPEN);
            // set bly & co to aggressive & start moving to top of stairs
            ClassicZFInitBlyCrewMember(instance, me, ENTRY_ZF_BLY, 1887.17f, 1263.72f, 41.484f);
            ClassicZFInitBlyCrewMember(instance, me, ENTRY_ZF_RAVEN, 1890.76f, 1265.82f, 41.43f);
            ClassicZFInitBlyCrewMember(instance, me, ENTRY_ZF_ORO, 1883.3f, 1272.53f, 41.87f);
            ClassicZFInitBlyCrewMember(instance, me, ENTRY_ZF_WEEGLI, 1883.87f, 1263.49f, 41.55f);
            ClassicZFInitBlyCrewMember(instance, me, ENTRY_ZF_MURTA, 1886.48f, 1272.76f, 41.76f);
        }
        return false;
    }
};

/*######
## npc_weegli_blastfuse
######*/

enum ClassicZFWeegli
{
    SPELL_ZF_BOMB               = 8858,
    SPELL_ZF_GOBLIN_LAND_MINE   = 21688,
    SPELL_ZF_SHOOT              = 6660,
    SPELL_ZF_WEEGLIS_BARREL     = 10772,
    SPELL_ZF_EXPLOSIVE_CHARGE   = 13259,

    GO_ZF_EXPLOSIVE_CHARGE      = 144065,

    SAY_ZF_WEEGLI_OHNO          = 3744,
    SAY_ZF_WEEGLI_OK_I_GO       = 3785,
    SAY_ZF_CHIEF_UKORZ_DOOR     = 6067,

    GOSSIP_ZF_WEEGLI_NOT_STARTED = 1511,
    GOSSIP_ZF_WEEGLI_IN_PROGRESS = 1513,
    GOSSIP_ZF_WEEGLI_DOOR        = 1514
};

#define GOSSIP_ZF_WEEGLI_TEXT "Will you blow up that door now?"

struct classic_npc_weegli_blastfuse : public ScriptedAI
{
    classic_npc_weegli_blastfuse(Creature* creature) : ScriptedAI(creature), _bombTimer(10000),
        _destroyingDoor(false), _runAway(false), _disappear(false), _regen(false)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override { }

    void DoAction(int32 action) override
    {
        if (action == ACTION_ZF_WEEGLI_DOOR)
            DestroyDoor();
    }

    void HealIfAlive(uint32 entry)
    {
        if (Creature* crew = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(entry)))
            if (crew->IsAlive())
                crew->SetFullHealth();
    }

    void AssistIfIdle(uint32 entry, Unit* victim)
    {
        if (Creature* crew = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(entry)))
            if (crew->IsAlive() && !crew->GetVictim() && crew->AI())
                crew->AI()->AttackStart(victim);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_instance)
            return;

        if (!_regen && _instance->GetData(EVENT_ZF_PYRAMID) == PYRAMID_ZF_KILLED_ALL_TROLLS)
        {
            _regen = true;

            HealIfAlive(ENTRY_ZF_ORO);
            HealIfAlive(ENTRY_ZF_MURTA);
            HealIfAlive(ENTRY_ZF_BLY);
            HealIfAlive(ENTRY_ZF_RAVEN);

            if (me->IsAlive())
                me->SetFullHealth();
        }

        if (!UpdateVictim())
            return;

        if (_instance->GetData(EVENT_ZF_PYRAMID) != PYRAMID_ZF_KILLED_ALL_TROLLS)
        {
            AssistIfIdle(ENTRY_ZF_ORO, me->GetVictim());
            AssistIfIdle(ENTRY_ZF_MURTA, me->GetVictim());
            AssistIfIdle(ENTRY_ZF_BLY, me->GetVictim());
        }

        if (_bombTimer < diff)
        {
            DoCastVictim(SPELL_ZF_BOMB);
            _bombTimer = 10000;
        }
        else
            _bombTimer -= diff;

        if (me->isAttackReady() && !me->IsWithinMeleeRange(me->GetVictim()))
        {
            DoCastVictim(SPELL_ZF_SHOOT);
            me->SetSheath(SHEATH_STATE_RANGED);
        }
        else
            me->SetSheath(SHEATH_STATE_MELEE);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (!_instance || type != POINT_MOTION_TYPE)
            return;

        if (_instance->GetData(EVENT_ZF_PYRAMID) == PYRAMID_ZF_CAGES_OPEN)
        {
            if (id == 1)
            {
                _instance->SetData(EVENT_ZF_PYRAMID, PYRAMID_ZF_ARRIVED_AT_STAIR);
                ClassicScriptText(SAY_ZF_WEEGLI_OHNO, me);
                me->SetWalk(false);
                me->GetMotionMaster()->MovePoint(2, 1883.27f, 1268.72f, 41.73f);
            }
        }
        else if (_instance->GetData(EVENT_ZF_PYRAMID) == PYRAMID_ZF_WAVE_1 && id == 2)
        {
            me->GetMotionMaster()->MovePoint(3, 1888.55f, 1272.19f, 41.67f);
            me->SetHomePosition(1888.55f, 1272.19f, 41.67f, 4.7f);
            me->SetWalk(true);
        }
        else
        {
            if (_destroyingDoor)
            {
                if (GameObject* go = me->SummonGameObject(GO_ZF_EXPLOSIVE_CHARGE, 1856.314209f, 1144.990479f, 15.486275f, 5.6635f,
                    QuaternionData::fromEulerAnglesZYX(5.6635f, 0.0f, 0.0f), 0s))
                    _explosiveGUID = go->GetGUID();
                _destroyingDoor = false;
                RunAfterExplosion1();
            }
            if (_runAway && id == 1)
            {
                if (GameObject* boom = me->GetMap()->GetGameObject(_explosiveGUID))
                {
                    boom->SetSpellId(SPELL_ZF_EXPLOSIVE_CHARGE);
                    boom->UseDoorOrButton();
                }
                _instance->DoUseDoorOrButton(_instance->GetGuidData(GO_ZF_END_DOOR), 0, true);
                _instance->SetData(EVENT_ZF_END_DOOR, DONE);
                if (Creature* chief = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(ENTRY_ZF_UKORZ)))
                    ClassicScriptText(SAY_ZF_CHIEF_UKORZ_DOOR, chief);
                RunAfterExplosion2();
                _runAway = false;
            }
            if (_disappear && id == 2)
                me->DespawnOrUnsummon();
        }
    }

    void DestroyDoor()
    {
        if (me->IsAlive())
        {
            me->SetFaction(FACTION_ZF_FRIENDLY);
            me->SetWalk(false);
            me->GetMotionMaster()->MovePoint(0, 1858.57f, 1146.35f, 14.745f);
            ClassicScriptText(SAY_ZF_WEEGLI_OK_I_GO, me);
            _destroyingDoor = true;
        }
    }

    void RunAfterExplosion1()
    {
        if (me->IsAlive())
        {
            me->GetMotionMaster()->MovePoint(1, 1863.77f, 1176.99f, 9.993f);
            _runAway = true;
        }
    }

    void RunAfterExplosion2()
    {
        if (me->IsAlive())
        {
            me->GetMotionMaster()->MovePoint(2, 1827.1f, 1184.0f, 8.993f);
            _disappear = true;
        }
    }

    bool OnGossipHello(Player* player) override
    {
        if (!_instance)
            return false;

        switch (_instance->GetData(EVENT_ZF_PYRAMID))
        {
            case PYRAMID_ZF_KILLED_ALL_TROLLS:
                AddGossipItemFor(player, GossipOptionNpc::None, GOSSIP_ZF_WEEGLI_TEXT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, GOSSIP_ZF_WEEGLI_DOOR, me->GetGUID());          // if event can proceed to end
                break;
            case PYRAMID_ZF_NOT_STARTED:
                SendGossipMenuFor(player, GOSSIP_ZF_WEEGLI_NOT_STARTED, me->GetGUID());   // if event not started
                break;
            default:
                SendGossipMenuFor(player, GOSSIP_ZF_WEEGLI_IN_PROGRESS, me->GetGUID());   // if event are in progress
                break;
        }
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            CloseGossipMenuFor(player);
            // here we make him run to door, set the charge and run away off to nowhere
            DoAction(ACTION_ZF_WEEGLI_DOOR);
        }
        return true;
    }

private:
    InstanceScript* _instance;
    ObjectGuid _explosiveGUID;
    uint32 _bombTimer;
    bool _destroyingDoor;
    bool _runAway;
    bool _disappear;
    bool _regen;
};

/*######
## go_shallow_grave
######*/

enum ClassicZFShallowGrave
{
    NPC_ZF_ZOMBIE               = 7286,
    NPC_ZF_DEAD_HERO            = 7276,
    ZF_ZOMBIE_CHANCE            = 65,
    ZF_DEAD_HERO_CHANCE         = 10
};

// VMaNGOS pGOOpen: the grave (chest, lock 1539) is opened by a spell -> TC Spell::EffectOpenLock -> GameObject::Use -> OnGossipHello
struct classic_go_shallow_grave : public GameObjectAI
{
    classic_go_shallow_grave(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        // randomly summon a zombie or dead hero the first time a grave is used
        if (me->GetUseCount() == 0)
        {
            uint32 randomchance = urand(0, 100);
            if (randomchance < ZF_ZOMBIE_CHANCE)
                me->SummonCreature(NPC_ZF_ZOMBIE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30s);
            else if ((randomchance - ZF_ZOMBIE_CHANCE) < ZF_DEAD_HERO_CHANCE)
                me->SummonCreature(NPC_ZF_DEAD_HERO, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30s);
        }
        me->AddUse();
        // VMaNGOS returned true from its GOOpen hook; returning true from TC OnGossipHello would suppress the grave loot,
        // so let TC continue the normal chest use.
        return false;
    }
};

/*######
## go_table_theka
######*/

enum ClassicZFTheka
{
    QUEST_ZF_THE_TABLET_OF_THEKA = 2936,
    GOSSIP_ZF_TABLET_OF_THEKA    = 1653
};

struct classic_go_table_theka : public GameObjectAI
{
    classic_go_table_theka(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (player->GetQuestStatus(QUEST_ZF_THE_TABLET_OF_THEKA) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(QUEST_ZF_THE_TABLET_OF_THEKA);

        SendGossipMenuFor(player, GOSSIP_ZF_TABLET_OF_THEKA, me->GetGUID());
        return true;
    }
};

/*######
## ward_zumrah
######*/

enum ClassicZFWardZumrah
{
    SPELL_ZF_SUMMON_ZOMBIE_SKELETON = 11088
};

struct classic_ward_zumrah : public ScriptedAI
{
    classic_ward_zumrah(Creature* creature) : ScriptedAI(creature), _skeletonTimer(5000) { }

    void Reset() override
    {
        _skeletonTimer = 5000;
        me->SetDefaultMovementType(IDLE_MOTION_TYPE);
    }

    void UpdateAI(uint32 diff) override
    {
        me->SetDefaultMovementType(IDLE_MOTION_TYPE);

        if (_skeletonTimer < diff)
        {
            if (DoCastSelf(SPELL_ZF_SUMMON_ZOMBIE_SKELETON) == SPELL_CAST_OK)
                _skeletonTimer = 5000;
        }
        else
            _skeletonTimer -= diff;
    }

private:
    uint32 _skeletonTimer;
};

/*######
## at_zumrah
######*/

enum ClassicZFZumrah
{
    NPC_ZF_WITCH_DOCTOR_ZUMRAH  = 7271,
    FACTION_ZF_ZUMRAH_HOSTILE   = 37,

    SAY_ZF_ZUMRAH_TRIGGER       = 3622,
    SAY_ZF_ZUMRAH_YELL          = 6221,
    SAY_ZF_ZUMRAH_KILLED        = 6222
};

class classic_at_zumrah : public AreaTriggerScript
{
public:
    classic_at_zumrah() : AreaTriggerScript("classic_at_zumrah") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
    {
        Creature* zumrah = player->FindNearestCreature(NPC_ZF_WITCH_DOCTOR_ZUMRAH, 30.0f);

        if (!zumrah || !zumrah->IsAlive())
            return false;

        if (zumrah->GetFaction() != FACTION_ZF_ZUMRAH_HOSTILE)
        {
            if (InstanceScript* instance = zumrah->GetInstanceScript())
                instance->SetData(EVENT_ZF_ZUMRAH, IN_PROGRESS);

            zumrah->SetImmuneToPC(false);
            zumrah->SetFaction(FACTION_ZF_ZUMRAH_HOSTILE);
            ClassicScriptText(SAY_ZF_ZUMRAH_TRIGGER, zumrah);
        }

        return true;
    }
};

/*######
## at_antusul
######*/

enum ClassicZFAntusul
{
    NPC_ZF_ANTUSUL              = 8127,
    NPC_ZF_SULITHUZ_BROODLING   = 8138,
    SAY_ZF_ANTUSUL_TRIGGER      = 4166
};

class classic_at_antusul : public AreaTriggerScript
{
public:
    classic_at_antusul() : AreaTriggerScript("classic_at_antusul") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
    {
        Creature* antusul = player->FindNearestCreature(NPC_ZF_ANTUSUL, 100.0f);

        if (!antusul || !antusul->IsAlive() || antusul->IsInCombat())
            return false;

        InstanceScript* instance = antusul->GetInstanceScript();
        if (!instance || instance->GetData(EVENT_ZF_ANTUSUL) != NOT_STARTED)
            return false;

        instance->SetData(EVENT_ZF_ANTUSUL, IN_PROGRESS);
        ClassicScriptText(SAY_ZF_ANTUSUL_TRIGGER, antusul);

        // VMaNGOS: AddLambdaEventAtOffset(..., BATCHING_INTERVAL * 3) (BATCHING_INTERVAL = 400 ms)
        antusul->m_Events.AddEventAtOffset([antusul]()
        {
            // World of Warcraft Client Patch 1.12.0 (2006-08-22)
            // - Antu'sul's Sul'lithuz Broodlings now only hatch 4 at a time and are significantly weaker.
            // (VMaNGOS spawns the group twice before patch 1.12; Classic 1.60 is post-1.12 -> once)
            Position const broodlingSpawns[] =
            {
                { 1823.415161f, 748.297485f, 20.794931f, 3.944444f },
                { 1786.019165f, 743.399048f, 15.481779f, 6.108652f },
                { 1827.460571f, 738.032410f, 19.131363f, 3.385939f },
                { 1810.196533f, 749.873230f, 17.597878f, 4.555309f }
            };

            for (Position const& pos : broodlingSpawns)
                if (TempSummon* broodling = antusul->SummonCreature(NPC_ZF_SULITHUZ_BROODLING, pos, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 25s)) // VMaNGOS default despawn time
                    CreatureAI::DoZoneInCombat(broodling);

            if (antusul->IsAlive() && !antusul->IsInCombat())
                antusul->GetMotionMaster()->MovePoint(0, 1805.133667f, 740.349304f, 14.763382f, true, {}, {}, MovementWalkRunSpeedSelectionMode::ForceRun);
        }, 1200ms);

        return true;
    }
};

void AddSC_classic_zulfarrak()
{
    RegisterCreatureAI(classic_npc_sergeant_bly);
    RegisterCreatureAI(classic_npc_weegli_blastfuse);
    RegisterGameObjectAI(classic_go_shallow_grave);
    RegisterGameObjectAI(classic_go_troll_cage);
    RegisterGameObjectAI(classic_go_table_theka);
    RegisterCreatureAI(classic_ward_zumrah);
    new classic_at_zumrah();
    new classic_at_antusul();
}
