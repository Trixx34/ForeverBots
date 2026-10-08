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

#include "BotPlayerLedPlan.h"
#include <algorithm>
#include <cmath>

namespace BotPlayerLed
{
Decision Decide(Facts const& f, Config const& cfg)
{
    if (!f.Follows)
        return { Act::None, "NOT_FOLLOWING" };
    if (!f.LeaderAlive)
        return { Act::None, "LEADER_DEAD" };
    if (f.SinceActMs < cfg.ActCooldownMs)
        return { Act::None, "COOLDOWN" };
    if (f.LeaderMapSinceMs < cfg.EnterDelayMs)
        return { Act::None, "LEADER_JUST_ARRIVED" };

    if (!f.BotAlive)
    {
        // raised only where the leader is in a dungeon and the fight is over; chat `revive` covers everything else
        if (!f.LeaderInDungeon)
            return { Act::None, "DEAD" };
        if (!cfg.RaiseDelayMs)
            return { Act::None, "RAISE_OFF" };
        if (f.BotInDungeon && !f.BotInInstanceOfLeader)
            return { Act::None, "OTHER_INSTANCE" };
        if (f.LeaderInCombat)
            return { Act::None, "FIGHT_ON" };
        if (f.DeadForMs < cfg.RaiseDelayMs)
            return { Act::None, "RAISE_WAIT" };
        return { Act::Raise, "RAISE" };
    }

    if (f.BotInCombat)
        return { Act::None, "BOT_IN_COMBAT" };

    if (f.LeaderInDungeon)
    {
        if (f.BotInInstanceOfLeader)
        {
            if (f.Distance <= cfg.CatchUpYards || f.FarForMs < cfg.CatchUpMs)
                return { Act::None, "KEEPING_UP" };
            if (f.LeaderInCombat)
                return { Act::None, "FIGHT_ON" };
            return { Act::CatchUp, "LEFT_BEHIND" };
        }
        if (f.BotInDungeon)
            return { Act::None, "OTHER_INSTANCE" };
        return { Act::Enter, "LEADER_INSIDE" };
    }

    if (f.BotInDungeon && f.LeaderOnWorldMap)
        return { Act::Leave, "LEADER_OUTSIDE" };
    return { Act::None, "NOTHING_TO_DO" };
}

char const* ActName(Act a)
{
    switch (a)
    {
        case Act::None: return "none";
        case Act::Enter: return "enter";
        case Act::Leave: return "leave";
        case Act::CatchUp: return "catch_up";
        case Act::Raise: return "raise";
    }
    return "none";
}

void Spread(uint32 index, float& dx, float& dy)
{
    // rings of 6 around the leader, 2.5 yards apart in radius, starting at 2.5 yards
    uint32 const ring = index / 6, slot = index % 6;
    float const radius = 2.5f * float(ring + 1);
    float const angle = float(slot) * (6.2831853f / 6.0f) + float(ring) * 0.5f;
    dx = radius * std::cos(angle);
    dy = radius * std::sin(angle);
}

std::vector<Need> ListNeeds(std::span<BotDungeon::MemberState const> bots, BotDungeon::ReadyConfig const& cfg)
{
    std::vector<Need> out;
    for (BotDungeon::MemberState const& m : bots)
    {
        Need n;
        n.Guid = m.Guid;
        if (!m.Alive)
            n.Why = NeedWhy::Dead;
        else if (!m.Present)
            n.Why = NeedWhy::Away;
        else if (m.HealthPct < cfg.MinHealthPct)
        {
            n.Why = NeedWhy::Health;
            n.Value = m.HealthPct;
        }
        else if (m.ManaPct < (m.AsRole == BotDungeon::Role::Healer ? cfg.MinHealerManaPct : cfg.MinManaPct))
        {
            n.Why = NeedWhy::Mana;
            n.Value = m.ManaPct;
        }
        else
            continue;
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](Need const& a, Need const& b)
    {
        if (a.Why != b.Why)
            return a.Why < b.Why;
        return a.Guid < b.Guid;
    });
    return out;
}

Notice NoticeDecision(bool wasNeedy, bool needy, bool leaderInCombat, uint32 sinceLastNoticeMs, uint32 repeatMs)
{
    if (leaderInCombat)
        return Notice::None;
    if (needy)
        return (!wasNeedy || sinceLastNoticeMs >= repeatMs) ? Notice::Resting : Notice::None;
    return wasNeedy ? Notice::Ready : Notice::None;
}

char const* NeedText(NeedWhy w)
{
    switch (w)
    {
        case NeedWhy::Dead: return "dead";
        case NeedWhy::Away: return "away";
        case NeedWhy::Health: return "health";
        case NeedWhy::Mana: return "mana";
    }
    return "";
}
}
