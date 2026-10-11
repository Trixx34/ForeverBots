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

// Crafting for players, see BotCraftOrder.h.

#include "BotCraftOrder.h"
#include "BotMgr.h"
#include "BotProfession.h"
#include "BotTradeLink.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>

namespace BotCraftOrder
{
namespace
{
    constexpr uint32 MAIL_ATTACHMENTS = 12;   // what the mail window shows; the core allows more

    std::string Lower(std::string_view s)
    {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    }

    std::vector<std::string_view> Words(std::string_view s)
    {
        std::vector<std::string_view> out;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            size_t const start = i;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            if (i > start)
                out.push_back(s.substr(start, i - start));
        }
        return out;
    }

    // "x5", "5x" or "5": the count, 0 when the word is not one.
    uint32 CountWord(std::string_view w)
    {
        if (!w.empty() && (w.front() == 'x' || w.front() == 'X'))
            w.remove_prefix(1);
        else if (!w.empty() && (w.back() == 'x' || w.back() == 'X'))
            w.remove_suffix(1);
        if (w.empty() || w.size() > 9 || !std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c); }))
            return 0;
        uint32 n = 0;
        std::from_chars(w.data(), w.data() + w.size(), n);
        return n;
    }
}

Request ParseRequest(std::string_view text, uint32 maxCount)
{
    Request req;
    std::string_view rest = text;
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
        rest.remove_prefix(1);
    if (rest.size() < 5 || Lower(rest.substr(0, 5)) != "craft" || (rest.size() > 5 && !std::isspace(static_cast<unsigned char>(rest[5]))))
        return req;
    rest.remove_prefix(5);

    // an item link has spaces in its name: take it out first
    std::string stripped;
    constexpr std::string_view tag = "|Hitem:";
    size_t const pos = rest.find(tag);
    if (pos != std::string_view::npos)
    {
        uint32 entry = 0;
        char const* end = rest.data() + rest.size();
        auto [ptr, ec] = std::from_chars(rest.data() + pos + tag.size(), end, entry);
        if (ec == std::errc() && entry && ptr != end && (*ptr == ':' || *ptr == '|'))
            req.ItemEntry = entry;
        size_t start = pos;
        if (start >= 10 && rest.substr(start - 10, 2) == "|c")
            start -= 10;
        size_t const h1 = rest.find("|h", pos + tag.size());
        size_t const h2 = h1 == std::string_view::npos ? h1 : rest.find("|h", h1 + 2);
        size_t stop = h2 == std::string_view::npos ? rest.size() : h2 + 2;
        if (rest.substr(stop, 2) == "|r")
            stop += 2;
        stripped = std::string(rest.substr(0, start)) + " " + std::string(rest.substr(std::min(stop, rest.size())));
        rest = stripped;
    }

    std::vector<std::string_view> words = Words(rest);
    uint32 count = 0;
    if (words.size() > (req.ItemEntry ? 0u : 1u))
    {
        if (uint32 n = CountWord(words.back()))
        {
            count = n;
            words.pop_back();
        }
        else if (uint32 first = CountWord(words.front()))
        {
            count = first;
            words.erase(words.begin());
        }
    }
    if (!req.ItemEntry && words.size() == 1 && CountWord(words[0]))
        return req;   // a bare number is no item name
    if (!req.ItemEntry && words.size() == 1 && Lower(words[0]) == "list")
    {
        req.Type = Kind::List;
        return req;
    }
    for (std::string_view w : words)
        if (!req.ItemEntry)
            req.Name += (req.Name.empty() ? "" : " ") + Lower(w);
    if (!req.ItemEntry && req.Name.empty())
        return req;
    req.Type = Kind::Craft;
    req.Count = std::clamp<uint32>(count ? count : 1, 1, std::max<uint32>(maxCount, 1));
    return req;
}

int Match(Request const& request, std::span<Option const> options)
{
    int best = -1;
    int bestScore = 0;
    for (size_t i = 0; i < options.size(); ++i)
    {
        Option const& o = options[i];
        int score = 0;
        if (request.ItemEntry)
            score = o.Item == request.ItemEntry ? 4 : 0;
        else if (!request.Name.empty())
        {
            if (o.Name == request.Name)
                score = 4;
            else if (o.Name.starts_with(request.Name))
                score = 3;
            else if (o.Name.find(request.Name) != std::string::npos)
                score = 2;
        }
        if (!score)
            continue;
        bool better = best < 0 || score > bestScore;
        if (!better && score == bestScore)
            better = o.Name.size() < options[best].Name.size() || (o.Name.size() == options[best].Name.size() && o.Spell < options[best].Spell);
        if (better)
        {
            best = int(i);
            bestScore = score;
        }
    }
    return best;
}

