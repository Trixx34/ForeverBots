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

// Paid repair and potion restocking, see BotRepair.h.

#include "BotRepair.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include <algorithm>
#include <mutex>

namespace BotRepair
{
namespace
{
using Trinity::StringFormat;

Config s_cfg;
std::once_flag s_cfgOnce;

// Potions a bot buys: Classic healing and mana potions, ranks from the lowest to the highest. An entry missing from the item template
// is skipped, the price and required level come from the template.
struct PotionRow { uint32 Entry; PotionKind Kind; };
constexpr PotionRow POTIONS[] =
{
    { 118, PotionKind::Health }, { 858, PotionKind::Health }, { 929, PotionKind::Health }, { 1710, PotionKind::Health }, { 3928, PotionKind::Health }, { 13446, PotionKind::Health },
    { 2455, PotionKind::Mana }, { 3385, PotionKind::Mana }, { 3827, PotionKind::Mana }, { 6149, PotionKind::Mana }, { 13443, PotionKind::Mana }, { 13444, PotionKind::Mana },
};

struct GearState
{
    int32 LowestPct = 100;
    uint64 Cost = 0;
};

GearState ReadGear(Player* bot)
{
    GearState g;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* it = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (uint32 maxDur = it->m_itemData->MaxDurability)
            {
                g.LowestPct = std::min(g.LowestPct, int32(100u * it->m_itemData->Durability / maxDur));
                g.Cost += it->CalculateDurabilityRepairCost(1.0f);
            }
    return g;
}
}

Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Repair.Enabled", false);
        s_cfg.Paid = sConfigMgr->GetBoolDefault("Bot.AI.Repair.Paid", true);
        s_cfg.FreeWhenBroke = sConfigMgr->GetBoolDefault("Bot.AI.Repair.FreeWhenBroke", false);
        s_cfg.BelowPct = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Repair.BelowPct", 70), 1, 100);
        s_cfg.Supplies = sConfigMgr->GetBoolDefault("Bot.AI.Repair.Supplies", true);
        s_cfg.PotionsPerKind = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Repair.PotionsPerKind", 5), 0, 20));
        s_cfg.MaxSpendPct = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Repair.MaxSpendPct", 60), 0, 100);
    });
    return s_cfg;
}

bool Prepare(BotAI* ai, Player* bot, char const* context, bool force)
{
    Config cfg = Cfg();
    if (!cfg.Enabled || !ai || !bot || !bot->IsInWorld())
        return false;
    if (force)
        cfg.BelowPct = 101;
    bool didSomething = false;
    uint64 const goldBefore = bot->GetMoney();
    uint64 cap = SpendCap(goldBefore, cfg);

    GearState const gear = ReadGear(bot);
    RepairFacts rf;
    rf.LowestPct = gear.LowestPct;
    rf.Cost = gear.Cost;
    rf.Gold = std::min(goldBefore, cap);
    RepairPick const rp = DecideRepair(rf, cfg);
    if (rp.Kind == RepairKind::Paid)
    {
        bot->DurabilityRepairAll(true, 1.0f, false);
        uint64 const paid = goldBefore - std::min(goldBefore, bot->GetMoney());
        cap -= std::min(cap, paid);
        ai->EmitEvent(bot, "decision", BOTLOG_INFO, "REPAIR_PAID", StringFormat("repairs the gear and pays {} copper", paid),
            StringFormat(R"({{"context":"{}","durability_pct":{},"cost":{},"paid":{}}})", context, gear.LowestPct, gear.Cost, paid));
        didSomething = true;
    }
    else if (rp.Kind == RepairKind::Free)
    {
        bot->DurabilityRepairAll(false, 0.0f, false);
        ai->EmitEvent(bot, "decision", BOTLOG_INFO, "REPAIR_FREE", "repairs the gear for free",
            StringFormat(R"({{"context":"{}","durability_pct":{},"cost":{},"paid":false}})", context, gear.LowestPct, gear.Cost));
        didSomething = true;
    }
    else if (rp.Kind == RepairKind::TooPoor)
        ai->EmitEvent(bot, "decision", BOTLOG_WARN, "REPAIR_TOO_POOR", "cannot afford the repair",
            StringFormat(R"({{"context":"{}","durability_pct":{},"cost":{},"gold":{}}})", context, gear.LowestPct, gear.Cost, goldBefore));

    if (cfg.Supplies && cfg.PotionsPerKind)
    {
        std::vector<Product> products;
        SupplyFacts sf;
        sf.Level = int32(bot->GetLevel());
        sf.UsesMana = bot->GetPowerType() == POWER_MANA;
        sf.Budget = cap;
        for (PotionRow const& row : POTIONS)
        {
            ItemTemplate const* tpl = sObjectMgr->GetItemTemplate(row.Entry);
            if (!tpl)
                continue;
            (row.Kind == PotionKind::Health ? sf.OwnedHealth : sf.OwnedMana) += bot->GetItemCount(row.Entry, false);
            Product p;
            p.Entry = row.Entry;
            p.Kind = row.Kind;
            p.ReqLevel = tpl->GetBaseRequiredLevel();
            p.Price = tpl->GetBuyPrice();
            products.push_back(p);
        }
        for (Purchase const& buy : PlanSupplies(products, sf, cfg))
        {
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, buy.Entry, buy.Count) != EQUIP_ERR_OK)
                continue;
            if (!bot->HasEnoughMoney(buy.Cost) || !bot->StoreNewItem(dest, buy.Entry, true))
                continue;
            bot->ModifyMoney(-int64(buy.Cost));
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "SUPPLY_BOUGHT", StringFormat("buys {} potion(s) for {} copper", buy.Count, buy.Cost),
                StringFormat(R"({{"context":"{}","item":{},"count":{},"cost":{}}})", context, buy.Entry, buy.Count, buy.Cost));
            didSomething = true;
        }
    }
    return didSomething;
}
}
