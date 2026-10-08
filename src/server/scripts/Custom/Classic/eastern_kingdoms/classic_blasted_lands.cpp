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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/blasted_lands/blasted_lands.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"

/*######
## go_stone_of_binding
######*/

enum StoneOfBinding
{
    GO_STONE_OF_BINDING_RAZELIKH    = 141812,   // <= 7668 Servant of Razelikh
    GO_STONE_OF_BINDING_GROL        = 141857,   // <= 7669 Servant of Grol
    GO_STONE_OF_BINDING_ALLISTARJ   = 141858,   // <= 7670 Servant of Allistarj
    GO_STONE_OF_BINDING_SEVINE      = 141859,   // <= 7671 Servant of Sevine

    SPELL_SERVANT_OF_RAZELIKH       = 10805,
    SPELL_SERVANT_OF_GROL           = 10834,
    SPELL_SERVANT_OF_ALLISTARJ      = 10836,
    SPELL_SERVANT_OF_SEVINE         = 10835
};

struct classic_go_stone_of_binding : public GameObjectAI
{
    classic_go_stone_of_binding(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        switch (me->GetEntry())
        {
            case GO_STONE_OF_BINDING_RAZELIKH:
                me->CastSpell(nullptr, SPELL_SERVANT_OF_RAZELIKH, true);
                break;
            case GO_STONE_OF_BINDING_GROL:
                me->CastSpell(nullptr, SPELL_SERVANT_OF_GROL, true);
                break;
            case GO_STONE_OF_BINDING_ALLISTARJ:
                me->CastSpell(nullptr, SPELL_SERVANT_OF_ALLISTARJ, true);
                break;
            case GO_STONE_OF_BINDING_SEVINE:
                me->CastSpell(nullptr, SPELL_SERVANT_OF_SEVINE, true);
                break;
        }
        return false; // continue with the default goober use
    }
};

void AddSC_classic_blasted_lands()
{
    RegisterGameObjectAI(classic_go_stone_of_binding);
}
