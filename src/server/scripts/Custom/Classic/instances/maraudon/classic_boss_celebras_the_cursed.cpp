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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/desolace/maraudon/boss_celebras_the_cursed.cpp (ScriptDev2 lineage, GPL-2)
// Ported: celebras_the_cursed (12225), celebras_spirit (13716, escort for quest 7046), go_book_celebras (178965)

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "classic_maraudon.h"
#include "classic_script_text.h"
#include <iterator>
#include <list>

namespace
{
enum ClassicCelebras
{
    SPELL_CELEBRAS_WRATH            = 21807,
    SPELL_CELEBRAS_ENTANGLINGROOTS  = 12747,
    SPELL_CELEBRAS_CORRUPT_FORCES   = 21968,

    GO_CELEBRAS_CREATOR             = 178560,
    GO_CELEBRAS_BLUE_AURA           = 178964,
    GO_CELEBRAS_TOME                = 178965,

    SAY_CELEBRAS_WP_1               = 8953,
    SAY_CELEBRAS_WP_3               = 8954,
    SAY_CELEBRAS_WP_5               = 8949,
    SAY_CELEBRAS_WP_6               = 8955,
    SAY_CELEBRAS_ACCEPT             = 8952,
    SAY_CELEBRAS_PRE_READ           = 8950,
    SAY_CELEBRAS_POST_READ          = 8948,

    SPELL_CELEBRAS_CHANNEL          = 21916,
    EMOTE_CELEBRAS_CHANNEL          = 8951,

    QUEST_CELEBRAS_SCEPTER          = 7046
};

struct ClassicCelebrasEscortPoint
{
    float X, Y, Z;
    uint32 WaitMs;
};

// VMaNGOS script_waypoint entry 13716 (pointid 0..14)
ClassicCelebrasEscortPoint const ClassicCelebrasEscortPath[] =
{
    { 653.71f,  86.35f,   -86.8495f, 0    },    // 0
    { 654.188f, 85.7842f, -86.8483f, 8000 },    // 1: SAY_WP_1, SetRun(false)
    { 657.902f, 80.6139f, -86.8336f, 0    },    // 2
    { 657.214f, 73.26f,   -86.8296f, 0    },    // 3: SAY_WP_3
    { 656.63f,  73.2994f, -86.8296f, 0    },    // 4: pause (book event)
    { 653.727f, 73.8462f, -85.87f,   4000 },    // 5: SAY_WP_5, blue aura
    { 656.63f,  73.2994f, -86.8296f, 0    },    // 6: SAY_WP_6, scepter creators
    { 655.702f, 67.1959f, -86.8284f, 0    },    // 7
    { 648.527f, 65.1264f, -86.7631f, 0    },    // 8
    { 649.337f, 65.6617f, -86.7435f, 3000 },    // 9: remove blue aura
    { 655.702f, 67.1959f, -86.8284f, 0    },    // 10
    { 657.902f, 80.6139f, -86.8336f, 0    },    // 11
    { 653.71f,  86.35f,   -86.8495f, 0    },    // 12
    { 654.188f, 85.7842f, -86.8483f, 0    },    // 13: stop, quest credit
    { 651.823f, 88.4863f, -86.8542f, 0    }     // 14
};
}

/*######
## celebras_the_cursed
######*/

struct classic_celebras_the_cursed : public ScriptedAI
{
    classic_celebras_the_cursed(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
        Initialize();
    }

    void Initialize()
    {
        _wrathTimer = 8000;
        _entanglingRootsTimer = 2000;
        _corruptForcesTimer = 30000;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_instance)
            _instance->SetData(CLASSIC_MARA_TYPE_CELEBRAS, DONE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_wrathTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_CELEBRAS_WRATH);
            _wrathTimer = 8000;
        }
        else
            _wrathTimer -= diff;

        if (_entanglingRootsTimer < diff)
        {
            DoCastVictim(SPELL_CELEBRAS_ENTANGLINGROOTS);
            _entanglingRootsTimer = 20000;
        }
        else
            _entanglingRootsTimer -= diff;

        if (_corruptForcesTimer < diff)
        {
            me->InterruptNonMeleeSpells(false);
            DoCastSelf(SPELL_CELEBRAS_CORRUPT_FORCES);
            _corruptForcesTimer = 20000;
        }
        else
            _corruptForcesTimer -= diff;
    }

private:
    InstanceScript* _instance;
    uint32 _wrathTimer;
    uint32 _entanglingRootsTimer;
    uint32 _corruptForcesTimer;
};

/*######
## celebras_spirit
######*/

struct classic_celebras_spirit : public EscortAI
{
    classic_celebras_spirit(Creature* creature) : EscortAI(creature), _phase(0), _eventTimer(0), _bookRead(false) { }

    void Reset() override
    {
        _phase = 0;
        _eventTimer = 0;
        _auraGUID.Clear();
        _bookRead = false;
    }

