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

#ifndef TRINITY_BOT_REPAIR_PLAN_H
#define TRINITY_BOT_REPAIR_PLAN_H

// Paid repairs and potion restocking before a dungeon run (Bot.AI.Repair.*, BotRepair.cpp). Pure over plain data so it is unit tested
// without a map or a Player. See docs/playerbots/feature-bot-revive-repair-20261008.md.

#include "Define.h"
#include <span>
#include <vector>

namespace BotRepair
{
    struct Config
    {
        bool Enabled = false;
        bool Paid = true;                // false = repairs cost nothing (the old placeholder)
        bool FreeWhenBroke = false;      // a bot that cannot pay is repaired for free instead of staying broken
        int32 BelowPct = 70;             // repair when the worst equipped item is below this durability
        bool Supplies = true;            // also restock healing and mana potions
        uint32 PotionsPerKind = 5;       // target count of each potion kind
        int32 MaxSpendPct = 60;          // share of the bot's gold that one visit may spend in total
    };

    enum class RepairKind : uint8 { None, Free, Paid, TooPoor };

    struct RepairFacts
    {
        int32 LowestPct = 100;           // lowest durability percentage of the equipped gear
        uint64 Cost = 0;                 // copper for a full repair
        uint64 Gold = 0;                 // copper the bot owns
    };

    struct RepairPick
    {
        RepairKind Kind = RepairKind::None;
        uint64 Spend = 0;
    };

    // Whether to repair and how it is paid. Nothing when the gear is fine or there is nothing to fix.
    TC_GAME_API RepairPick DecideRepair(RepairFacts const& f, Config const& cfg);

    enum class PotionKind : uint8 { Health, Mana };

    struct Product
    {
        uint32 Entry = 0;
        PotionKind Kind = PotionKind::Health;
        int32 ReqLevel = 1;
        uint32 Price = 0;                // copper for one
    };

    struct Purchase
    {
        uint32 Entry = 0;
        uint32 Count = 0;
        uint64 Cost = 0;
    };

    struct SupplyFacts
    {
        int32 Level = 1;
        bool UsesMana = false;
        uint32 OwnedHealth = 0;          // potions of each kind in the bags
        uint32 OwnedMana = 0;
        uint64 Budget = 0;               // copper this visit may spend (already net of the repair)
    };

    // What to buy: per kind the best potion the bot may use (highest ReqLevel not above its level), up to PotionsPerKind in total,
    // health before mana, and only as many as the budget pays for. Empty when nothing is needed or affordable.
    TC_GAME_API std::vector<Purchase> PlanSupplies(std::span<Product const> products, SupplyFacts const& f, Config const& cfg);

    // Copper a visit may spend in total: MaxSpendPct of the gold the bot owns.
    TC_GAME_API uint64 SpendCap(uint64 gold, Config const& cfg);
}

#endif
