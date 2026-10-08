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

// Dynamic bot population, see BotPopulation.h.

#include "BotPopulation.h"
#include "BotMgr.h"
#include "BotPopulationPlan.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>

namespace BotPopulation
{
namespace
{
struct Settings
{
    Config Plan;
    uint32 IntervalMs = 60000;   // Bot.Population.IntervalSec
    uint32 StartDelayMs = 60000; // Bot.Population.StartDelaySec: no change before this much uptime, so a restart does not fight the saved bots
};
Settings s_cfg;
std::once_flag s_cfgOnce;
uint32 s_sinceMs = 0;
uint32 s_uptimeMs = 0;
uint32 s_lastTarget = 0xFFFFFFFFu;

Settings const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        Config& c = s_cfg.Plan;
        c.Enabled = sConfigMgr->GetBoolDefault("Bot.Population.Enabled", false);
        c.Base = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.Base", 100), 0, 5000));
        c.PerPlayerTenths = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.PerPlayerTenths", 0), 0, 1000));
        c.Max = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.Max", 200), 0, 5000));
        c.Min = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.Min", 0), 0, 5000));
        c.StepUp = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.StepUp", 5), 1, 100));
        c.StepDown = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.StepDown", 3), 1, 100));
        c.Hysteresis = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.Hysteresis", 2), 0, 100));
        std::string const hours = sConfigMgr->GetStringDefault("Bot.Population.HourPct", "");
        if (!hours.empty() && !ParseHours(hours, c.HourPct))
            TC_LOG_ERROR("server.worldserver", "Bot.Population.HourPct '{}' is not a list of up to 24 percentages, ignored", hours);
        s_cfg.IntervalMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.IntervalSec", 60), 5, 3600)) * 1000;
        s_cfg.StartDelayMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Population.StartDelaySec", 60), 0, 3600)) * 1000;
    });
    return s_cfg;
}

uint32 RealPlayers()
{
    uint32 n = 0;
    for (auto const& [id, session] : sWorld->GetAllSessions())
        if (session && !session->IsBot())
            if (Player* p = session->GetPlayer(); p && p->IsInWorld())
                ++n;
    return n;
}
}

void Update(uint32 diff)
{
    Settings const& s = Cfg();
    if (!s.Plan.Enabled || !sWorld->getBoolConfig(CONFIG_BOT_ENABLED))
        return;
    s_uptimeMs += diff;
    if (s_uptimeMs < s.StartDelayMs || (s_sinceMs += diff) < s.IntervalMs)
        return;
    s_sinceMs = 0;

    uint32 const players = RealPlayers();
    tm const* now = GameTime::GetDateAndTime();
    uint32 const target = Target(s.Plan, players, now ? uint32(now->tm_hour) : 12);
    uint32 const current = sBotMgr->PoolBotCount();
    int32 const step = Step(s.Plan, current, target);
    if (target != s_lastTarget || step)
    {
        TC_LOG_INFO("server.worldserver", "Bot population: {} bots, target {} ({} players), step {}", current, target, players, step);
        s_lastTarget = target;
    }
    if (step > 0)
    {
        BotSpawnResult const r = sBotMgr->SpawnBots(uint32(step), 0, -1, std::nullopt);
        if (r.Failed && !r.Error.empty())
            TC_LOG_WARN("server.worldserver", "Bot population: spawn failed: {}", r.Error);
    }
    else if (step < 0)
        sBotMgr->TrimPoolBots(uint32(-step));
}
}
