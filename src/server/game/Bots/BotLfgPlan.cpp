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

#include "BotLfgPlan.h"
#include <algorithm>
#include <cctype>

namespace BotLfg
{
namespace
{
using BotDungeon::Role;

std::string Lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool RoleFromToken(std::string const& t, Role& out)
{
    if (t == "tank")
        out = Role::Tank;
    else if (t == "healer" || t == "heal" || t == "healing")
        out = Role::Healer;
    else if (t == "dps" || t == "damage" || t == "dd")
        out = Role::Dps;
    else
        return false;
    return true;
}

uint32 RoleIndex(Role r) { return uint32(r); }
}

Request ParseRequest(std::string_view text)
{
    Request req;
    std::string const s = Lower(Trim(text));
    if (s.size() < 3 || s.compare(0, 3, "lfg") != 0)
        return req;
    if (s.size() > 3 && !std::isspace(static_cast<unsigned char>(s[3])))
        return req;                       // "lfgxyz" is an ordinary word
    std::string_view const arg = Trim(std::string_view(s).substr(3));
    if (arg.empty())
    {
        req.What = Action::Join;
        return req;
    }
    std::string const a(arg);
    if (a == "off" || a == "cancel" || a == "leave" || a == "stop")
        req.What = Action::Leave;
    else if (a == "status")
        req.What = Action::Status;
    else if (RoleFromToken(a, req.AsRole))
    {
        req.What = Action::Join;
        req.HasRole = true;
    }
    else
    {
        req.What = Action::Join;
        req.BadArgs = true;
    }
    return req;
}

FillResult OpenPlaces(std::span<Role const> have, Config const& cfg)
{
    FillResult res;
    uint32 const size = std::clamp<uint32>(cfg.GroupSize, 1, 5);
    uint32 const present = uint32(have.size());
    if (present >= size)
        return res;
    uint32 const free = size - present;
    uint32 count[3] = { 0, 0, 0 };
    for (Role r : have)
        ++count[RoleIndex(r)];
    res.MissingTanks = std::min<uint32>(count[RoleIndex(Role::Tank)] ? 0 : 1, free);
    res.MissingHealers = std::min<uint32>(count[RoleIndex(Role::Healer)] ? 0 : 1, free - res.MissingTanks);
    res.MissingDps = free - res.MissingTanks - res.MissingHealers;
    return res;
}

FillResult PlanFill(std::span<Candidate const> candidates, Wanted const& want, Config const& cfg)
{
    FillResult res = OpenPlaces(want.Have, cfg);

    uint32 const lo = want.Level > cfg.MaxLevelSpread ? want.Level - cfg.MaxLevelSpread : 0;
    uint32 const hi = want.Level + cfg.MaxLevelSpread;
    std::vector<Candidate> pool;
    for (Candidate const& c : candidates)
    {
        if (!BotDungeon::RolesOfClass(c.ClassId) || c.Team != want.Team || c.Level < lo || c.Level > hi || c.Level < cfg.MinLevel)
            continue;
        if (!cfg.Teleport && (!c.SameMap || c.Distance > cfg.MaxDistance))
            continue;
        pool.push_back(c);
    }

    auto take = [&](Role role, uint32& open)
    {
        if (!open)
            return;
        std::vector<Candidate> able;
        for (Candidate const& c : pool)
            if (BotDungeon::RolesOfClass(c.ClassId) & BotDungeon::RoleBit(role))
                able.push_back(c);
        std::sort(able.begin(), able.end(), [&](Candidate const& a, Candidate const& b)
        {
            bool const pa = BotDungeon::PreferredRole(a.ClassId) == role, pb = BotDungeon::PreferredRole(b.ClassId) == role;
            if (pa != pb)
                return pa;
            if (a.Distance != b.Distance)
                return a.Distance < b.Distance;
            return a.Guid < b.Guid;
        });
        for (Candidate const& c : able)
        {
            if (!open)
                break;
            res.Picks.push_back({ c.Guid, role });
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&](Candidate const& p) { return p.Guid == c.Guid; }), pool.end());
            --open;
        }
    };
    take(Role::Tank, res.MissingTanks);
    take(Role::Healer, res.MissingHealers);
    take(Role::Dps, res.MissingDps);
    return res;
}

Outcome Evaluate(EntryFacts const& f, Config const& cfg)
{
    if (!f.PlayerOnline)
        return f.GoneSec >= cfg.ReleaseSec ? Outcome{ Verdict::Release, "offline" } : Outcome{};
    if (!f.PlayerMayLead)
        return { Verdict::Release, "not_leader" };
    if (!f.Searching)
        return {};
    if (!f.Missing)
        return { Verdict::Complete, "full" };
    if (f.AgeSec >= cfg.TimeoutSec)
        return { Verdict::Expire, "timeout" };
    return {};
}

std::string DescribeMissing(uint32 tanks, uint32 healers, uint32 dps)
{
    std::vector<std::string> parts;
    if (tanks)
        parts.push_back(std::to_string(tanks) + (tanks == 1 ? " tank" : " tanks"));
    if (healers)
        parts.push_back(std::to_string(healers) + (healers == 1 ? " healer" : " healers"));
    if (dps)
        parts.push_back(std::to_string(dps) + (dps == 1 ? " damage dealer" : " damage dealers"));
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            out += i + 1 == parts.size() ? " and " : ", ";
        out += parts[i];
    }
    return out;
}

char const* RoleText(Role r)
{
    return BotDungeon::RoleName(r);
}
}
