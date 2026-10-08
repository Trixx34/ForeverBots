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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_dathrohan_balnazzar.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicDathrohanBalnazzar : uint32
{
    //Dathrohan spells
    SPELL_CRUSADERSHAMMER           = 17286,                //AOE stun
    SPELL_CRUSADERSTRIKE            = 17281,
    SPELL_HOLYSTRIKE                = 17284,                //weapon dmg +3

    //Transform
    SPELL_BALNAZZARTRANSFORM        = 17288,                //restore full HP/mana, trigger spell Balnazzar Transform Stun

    //Balnazzar spells
    SPELL_SHADOWSHOCK               = 17399,
    SPELL_MINDBLAST                 = 17287,
    SPELL_PSYCHICSCREAM             = 13704,
    SPELL_BALNAZZAR_SLEEP           = 12098,
    SPELL_BALNAZZAR_MINDCONTROL     = 17405,

    NPC_DATHROHAN                   = NPC_STRAT_DATHROHAN,
    NPC_BALNAZZAR                   = 10813,
    //NPC_ZOMBIE                      = 10698                 //probably incorrect
    NPC_SKEL_BERSERKER              = 10391,
    NPC_SKEL_GUARDIAN               = 10390,

    SAY_DATHROHAN_AGGRO             = 6441,
    SAY_DATHROHAN_TRANSFORM         = 6447,
    SAY_DATHROHAN_DEATH             = 6442
};

Position const DathrohanSummonPoint[] =
{
    {3444.156f, -3090.626f, 135.002f, 2.240f},              //G1 front, left
    {3449.123f, -3087.009f, 135.002f, 2.240f},              //G1 front, right
    {3446.246f, -3093.466f, 135.002f, 2.240f},              //G1 back left
    {3451.160f, -3089.904f, 135.002f, 2.240f},              //G1 back, right

    {3457.995f, -3080.916f, 135.002f, 3.784f},              //G2 front, left
    {3454.302f, -3076.330f, 135.002f, 3.784f},              //G2 front, right
    {3460.975f, -3078.901f, 135.002f, 3.784f},              //G2 back left
    {3457.338f, -3073.979f, 135.002f, 3.784f},              //G2 back, right

    {3479.995f, -3062.916f, 135.002f, 3.784f},              //G3 front, left
    {3476.302f, -3058.330f, 135.002f, 3.784f},              //G3 front, right
    {3482.975f, -3060.901f, 135.002f, 3.784f},              //G3 back left
    {3479.338f, -3055.979f, 135.002f, 3.784f},              //G3 back, right

    {3501.995f, -3074.916f, 134.997f, 3.784f},              //G4 front, left
    {3498.302f, -3070.330f, 134.997f, 3.784f},              //G4 front, right
    {3504.975f, -3072.901f, 134.997f, 3.784f},              //G4 back left
    {3501.338f, -3067.979f, 134.997f, 3.784f},              //G4 back, right

    {3530.995f, -3053.916f, 134.997f, 3.784f},              //G5 front, left
    {3527.302f, -3049.330f, 134.997f, 3.784f},              //G5 front, right
    {3533.975f, -3051.901f, 134.997f, 3.784f},              //G5 back left
    {3530.338f, -3046.979f, 134.997f, 3.784f},              //G5 back, right

    {3559.995f, -3065.916f, 134.997f, 3.784f},              //G6 front, left
    {3556.302f, -3061.330f, 134.997f, 3.784f},              //G6 front, right
    {3562.975f, -3063.901f, 134.997f, 3.784f},              //G6 back left
    {3559.338f, -3058.979f, 134.997f, 3.784f},              //G6 back, right

    {3591.995f, -3085.916f, 135.664f, 3.784f},              //G7 front, left
    {3588.302f, -3081.330f, 135.664f, 3.784f},              //G7 front, right
    {3594.975f, -3083.901f, 135.664f, 3.784f},              //G7 back left
    {3591.338f, -3078.979f, 135.664f, 3.784f},              //G7 back, right

    {3624.995f, -3091.916f, 134.122f, 3.784f},              //G8 front, left
    {3621.302f, -3087.330f, 134.122f, 3.784f},              //G8 front, right
    {3627.975f, -3089.901f, 134.122f, 3.784f},              //G8 back left
    {3624.338f, -3084.979f, 134.122f, 3.784f}               //G8 back, right
};
}

