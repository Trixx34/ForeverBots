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

#ifndef TRINITY_BOT_CONSUMABLE_PLAN_H
#define TRINITY_BOT_CONSUMABLE_PLAN_H

// Potion choice of the consumables strategy (Bot.AI.Consumables.*, BotConsumables.cpp). Pure over plain data so it is unit tested without a
// map or a Player. See docs/playerbots/feature-bot-consumables-20261008.md.

#include "Define.h"
#include <span>

namespace BotConsumable
{
    enum class Kind : uint8 { Health, Mana };

    struct Config
    {
        int32 HealthBelowPct = 35;    // drink a healing potion in combat below this health percent
        int32 ManaBelowPct = 20;      // drink a mana potion in combat below this mana percent
    };

    struct Candidate
    {
        uint32 Entry = 0;
        Kind K = Kind::Health;
        uint32 Restore = 0;           // points restored by one potion
        uint32 Count = 1;
    };

    struct Facts
    {
        bool InCombat = false;
        int32 HpPct = 100;
        uint32 HpMax = 0;
        bool UsesMana = false;        // the main power of the bot is mana
        int32 ManaPct = 100;
        uint32 ManaMax = 0;
        bool HealthReady = true;      // the potion cooldown of the kind is over
        bool ManaReady = true;
    };

    struct Pick
    {
        int32 Index = -1;             // into the candidates, -1 = drink nothing
        Kind K = Kind::Health;
    };

    // Health first (a dying bot matters more than a dry one). Within a kind the smallest potion that covers the missing points wins, so
    // a big potion is not wasted on a small hole; when none covers it the biggest is used.
    TC_GAME_API Pick Choose(std::span<Candidate const> candidates, Facts const& f, Config const& cfg);
}

#endif
