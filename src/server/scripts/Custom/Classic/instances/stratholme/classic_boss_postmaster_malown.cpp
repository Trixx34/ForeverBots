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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_postmaster_malown.cpp (ScriptDev2 lineage, GPL-2)
// Spell ID to summon this guy is 24627 "Summon Postmaster Malown"; he is spawned once the third postbox has been opened.

#include "ScriptMgr.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"

namespace
{
enum ClassicPostmasterMalown : uint32
{
    SAY_MALOWN_AGGRO      = 6504,
    SAY_MALOWN_KILLED     = 6530,

    SPELL_WAILINGDEAD     = 7713,
    SPELL_BACKHAND        = 6253,
    SPELL_CURSEOFWEAKNESS = 8552,
    SPELL_CURSEOFTONGUES  = 12889,
    SPELL_CALLOFTHEGRAVE  = 17831
};
}

struct classic_boss_postmaster_malown : public ScriptedAI
{
    classic_boss_postmaster_malown(Creature* creature) : ScriptedAI(creature) { }

    uint32 WailingDead_Timer = 0;
    uint32 Backhand_Timer = 0;
    uint32 CurseOfWeakness_Timer = 0;
    uint32 CurseOfTongues_Timer = 0;
    uint32 CallOfTheGrave_Timer = 0;
    bool HasYelled = false;

    void Reset() override
    {
        WailingDead_Timer = 19000; //lasts 6 sec
        Backhand_Timer = 8000; //2 sec stun
        CurseOfWeakness_Timer = 20000; //lasts 2 mins
        CurseOfTongues_Timer = 22000;
        CallOfTheGrave_Timer = 25000;
        HasYelled = false;
    }

    void JustEngagedWith(Unit* who) override
    {
        ClassicScriptText(SAY_MALOWN_AGGRO, me);
        ScriptedAI::JustEngagedWith(who);
    }

    void KilledUnit(Unit* victim) override
    {
        ClassicScriptText(SAY_MALOWN_KILLED, me);
        ScriptedAI::KilledUnit(victim);
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //WailingDead
        if (WailingDead_Timer < diff)
        {
            if (urand(0, 99) < 65) //65% chance to cast
                DoCastVictim(SPELL_WAILINGDEAD);
            WailingDead_Timer = 19000;
        }
        else
            WailingDead_Timer -= diff;

        //Backhand
        if (Backhand_Timer < diff)
        {
            if (urand(0, 99) < 45) //45% chance to cast
                DoCastVictim(SPELL_BACKHAND);
            Backhand_Timer = 8000;
        }
        else
            Backhand_Timer -= diff;

        //CurseOfWeakness
        if (CurseOfWeakness_Timer < diff)
        {
            if (urand(0, 99) < 3) //3% chance to cast
                DoCastVictim(SPELL_CURSEOFWEAKNESS);
            CurseOfWeakness_Timer = 20000;
        }
        else
            CurseOfWeakness_Timer -= diff;

        //CurseOfTongues
        if (CurseOfTongues_Timer < diff)
        {
            if (urand(0, 99) < 3) //3% chance to cast
                DoCastVictim(SPELL_CURSEOFTONGUES);
            CurseOfTongues_Timer = 22000;
        }
        else
            CurseOfTongues_Timer -= diff;

        //CallOfTheGrave
        if (CallOfTheGrave_Timer < diff)
        {
            if (urand(0, 99) < 5) //5% chance to cast
                DoCastVictim(SPELL_CALLOFTHEGRAVE);
            CallOfTheGrave_Timer = 25000;
        }
        else
            CallOfTheGrave_Timer -= diff;
    }
};

void AddSC_classic_boss_postmaster_malown()
{
    RegisterCreatureAI(classic_boss_postmaster_malown);
}