struct classic_boss_dathrohan_balnazzar : public ScriptedAI
{
    classic_boss_dathrohan_balnazzar(Creature* creature) : ScriptedAI(creature) { }

    uint32 m_uiCrusadersHammer_Timer = 0;
    uint32 m_uiCrusaderStrike_Timer = 0;
    uint32 m_uiMindBlast_Timer = 0;
    uint32 m_uiHolyStrike_Timer = 0;
    uint32 m_uiShadowShock_Timer = 0;
    uint32 m_uiPsychicScream_Timer = 0;
    uint32 m_uiDeepSleep_Timer = 0;
    uint32 m_uiMindControl_Timer = 0;
    uint32 m_uiTransform_Timer = 0;
    bool m_bTransformed = false;

    ObjectGuid MCPlayerGuid;
    float MCPlayerAggro = 0.0f;
    ObjectGuid SleepPlayerGuid;
    float SleepPlayerAggro = 0.0f;

    void Reset() override
    {
        m_uiCrusadersHammer_Timer = 8000;
        m_uiCrusaderStrike_Timer = 12000;
        m_uiMindBlast_Timer = 6000;
        m_uiHolyStrike_Timer = 18000;
        m_uiShadowShock_Timer = 3000;
        m_uiPsychicScream_Timer = 12000;
        m_uiDeepSleep_Timer = 9000;
        m_uiMindControl_Timer = 18000;
        m_uiTransform_Timer = 0;
        m_bTransformed = false;

        MCPlayerGuid.Clear();
        MCPlayerAggro = 0;
        SleepPlayerGuid.Clear();
        SleepPlayerAggro = 0;

        if (me->GetEntry() == NPC_BALNAZZAR)
            me->UpdateEntry(NPC_DATHROHAN);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_DATHROHAN_DEATH, me);

