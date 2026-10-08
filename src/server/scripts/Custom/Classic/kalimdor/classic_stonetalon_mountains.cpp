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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/stonetalon_mountains/stonetalon_mountains.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 1090 (Gerenzo's Orders - defend Piznik)

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"

/*######
## npc_piznik (quest 1090 by Rockette)
######*/

enum Piznik
{
    QUEST_GERENZOS_ORDERS           = 1090,

    NPC_PIZNIK_ATTACKER_3998            = 3998,
    NPC_PIZNIK_ATTACKER_4001        = 4001,
    NPC_PIZNIK_ATTACKER_4003         = 4003
};

struct classic_npc_piznik : public ScriptedAI
{
    enum Phase
    {
        EVENT_PHASE_INIT,
        EVENT_PHASE_SECOND_WAVE,
        EVENT_PHASE_THIRD_WAVE,
        EVENT_PHASE_END
    };

    classic_npc_piznik(Creature* creature) : ScriptedAI(creature), _inEvent(false), _eventTimer(0), _eventPhase(EVENT_PHASE_INIT) { }

    void Reset() override { }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        me->SetImmuneToNPC(true);
        ScriptedAI::JustAppeared();
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->GetMotionMaster()->MovePoint(2, 959.93f, -261.39f, -5.75f);
        summoned->SetHomePosition(959.931335f, -261.39f, -5.74659f, 5.32f);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_inEvent)
            if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                player->FailQuest(QUEST_GERENZOS_ORDERS);
        _inEvent = false;
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_GERENZOS_ORDERS)
            StartEvent(player);
    }

    void StartEvent(Player* player)
    {
        if (_inEvent)
            return;
        _inEvent = true;
        _eventPhase = EVENT_PHASE_INIT;
        _eventTimer = 0;
        _playerGuid = player->GetGUID();
        me->SetPvP(true);                                   // VMaNGOS UNIT_FLAG_PVP
        me->SetImmuneToNPC(false);
        me->SetFaction(FACTION_ESCORTEE_N_FRIEND_ACTIVE);   // restored on respawn
    }

    void AttackStart(Unit* who) override
    {
        if (_inEvent)
            return;

        ScriptedAI::AttackStart(who);
    }

    void UpdateAI(uint32 diff) override
    {
        if (UpdateVictim())
            return;

        if (!_inEvent)
            return;

        if (_eventPhase == EVENT_PHASE_INIT)
        {
            me->SummonCreature(NPC_PIZNIK_ATTACKER_3998, 935.6930f, -262.0789f, -2.1552f, 0.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            me->SummonCreature(NPC_PIZNIK_ATTACKER_4001, 931.6735f, -261.3967f, -2.0203f, 6.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            _eventPhase = EVENT_PHASE_SECOND_WAVE;
        }
        if (_eventTimer > 60000 && _eventPhase == EVENT_PHASE_SECOND_WAVE)
        {
            me->SummonCreature(NPC_PIZNIK_ATTACKER_3998, 935.6930f, -262.0789f, -2.1552f, 0.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            me->SummonCreature(NPC_PIZNIK_ATTACKER_4001, 931.6735f, -261.3967f, -2.0203f, 6.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            me->SummonCreature(NPC_PIZNIK_ATTACKER_3998, 930.4216f, -266.4603f, -1.6689f, 0.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            _eventPhase = EVENT_PHASE_THIRD_WAVE;
        }
        if (_eventTimer > 120000 && _eventPhase == EVENT_PHASE_THIRD_WAVE)
        {
            me->SummonCreature(NPC_PIZNIK_ATTACKER_3998, 935.6930f, -262.0789f, -2.1552f, 0.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            me->SummonCreature(NPC_PIZNIK_ATTACKER_4001, 931.6735f, -261.3967f, -2.0203f, 6.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            me->SummonCreature(NPC_PIZNIK_ATTACKER_4003, 930.4216f, -266.4603f, -1.6689f, 0.5f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s);
            _eventPhase = EVENT_PHASE_END;
        }
        if (_eventTimer > 180000 && _eventPhase == EVENT_PHASE_END)
        {
            if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                player->GroupEventHappens(QUEST_GERENZOS_ORDERS, me);
            _inEvent = false;
            me->SetPvP(false);
            me->RestoreFaction();
        }
        _eventTimer += diff;
    }

private:
    bool _inEvent;
    uint32 _eventTimer;
    Phase _eventPhase;
    ObjectGuid _playerGuid;
};

void AddSC_classic_stonetalon_mountains()
{
    RegisterCreatureAI(classic_npc_piznik);
}
