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

// Bot bank and mail, see BotBank.h.

#include "BotBank.h"
#include "BotMgr.h"
#include "BotProfession.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace BotBank
{
namespace
{
    using Trinity::StringFormat;

    struct Settings
    {
        bool Enabled = false;
        bool Mail = false;
        std::string Recipient;
        uint32 CooldownMs = 600000;
        BotBankPlan::Config Plan;
        bool Loaded = false;
    };

    // Config is read on first use from the map threads; the values never change afterwards.
    Settings const& Cfg()
    {
        static Settings const s = []()
        {
            Settings v;
            v.Enabled = sConfigMgr->GetBoolDefault("Bot.Bank.Enabled", false);
            v.Recipient = sConfigMgr->GetStringDefault("Bot.Bank.Mail.Recipient", "");
            v.Mail = sConfigMgr->GetBoolDefault("Bot.Bank.Mail.Enabled", false) && !v.Recipient.empty();
            v.CooldownMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Bank.VisitCooldownSec", 600), 60, 86400)) * 1000;
            v.Plan.FreeSlotsBelow = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Bank.FreeSlotsBelow", 4), 0, 30));
            v.Plan.FreeSlotsTarget = std::max(v.Plan.FreeSlotsBelow + 1, uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Bank.FreeSlotsTarget", 10), 1, 40)));
            v.Plan.MaxStacksPerVisit = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Bank.MaxStacksPerVisit", 12), 1, 40));
            v.Plan.GoldReserve = uint64(std::max<int32>(0, sConfigMgr->GetIntDefault("Bot.Bank.Mail.ReserveGold", 50))) * 10000;
            v.Plan.MailMinGold = uint64(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Bank.Mail.MinGold", 20))) * 10000;
            v.Plan.MailMaxGold = uint64(std::max<int32>(0, sConfigMgr->GetIntDefault("Bot.Bank.Mail.MaxGold", 0))) * 10000;
            v.Loaded = true;
            return v;
        }();
        return s;
    }

    void Log(Player* bot, char const* code, std::string summary, uint32 entry = 0)
    {
        BotEvent ev;
        ev.BotGuid = bot->GetGUID().GetCounter();
        ev.Type = "decision";
        ev.Severity = BOTLOG_INFO;
        ev.Reason = code;
        ev.Summary = std::move(summary);
        ev.Level = bot->GetLevel();
        ev.MapId = uint16(bot->GetMapId());
        if (entry)
            ev.TargetEntry = entry;
        sBotMgr->LogEvent(std::move(ev));
    }

    BotBankPlan::Group GroupOf(ItemTemplate const* proto)
    {
        using G = BotBankPlan::Group;
        if (proto->GetQuality() == ITEM_QUALITY_POOR)
            return G::Junk;
        switch (proto->GetClass())
        {
            case ITEM_CLASS_TRADE_GOODS: return G::TradeGood;
            case ITEM_CLASS_GEM: return G::Gem;
            case ITEM_CLASS_RECIPE: return G::Recipe;
            case ITEM_CLASS_REAGENT: return G::Reagent;
            case ITEM_CLASS_CONSUMABLE: return G::Consumable;
            case ITEM_CLASS_CONTAINER:
            case ITEM_CLASS_QUIVER:
            case ITEM_CLASS_PROJECTILE: return G::Container;
            case ITEM_CLASS_WEAPON:
            case ITEM_CLASS_ARMOR: return G::Gear;
            case ITEM_CLASS_QUEST: return G::Quest;
            default: return G::Other;
        }
    }

    // Reagents of every recipe the bot knows for a crafting profession: the bot crafts from its bags (BotQuest CraftForSkill), so these stay.
    std::unordered_set<uint32> CraftInputs(Player* bot)
    {
        std::unordered_set<uint32> inputs;
        for (BotProfession::Info const& info : BotProfession::All())
        {
            if (info.Gathering || !bot->HasSkill(info.Skill))
                continue;
            for (auto const& [spellId, ps] : bot->GetSpellMap())
            {
                if (ps.state == PLAYERSPELL_REMOVED || !ps.active || ps.disabled)
                    continue;
                auto bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
                bool ours = false;
                for (auto it = bounds.first; it != bounds.second; ++it)
                    ours = ours || it->second->SkillLine == info.Skill;
                if (!ours)
                    continue;
                if (SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
                    for (size_t i = 0; i < si->Reagent.size(); ++i)
                        if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
                            inputs.insert(uint32(si->Reagent[i]));
            }
        }
        return inputs;
    }

    bool Deposit(Player* bot, Item* item, std::unordered_set<uint32> const& crafting)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || item->IsNotEmptyBag() || item->IsEquipped())
            return false;
        BotBankPlan::ItemFacts f;
        f.G = GroupOf(proto);
        f.Bound = item->IsSoulBound();
        f.Wanted = bot->HasQuestForItem(item->GetEntry()) || proto->GetStartQuest() || crafting.count(item->GetEntry());
        return BotBankPlan::Depositable(f);
    }

    std::vector<Item*> DepositCandidates(Player* bot)
    {
        std::vector<Item*> out;
        std::unordered_set<uint32> const crafting = CraftInputs(bot);
        bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
        {
            if (Deposit(bot, item, crafting))
                out.push_back(item);
            return ItemSearchCallbackResult::Continue;
        });
        return out;
    }

    std::vector<Item*> WantedInBank(Player* bot, WantFn const& wanted)
    {
        std::vector<Item*> out;
        bot->ForEachItem(ItemSearchLocation::Bank, [&](Item* item)
        {
            if (wanted(bot, item))
                out.push_back(item);
            return ItemSearchCallbackResult::Continue;
        });
        return out;
    }
}

bool Enabled()
{
    return Cfg().Enabled;
}

bool MailEnabled()
{
    return Cfg().Mail;
}

uint32 VisitCooldownMs()
{
    return Cfg().CooldownMs;
}

BotBankPlan::Config const& PlanConfig()
{
    return Cfg().Plan;
}

BotBankPlan::Visit PlanVisit(Player* bot, WantFn const& wanted)
{
    uint32 const free = bot->GetFreeInventorySlotCount();
    uint32 const bankFree = bot->GetFreeInventorySlotCount(ItemSearchLocation::Bank);
    uint32 const stacks = free <= Cfg().Plan.FreeSlotsBelow ? uint32(DepositCandidates(bot).size()) : 0;
    uint32 const want = free ? uint32(WantedInBank(bot, wanted).size()) : 0;
    return BotBankPlan::BankVisit(Cfg().Plan, free, stacks, bankFree, want);
}

uint64 MailGold(Player* bot)
{
    return Cfg().Mail ? BotBankPlan::GoldToMail(Cfg().Plan, bot->GetMoney()) : 0;
}

void PostBank(ObjectGuid botGuid, ObjectGuid banker, WantFn wanted)
{
    sBotMgr->PostWorldTask([botGuid, banker, wanted = std::move(wanted)]()
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld() || !bot->GetSession())
            return;
        if (!bot->GetNPCIfCanInteractWith(banker, UNIT_NPC_FLAG_BANKER, UNIT_NPC_FLAG_2_NONE))
        {
            Log(bot, "BANK_REFUSED", "banker not in reach");
            return;
        }

        // 1. fetch wanted items first, they need one free slot each
        uint32 fetched = 0, deposited = 0;
        for (Item* item : WantedInBank(bot, wanted))
        {
            if (fetched >= 3)
                break;
            ItemPosCountVec dest;
            if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
                break;
            uint32 const entry = item->GetEntry(), count = item->GetCount();
            bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
            if (Item const* stored = bot->StoreItem(dest, item, true))
                bot->ItemAddedQuestCheck(stored->GetEntry(), stored->GetCount());
            ++fetched;
            Log(bot, "BANK_WITHDREW", StringFormat("took item {} x{} out of the bank", entry, count), entry);
        }

        // 2. make room when the bags are tight
        BotBankPlan::Config const& plan = Cfg().Plan;
        uint32 const free = bot->GetFreeInventorySlotCount();
        std::vector<Item*> const cands = free <= plan.FreeSlotsBelow ? DepositCandidates(bot) : std::vector<Item*>();
        uint32 const n = BotBankPlan::StacksToDeposit(plan, free, uint32(cands.size()), bot->GetFreeInventorySlotCount(ItemSearchLocation::Bank));
        for (Item* item : cands)
        {
            if (deposited >= n)
                break;
            ItemPosCountVec dest;
            if (bot->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK || (dest.size() == 1 && dest[0].pos == item->GetPos()))
                break;
            uint32 const entry = item->GetEntry(), count = item->GetCount();
            bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
            bot->ItemRemovedQuestCheck(entry, count);
            bot->BankItem(dest, item, true);
            ++deposited;
            Log(bot, "BANK_DEPOSIT", StringFormat("put item {} x{} in the bank", entry, count), entry);
        }
        Log(bot, "BANK_VISIT", StringFormat("deposited {} stacks, took {} back, {} bag slots free now", deposited, fetched, bot->GetFreeInventorySlotCount()));
    });
}

