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

#include "BotGroupControl.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace BotGroupControl
{
namespace
{
// expired when `until` is not in the future (wrap-around safe)
bool Expired(uint32 untilMs, uint32 nowMs)
{
    return int32(nowMs - untilMs) >= 0;
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
}

// ---------------------------------------------------------------------------------------------------------------------
// claims
// ---------------------------------------------------------------------------------------------------------------------
bool ClaimBoard::Claim(uint64 key, uint64 claimer, uint32 nowMs, uint32 holdMs)
{
    std::lock_guard<std::mutex> guard(_lock);
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(), [&](Entry const& e) { return Expired(e.UntilMs, nowMs); }), _entries.end());
    for (Entry& e : _entries)
        if (e.Key == key)
        {
            if (e.Claimer != claimer)
                return false;
            e.UntilMs = nowMs + holdMs;
            return true;
        }
    if (_entries.size() >= MaxEntries)
        _entries.erase(_entries.begin());
    _entries.push_back({ key, claimer, nowMs + holdMs });
    return true;
}

bool ClaimBoard::HeldByOther(uint64 key, uint64 claimer, uint32 nowMs) const
{
    std::lock_guard<std::mutex> guard(_lock);
    for (Entry const& e : _entries)
        if (e.Key == key && e.Claimer != claimer && !Expired(e.UntilMs, nowMs))
            return true;
    return false;
}

void ClaimBoard::Release(uint64 key, uint64 claimer)
{
    std::lock_guard<std::mutex> guard(_lock);
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(), [&](Entry const& e) { return e.Key == key && e.Claimer == claimer; }), _entries.end());
}

size_t ClaimBoard::Size() const
{
    std::lock_guard<std::mutex> guard(_lock);
    return _entries.size();
}

ClaimBoard& InterruptClaims() { static ClaimBoard board; return board; }
ClaimBoard& DispelClaims()    { static ClaimBoard board; return board; }
ClaimBoard& ControlClaims()   { static ClaimBoard board; return board; }

// ---------------------------------------------------------------------------------------------------------------------
// interrupts
// ---------------------------------------------------------------------------------------------------------------------
bool WorthInterrupting(CastFacts const& cast, InterruptConfig const& cfg)
{
    if (!cast.Interruptible || cast.RemainingMs < cfg.MinRemainingMs)
        return false;
    if (cast.Heal || cast.Control)
        return true;
    return cast.TotalMs >= cfg.MinCastMs;
}

// ---------------------------------------------------------------------------------------------------------------------
// dispels
// ---------------------------------------------------------------------------------------------------------------------
uint32 DispelBitOf(uint8 dispelType)
{
    switch (dispelType)
    {
        case 1: return DISPEL_BIT_MAGIC;
        case 2: return DISPEL_BIT_CURSE;
        case 3: return DISPEL_BIT_DISEASE;
        case 4: return DISPEL_BIT_POISON;
        default: return 0;
    }
}

int32 AuraPriority(AuraFact const& aura, uint32 canMask)
{
    if (!aura.Harmful || !(DispelBitOf(aura.DispelType) & canMask) || aura.RemainingMs < 2000)
        return 0;
    if (aura.Hard)
        return PRIORITY_HARD;
    if (aura.Damage)
        return PRIORITY_DAMAGE;
    if (aura.Slow)
        return PRIORITY_SLOW;
    return PRIORITY_OTHER;
}

int32 PickDispelTarget(std::vector<DispelCandidate> const& members, int32 minPriority)
{
    int32 best = -1;
    for (size_t i = 0; i < members.size(); ++i)
    {
        DispelCandidate const& m = members[i];
        if (m.Priority <= 0 || m.Priority < minPriority)
            continue;
        if (best < 0)
        {
            best = int32(i);
            continue;
        }
        DispelCandidate const& b = members[size_t(best)];
        if (m.Priority != b.Priority ? m.Priority > b.Priority : m.HealthPct != b.HealthPct ? m.HealthPct < b.HealthPct : m.Key < b.Key)
            best = int32(i);
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------
// crowd control
// ---------------------------------------------------------------------------------------------------------------------
uint32 ParseMarkList(std::string_view list, std::vector<std::string>* unknown)
{
    static constexpr char const* names[8] = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" };
    uint32 mask = 0;
    size_t pos = 0;
    while (pos <= list.size())
    {
        size_t end = list.find(',', pos);
        if (end == std::string_view::npos)
            end = list.size();
        std::string_view tok = list.substr(pos, end - pos);
        pos = end + 1;
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.remove_prefix(1);
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.remove_suffix(1);
        if (tok.empty() || EqI(tok, "none"))
            continue;
        bool found = false;
        for (uint32 i = 0; i < 8 && !found; ++i)
            if (EqI(tok, names[i]))
            {
                mask |= 1u << i;
                found = true;
            }
        if (!found && tok.size() == 1 && tok[0] >= '1' && tok[0] <= '8')
        {
            mask |= 1u << (tok[0] - '1');
            found = true;
        }
        if (!found && unknown)
            unknown->emplace_back(tok);
    }
    return mask;
}

bool MarkInMask(int8 icon, uint32 mask)
{
    return icon >= 0 && icon < 8 && (mask & (1u << icon)) != 0;
}

int32 PickControlTarget(std::vector<ControlCandidate> const& mobs, ControlConfig const& cfg)
{
    uint32 controlled = 0;
    for (ControlCandidate const& m : mobs)
        if (m.Controlled && !m.IsFightTarget)
            ++controlled;

    auto usable = [](ControlCandidate const& m)
    {
        return !m.Controlled && !m.IsFightTarget && m.TypeOk && m.Reachable && !m.Tried && !m.ClaimedByOther;
    };
    auto stronger = [](ControlCandidate const& a, ControlCandidate const& b)
    {
        return a.Level != b.Level ? a.Level > b.Level : a.Key < b.Key;
    };

    int32 best = -1;
    for (size_t i = 0; i < mobs.size(); ++i)
        if (usable(mobs[i]) && MarkInMask(mobs[i].Mark, cfg.MarkMask) && (best < 0 || stronger(mobs[i], mobs[size_t(best)])))
            best = int32(i);
    if (best >= 0 || !cfg.Auto || controlled >= cfg.MaxAuto)
        return best;
    for (size_t i = 0; i < mobs.size(); ++i)
        if (usable(mobs[i]) && mobs[i].Mark != ICON_SKULL && (best < 0 || stronger(mobs[i], mobs[size_t(best)])))
            best = int32(i);
    return best;
}

bool ReservedForControl(bool controlled, int8 mark, uint32 markMask)
{
    return controlled || MarkInMask(mark, markMask);
}
}
