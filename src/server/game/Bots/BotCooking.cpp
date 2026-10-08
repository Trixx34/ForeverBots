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

#include "BotCooking.h"
#include "BotTradeLink.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <mutex>

namespace BotCooking
{
using namespace BotCook;

namespace
{
Config s_cfg;
std::once_flag s_cfgOnce;
bool s_enabled = false, s_chat = false;
uint32 s_maxListed = 12;

void Load()
{
    std::call_once(s_cfgOnce, []()
    {
        s_enabled = sConfigMgr->GetBoolDefault("Bot.AI.Cooking.Enabled", false);
        s_cfg.StockPlain = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Cooking.StockPlain", 20), 0, 200));
        s_cfg.StockBuff = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Cooking.StockBuff", 10), 0, 200));
        s_cfg.MinFreeSlots = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Cooking.MinFreeSlots", 4), 1, 20));
        s_chat = sConfigMgr->GetBoolDefault("Bot.Chat.Food.Enabled", false);
        s_maxListed = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Chat.Food.MaxListed", 12), 1, 30));
    });
}

bool Tradable(Item* item, Player* to)
{
    return item->CanBeTraded(false, true) && (!to || !item->IsBindedNotWith(to));
}

std::string Link(uint32 entry)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
        return std::to_string(entry);
    uint32 const q = std::min<uint32>(proto->GetQuality(), MAX_ITEM_QUALITY - 1);
    return Trinity::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:0|h[{}]|h|r", ItemQualityColors[q], entry, proto->GetName(LOCALE_enUS));
}
}

bool Enabled()
{
    Load();
    return s_enabled && sWorld->getBoolConfig(CONFIG_BOT_ENABLED);
}

Config const& Cfg()
{
    Load();
    return s_cfg;
}

bool ChatEnabled()
{
    Load();
    return s_chat && sWorld->getBoolConfig(CONFIG_BOT_ENABLED);
}

Kind Classify(ItemTemplate const* proto)
{
    if (!proto || proto->GetClass() != ITEM_CLASS_CONSUMABLE || proto->GetSubClass() != ITEM_SUBCLASS_FOOD_DRINK)
        return Kind::None;
    bool eats = false, drinks = false, other = false;
    for (ItemEffectEntry const* eff : proto->Effects)
    {
        if (!eff || eff->TriggerType != ITEM_SPELLTRIGGER_ON_USE || eff->SpellID <= 0)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(uint32(eff->SpellID), DIFFICULTY_NONE);
        if (!si)
            continue;
        for (SpellEffectInfo const& e : si->GetEffects())
        {
            if (!e.IsAura())
                continue;
            switch (e.ApplyAuraName)
            {
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_PERIODIC_HEAL:
                    eats = true;
                    break;
                case SPELL_AURA_OBS_MOD_POWER:
                case SPELL_AURA_PERIODIC_ENERGIZE:
                    drinks = true;
                    break;
                case SPELL_AURA_DUMMY:
                case SPELL_AURA_NONE:
                    break;
                default:
                    other = true;   // a stat aura, or a trigger of "Well Fed"
                    break;
            }
        }
    }
    if (other)
        return Kind::Buff;
    if (eats)
        return Kind::Plain;
    return drinks ? Kind::Drink : Kind::None;
}

std::vector<Food> Holdings(Player* bot, Player* to)
{
    std::map<uint32, Food> byEntry;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (!Tradable(item, to))
            return ItemSearchCallbackResult::Continue;
        ItemTemplate const* proto = item->GetTemplate();
        Kind const k = Classify(proto);
        if (k == Kind::None)
            return ItemSearchCallbackResult::Continue;
        Food& f = byEntry[item->GetEntry()];
        f.Entry = item->GetEntry();
        f.K = k;
        f.ReqLevel = uint32(std::max<int32>(0, proto->GetBaseRequiredLevel()));
        f.Count += item->GetCount();
        return ItemSearchCallbackResult::Continue;
    });
    std::vector<Food> out;
    for (auto const& [entry, f] : byEntry)
        out.push_back(f);
    return out;
}

Stock CountStock(Player* bot)
{
    std::vector<Food> const held = Holdings(bot, nullptr);
    Stock s;
    s.Plain = CountOf(held, Kind::Plain);
    s.Buff = CountOf(held, Kind::Buff);
    return s;
}

bool Describe(Player* bot, SpellInfo const* spell, Recipe& out)
{
    for (SpellEffectInfo const& e : spell->GetEffects())
    {
        if (!e.IsEffect(SPELL_EFFECT_CREATE_ITEM) || !e.ItemType)
            continue;
        Kind const k = Classify(sObjectMgr->GetItemTemplate(e.ItemType));
        if (k != Kind::Plain && k != Kind::Buff)
            return false;
        out.Produces = k;
        out.Yield = uint32(std::max<int32>(1, e.CalcValueAsInt(bot)));
        uint32 batches = UINT32_MAX;
        for (size_t i = 0; i < spell->Reagent.size(); ++i)
            if (spell->Reagent[i] > 0 && spell->ReagentCount[i] > 0)
                batches = std::min(batches, bot->GetItemCount(uint32(spell->Reagent[i])) / uint32(spell->ReagentCount[i]));
        out.Batches = batches == UINT32_MAX ? 0 : batches;
        return true;
    }
    return false;
}

char const* OfferFood(Player* bot, Player* to)
{
    if (!ChatEnabled())
        return "DISABLED";
    if (!bot->IsAlive())
        return "DEAD";
    std::vector<Food> const held = Holdings(bot, to);
    std::vector<size_t> const order = OfferOrder(held, to->GetLevel(), s_maxListed);
    if (order.empty())
        return "NO_FOOD";
    std::vector<std::string> pieces;
    for (size_t i : order)
    {
        Food const& f = held[i];
        std::string p = f.Count > 1 ? Trinity::StringFormat("{} x{}", Link(f.Entry), f.Count) : Link(f.Entry);
        if (f.K == Kind::Buff)
            p += " (well fed)";
        pieces.push_back(std::move(p));
    }
    std::vector<std::string> lines = BotTradeLink::PackLines(pieces, 200);
    lines.front() = "I can give you: " + lines.front();
    lines.push_back(BotTradeLink::Enabled() ? "Open a trade with me and whisper the links of what you want." : "Ask the group leader to trade it for you.");
    for (std::string const& l : lines)
        bot->Whisper(l, LANG_UNIVERSAL, to);
    return "OK";
}
}