void PostMailGold(ObjectGuid botGuid, ObjectGuid mailbox)
{
    sBotMgr->PostWorldTask([botGuid, mailbox]()
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld() || !bot->GetSession() || !Cfg().Mail)
            return;
        if (!bot->FindNearestGameObject(mailbox.GetEntry(), 10.0f))
        {
            Log(bot, "MAIL_REFUSED", "mailbox not in reach");
            return;
        }
        ObjectGuid const to = sCharacterCache->GetCharacterGuidByName(Cfg().Recipient);
        if (to.IsEmpty() || to == bot->GetGUID())
        {
            Log(bot, "MAIL_NO_RECIPIENT", StringFormat("recipient '{}' is not a character or is the bot itself", Cfg().Recipient));
            return;
        }
        uint64 const gold = BotBankPlan::GoldToMail(Cfg().Plan, bot->GetMoney());
        if (!gold)
            return;

        bot->ModifyMoney(-int64(gold + BotBankPlan::MAIL_POSTAGE));
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft(std::string("Surplus gold"), std::string())
            .AddMoney(gold)
            .SendMailTo(trans, MailReceiver(ObjectAccessor::FindConnectedPlayer(to), to.GetCounter()), MailSender(bot), MAIL_CHECK_MASK_COPIED);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        Log(bot, "MAIL_SENT", StringFormat("mailed {} copper to {}", gold, Cfg().Recipient));
    });
}
}
