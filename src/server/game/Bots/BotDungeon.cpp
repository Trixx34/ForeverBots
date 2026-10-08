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

#include "BotDungeon.h"
#include "Config.h"
#include <algorithm>
#include <mutex>

namespace BotDungeon
{
namespace
{
std::once_flag s_once;
Settings s_cfg;

int32 Opt(char const* name, int32 def, int32 lo, int32 hi) { return std::clamp<int32>(sConfigMgr->GetIntDefault(name, def), lo, hi); }
}

Settings const& Cfg()
{
    std::call_once(s_once, []()
    {
        Settings& s = s_cfg;
        s.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Dungeon.Enabled", false);
        s.GroupSize = uint32(Opt("Bot.AI.Dungeon.GroupSize", 5, 2, 5));
        s.Comp.Size = s.GroupSize;
        s.Comp.Tanks = uint32(Opt("Bot.AI.Dungeon.Tanks", 1, 0, 2));
        s.Comp.Healers = uint32(Opt("Bot.AI.Dungeon.Healers", 1, 0, 2));
        s.Compose.MaxLevelSpread = uint32(Opt("Bot.AI.Dungeon.MaxLevelSpread", 4, 0, 20));
        s.Compose.MinLevel = uint32(Opt("Bot.AI.Dungeon.MinLevel", 15, 1, 80));
        s.Ready.MinHealthPct = Opt("Bot.AI.Dungeon.MinHealthPct", 85, 1, 100);
        s.Ready.MinManaPct = Opt("Bot.AI.Dungeon.MinManaPct", 70, 0, 100);
        s.Ready.MinHealerManaPct = Opt("Bot.AI.Dungeon.MinHealerManaPct", 85, 0, 100);
        s.Ready.MinDurabilityPct = Opt("Bot.AI.Dungeon.MinDurabilityPct", 20, 0, 100);
        s.Pull.MaxMobsPerPull = uint32(Opt("Bot.AI.Dungeon.MaxMobsPerPull", 4, 1, 12));
        s.Pull.MaxLevelOverGroup = Opt("Bot.AI.Dungeon.MaxLevelOverGroup", 2, 0, 10);
        s.Pull.SkipPatrols = sConfigMgr->GetBoolDefault("Bot.AI.Dungeon.SkipPatrols", true);
        s.Run.MaxWipes = uint32(Opt("Bot.AI.Dungeon.MaxWipes", 3, 0, 20));
        s.Run.RunTimeoutMs = uint32(Opt("Bot.AI.Dungeon.RunTimeoutMin", 90, 5, 600)) * 60000u;
    });
    return s_cfg;
}
}
