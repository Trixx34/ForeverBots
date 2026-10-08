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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_razuvious.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_razuvious, deathknight_understudy_ai

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include <list>
#include <vector>

namespace
{
enum ClassicNaxxRazuviousData : uint32
{
    SAY_RAZ_AGGRO1               = 13072,
    SAY_RAZ_AGGRO2               = 13073,
    SAY_RAZ_AGGRO3               = 13074,
    SAY_RAZ_SLAY1                = 13080,
    SAY_RAZ_SLAY2                = 13081,
    SAY_RAZ_COMMAND1             = 13075,
    SAY_RAZ_COMMAND2             = 13076,
    SAY_RAZ_COMMAND3             = 13077,
    SAY_RAZ_COMMAND4             = 13078,
    SAY_RAZ_DEATH                = 13079,

    EMOTE_RAZ_SHOUT              = 13082,

    SPELL_RAZ_UNBALANCING_STRIKE = 26613,
    SPELL_RAZ_DISRUPTING_SHOUT   = 29107,
    SPELL_RAZ_HOPELESS           = 29125,

    NPC_RAZ_DK_UNDERSTUDY        = 16803,
    NPC_RAZ_COMBAT_DUMMY         = 16211
};

constexpr float ClassicRazuviousAddPositions[4][4] =
{
    {2757.48f, -3111.52f, 267.768f, 3.92699f },
    {2762.05f, -3084.47f, 267.768f, 2.1293f  },
    {2778.91f, -3114.14f, 267.768f, 5.28835f },
    {2781.87f, -3088.19f, 267.768f, 0.907571f},
};

enum ClassicNaxxRazuviousEvents : uint32
{
    EVENT_RAZ_UNBALANCING_STRIKE = 1,
    EVENT_RAZ_DISRUPTING_SHOUT,
    EVENT_RAZ_COMMAND,

    // out of combat rp
    EVENT_RAZ_TURN_TO_TRAINEE,
    EVENT_RAZ_EMOTE_SHOUT,
    EVENT_RAZ_ADD_TURN_RAZUV,
    EVENT_RAZ_ADD_TALK,
    EVENT_RAZ_ADD_SALUTE,
    EVENT_RAZ_ADD_TURN_BACK,
    EVENT_RAZ_ADD_ATTACK
};
}

struct classic_deathknight_understudy_ai : public ScriptedAI
{
    classic_deathknight_understudy_ai(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature)) { }

    classic_instance_naxxramas_InstanceScript* m_pInstance;

    uint32 attackTimer = 0;
    bool runAttack = true;

    void Reset() override
    {
        me->SetEmoteState(EMOTE_STATE_READY1H);
        attackTimer = urand(5000, 10000);
        runAttack = true;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        runAttack = false;
        me->CallForHelp(30.0f);
    }

    void UpdateAI(uint32 diff) override
    {
        if (runAttack)
        {
            if (attackTimer < diff)
            {
                me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK1H);
                attackTimer = urand(5000, 10000);
            }
            else
                attackTimer -= diff;
        }

        if (!UpdateVictim())
            return;
    }
};