    void LoadCelebrasPath()
    {
        ResetPath();
        for (uint32 i = 0; i < std::size(ClassicCelebrasEscortPath); ++i)
        {
            ClassicCelebrasEscortPoint const& point = ClassicCelebrasEscortPath[i];
            Optional<Milliseconds> waitTime;
            if (point.WaitMs)
                waitTime = Milliseconds(point.WaitMs);
            // VMaNGOS Start(true /*run*/, ...) then SetRun(false) at point 1: nodes 0 and 1 are run, the rest walk
            AddWaypoint(i, point.X, point.Y, point.Z, 0.0f, waitTime, i <= 1);
        }
    }

    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 0:
                me->SetFacingTo(5.342044f);
                me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 5.342044f);
                break;
            case 1:
                ClassicScriptText(SAY_CELEBRAS_WP_1, me);
                break;
            case 3:
                if (Player* player = GetPlayerForEscort())
                    ClassicScriptText(SAY_CELEBRAS_WP_3, me, player);
                _eventTimer = 4000;
                break;
            case 4:
                me->SetFacingTo(3.009412f);
                SetEscortPaused(true);
                break;
            case 5:
                // trigger => Player click on the book
                if (GameObject* go = me->FindNearestGameObject(GO_CELEBRAS_BLUE_AURA, 250.0f))
                {
                    go->SetRespawnTime(6 * MINUTE);
                    go->Refresh();
                }
                ClassicScriptText(SAY_CELEBRAS_WP_5, me);
                if (GameObject* aura = me->SummonGameObject(GO_CELEBRAS_BLUE_AURA, 652.463013f, 74.085098f, -85.335297f, 3.054616f,
                    QuaternionData::fromEulerAnglesZYX(3.054616f, 0.0f, 0.0f), 0s))
                    _auraGUID = aura->GetGUID();
                break;
            case 6:
            {
                ClassicScriptText(SAY_CELEBRAS_WP_6, me);

                std::list<GameObject*> scepterList;
                GetGameObjectListWithEntryInGrid(scepterList, me, GO_CELEBRAS_CREATOR, 40.0f);
                for (GameObject* scepter : scepterList)
                    scepter->SetGoState(GO_STATE_ACTIVE);
                break;
            }
            case 9:
                if (GameObject* aura = me->GetMap()->GetGameObject(_auraGUID))
                    aura->Delete();
                break;
            case 13:
                // TODO(classic): VMaNGOS npc_escortAI::Stop(); TC EscortAI has no equivalent, pausing keeps her at this node
                SetEscortPaused(true);
                _eventTimer = 3000;
                break;
            default:
                break;
        }
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_CELEBRAS_SCEPTER)
            return;

        Reset();
        ClassicScriptText(SAY_CELEBRAS_ACCEPT, me);
        LoadCelebrasPath();
        Start(true, player->GetGUID(), quest);

        // VMaNGOS JustStartedEscort()
        SetEscortPaused(true);
        _eventTimer = 5000;
        _phase = 1;
    }

    void BookRead()
    {
        _bookRead = true;
        if (_eventTimer > 1000)
            _eventTimer = 1000;
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (_eventTimer && !me->GetVictim())
        {
            if (_eventTimer <= diff)
            {
                _eventTimer = 0;

                switch (_phase)
                {
                    case 1:
                        SetEscortPaused(false);
                        break;
                    case 2:
                        ClassicScriptText(SAY_CELEBRAS_PRE_READ, me);
                        _eventTimer = 1000;
                        break;
                    case 3:
                        me->SummonGameObject(GO_CELEBRAS_TOME, 652.431f, 74.7087f, -85.3355f, 6.16101f,
                            QuaternionData(0.0f, 0.0f, -0.0610485f, 0.998135f), 0s);
                        _eventTimer = 1000;
                        break;
                    case 4:
                        // me->CastSpell(me, SPELL_CELEBRAS_CHANNEL, true); (disabled in VMaNGOS)
                        ClassicScriptText(EMOTE_CELEBRAS_CHANNEL, me); // VMaNGOS forces CHAT_TYPE_TEXT_EMOTE
                        SetEscortPaused(true);
                        _eventTimer = _bookRead ? 1000 : 30000;
                        break;
                    case 5:
                        if (!_bookRead)
                        {
                            // timed out waiting for book to be read
                            // TODO(classic): VMaNGOS ResetEscort(); TC EscortAI has none, despawn and respawn instead
                            me->DespawnOrUnsummon(0s, 1s);
                            return;
                        }

                        ClassicScriptText(SAY_CELEBRAS_POST_READ, me);
                        _eventTimer = 1000;
                        break;
                    case 6:
                        SetEscortPaused(false);
                        break;
                    case 7:
                        me->SetNpcFlag(NPCFlags(UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP));

                        if (Player* player = GetPlayerForEscort())
                        {
                            if (player->GetQuestStatus(QUEST_CELEBRAS_SCEPTER) == QUEST_STATUS_INCOMPLETE)
                            {
                                player->AreaExploredOrEventHappens(QUEST_CELEBRAS_SCEPTER);
                                player->PrepareGossipMenu(me, me->GetGossipMenuId(), true);
                                player->SendPreparedGossip(me);
                            }
                        }
                        break;
                    default:
                        break;
                }
                ++_phase;
            }
            else
                _eventTimer -= diff;
        }

        UpdateVictim();
    }

private:
    uint8 _phase;
    uint32 _eventTimer;
    ObjectGuid _auraGUID;
    bool _bookRead;
};

/*######
## go_book_celebras
######*/

struct classic_go_book_celebras : public GameObjectAI
{
    classic_go_book_celebras(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (player->GetQuestStatus(QUEST_CELEBRAS_SCEPTER) == QUEST_STATUS_INCOMPLETE)
        {
            player->Say("Shal myrinan ishnu daldorah...", LANG_UNIVERSAL);
            me->DespawnOrUnsummon(); // VMaNGOS Delete()

            std::list<Creature*> celebrasList;
            GetCreatureListWithEntryInGrid(celebrasList, player, NPC_MARA_CELEBRAS_REDEEMED, 40.0f);
            for (Creature* celebras : celebrasList)
                if (classic_celebras_spirit* celebrasSpirit = dynamic_cast<classic_celebras_spirit*>(celebras->AI()))
                    celebrasSpirit->BookRead();
        }
        return true;
    }
};

void AddSC_classic_boss_celebras_the_cursed()
{
    RegisterCreatureAI(classic_celebras_the_cursed);
    RegisterCreatureAI(classic_celebras_spirit);
    RegisterGameObjectAI(classic_go_book_celebras);
}