Plan PlanOrder(Facts const& f)
{
    Plan plan;
    uint32 const yield = std::max<uint32>(f.Yield, 1);
    if (!f.MaxItems || !f.Wanted)
    {
        plan.Refusal = "TOO_MANY";
        return plan;
    }
    if (!f.CastsPossible)
    {
        plan.Refusal = "NO_REAGENTS";
        return plan;
    }
    uint32 const capItems = std::min<uint64>(f.MaxItems, uint64(MAIL_ATTACHMENTS) * std::max<uint32>(f.MaxStack, 1));
    uint32 const wantedCasts = (f.Wanted + yield - 1) / yield;
    uint32 const capCasts = std::max<uint32>(capItems / yield, 1);
    plan.Casts = std::min({ wantedCasts, capCasts, f.CastsPossible });
    plan.Items = plan.Casts * yield;
    plan.Trimmed = plan.Items < f.Wanted;
    return plan;
}

std::vector<uint32> MailStacks(uint32 items, uint32 maxStack)
{
    std::vector<uint32> out;
    maxStack = std::max<uint32>(maxStack, 1);
    for (; items > maxStack; items -= maxStack)
        out.push_back(maxStack);
    if (items)
        out.push_back(items);
    return out;
}

uint32 Fee(uint32 sellPrice, uint32 items, uint32 percent)
{
    uint64 const fee = uint64(sellPrice) * items * percent / 100;
    return uint32(std::min<uint64>(fee, std::numeric_limits<uint32>::max()));
}

std::string FormatMoney(uint32 copper)
{
    uint32 const g = copper / 10000, s = copper / 100 % 100, c = copper % 100;
    std::string out;
    auto add = [&](uint32 n, char const* unit)
    {
        if (n)
            out += (out.empty() ? "" : " ") + std::to_string(n) + " " + unit;
    };
    add(g, "gold");
    add(s, "silver");
    add(c, "copper");
    return out.empty() ? "0 copper" : out;
}

namespace
{
    using Trinity::StringFormat;

    struct Recipe
    {
        Option Opt;
        uint32 CastsPossible = 0;
        std::vector<std::pair<uint32, uint32>> Reagents;   // entry, count per cast
    };

    struct Order
    {
        ObjectGuid Bot, Player;
        uint32 Spell = 0, Item = 0;
        uint32 CastsLeft = 0, PlannedItems = 0, Before = 0;
        uint32 ElapsedMs = 0, DeadlineMs = 0;
        uint32 Percent = 0;
        uint8 Failures = 0;
        char const* StopReason = nullptr;
    };

    std::vector<Order> s_orders;
    uint32 s_tickMs = 0;
    constexpr uint32 TICK_MS = 500;

