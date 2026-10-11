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

// Settings and shared state of the additional rules, see BotRules.h.

#include "BotRules.h"
#include "BotConfigReload.h"
#include "BotTravelPlan.h"
#include "Config.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>

namespace BotRules
{
namespace
{
Settings s_cfg;
BotCfg::Flag s_cfgOnce;
BotRule::StallBook s_stalls;
BotRule::CrowdBook s_crowds;

int32 Int(char const* name, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(name, def), lo, hi); }
bool Bool(char const* name, bool def) { return sConfigMgr->GetBoolDefault(name, def); }
}

Settings const& Cfg()
{
    BotCfg::Load(s_cfgOnce, []()
    {
        Settings s;
        s.ArmPct = uint32(Int("Bot.Rules.ArmPct", 0, 0, 100));

        s.XpStall = Bool("Bot.Rules.XpStall.Enabled", true);
        s.XpStallRelog = Bool("Bot.Rules.XpStall.Relog", true);
        s.Stall.ClearSec = uint32(Int("Bot.Rules.XpStall.ClearSec", 1200, 60, 86400));
        s.Stall.RelogSec = std::max(s.Stall.ClearSec, uint32(Int("Bot.Rules.XpStall.RelogSec", 2400, 60, 172800)));
        s.Stall.HubSec = std::max(s.Stall.RelogSec, uint32(Int("Bot.Rules.XpStall.HubSec", 5400, 60, 259200)));
        s.Stall.MaxPerHour = uint32(Int("Bot.Rules.XpStall.MaxPerHour", 6, 0, 120));

        s.ZoneFence = Bool("Bot.Rules.ZoneFence.Enabled", true);
        s.FenceMargin = Int("Bot.Rules.ZoneFence.Margin", 4, 0, 20);
        s.FenceStepYd = float(Int("Bot.Rules.ZoneFence.StepYards", 150, 30, 1000));

        s.PullGate = Bool("Bot.Rules.PullGate.Enabled", true);
        s.Pull.HealthPct = float(Int("Bot.Rules.PullGate.HealthPct", 80, 0, 100));
        s.Pull.PowerPct = float(Int("Bot.Rules.PullGate.PowerPct", 50, 0, 100));
        s.Pull.MaxWaitSec = uint32(Int("Bot.Rules.PullGate.MaxWaitSec", 120, 10, 3600));

        s.NoDamage = Bool("Bot.Rules.NoDamage.Enabled", true);
        s.NoDmg.WindowMs = uint32(Int("Bot.Rules.NoDamage.WindowSec", 10, 3, 120)) * 1000;
        s.NoDmg.MinPct = float(Int("Bot.Rules.NoDamage.MinPct", 5, 1, 50));

        s.BagPressure = Bool("Bot.Rules.BagPressure.Enabled", true);
        s.Bag.TripFreeSlots = uint32(Int("Bot.Rules.BagPressure.TripFreeSlots", 4, 1, 20));
        s.Bag.VendorYd = float(Int("Bot.Rules.BagPressure.VendorYards", 400, 50, 3000));
        s.Bag.GreyFreeSlots = uint32(Int("Bot.Rules.BagPressure.GreyFreeSlots", 4, 0, 20));

        s.Crowd = Bool("Bot.Rules.Crowd.Enabled", true);
        s.CrowdCap = uint32(Int("Bot.Rules.Crowd.Cap", 8, 1, 200));
        s.CrowdRadius = float(Int("Bot.Rules.Crowd.RadiusYards", 150, 20, 1000));
        s.CrowdFactor = float(Int("Bot.Rules.Crowd.CrowdedPct", 35, 0, 100)) / 100.0f;

        s.ZoneLeave = Bool("Bot.Rules.ZoneLeave.Enabled", true);
        s.Leave.HubNoneRows = uint32(Int("Bot.Rules.ZoneLeave.HubNoneRows", 2, 1, 20));
        s.Leave.GrindMinutes = uint32(Int("Bot.Rules.ZoneLeave.GrindMinutes", 10, 1, 240));
        s.Leave.MinXpPerKill = uint32(Int("Bot.Rules.ZoneLeave.MinXpPerKill", 40, 1, 100000));

        s.ClassMargin = Bool("Bot.Rules.ClassMargin.Enabled", false);
        s.ClassMarginUntil = uint32(Int("Bot.Rules.ClassMargin.UntilLevel", 10, 1, 60));
        s.ClassMarginWeak = Int("Bot.Rules.ClassMargin.WeakClasses", 0, 0, 10);
        s.ClassMarginSturdy = Int("Bot.Rules.ClassMargin.SturdyClasses", 1, 0, 10);

        s.LogDiet = Bool("Bot.Log.Diet.Enabled", true);
        s.HourlyCaps = BotRule::ParseHourlyCaps(sConfigMgr->GetStringDefault("Bot.Log.Diet.HourlyCaps", "TARGET_PICKED:60,AGGRO_AVOID:30"));
        s.AuraDeathOnly = Bool("Bot.Log.Diet.AuraDeathOnly", true);
        s_cfg = std::move(s);
    });
    return s_cfg;
}

bool Applies(uint64 guidCounter)
{
    uint32 const pct = Cfg().ArmPct;
    return !pct || uint32(guidCounter % 100) < pct;
}

bool Applies(Player const* bot)
{
    return bot && Applies(bot->GetGUID().GetCounter());
}

BotRule::StallBook& Stalls() { return s_stalls; }
BotRule::CrowdBook& Crowds() { return s_crowds; }

bool Fenced(Player* bot, float x, float y, float z, uint32& zone, uint32& minLevel)
{
    Settings const& c = Cfg();
    if (!c.ZoneFence || !bot || !bot->GetMap() || !Applies(bot))
        return false;
    uint32 const area = bot->GetMap()->GetZoneId(bot->GetPhaseShift(), x, y, z);
    BotTravel::Zone const* band = area ? BotTravel::FindZone(area) : nullptr;
    if (!band || band->Map != bot->GetMapId())
        return false;
    if (!BotRule::ZoneFenced(int(bot->GetLevel()), int(band->MinLevel), c.FenceMargin))
        return false;
    zone = band->Area;
    minLevel = band->MinLevel;
    return true;
}

bool LegFenced(Player* bot, float x, float y, float z, uint32& zone, uint32& minLevel)
{
    Settings const& c = Cfg();
    if (!c.ZoneFence || !bot || !Applies(bot))
        return false;
    // a bot that already stands in a fenced zone may stay in it: only other fenced zones close the leg
    uint32 homeZone = 0, homeMin = 0;
    bool const homeFenced = Fenced(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), homeZone, homeMin);
    for (BotRule::Pt2 const& p : BotRule::LegSamples(bot->GetPositionX(), bot->GetPositionY(), x, y, c.FenceStepYd, c.FenceMaxPoints))
    {
        bool const last = p.X == x && p.Y == y;
        if (Fenced(bot, p.X, p.Y, last ? z : bot->GetPositionZ(), zone, minLevel) && !(homeFenced && zone == homeZone))
            return true;
    }
    return false;
}
}