struct classic_boss_razuvious : public ScriptedAI
{
    classic_boss_razuvious(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature)) { }

    classic_instance_naxxramas_InstanceScript* m_pInstance;
    std::vector<ObjectGuid> summonedAdds;
    EventMap events;

    EventMap rpEvents;
    ObjectGuid rpBuddy;

    void Reset() override
    {
        events.Reset();
    }

    // VMaNGOS spawns the understudies from the AI constructor
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        RespawnAdds();
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (!me->IsWithinDistInMap(pWho, 33.0f))
            return;

        // VMaNGOS CanInitiateAttack() && IsTargetableBy() && IsHostileTo()
        if (me->HasReactState(REACT_AGGRESSIVE) && !me->IsInEvadeMode() && me->IsValidAttackTarget(pWho) && me->IsHostileTo(pWho))
        {
            if (pWho->isInAccessiblePlaceFor(me) && me->IsWithinLOSInMap(pWho))
            {
                if (!me->GetVictim())
                    AttackStart(pWho);
                else if (me->GetMap()->IsDungeon())
                    me->EngageWithTarget(pWho);
            }
        }
    }

    void RespawnAdds()
    {
        if (m_pInstance && m_pInstance->GetData(TYPE_RAZUVIOUS) == DONE)
            return;

        // start by despawning any adds that may still be around
        for (auto it = summonedAdds.begin(); it != summonedAdds.end();)
        {
            if (Creature* cg = me->GetMap()->GetCreature(*it))
            {
                if (TempSummon* tmpSumm = cg->ToTempSummon())
                    tmpSumm->UnSummon();
            }
            it = summonedAdds.erase(it);
        }

        // respawn all 4 adds
        for (int i = 0; i < 4; i++)
        {
            if (Creature* pAdd = me->SummonCreature(NPC_RAZ_DK_UNDERSTUDY, ClassicRazuviousAddPositions[i][0], ClassicRazuviousAddPositions[i][1],
                ClassicRazuviousAddPositions[i][2], ClassicRazuviousAddPositions[i][3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000ms))
            {
                if (i == 1)
                    rpBuddy = pAdd->GetGUID();
                summonedAdds.push_back(pAdd->GetGUID());
                pAdd->SetEmoteState(EMOTE_STATE_READY1H);
            }
        }
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_RAZUVIOUS, FAIL);

        RespawnAdds();
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (urand(0, 3))
            return;
        ClassicScriptText(urand(SAY_RAZ_SLAY1, SAY_RAZ_SLAY2), me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_RAZ_DEATH, me);
        DoCastSelf(SPELL_RAZ_HOPELESS, true);
        if (m_pInstance)
            m_pInstance->SetData(TYPE_RAZUVIOUS, DONE);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(urand(SAY_RAZ_AGGRO1, SAY_RAZ_AGGRO3), me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_RAZUVIOUS, IN_PROGRESS);

        events.Reset();
        rpEvents.Reset();
        me->CallForHelp(30.0f);

        events.ScheduleEvent(EVENT_RAZ_UNBALANCING_STRIKE, 30s);
        events.ScheduleEvent(EVENT_RAZ_DISRUPTING_SHOUT, 15s);
        events.ScheduleEvent(EVENT_RAZ_COMMAND, 40s);
    }

    void MovementInform(uint32 movementType, uint32 id) override
    {
        if (movementType != WAYPOINT_MOTION_TYPE)
            return;

        // TODO(classic): VMaNGOS waypoint point 6; verify the node id numbering of the imported waypoint path.
        if (id == 6)
        {
            rpEvents.Reset();
            rpEvents.ScheduleEvent(EVENT_RAZ_TURN_TO_TRAINEE, 0s);
            rpEvents.ScheduleEvent(EVENT_RAZ_EMOTE_SHOUT, 1s);
            rpEvents.ScheduleEvent(EVENT_RAZ_ADD_TURN_RAZUV, 1750ms);
            rpEvents.ScheduleEvent(EVENT_RAZ_ADD_TALK, 3500ms);
            rpEvents.ScheduleEvent(EVENT_RAZ_ADD_SALUTE, 8s);
            rpEvents.ScheduleEvent(EVENT_RAZ_ADD_TURN_BACK, 12s);
            rpEvents.ScheduleEvent(EVENT_RAZ_ADD_ATTACK, 12500ms);
        }
    }

    Creature* getRPBuddy()
    {
        return me->GetMap()->GetCreature(rpBuddy);
    }

    void UpdateRP(uint32 diff)
    {
        rpEvents.Update(diff);
        while (uint32 eventId = rpEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_RAZ_TURN_TO_TRAINEE:
                    if (Creature* b = getRPBuddy())
                        me->SetFacingToObject(b);
                    break;
                case EVENT_RAZ_EMOTE_SHOUT:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                    break;
                case EVENT_RAZ_ADD_TURN_RAZUV:
                    if (Creature* b = getRPBuddy())
                    {
                        if (classic_deathknight_understudy_ai* ai = dynamic_cast<classic_deathknight_understudy_ai*>(b->AI()))
                        {
                            ai->attackTimer = 0;
                            ai->runAttack = false;
                        }
                        b->SetFacingToObject(me);
                        b->SetEmoteState(EMOTE_STATE_STAND);
                    }
                    break;
                case EVENT_RAZ_ADD_TALK:
                    if (Creature* b = getRPBuddy())
                        b->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                    break;
                case EVENT_RAZ_ADD_SALUTE:
                    if (Creature* b = getRPBuddy())
                        b->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
                    break;
                case EVENT_RAZ_ADD_TURN_BACK:
                    if (Creature* b = getRPBuddy())
                    {
                        std::list<Creature*> lst;
                        b->GetCreatureListWithEntryInGrid(lst, NPC_RAZ_COMBAT_DUMMY, 5.0f);
                        if (!lst.empty())
                            b->SetFacingToObject((*lst.begin()));
                    }
                    break;
                case EVENT_RAZ_ADD_ATTACK:
                    if (Creature* b = getRPBuddy())
                    {
                        b->SetEmoteState(EMOTE_STATE_READY1H);
                        if (classic_deathknight_understudy_ai* ai = dynamic_cast<classic_deathknight_understudy_ai*>(b->AI()))
                        {
                            ai->attackTimer = 500;
                            ai->runAttack = true;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!me->IsInCombat())
            UpdateRP(uiDiff);

        if (!UpdateVictim())
            return;

        if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
            return;

        events.Update(uiDiff);
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_RAZ_UNBALANCING_STRIKE:
                    DoCastVictim(SPELL_RAZ_UNBALANCING_STRIKE);
                    events.Repeat(30s);
                    break;
                case EVENT_RAZ_DISRUPTING_SHOUT:
                    DoCastVictim(SPELL_RAZ_DISRUPTING_SHOUT);
                    ClassicScriptText(EMOTE_RAZ_SHOUT, me);
                    events.Repeat(25s);
                    break;
                case EVENT_RAZ_COMMAND:
                    ClassicScriptText(urand(SAY_RAZ_COMMAND1, SAY_RAZ_COMMAND4), me);
                    events.Repeat(Seconds(urand(30, 60)));
                    break;
                default:
                    break;
            }
        }
    }
};

void AddSC_classic_boss_razuvious()
{
    RegisterCreatureAI(classic_boss_razuvious);
    RegisterCreatureAI(classic_deathknight_understudy_ai);
}
