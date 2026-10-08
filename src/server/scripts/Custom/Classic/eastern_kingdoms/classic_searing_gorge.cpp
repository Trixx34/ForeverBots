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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/searing_gorge/searing_gorge.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 3566 (Rise, Obsidion!)

#include "ScriptMgr.h"
#include "GossipDef.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include <vector>

/*######
## Quest 3566
######*/

enum ObsidionData
{
    QUEST_RISE_OBSIDION     = 3566,

    SAY_DORIUS1             = 4393,
    SAY_DORIUS2             = 4394,
    SAY_DORIUS3             = 4395,
    SAY_DORIUS4             = 4396,
    SAY_DORIUS5             = 4397,
    SAY_DORIUS6             = 4398,
    EMOTE_DORIUS7           = 4399,
    SAY_LATHORIC1           = 4391,
    SAY_LATHORIC2           = 4392,

    NPC_DORIUS              = 8421,
    NPC_LATHORIC_THE_BLACK  = 8391,
    NPC_OBSIDION            = 8400,

    SPELL_GROUND_SMASH      = 12734,
    SPELL_KNOCK_AWAY        = 10101
};

struct classic_npc_obsidion : public ScriptedAI
{
    classic_npc_obsidion(Creature* creature) : ScriptedAI(creature), m_nextText(0), m_uiTalkTimer(0),
        m_uiGroundSmashTimer(8000), m_uiKnockAwayTimer(12000), m_IsEventRunning(false) { }

    int32 m_nextText;
    uint32 m_uiTalkTimer;
    uint32 m_uiGroundSmashTimer;
    uint32 m_uiKnockAwayTimer;
    GuidList m_playerList;
    ObjectGuid m_Dorius;
    bool m_IsEventRunning;

    void Reset() override
    {
        m_uiGroundSmashTimer = 8000;
        m_uiKnockAwayTimer = 12000;
        m_playerList.clear();
        m_IsEventRunning = false;
        me->SetStandState(UNIT_STAND_STATE_DEAD);
        me->SetImmuneToPC(true);

        // clear the guid first: despawning Lathoric calls SummonedCreatureDespawn -> Reset() again
        ObjectGuid doriusGuid = m_Dorius;
        m_Dorius.Clear();
        if (Creature* cr = ObjectAccessor::GetCreature(*me, doriusGuid))
            cr->DespawnOrUnsummon();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->SetImmuneToPC(false);
    }

    void StartEvent()
    {
        m_uiTalkTimer = 5000;
        m_nextText = SAY_DORIUS1;
        m_IsEventRunning = true;

        if (Creature* dorius = me->SummonCreature(NPC_DORIUS, -6460.25f, -1244.86f, 180.36f, 3.04f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 3min))
            m_Dorius = dorius->GetGUID();
    }

    void SummonedCreatureDespawn(Creature* creature) override
    {
        if (creature->GetEntry() == NPC_LATHORIC_THE_BLACK && (!me->IsAlive() || !me->IsInCombat()))
            Reset();
    }

    // VMaNGOS JustRespawned
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        Reset();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_IsEventRunning)
            return;

        // talking, only if not in combat
        if (!me->IsInCombat())
        {
            if (m_nextText == SAY_LATHORIC1)
            {
                if (Creature* dorius = ObjectAccessor::GetCreature(*me, m_Dorius))
                    if (dorius->GetEntry() != NPC_LATHORIC_THE_BLACK)
                        dorius->UpdateEntry(NPC_LATHORIC_THE_BLACK);
            }

            if (m_nextText <= SAY_DORIUS6 && m_nextText >= SAY_LATHORIC1)
            {
                if (m_uiTalkTimer < diff)
                {
                    if (Creature* dorius = ObjectAccessor::GetCreature(*me, m_Dorius))
                        ClassicScriptText(uint32(m_nextText), dorius);
                    m_nextText++;
                    if (m_nextText > SAY_DORIUS6)
                        m_nextText = SAY_LATHORIC1;
                    if (m_nextText == SAY_DORIUS1)
                        m_nextText += 10;
                    m_uiTalkTimer = 6000;
                }
                else
                    m_uiTalkTimer -= diff;
            }

            if (m_nextText == SAY_LATHORIC2) // finished talking, start fighting
            {
                for (ObjectGuid const& guid : m_playerList)
                {
                    if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
                    {
                        me->SetStandState(UNIT_STAND_STATE_STAND);
                        me->SetImmuneToPC(false);

                        AttackStart(player);
                        if (Creature* lathoric = ObjectAccessor::GetCreature(*me, m_Dorius))
                        {
                            lathoric->SetImmuneToPC(false);
                            if (lathoric->AI())
                                lathoric->AI()->AttackStart(player);
                        }
                        break;
                    }
                }
            }
        }

        // fighting
        if (!UpdateVictim())
            return;

        if (m_uiGroundSmashTimer < diff)
        {
            if (DoCastVictim(SPELL_GROUND_SMASH) == SPELL_CAST_OK)
                m_uiGroundSmashTimer = 8000;
        }
        else
            m_uiGroundSmashTimer -= diff;

        if (m_uiKnockAwayTimer < diff)
        {
            if (DoCastVictim(SPELL_KNOCK_AWAY) == SPELL_CAST_OK)
                m_uiKnockAwayTimer = 12000;
        }
        else
            m_uiKnockAwayTimer -= diff;
    }
};

struct classic_npc_dying_archaeologist : public ScriptedAI
{
    classic_npc_dying_archaeologist(Creature* creature) : ScriptedAI(creature) { }

    bool OnGossipHello(Player* player) override
    {
        if (GetClosestCreatureWithEntry(me, NPC_OBSIDION, VISIBLE_RANGE))
            return false; // everything is ok

        // VMaNGOS PrepareQuestMenu(guid, QUEST_RISE_OBSIDION): quest menu without quest 3566
        player->PrepareQuestMenu(me->GetGUID());
        QuestMenu& questMenu = player->PlayerTalkClass->GetQuestMenu();
        if (questMenu.HasItem(QUEST_RISE_OBSIDION))
        {
            std::vector<QuestMenuItem> keep;
            for (uint8 i = 0; i < questMenu.GetMenuItemCount(); ++i)
                if (questMenu.GetItem(i).QuestId != QUEST_RISE_OBSIDION)
                    keep.push_back(questMenu.GetItem(i));
            questMenu.ClearMenu();
            for (QuestMenuItem const& item : keep)
                questMenu.AddMenuItem(item.QuestId, item.QuestIcon);
        }
        player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, me->GetGUID());
        return true;
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_RISE_OBSIDION)
        {
            if (Creature* obsidion = GetClosestCreatureWithEntry(me, NPC_OBSIDION, VISIBLE_RANGE))
            {
                if (classic_npc_obsidion* obsidionAI = dynamic_cast<classic_npc_obsidion*>(obsidion->AI()))
                {
                    if (obsidionAI->m_IsEventRunning || !obsidion->IsAlive())
                        return;
                    obsidionAI->StartEvent();
                    obsidionAI->m_playerList.push_back(player->GetGUID());

                    if (Group* group = player->GetGroup())
                        for (GroupReference const& itr : group->GetMembers())
                            if (Player* member = itr.GetSource())
                                obsidionAI->m_playerList.push_back(member->GetGUID());
                }
            }
            // else: Obsidion dead -> event does not start (VMaNGOS returns true here; TC cannot refuse the accept)
        }
    }
};

void AddSC_classic_searing_gorge()
{
    RegisterCreatureAI(classic_npc_obsidion);
    RegisterCreatureAI(classic_npc_dying_archaeologist);
}