    uint32 MaxItems() { return uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Craft.MaxItems", 20), 0, 200)); }

    std::string Link(uint32 entry)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!proto)
            return std::to_string(entry);
        uint32 const q = std::min<uint32>(proto->GetQuality(), MAX_ITEM_QUALITY - 1);
        return StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:0|h[{}]|h|r", ItemQualityColors[q], entry, proto->GetName(LOCALE_enUS));
    }

    void Say(Player* bot, Player* to, std::string const& text)
    {
        if (bot && to)
            bot->Whisper(text, LANG_UNIVERSAL, to);
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

    // Every item the bot's crafting professions can make with a spell it knows, with the casts its reagents allow now.
    std::vector<Recipe> Recipes(Player* bot)
    {
        std::vector<Recipe> out;
        for (auto const& [spellId, ps] : bot->GetSpellMap())
        {
            if (ps.state == PLAYERSPELL_REMOVED || !ps.active || ps.disabled)
                continue;
            bool crafting = false;
            auto bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
            for (auto it = bounds.first; it != bounds.second; ++it)
                if (BotProfession::Info const* info = BotProfession::Find(it->second->SkillLine))
                    crafting = crafting || (!info->Gathering && bot->HasSkill(info->Skill));
            if (!crafting)
                continue;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!si)
                continue;
            Recipe r;
            for (SpellEffectInfo const& eff : si->GetEffects())
                if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && eff.ItemType)
                {
                    r.Opt.Item = eff.ItemType;
                    r.Opt.Yield = uint32(std::max<int32>(eff.CalcValueAsInt(bot), 1));
                    break;
                }
            ItemTemplate const* proto = r.Opt.Item ? sObjectMgr->GetItemTemplate(r.Opt.Item) : nullptr;
            if (!proto)
                continue;
            r.Opt.Spell = spellId;
            r.Opt.Name = Lower(proto->GetName(LOCALE_enUS));
            uint32 possible = 1000;
            for (size_t i = 0; i < si->Reagent.size(); ++i)
                if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
                {
                    r.Reagents.emplace_back(uint32(si->Reagent[i]), uint32(si->ReagentCount[i]));
                    possible = std::min(possible, bot->GetItemCount(uint32(si->Reagent[i])) / uint32(si->ReagentCount[i]));
                }
            r.CastsPossible = possible;
            out.push_back(std::move(r));
        }
        std::sort(out.begin(), out.end(), [](Recipe const& a, Recipe const& b) { return a.Opt.Name != b.Opt.Name ? a.Opt.Name < b.Opt.Name : a.Opt.Spell < b.Opt.Spell; });
        return out;
    }

    void Finish(Order const& o)
    {
        Player* bot = ObjectAccessor::FindPlayer(o.Bot);
        Player* to = ObjectAccessor::FindConnectedPlayer(o.Player);
        if (!bot || !bot->IsInWorld())
            return;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(o.Item);
        uint32 const have = bot->GetItemCount(o.Item);
        uint32 const made = have > o.Before ? std::min(have - o.Before, o.PlannedItems) : 0;
        if (!proto || !made)
        {
            Say(bot, to, StringFormat("I could not make {}.{}", Link(o.Item), o.StopReason ? std::string(" ") + o.StopReason : std::string()));
            Log(bot, "CRAFT_FAIL", StringFormat("order for item {} made nothing", o.Item), o.Item);
            return;
        }

        uint32 const fee = Fee(proto->GetSellPrice(), made, o.Percent);
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft draft(StringFormat("Crafted: {}", proto->GetName(LOCALE_enUS)), StringFormat("Made for you by {}.", bot->GetName()));
        for (uint32 n : MailStacks(made, proto->GetMaxStackSize()))
            if (Item* item = Item::CreateItem(o.Item, n, ItemContext::NONE, bot))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
        bot->DestroyItemCount(o.Item, made, true);
        if (fee)
            draft.AddCOD(fee);
        draft.SendMailTo(trans, MailReceiver(to, o.Player.GetCounter()), MailSender(bot), MAIL_CHECK_MASK_COPIED);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        std::string msg = StringFormat("Mailed you {} x{}.", Link(o.Item), made);
        if (fee)
            msg += StringFormat(" Cash on delivery: {}.", FormatMoney(fee));
        if (made < o.PlannedItems && o.StopReason)
            msg += StringFormat(" I stopped early: {}", o.StopReason);
        Say(bot, to, msg);
        Log(bot, "CRAFT_DONE", StringFormat("crafted {} of item {} for {}, cod {} copper", made, o.Item, o.Player.ToString(), fee), o.Item);
    }

    char const* FailureText(SpellCastResult res)
    {
        switch (res)
        {
            case SPELL_FAILED_REQUIRES_SPELL_FOCUS: return "I need to stand next to a forge, anvil, fire or similar.";
            case SPELL_FAILED_TOTEMS: return "I am missing a tool for it.";
            case SPELL_FAILED_REAGENTS: return "I ran out of materials.";
            default: return "The cast failed.";
        }
    }
}

bool Enabled()
{
    return sWorld->getBoolConfig(CONFIG_BOT_ENABLED) && sConfigMgr->GetBoolDefault("Bot.Craft.Enabled", false);
}

