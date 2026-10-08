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

#include "BotControl.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <system_error>

namespace BotControl
{
namespace
{
std::string_view Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool EqI(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string Normalize(std::string_view s)
{
    s = Trim(s);
    while (!s.empty() && (s.back() == '?' || s.back() == '.' || s.back() == '!'))
        s.remove_suffix(1);
    std::string out;
    bool space = false;
    for (char c : Trim(s))
    {
        if (std::isspace(static_cast<unsigned char>(c)))
            space = true;
        else
        {
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += char(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}
}

char const* RoleName(Role r)
{
    switch (r)
    {
        case Role::Tank: return "tank";
        case Role::Healer: return "healer";
        case Role::Dps: return "dps";
        default: return "auto";
    }
}

char const* StanceName(Stance s)
{
    switch (s)
    {
        case Stance::Defensive: return "defensive";
        case Stance::Passive: return "passive";
        default: return "aggressive";
    }
}

bool ParseRole(std::string_view t, Role& out)
{
    t = Trim(t);
    if (EqI(t, "tank"))
        out = Role::Tank;
    else if (EqI(t, "healer") || EqI(t, "heal") || EqI(t, "heals"))
        out = Role::Healer;
    else if (EqI(t, "dps") || EqI(t, "dd") || EqI(t, "damage"))
        out = Role::Dps;
    else if (EqI(t, "auto") || EqI(t, "default") || EqI(t, "class"))
        out = Role::Auto;
    else
        return false;
    return true;
}

bool ParseStance(std::string_view t, Stance& out)
{
    t = Trim(t);
    if (EqI(t, "aggressive") || EqI(t, "aggro"))
        out = Stance::Aggressive;
    else if (EqI(t, "defensive") || EqI(t, "defend"))
        out = Stance::Defensive;
    else if (EqI(t, "passive"))
        out = Stance::Passive;
    else
        return false;
    return true;
}

char const* ParseDistance(std::string_view args, float minYards, float maxYards, DistanceArgs& out)
{
    args = Trim(args);
    DistanceArgs d;
    if (EqI(args, "default") || EqI(args, "reset") || EqI(args, "auto"))
    {
        d.Reset = true;
        out = d;
        return nullptr;
    }
    if (args.empty() || args.size() > 12)
        return "BAD_ARGS";
    if (args.back() == 'y' || args.back() == 'Y')
        args.remove_suffix(1);   // "15y"
    float v = 0.0f;
    auto [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), v);
    if (ec != std::errc() || ptr != args.data() + args.size() || !std::isfinite(v) || v < minYards || v > maxYards)
        return "BAD_ARGS";
    d.Yards = v;
    out = d;
    return nullptr;
}

void FollowThresholds(float distance, float& start, float& arrive)
{
    if (!(distance > 0.0f))
    {
        start = 10.0f;
        arrive = 5.0f;
        return;
    }
    start = distance;
    arrive = std::max(2.0f, distance * 0.5f);
}

bool StanceAllows(Stance stance, FoeFacts const& foe)
{
    switch (stance)
    {
        case Stance::Passive: return foe.AttacksSelf;
        case Stance::Defensive: return foe.AttacksSelf || foe.AttacksGroup;
        default: return true;
    }
}

int32 TankPriority(Role role, bool isWarrior)
{
    if (role == Role::Tank)
        return 0;
    if (role == Role::Auto && isWarrior)
        return 1;
    return -1;
}

std::string DescribeDoing(ReportFacts const& f)
{
    if (!f.Alive)
        return f.Ghost ? "a ghost, waiting to be revived or to find my body" : "dead";
    if (f.InCombat)
        return f.FightTarget.empty() ? "fighting" : "fighting " + f.FightTarget;
    if (f.Resting)
        return "resting";
    if (f.HasGoal && f.GoalTag != "follow")
    {
        if (f.GoalTag == "goto")
            return "heading to a spot you sent me to";
        if (f.GoalTag == "quest")
            return "working on a quest";
        return "travelling (" + f.GoalTag + ")";
    }
    if (f.Staying)
        return "holding position";
    if (f.Following || (f.HasGoal && f.GoalTag == "follow"))
        return f.Mounted ? "following you, mounted" : "following you";
    return f.Mounted ? "idle, mounted" : "idle";
}

std::string DescribeReport(ReportFacts const& f)
{
    std::string out = f.Name + ": " + DescribeDoing(f);
    std::string settings;
    auto add = [&](std::string const& s) { settings += settings.empty() ? s : ", " + s; };
    if (f.RoleSet != Role::Auto)
        add(std::string("role ") + RoleName(f.RoleSet));
    if (f.StanceSet != Stance::Aggressive)
        add(std::string("stance ") + StanceName(f.StanceSet));
    if (f.FollowYards > 0.0f)
        add("follow " + std::to_string(uint32(std::lround(f.FollowYards))) + "y");
    if (!f.Focus.empty())
        add("focus " + f.Focus);
    if (!settings.empty())
        out += "; " + settings;
    if (f.Alive)
    {
        out += "; hp " + std::to_string(f.HealthPct) + "%";
        if (f.UsesMana)
            out += ", mana " + std::to_string(f.ManaPct) + "%";
    }
    return out;
}

bool IsStatusQuestion(std::string_view text)
{
    if (text.empty() || text.size() > 40)
        return false;
    std::string const t = Normalize(text);
    return t == "what are you doing" || t == "what are you up to" || t == "what you doing" || t == "what's up" || t == "whats up"
        || t == "what is up" || t == "report";
}
}
