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

// Bot consumables, see BotConsumables.h.

#include "BotConsumables.h"
#include "BotAI.h"
#include "BotConsumablePlan.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace BotConsumable
{
namespace
{
using Trinity::StringFormat;

struct Settings
{
    bool Enabled = false;     // Bot.AI.Consumables.Enabled
    Config Plan;
};
Settings s_cfg;
std::once_flag s_cfgOnce;

Settings const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Consumables.Enabled", false);
        s_cfg.Plan.HealthBelowPct = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Consumables.HealthBelowPct", 35), 1, 90);
        s_cfg.Plan.ManaBelowPct = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Consumables.ManaBelowPct", 20), 1, 90);
    });
    return s_cfg;
}

struct Found
{
    Candidate C;
    Item* It = nullptr;
    uint32 Spell = 0;
};

// An instant healing or mana potion the bot can use: a consumable whose on-use spell has a heal or a mana energize effect and no aura
// (food and drink are applied as auras and are the rest strategy's job).
bool Classify(Player* bot, Item* item, Found& out)
{
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto || proto->GetClass() != ITEM_CLASS_CONSUMABLE || proto->GetBaseRequiredLevel() > int32(bot->GetLevel()) || bot->CanUseItem(item) != EQUIP_ERR_OK)
        return false;
    for (ItemEffectEntry const* eff : proto->Effects)
    {
        if (!eff || eff->TriggerType != ITEM_SPELLTRIGGER_ON_USE || eff->SpellID <= 0)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(uint32(eff->SpellID), DIFFICULTY_NONE);
        if (!si)
            continue;
        bool aura = false;
        int32 heal = 0, mana = 0;
        for (SpellEffectInfo const& e : si->GetEffects())
        {
            if (e.IsAura())
                aura = true;
            else if (e.IsEffect(SPELL_EFFECT_HEAL))
                heal += std::max(0, e.CalcValueAsInt(bot));
            else if (e.IsEffect(SPELL_EFFECT_ENERGIZE) && e.MiscValue == POWER_MANA)
                mana += std::max(0, e.CalcValueAsInt(bot));
        }
        if (aura || (heal <= 0 && mana <= 0))
            continue;
        out.C.Entry = item->GetEntry();
        out.C.K = heal > 0 ? Kind::Health : Kind::Mana;
        out.C.Restore = uint32(heal > 0 ? heal : mana);
        out.C.Count = item->GetCount();
        out.It = item;
        out.Spell = uint32(eff->SpellID);
        return true;
    }
    return false;
}

class ConsumableAction : public Action
{
public:
    explicit ConsumableAction(BotAI* ai) : Action(ai, "consumable_use", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool Execute() override
    {
        Player* bot = GetBot();
        if (!bot->IsAlive() || bot->IsNonMeleeSpellCast(false, false, true))
            return false;
        Facts f;
        f.InCombat = bot->IsInCombat();
        f.HpPct = int32(bot->GetHealthPct());
        f.HpMax = uint32(bot->GetMaxHealth());
        f.UsesMana = bot->GetPowerType() == POWER_MANA;
        uint32 const maxMana = bot->GetMaxPower(POWER_MANA);
        f.ManaMax = maxMana;
        f.ManaPct = maxMana ? int32(uint64(bot->GetPower(POWER_MANA)) * 100 / maxMana) : 100;
        // cheap exit before the bag scan
        if (!f.InCombat || (f.HpPct >= Cfg().Plan.HealthBelowPct && !(f.UsesMana && f.ManaPct < Cfg().Plan.ManaBelowPct)))
            return false;

        std::vector<Found> found;
        bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
        {
            Found fd;
            if (Classify(bot, item, fd) && !bot->GetSpellHistory()->HasCooldown(fd.Spell))
                found.push_back(fd);
            return ItemSearchCallbackResult::Continue;
        });
        if (found.empty())
            return false;
        std::vector<Candidate> cands;
        cands.reserve(found.size());
        for (Found const& fd : found)
            cands.push_back(fd.C);
        Pick const p = Choose(cands, f, Cfg().Plan);
        if (p.Index < 0)
            return false;
        Found const& fd = found[size_t(p.Index)];
        SpellCastResult const res = bot->CastSpell(bot, fd.Spell, CastSpellExtraArgs(TRIGGERED_NONE).SetCastItem(fd.It));
        SetResult(p.K == Kind::Health ? "POTION_HEALTH" : "POTION_MANA", res == SPELL_CAST_OK ? "drinks a potion" : "drinking a potion failed",
            StringFormat(R"({{"item":{},"restore":{},"result":{},"hp_pct":{},"mana_pct":{}}})", fd.C.Entry, fd.C.Restore, uint32(res), f.HpPct, f.ManaPct));
        return res == SPELL_CAST_OK;
    }
};

class ConsumableTrigger : public Trigger
{
public:
    explicit ConsumableTrigger(BotAI* ai) : Trigger(ai, "consumable_due", 1000) { }
    bool IsActive() override
    {
        if (!Cfg().Enabled)
            return false;
        Player* bot = GetBot();
        return bot && bot->IsInCombat();
    }
};

class ConsumableStrategy : public Strategy
{
public:
    ConsumableStrategy() : Strategy("consumables") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // below the class heal (combat_heal, High + 40) so that tries first; the action returns false when it drinks nothing
        t.push_back({ "consumable_due", { { "consumable_use", BotRelevance::High + 30.0f } } });
    }
};
} // namespace

bool Enabled()
{
    return Cfg().Enabled;
}
} // namespace BotConsumable

void RegisterConsumableBotObjects(BotRegistry& r)
{
    BotConsumable::Enabled();
    r.AddTrigger("consumable_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotConsumable::ConsumableTrigger>(ai); });
    r.AddAction("consumable_use", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotConsumable::ConsumableAction>(ai); });
    r.AddStrategy("consumables", BotStateBit(BotState::Combat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotConsumable::ConsumableStrategy>(); });
}
