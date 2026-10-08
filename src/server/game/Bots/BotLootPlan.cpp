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

#include "BotLootPlan.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace BotLoot
{
// ---------------------------------------------------------------------------------------------------------------------
// spawn choice
// ---------------------------------------------------------------------------------------------------------------------
PickResult PickSpawn(std::span<SpawnCandidate const> c, PickConfig const& cfg, CostFn const& cost,
    VetoFn const& deadly, VetoFn const& blacklisted, VetoFn const& filter, bool wantReasons)
{
    PickResult res;
    if (wantReasons)
        res.Why.assign(c.size(), Reject::NotProbed);

    auto why = [&](size_t i, Reject r) { if (wantReasons) res.Why[i] = r; };

    std::vector<uint32> survivors;
    survivors.reserve(c.size());
    for (size_t i = 0; i < c.size(); ++i)
    {
        SpawnCandidate const& s = c[i];
        if (s.Straight > cfg.MaxYards)
        {
            ++res.RejectedFar;
            why(i, Reject::TooFar);
        }
        else if (deadly && deadly(s))
        {
            ++res.RejectedDeadly;
            why(i, Reject::Deadly);
        }
        else if (blacklisted && blacklisted(s))
        {
            ++res.RejectedBlacklisted;
            why(i, Reject::Blacklisted);
        }
        else if (filter && filter(s))
        {
            ++res.RejectedFiltered;
            why(i, Reject::Filtered);
        }
        else
            survivors.push_back(uint32(i));
    }
    if (survivors.empty())
        return res;

    // nearest first: the straight-line distance orders the probes, the path cost decides
    std::stable_sort(survivors.begin(), survivors.end(), [&](uint32 a, uint32 b) { return c[a].Straight < c[b].Straight; });

    float bestCost = 0.0f;
    for (uint32 idx : survivors)
    {
        SpawnCandidate const& s = c[idx];
        // a spawn whose straight distance already exceeds the best cost found cannot win (path >= straight line)
        if (res.Index >= 0 && s.Straight >= bestCost)
            break;
        if (res.Probes >= cfg.MaxProbes)
            break;
        ++res.Probes;
        PathCost const pc = cost ? cost(s) : PathCost{ true, false, s.Straight };
        if (!pc.Ok)
        {
            ++res.RejectedPath;
            why(idx, Reject::NoPath);
            continue;
        }
        if (pc.Partial && !cfg.AllowPartial)
        {
            ++res.RejectedPath;
            why(idx, Reject::PartialPath);
            continue;
        }
        if (pc.Length > cfg.MaxYards)
        {
            ++res.RejectedFar;
            why(idx, Reject::TooLong);
            continue;
        }
        float total = pc.Length * (pc.Partial ? cfg.PartialPenalty : 1.0f) * (s.Pooled ? cfg.PooledPenalty : 1.0f);
        why(idx, Reject::None);
        if (res.Index < 0 || total < bestCost)
        {
            res.Index = int32(idx);
            bestCost = total;
            res.Cost = total;
        }
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------------------
// deadly areas
// ---------------------------------------------------------------------------------------------------------------------
void DeadlyAreas::Note(uint32 mapId, float x, float y, uint32 nowMs)
{
    std::lock_guard<std::mutex> lk(_mx);
    Rec r{ mapId, x, y, nowMs ? nowMs : 1 };
    if (_recs.size() < _cap)
        _recs.push_back(r);
    else
    {
        _recs[_next % _recs.size()] = r;
        ++_next;
    }
}

bool DeadlyAreas::IsDeadly(uint32 mapId, float x, float y, float radius, uint32 nowMs, uint32 windowMs) const
{
    std::lock_guard<std::mutex> lk(_mx);
    float const r2 = radius * radius;
    for (Rec const& r : _recs)
    {
        if (r.Map != mapId || nowMs - r.Ms > windowMs)
            continue;
        float const dx = r.X - x, dy = r.Y - y;
        if (dx * dx + dy * dy <= r2)
            return true;
    }
    return false;
}

size_t DeadlyAreas::Size() const
{
    std::lock_guard<std::mutex> lk(_mx);
    return _recs.size();
}

void DeadlyAreas::Clear()
{
    std::lock_guard<std::mutex> lk(_mx);
    _recs.clear();
    _next = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// blacklist
// ---------------------------------------------------------------------------------------------------------------------
uint32 SpawnBlacklist::Add(uint64 spawnId, uint32 nowMs, uint32 baseMs, uint32 maxMs)
{
    Entry* hit = nullptr;
    for (Entry& e : _e)
        if (e.Id == spawnId)
            hit = &e;
    if (!hit)
    {
        if (_e.size() >= CAP)
        {
            auto old = std::min_element(_e.begin(), _e.end(), [nowMs](Entry const& a, Entry const& b) { return nowMs - a.LastMs > nowMs - b.LastMs; });
            _e.erase(old);
        }
        _e.push_back({ spawnId, 0, 0, nowMs });
        hit = &_e.back();
    }
    ++hit->Strikes;
    hit->LastMs = nowMs;
    uint64 dur = uint64(baseMs) << std::min<uint32>(hit->Strikes - 1, 8);
    dur = std::min<uint64>(dur, maxMs);
    hit->UntilMs = nowMs + uint32(dur);
    return uint32(dur);
}

bool SpawnBlacklist::Blocked(uint64 spawnId, uint32 nowMs) const
{
    for (Entry const& e : _e)
        if (e.Id == spawnId)
            return int32(e.UntilMs - nowMs) > 0;
    return false;
}

uint32 SpawnBlacklist::Strikes(uint64 spawnId) const
{
    for (Entry const& e : _e)
        if (e.Id == spawnId)
            return e.Strikes;
    return 0;
}

void SpawnBlacklist::Forget(uint64 spawnId)
{
    std::erase_if(_e, [spawnId](Entry const& e) { return e.Id == spawnId; });
}

void SpawnBlacklist::Prune(uint32 nowMs)
{
    // an expired entry keeps its strikes for a while (so repeats grow), then goes
    std::erase_if(_e, [nowMs](Entry const& e) { return int32(e.UntilMs - nowMs) <= 0 && nowMs - e.LastMs > 3 * 3600 * 1000u; });
}

// ---------------------------------------------------------------------------------------------------------------------
// chest danger and spawn quarantine
// ---------------------------------------------------------------------------------------------------------------------
bool ChestDangerous(std::span<MobFacts const> mobs, DangerConfig const& cfg)
{
    for (MobFacts const& m : mobs)
    {
        if (m.DistToSpawn > cfg.SpawnRadius && m.DistToBot > cfg.BotRadius)
            continue;
        if (m.LevelDiff + (m.Elite ? cfg.EliteBonus : 0) >= cfg.MaxGap)
            return true;
    }
    return false;
}

bool SpawnQuarantine::Note(uint64 spawnId, uint32 nowMs, uint32 strikes, uint32 windowMs, uint32 baseMs)
{
    std::lock_guard<std::mutex> lk(_mx);
    Entry* hit = nullptr;
    for (Entry& e : _e)
        if (e.Id == spawnId)
            hit = &e;
    if (!hit)
    {
        if (_e.size() >= 512)
            _e.erase(_e.begin());   // oldest first; the table is a safety net, not a database
        _e.push_back({ spawnId, 0, nowMs, 0, 0 });
        hit = &_e.back();
    }
    if (int32(hit->UntilMs - nowMs) > 0)
        return false;   // already quarantined
    if (!hit->Count || nowMs - hit->FirstMs > windowMs)
    {
        hit->Count = 0;
        hit->FirstMs = nowMs;
    }
    if (++hit->Count < std::max<uint32>(1, strikes))
        return false;
    hit->Count = 0;
    ++hit->Rounds;
    uint64 const dur = std::min<uint64>(uint64(baseMs) << std::min<uint32>(hit->Rounds - 1, 3), uint64(baseMs) * 8);
    hit->UntilMs = nowMs + uint32(dur);
    return true;
}

bool SpawnQuarantine::Quarantined(uint64 spawnId, uint32 nowMs) const
{
    std::lock_guard<std::mutex> lk(_mx);
    for (Entry const& e : _e)
        if (e.Id == spawnId)
            return int32(e.UntilMs - nowMs) > 0;
    return false;
}

void SpawnQuarantine::Clear(uint64 spawnId)
{
    std::lock_guard<std::mutex> lk(_mx);
    std::erase_if(_e, [spawnId](Entry const& e) { return e.Id == spawnId; });
}

size_t SpawnQuarantine::Size() const
{
    std::lock_guard<std::mutex> lk(_mx);
    return _e.size();
}

void SpawnQuarantine::Reset()
{
    std::lock_guard<std::mutex> lk(_mx);
    _e.clear();
}

// ---------------------------------------------------------------------------------------------------------------------
// bags
// ---------------------------------------------------------------------------------------------------------------------
BagPlan PlanBags(BagFacts const& f, BagConfig const& cfg)
{
    if (f.FreeSlots < cfg.NeedFree)
        return f.VendorKnown ? BagPlan::VendorFirst : BagPlan::Blocked;
    if (f.FreeSlots - cfg.NeedFree < cfg.TripBelow)
        return BagPlan::Tight;
    return BagPlan::Ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// supply of ItemDrop quests
// ---------------------------------------------------------------------------------------------------------------------
std::vector<SupplyItem> PlanQuestSupply(SupplyQuest const& q)
{
    std::vector<SupplyItem> out;
    for (SupplyObjective const& o : q.Objectives)
    {
        if (o.Type != OBJECTIVE_TYPE_ITEM || o.Optional || o.ObjectId <= 0 || o.Have >= o.Amount)
            continue;
        uint32 const item = uint32(o.ObjectId);
        if (q.SrcItemId == item || o.HasWorldSource)
            continue;   // the core hands over the start item; a world source is looted normally
        uint32 drop = 0;
        for (auto const& [dropItem, qty] : q.ItemDrops)
            if (dropItem == item)
                drop = std::max(drop, std::max<uint32>(1, qty));
        if (!drop)
            continue;
        uint32 const need = uint32(o.Amount - o.Have);
        // the classifier only accepts the quest as supplied when the drop amount covers the objective, so this never short-changes
        uint32 const give = std::min(need, drop);
        auto existing = std::find_if(out.begin(), out.end(), [item](SupplyItem const& s) { return s.Item == item; });
        if (existing == out.end())
            out.push_back({ item, give });
        else
            existing->Count = std::max(existing->Count, give);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// spawn filter
// ---------------------------------------------------------------------------------------------------------------------
SpawnUse ClassifySpawn(SpawnFacts const& f)
{
    if (f.ManualSpawnGroup || f.SystemSpawnGroup)
        return SpawnUse::SkipSpawnGroup;
    if (!f.Difficulties.empty() && std::find(f.Difficulties.begin(), f.Difficulties.end(), 0) == f.Difficulties.end())
        return SpawnUse::SkipDifficulty;
    if (f.PhaseId || f.PhaseGroup || f.TerrainSwapMap >= 0)
        return SpawnUse::SkipPhase;
    return f.PoolId ? SpawnUse::Pooled : SpawnUse::Plain;
}
}
