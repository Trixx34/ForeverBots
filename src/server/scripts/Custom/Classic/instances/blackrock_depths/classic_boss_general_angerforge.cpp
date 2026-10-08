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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/burning_steppes/blackrock_depths/boss_general_angerforge.cpp
// (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"

enum GeneralAngerforge
{
    EMOTE_ALARM             = 5286,

    // SPELL_FLURRY          = 15088,       // creature_template_addon
    // SPELL_ENRAGE          = 15097,       // creature_template_addon
    SPELL_SUNDER_ARMOR      = 15572,

    NPC_ANVILRAGE_MEDIC     = 8894,
    NPC_ANVILRAGE_RESERVIST = 8901,

    NPC_ADD_COUNT           = 10,
};

namespace
{
struct AngerforgeSpawnLocation
{
    uint32 m_uiEntry;
    float m_fX, m_fY, m_fZ, m_fO;
};

AngerforgeSpawnLocation const m_aAddspawnLocs[NPC_ADD_COUNT] =
{
    { NPC_ANVILRAGE_RESERVIST,  716.8168f, 23.03471f, -45.34414f, 3.159046f },
    { NPC_ANVILRAGE_RESERVIST,  719.8195f, 25.44250f, -45.32854f, 3.193953f },
    { NPC_ANVILRAGE_RESERVIST,  720.0683f, 22.93752f, -45.34140f, 3.159046f },
    { NPC_ANVILRAGE_RESERVIST,  719.9299f, 19.80474f, -45.35873f, 3.106686f },
    { NPC_ANVILRAGE_RESERVIST,  724.4819f, 25.27536f, -45.31646f, 3.193953f },
    { NPC_ANVILRAGE_RESERVIST,  724.4958f, 22.62163f, -45.32786f, 3.159046f },
    { NPC_ANVILRAGE_RESERVIST,  724.7056f, 19.89114f, -45.33829f, 3.124139f },
    { NPC_ANVILRAGE_RESERVIST,  728.7010f, 18.92765f, -46.00228f, 3.106686f },
    { NPC_ANVILRAGE_MEDIC,      728.5464f, 21.52842f, -45.89260f, 3.141593f },
    { NPC_ANVILRAGE_MEDIC,      728.6478f, 24.58055f, -45.94735f, 3.176499f },
};
}

struct classic_boss_general_angerforge : public ScriptedAI
{
    classic_boss_general_angerforge(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    uint32 m_uiSunderArmorTimer;
    uint32 m_uiAlarmTimer;

    void Initialize()
    {
        m_uiSunderArmorTimer = urand(5 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
        m_uiAlarmTimer = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->GetMotionMaster()->MoveFollow(me, 0.0f, ChaseAngle(0.0f));
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        // Sunder Armor
        if (m_uiSunderArmorTimer < diff)
        {
            if (DoCastVictim(SPELL_SUNDER_ARMOR) == SPELL_CAST_OK)
                m_uiSunderArmorTimer = urand(5 * IN_MILLISECONDS, 15 * IN_MILLISECONDS);
        }
        else
            m_uiSunderArmorTimer -= diff;

        // Alarm
        if (me->GetHealthPct() < 30.0f)
        {
            if (m_uiAlarmTimer < diff)
            {
                ClassicScriptText(EMOTE_ALARM, me);

                for (AngerforgeSpawnLocation const& spawnData : m_aAddspawnLocs)
                    me->SummonCreature(spawnData.m_uiEntry, spawnData.m_fX, spawnData.m_fY, spawnData.m_fZ, spawnData.m_fO, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30s);

                m_uiAlarmTimer = 3 * MINUTE * IN_MILLISECONDS;
            }
            else
                m_uiAlarmTimer -= diff;
        }
    }
};

void AddSC_classic_boss_general_angerforge()
{
    RegisterCreatureAI(classic_boss_general_angerforge);
}