        for (Position const& pos : DathrohanSummonPoint)
        {
            switch (urand(0, 1))
            {
                case 0:
                    me->SummonCreature(NPC_SKEL_BERSERKER, pos, TEMPSUMMON_DEAD_DESPAWN, Milliseconds(HOUR * IN_MILLISECONDS));
                    break;
                case 1:
                    me->SummonCreature(NPC_SKEL_GUARDIAN, pos, TEMPSUMMON_DEAD_DESPAWN, Milliseconds(HOUR * IN_MILLISECONDS));
                    break;
                default:
                    break;
            }
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_DATHROHAN_AGGRO, me);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!UpdateVictim())
            return;

        //START NOT TRANSFORMED
        if (!m_bTransformed)
        {
            //MindBlast
            if (m_uiMindBlast_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_MINDBLAST) == SPELL_CAST_OK)
                    m_uiMindBlast_Timer = urand(15000, 20000);
            }
            else
                m_uiMindBlast_Timer -= uiDiff;

            //CrusadersHammer
            if (m_uiCrusadersHammer_Timer < uiDiff)
            {
                if (DoCast(me, SPELL_CRUSADERSHAMMER) == SPELL_CAST_OK)
                    m_uiCrusadersHammer_Timer = 12000;
            }
            else
                m_uiCrusadersHammer_Timer -= uiDiff;

            //CrusaderStrike
            if (m_uiCrusaderStrike_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_CRUSADERSTRIKE) == SPELL_CAST_OK)
                    m_uiCrusaderStrike_Timer = 15000;
            }
            else
                m_uiCrusaderStrike_Timer -= uiDiff;

            //HolyStrike
            if (m_uiHolyStrike_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_HOLYSTRIKE) == SPELL_CAST_OK)
                    m_uiHolyStrike_Timer = 15000;
            }
            else
                m_uiHolyStrike_Timer -= uiDiff;

            //BalnazzarTransform
            if (me->GetHealthPct() < 40.0f)
            {
                if (me->IsNonMeleeSpellCast(false))
                    me->InterruptNonMeleeSpells(false);

                //restore hp, mana and stun
                DoCast(me, SPELL_BALNAZZARTRANSFORM);
                me->UpdateEntry(NPC_BALNAZZAR);
                m_uiTransform_Timer = 4000;
                m_bTransformed = true;
                return;
            }
        }
        else
        {
            if (m_uiTransform_Timer)
            {
                if (m_uiTransform_Timer <= uiDiff)
                {
                    ClassicScriptText(SAY_DATHROHAN_TRANSFORM, me);
                    m_uiTransform_Timer = 0;
                }
                else
                {
                    m_uiTransform_Timer -= uiDiff;
                    return;
                }
            }

            if (!MCPlayerGuid.IsEmpty())
            {
                if (Player* pTarget = ObjectAccessor::GetPlayer(*me, MCPlayerGuid))
                {
                    if (!pTarget->HasAura(SPELL_BALNAZZAR_MINDCONTROL))
                    {
                        me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                        me->GetThreatManager().AddThreat(pTarget, MCPlayerAggro, nullptr, true, true);
                        MCPlayerGuid.Clear();
                        MCPlayerAggro = 0;
                    }
                }
            }
            else
            {
                MCPlayerGuid.Clear();
                MCPlayerAggro = 0;
            }

            if (!SleepPlayerGuid.IsEmpty())
            {
                if (Player* pTarget = ObjectAccessor::GetPlayer(*me, SleepPlayerGuid))
                {
                    if (!pTarget->HasAura(SPELL_BALNAZZAR_SLEEP))
                    {
                        me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                        me->GetThreatManager().AddThreat(pTarget, SleepPlayerAggro, nullptr, true, true);
                        SleepPlayerGuid.Clear();
                        SleepPlayerAggro = 0;
                    }
                }
            }
            else
            {
                SleepPlayerGuid.Clear();
                SleepPlayerAggro = 0;
            }

            //MindBlast
            if (m_uiMindBlast_Timer < uiDiff)
            {
                if (DoCastVictim(SPELL_MINDBLAST) == SPELL_CAST_OK)
                    m_uiMindBlast_Timer = urand(15000, 20000);
            }
            else
                m_uiMindBlast_Timer -= uiDiff;

            //ShadowShock
            if (m_uiShadowShock_Timer < uiDiff)
            {
                DoCastVictim(SPELL_SHADOWSHOCK);
                m_uiShadowShock_Timer = 11000;
            }
            else
                m_uiShadowShock_Timer -= uiDiff;

            //PsychicScream
            if (m_uiPsychicScream_Timer < uiDiff)
            {
                if (DoCast(me, SPELL_PSYCHICSCREAM) == SPELL_CAST_OK)
                    m_uiPsychicScream_Timer = 20000;
            }
            else
                m_uiPsychicScream_Timer -= uiDiff;

            //DeepSleep
            if (m_uiDeepSleep_Timer < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 1))
                {
                    if (!pTarget->HasAura(SPELL_BALNAZZAR_SLEEP))
                    {
                        SleepPlayerGuid = pTarget->GetGUID();
                        SleepPlayerAggro = me->GetThreatManager().GetThreat(pTarget);
                        if (DoCast(pTarget, SPELL_BALNAZZAR_SLEEP) == SPELL_CAST_OK)
                        {
                            me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                            m_uiDeepSleep_Timer = 15000;
                        }
                    }
                }
            }
            else
                m_uiDeepSleep_Timer -= uiDiff;

            //MindControl
            if (m_uiMindControl_Timer < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::MaxThreat, 1))
                {
                    if (!pTarget->HasAura(SPELL_BALNAZZAR_SLEEP))
                    {
                        MCPlayerGuid = pTarget->GetGUID();
                        MCPlayerAggro = me->GetThreatManager().GetThreat(pTarget);
                        if (DoCast(pTarget, SPELL_BALNAZZAR_MINDCONTROL) == SPELL_CAST_OK)
                            m_uiMindControl_Timer = urand(25000, 30000);
                    }
                }
            }
            else
                m_uiMindControl_Timer -= uiDiff;
        }
    }
};

void AddSC_classic_boss_dathrohan_balnazzar()
{
    RegisterCreatureAI(classic_boss_dathrohan_balnazzar);
}
