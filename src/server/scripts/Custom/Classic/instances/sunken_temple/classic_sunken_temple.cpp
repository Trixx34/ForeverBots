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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/swamp_of_sorrows/sunken_temple/sunken_temple.cpp
// (ScriptDev2 / ScriptDev0 lineage, GPL-2)
// Ported: npc_malfurion_stormrage (15362), at_shade_of_eranikus (4016). Quest support: 8733

#include "ScriptMgr.h"
#include "Creature.h"
#include "DB2Structure.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_sunken_temple.h"

namespace
{
enum ClassicSTQuests
{
    QUEST_ST_THE_CHARGE_OF_DRAGONFLIGHTS = 8555,
    QUEST_ST_ERANIKUS_TYRANT_OF_DREAMS   = 8733
};

enum ClassicSTMalfurion
{
    EMOTE_ST_MALFURION1                 = 11191,
    SAY_ST_MALFURION1                   = 11193,
    SAY_ST_MALFURION2                   = 11194,
    SAY_ST_MALFURION3                   = 11195,
    SAY_ST_MALFURION4                   = 11196,

    SPELL_ST_RESURRECTION_VISUAL        = 20761,

    MAX_ST_MALFURION_TEMPLE_SPEECHES    = 7
};
}

/*######
## npc_malfurion_stormrage
######*/

struct classic_npc_malfurion_stormrage : public ScriptedAI
{
    classic_npc_malfurion_stormrage(Creature* creature) : ScriptedAI(creature), _sayTimer(3000), _speech(0), _inDungeon(false) { }

    void Reset() override { }

    // VMaNGOS did this in the AI constructor
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();

        _speech = 0;
        _sayTimer = 3000;
        me->RemoveNpcFlag(NPCFlags(UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP));

        // Prevents interference with Waking Legends
        _inDungeon = me->GetMap()->IsDungeon();

        if (_inDungeon)
        {
            // If in temple, spawn invisible, emote "Walls Tremble"
            me->SetVisible(false);
            ClassicScriptText(EMOTE_ST_MALFURION1, me);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        // We are in Sunken Temple
        if (!_inDungeon || _speech >= MAX_ST_MALFURION_TEMPLE_SPEECHES)
            return;

        if (_sayTimer <= diff)
        {
            switch (_speech)
            {
                case 0:
                    me->SetVisible(true);
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                    // Resurrection visual
                    me->CastSpell(me, SPELL_ST_RESURRECTION_VISUAL, true);
                    _sayTimer = 1500;
                    break;
                case 1:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
                    _sayTimer = 2000;
                    break;
                case 2:
                    ClassicScriptText(SAY_ST_MALFURION1, me);
                    _sayTimer = 10000;
                    break;
                case 3:
                    ClassicScriptText(SAY_ST_MALFURION2, me);
                    _sayTimer = 10000;
                    break;
                case 4:
                    ClassicScriptText(SAY_ST_MALFURION3, me);
                    _sayTimer = 8000;
                    break;
                case 5:
                    ClassicScriptText(SAY_ST_MALFURION4, me);
                    _sayTimer = 5000;
                    break;
                case 6:
                    me->SetNpcFlag(NPCFlags(UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP));
                    break;
                default:
                    break;
            }

            ++_speech;
        }
        else
            _sayTimer -= diff;
    }

private:
    uint32 _sayTimer;
    uint32 _speech;
    bool _inDungeon;
};

/*######
## at_shade_of_eranikus
######*/

// Summon Malfurion trigger (AQ scepter quest)
class classic_at_shade_of_eranikus : public AreaTriggerScript
{
public:
    classic_at_shade_of_eranikus() : AreaTriggerScript("classic_at_shade_of_eranikus") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* areaTrigger) override
    {
        if (!player || !player->IsAlive() || !areaTrigger)
            return false;

        if (areaTrigger->ID != AREATRIGGER_ST_MALFURION)
            return false;

        // Don't spawn if player did not complete Charge of Dragonflights, or already on/done with Malfurion quest
        if (!player->GetQuestRewardStatus(QUEST_ST_THE_CHARGE_OF_DRAGONFLIGHTS)
            || player->GetQuestStatus(QUEST_ST_ERANIKUS_TYRANT_OF_DREAMS) != QUEST_STATUS_NONE
            || player->GetQuestRewardStatus(QUEST_ST_ERANIKUS_TYRANT_OF_DREAMS))
            return false;

        // Check if Malfurion already spawned
        if (GetClosestCreatureWithEntry(player, NPC_ST_MALFURION, 50.0f))
            return true;

        // Summon for real now
        if (player->SummonCreature(NPC_ST_MALFURION, areaTrigger->Pos.X, areaTrigger->Pos.Y - 15, areaTrigger->Pos.Z, 1.52f, TEMPSUMMON_CORPSE_DESPAWN, 0s))
            return true;

        return false;
    }
};

void AddSC_classic_sunken_temple()
{
    RegisterCreatureAI(classic_npc_malfurion_stormrage);
    new classic_at_shade_of_eranikus();
}