void OnWhisper(Player* player, Player* bot, std::string_view text)
{
    if (!Enabled() || !bot || player->GetSession()->IsBot() || !bot->GetSession()->IsBot() || text.size() < 5)
        return;
    Request const req = ParseRequest(text, MaxItems());
    if (req.Type == Kind::None)
        return;
    if (!player->GetGroup() || player->GetGroup() != bot->GetGroup())
        return;   // only the bots of your own group listen
    if (!bot->IsAlive())
        return;
    if (std::any_of(s_orders.begin(), s_orders.end(), [&](Order const& o) { return o.Bot == bot->GetGUID(); }))
        return Say(bot, player, "I am still busy with another order.");
    if (!MaxItems())
        return Say(bot, player, "I do not take craft orders.");

    std::vector<Recipe> const recipes = Recipes(bot);
    if (req.Type == Kind::List)
    {
        uint32 const maxListed = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Craft.MaxListed", 15), 1, 40));
        std::vector<std::string> pieces;
        uint32 total = 0;
        for (Recipe const& r : recipes)
            if (r.CastsPossible)
            {
                if (++total <= maxListed)
                    pieces.push_back(r.CastsPossible >= 1000 ? Link(r.Opt.Item) : StringFormat("{} (up to {})", Link(r.Opt.Item), r.CastsPossible * r.Opt.Yield));
            }
        if (pieces.empty())
            return Say(bot, player, "I cannot make anything with what I carry.");
        std::vector<std::string> lines = BotTradeLink::PackLines(pieces, 200);
        lines.front() = "I can make: " + lines.front();
        if (total > pieces.size())
            lines.push_back(StringFormat("... and {} more. Whisper me: craft <item link> [x5]", total - pieces.size()));
        else
            lines.push_back("Whisper me: craft <item link> [x5]");
        for (std::string const& l : lines)
            Say(bot, player, l);
        return;
    }

    std::vector<Option> options;
    options.reserve(recipes.size());
    for (Recipe const& r : recipes)
        options.push_back(r.Opt);
    int const idx = Match(req, options);
    if (idx < 0)
        return Say(bot, player, "I do not know a recipe for that.");
    Recipe const& r = recipes[idx];
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(r.Opt.Item);
    if (!proto)
        return;

    Facts facts;
    facts.Wanted = req.Count;
    facts.Yield = r.Opt.Yield;
    facts.MaxItems = MaxItems();
    facts.MaxStack = proto->GetMaxStackSize();
    facts.CastsPossible = r.CastsPossible;
    Plan const plan = PlanOrder(facts);
    if (plan.Refusal)
    {
        std::string needs;
        for (auto const& [entry, n] : r.Reagents)
            needs += StringFormat("{}{} x{}", needs.empty() ? "" : ", ", Link(entry), n);
        Say(bot, player, StringFormat("I lack the materials for {}.{}", Link(r.Opt.Item), needs.empty() ? std::string() : " Each needs: " + needs + ". Trade them to me."));
        return;
    }

    Order o;
    o.Bot = bot->GetGUID();
    o.Player = player->GetGUID();
    o.Spell = r.Opt.Spell;
    o.Item = r.Opt.Item;
    o.CastsLeft = plan.Casts;
    o.PlannedItems = plan.Items;
    o.Before = bot->GetItemCount(o.Item);
    o.Percent = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Craft.Fee.VendorPercent", 0), 0, 1000));
    o.DeadlineMs = plan.Casts * 15000 + 30000;
    s_orders.push_back(o);

    uint32 const fee = Fee(proto->GetSellPrice(), plan.Items, o.Percent);
    std::string msg = StringFormat("On it: {} x{}. I will mail it to you.", Link(o.Item), plan.Items);
    if (plan.Trimmed)
        msg += " That is all I can make or send in one order.";
    if (fee)
        msg += StringFormat(" Cash on delivery: {}.", FormatMoney(fee));
    Say(bot, player, msg);
    Log(bot, "CRAFT_QUEUED", StringFormat("craft order from {}: spell {} x{} casts for item {}", player->GetName(), o.Spell, plan.Casts, o.Item), o.Item);
}

void Update(uint32 diff)
{
    if (s_orders.empty())
        return;
    s_tickMs += diff;
    if (s_tickMs < TICK_MS)
        return;
    uint32 const step = s_tickMs;
    s_tickMs = 0;

    for (size_t i = 0; i < s_orders.size();)
    {
        Order& o = s_orders[i];
        Player* bot = ObjectAccessor::FindPlayer(o.Bot);
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        {
            s_orders.erase(s_orders.begin() + i);
            continue;
        }
        o.ElapsedMs += step;
        bool done = false;
        if (bot->IsNonMeleeSpellCast(false))
        {
            // wait for the cast in flight
        }
        else if (!o.CastsLeft)
            done = true;
        else if (o.ElapsedMs > o.DeadlineMs)
        {
            o.StopReason = "it took too long.";
            done = true;
        }
        else if (!bot->IsInCombat() && !bot->isMoving() && !bot->GetFreeInventorySlotCount())
        {
            o.StopReason = "My bags are full.";
            done = true;
        }
        else if (!bot->IsInCombat() && !bot->isMoving())
        {
            SpellCastResult const res = bot->CastSpell(bot, o.Spell, CastSpellExtraArgs(TRIGGERED_NONE));
            if (res == SPELL_CAST_OK)
                --o.CastsLeft;
            else if (res == SPELL_FAILED_REQUIRES_SPELL_FOCUS || res == SPELL_FAILED_TOTEMS || res == SPELL_FAILED_REAGENTS
                || ++o.Failures >= 3)
            {
                o.StopReason = FailureText(res);
                done = true;
            }
        }

        if (!done)
        {
            ++i;
            continue;
        }
        Order const finished = o;
        s_orders.erase(s_orders.begin() + i);
        Finish(finished);
    }
}
}
