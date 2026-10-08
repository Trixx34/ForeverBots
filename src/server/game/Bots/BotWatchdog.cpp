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

// Bot watchdog, see BotWatchdog.h.

#include "BotWatchdog.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include "BotParty.h"
#include "BotWatchdogPlan.h"
#include "Config.h"
#include "Item.h"
#include "Map.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellHistory.h"
#include "WorldSession.h"
#include "StringFormat.h"
#include <algorithm>
#include <mutex>

namespace BotWatchdog
{
namespace
{
using Trinity::StringFormat;

constexpr uint32 SPELL_HEARTHSTONE = 8690;
constexpr uint32 ITEM_HEARTHSTONE = 6948;

struct Settings
{
    bool Enabled = false;     // Bot.AI.Watchdog.Enabled
    bool Hearth = true;       // Bot.AI.Watchdog.Hearth: stage 2 uses the hearthstone
    bool Home = true;         // Bot.AI.Watchdog.Home: stage 3 teleports to the home bind
    Config Plan;
};
Settings s_cfg;
std::once_flag s_cfgOnce;

Settings const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Watchdog.Enabled", false);
        s_cfg.Hearth = sConfigMgr->GetBoolDefault("Bot.AI.Watchdog.Hearth", true);
        s_cfg.Home = sConfigMgr->GetBoolDefault("Bot.AI.Watchdog.Home", true);
        s_cfg.Plan.StallSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Watchdog.StallSec", 600), 60, 86400));
        s_cfg.Plan.HearthMul = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Watchdog.HearthMul", 2), 1, 20));
        s_cfg.Plan.HomeMul = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Watchdog.HomeMul", 4), 1, 40));
        s_cfg.Plan.MoveYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Watchdog.MoveYards", 10), 1, 500));
        s_cfg.Plan.MaxPerHour = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Watchdog.MaxPerHour", 6), 0, 120));
    });
    return s_cfg;
}

// per-bot tracker (value "watchdog_ctx")
class WatchCtx : public UntypedValue
{
public:
    explicit WatchCtx(BotAI* ai) : UntypedValue(ai, "watchdog_ctx", 0) { }
    Tracker T;
};

Snapshot Read(Player* bot)
{
    Snapshot s;
    s.MapId = bot->GetMapId();
    s.X = bot->GetPositionX();
    s.Y = bot->GetPositionY();
    s.Xp = (uint64(bot->GetLevel()) << 40) | bot->GetXP();
    s.Money = bot->GetMoney();
    uint32 key = bot->GetRewardedQuestCount();
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 id = bot->GetQuestSlotQuestId(slot))
            key = key * 31u + id * 7u + bot->GetQuestSlotState(slot);
    s.QuestKey = key;
    Map const* map = bot->GetMap();
    // fights, flights, deaths, teleports and instance content have their own recovery; the watchdog stays out of them
    s.Busy = !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->IsBeingTeleported()
        || (map && (map->IsDungeon() || map->IsBattlegroundOrArena())) || BotParty::Busy(bot->GetSession() ? bot->GetSession()->GetBotAI() : nullptr);
    return s;
}

class WatchdogTickAction : public Action
{
public:
    explicit WatchdogTickAction(BotAI* ai) : Action(ai, "watchdog_tick", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        WatchCtx* c = static_cast<WatchCtx*>(ai->GetValueRaw("watchdog_ctx"));
        if (!c)
            return false;
        Verdict const v = c->T.Update(ai->GetNowMs(), Read(bot), Cfg().Plan);
        if (v.Exhausted)
        {
            ai->EmitEvent(bot, "decision", BOTLOG_WARN, "WATCHDOG_EXHAUSTED", StringFormat("stalled for {} s, hourly recovery cap used up", v.StalledSec),
                StringFormat(R"({{"stalled_sec":{},"max_per_hour":{}}})", v.StalledSec, Cfg().Plan.MaxPerHour));
            return false;
        }
        switch (v.Act)
        {
            case Step::None:
                return false;
            case Step::ClearGoal:
                ai->Motion().ClearGoal();
                BotMotion::Halt(bot);
                SetResult("WATCHDOG_CLEAR_GOAL", StringFormat("no progress for {} s, goal cleared", v.StalledSec),
                    StringFormat(R"({{"stalled_sec":{},"stage":1}})", v.StalledSec));
                return true;
            case Step::Hearth:
            {
                ai->Motion().ClearGoal();
                Item* stone = Cfg().Hearth ? bot->GetItemByEntry(ITEM_HEARTHSTONE) : nullptr;
                if (stone && !bot->GetSpellHistory()->HasCooldown(SPELL_HEARTHSTONE))
                {
                    BotMotion::Halt(bot);
                    SpellCastResult const res = bot->CastSpell(bot, SPELL_HEARTHSTONE, CastSpellExtraArgs(TRIGGERED_NONE).SetCastItem(stone));
                    SetResult("WATCHDOG_HEARTH", StringFormat("no progress for {} s, using the hearthstone", v.StalledSec),
                        StringFormat(R"({{"stalled_sec":{},"stage":2,"result":{}}})", v.StalledSec, uint32(res)));
                    return res == SPELL_CAST_OK;
                }
                SetResult("WATCHDOG_HEARTH_SKIPPED", StringFormat("no progress for {} s, no usable hearthstone", v.StalledSec),
                    StringFormat(R"({{"stalled_sec":{},"stage":2,"has_stone":{}}})", v.StalledSec, stone ? "true" : "false"));
                return true;
            }
            case Step::Home:
            {
                ai->Motion().ClearGoal();
                if (!Cfg().Home)
                {
                    SetResult("WATCHDOG_HOME_SKIPPED", StringFormat("no progress for {} s, home teleport disabled", v.StalledSec),
                        StringFormat(R"({{"stalled_sec":{},"stage":3}})", v.StalledSec));
                    return true;
                }
                BotMotion::Halt(bot);
                bool const ok = bot->TeleportTo(bot->m_homebind);
                SetResult("WATCHDOG_HOME", StringFormat("no progress for {} s, teleported to the home bind", v.StalledSec),
                    StringFormat(R"({{"stalled_sec":{},"stage":3,"ok":{},"map":{}}})", v.StalledSec, ok ? "true" : "false", bot->m_homebind.GetMapId()));
                return ok;
            }
        }
        return false;
    }
};

class WatchdogDueTrigger : public Trigger
{
public:
    explicit WatchdogDueTrigger(BotAI* ai) : Trigger(ai, "watchdog_due", 5000) { }
    bool IsActive() override { return Cfg().Enabled; }
};

class WatchdogStrategy : public Strategy
{
public:
    WatchdogStrategy() : Strategy("watchdog") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // just above the quest layer; the action returns false unless a recovery is carried out, so other actions still run this tick
        t.push_back({ "watchdog_due", { { "watchdog_tick", BotRelevance::Move + 4.0f } } });
    }
};
} // namespace

bool Enabled()
{
    return Cfg().Enabled;
}
} // namespace BotWatchdog

void RegisterWatchdogBotObjects(BotRegistry& r)
{
    BotWatchdog::Enabled();
    r.AddValue("watchdog_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotWatchdog::WatchCtx>(ai); });
    r.AddTrigger("watchdog_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotWatchdog::WatchdogDueTrigger>(ai); });
    r.AddAction("watchdog_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotWatchdog::WatchdogTickAction>(ai); });
    r.AddStrategy("watchdog", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotWatchdog::WatchdogStrategy>(); });
}
