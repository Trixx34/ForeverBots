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

#include "BotMovePlan.h"
#include <algorithm>
#include <cmath>

namespace BotMove
{
namespace
{
constexpr float PI = 3.14159265358979f;
constexpr float RAD2DEG = 180.0f / PI;

}

float Dist2D(Vec3 const& a, Vec3 const& b) { return std::hypot(a.x - b.x, a.y - b.y); }
float Dist3D(Vec3 const& a, Vec3 const& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

float PathLength(std::vector<Vec3> const& path)
{
    float len = 0.0f;
    for (size_t i = 1; i < path.size(); ++i)
        len += Dist3D(path[i - 1], path[i]);
    return len;
}

float TurnAngleDeg(Vec3 const& a, Vec3 const& b, Vec3 const& c)
{
    float const ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
    float const lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
    if (lu < 1e-4f || lv < 1e-4f)
        return 0.0f;
    float const cosv = std::clamp((ux * vx + uy * vy) / (lu * lv), -1.0f, 1.0f);
    return std::acos(cosv) * RAD2DEG;
}

uint64 Mix(uint64 a, uint64 b, uint64 c)
{
    uint64 z = a + 0x9E3779B97F4A7C15ull;
    auto step = [](uint64 v)
    {
        v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ull;
        v = (v ^ (v >> 27)) * 0x94D049BB133111EBull;
        return v ^ (v >> 31);
    };
    z = step(z);
    z = step(z ^ (b + 0x9E3779B97F4A7C15ull));
    z = step(z ^ (c * 0xD6E8FEB86659FD93ull + 0x632BE59BD9B4E019ull));
    return z;
}

// ---------------------------------------------------------------------------------------------------------------------
// smooth paths
// ---------------------------------------------------------------------------------------------------------------------
std::vector<Vec3> ShortcutPath(std::vector<Vec3> const& path, LosFn const& los, ShortcutConfig const& cfg)
{
    if (path.size() <= 2 || !los)
        return path;
    std::vector<Vec3> out;
    out.reserve(path.size());
    out.push_back(path.front());
    size_t i = 0;
    size_t const last = path.size() - 1;
    while (i < last)
    {
        size_t best = i + 1;   // the next point is always reachable: the original path walks it
        size_t const far = std::min<size_t>(last, i + cfg.Lookahead);
        for (size_t j = far; j > i + 1; --j)
        {
            if (Dist3D(path[i], path[j]) > cfg.MaxSegment)
                continue;
            if (los(path[i], path[j]))
            {
                best = j;
                break;
            }
        }
        out.push_back(path[best]);
        i = best;
    }
    return out;
}

std::vector<Vec3> RoundCorners(std::vector<Vec3> const& path, LosFn const& los, CornerConfig const& cfg)
{
    if (path.size() < 3)
        return path;
    std::vector<Vec3> out;
    out.reserve(path.size() * 2);
    out.push_back(path.front());
    for (size_t i = 1; i + 1 < path.size(); ++i)
    {
        Vec3 const& p = path[i];
        Vec3 const prev = out.back();   // the previous kept point (a chamfer end of the last corner counts)
        Vec3 const& next = path[i + 1];
        if (TurnAngleDeg(prev, p, next) < cfg.MinTurnDeg)
        {
            out.push_back(p);
            continue;
        }
        float const lIn = Dist2D(prev, p), lOut = Dist2D(p, next);
        float const cut = std::min({ cfg.MaxCut, lIn * cfg.CutFraction, lOut * cfg.CutFraction });
        if (cut < 0.75f)
        {
            out.push_back(p);
            continue;
        }
        auto lerp = [](Vec3 const& a, Vec3 const& b, float t) { return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; };
        Vec3 const a = lerp(p, prev, cut / lIn);
        Vec3 const b = lerp(p, next, cut / lOut);
        if (!los || los(a, b))
        {
            out.push_back(a);
            out.push_back(b);
        }
        else
            out.push_back(p);
    }
    out.push_back(path.back());
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// approach points, pacing
// ---------------------------------------------------------------------------------------------------------------------
Vec3 ApproachOffset(ApproachParams const& p)
{
    uint64 const h1 = Mix(p.BotKey, p.TargetKey, p.Attempt * 2u);
    uint64 const h2 = Mix(p.BotKey, p.TargetKey, p.Attempt * 2u + 1u);
    float const angle = p.BearingToBot + (Unit(h1) - 0.5f) * p.ArcRad;
    float const radius = p.Radius * Range(h2, 0.35f, 1.0f);
    return { std::cos(angle) * radius, std::sin(angle) * radius, 0.0f };
}

uint32 PauseMs(Pause kind, uint64 botKey, uint32 salt)
{
    uint64 const h = Mix(botKey, uint64(kind) + 1, salt);
    switch (kind)
    {
        case Pause::StartDelay: return uint32(Range(h, 0.0f, 900.0f));
        case Pause::Arrive:     return uint32(Range(h, 250.0f, 900.0f));
        case Pause::QuestGiver: return uint32(Range(h, 600.0f, 1800.0f));
        case Pause::Loot:       return uint32(Range(h, 400.0f, 1200.0f));
        case Pause::PostCombat: return uint32(Range(h, 800.0f, 2500.0f));
        case Pause::Look:       return uint32(Range(h, 700.0f, 1600.0f));
    }
    return 0;
}

float SpeedFactor(uint64 botKey, uint32 nowMs, float amplitude, uint32 epochMs)
{
    if (!epochMs)
        return 1.0f;
    uint32 const epoch = nowMs / epochMs;
    float const t = float(nowMs % epochMs) / float(epochMs);
    float const a = Range(Mix(botKey, epoch, 77), -1.0f, 1.0f);
    float const b = Range(Mix(botKey, epoch + 1, 77), -1.0f, 1.0f);
    float const smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f + amplitude * (a + (b - a) * smooth);
}

// ---------------------------------------------------------------------------------------------------------------------
// failed paths
// ---------------------------------------------------------------------------------------------------------------------
Reroute AdviceFor(uint32 fails)
{
    if (fails <= 1)
        return Reroute::Retry;
    if (fails == 2)
        return Reroute::SideRoute;
    if (fails <= 4)
        return Reroute::Wait;
    return Reroute::GiveUp;
}

uint32 BackoffMs(uint32 fails, uint64 botKey, uint64 legKey, uint32 baseMs, uint32 capMs)
{
    uint32 const shift = std::min<uint32>(fails ? fails - 1 : 0, 10);
    uint64 const raw = std::min<uint64>(uint64(baseMs) << shift, capMs);
    float const jitter = Range(Mix(botKey, legKey, fails), 0.75f, 1.25f);
    return std::max<uint32>(250, uint32(float(raw) * jitter));
}

bool FailTable::Blocked(uint64 key, uint32 nowMs) const
{
    for (Entry const& e : _e)
        if (e.Key == key)
            return int32(e.UntilMs - nowMs) > 0;
    return false;
}

uint32 FailTable::Fails(uint64 key) const
{
    for (Entry const& e : _e)
        if (e.Key == key)
            return e.Fails;
    return 0;
}

uint32 FailTable::UntilMs(uint64 key) const
{
    for (Entry const& e : _e)
        if (e.Key == key)
            return e.UntilMs;
    return 0;
}

uint32 FailTable::Fail(uint64 key, uint32 nowMs, uint64 botKey)
{
    Entry* hit = nullptr;
    for (Entry& e : _e)
        if (e.Key == key)
            hit = &e;
    if (!hit)
    {
        if (_e.size() >= _cap)
        {
            // drop the entry that was touched longest ago
            auto old = std::min_element(_e.begin(), _e.end(), [nowMs](Entry const& a, Entry const& b) { return nowMs - a.LastMs > nowMs - b.LastMs; });
            _e.erase(old);
        }
        _e.push_back({ key, 0, 0, nowMs });
        hit = &_e.back();
    }
    ++hit->Fails;
    hit->LastMs = nowMs;
    uint32 const wait = BackoffMs(hit->Fails, botKey, key);
    hit->UntilMs = nowMs + wait;
    return wait;
}

void FailTable::Success(uint64 key)
{
    std::erase_if(_e, [key](Entry const& e) { return e.Key == key; });
}

uint64 LegKey(Vec3 const& from, Vec3 const& to, float cell)
{
    auto q = [cell](float v) { return int64(std::floor(v / cell)); };
    return Mix(uint64(q(from.x)) * 73856093ull ^ uint64(q(from.y)) * 19349663ull, uint64(q(to.x)) * 83492791ull ^ uint64(q(to.y)) * 2654435761ull);
}

uint32 LegTracker::Issue(uint64 legKey, uint32 nowMs, uint32 windowMs)
{
    for (uint32 i = 0; i < _n; ++i)
    {
        Rec& r = _r[i];
        if (r.Key != legKey)
            continue;
        if (nowMs - r.FirstMs > windowMs)
        {
            r.FirstMs = nowMs;
            r.Count = 1;
            return 0;
        }
        uint32 const before = r.Count++;
        ++_repeats;
        return before;
    }
    if (_n < std::size(_r))
        _r[_n++] = { legKey, nowMs, 1 };
    else
    {
        // replace the oldest record
        uint32 oldest = 0;
        for (uint32 i = 1; i < _n; ++i)
            if (nowMs - _r[i].FirstMs > nowMs - _r[oldest].FirstMs)
                oldest = i;
        _r[oldest] = { legKey, nowMs, 1 };
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// stuck and idle
// ---------------------------------------------------------------------------------------------------------------------
StuckPlan PlanStuck(uint32 episode, uint64 botKey, uint32 nowMs)
{
    StuckPlan p;
    uint64 const h = Mix(botKey, episode, nowMs / 1000);
    float const side = Unit(Mix(h, 1)) < 0.5f ? -1.0f : 1.0f;
    if (episode <= 1)
    {
        p.Act = StuckAct::StepBack;
        p.LookPauseMs = uint32(Range(Mix(h, 2), 700.0f, 1500.0f));
        p.TurnRad = side * Range(Mix(h, 3), 0.6f, 2.2f);
        p.StepYards = Range(Mix(h, 4), 2.5f, 5.0f);
    }
    else if (episode == 2)
    {
        p.Act = StuckAct::SideRoute;
        p.LookPauseMs = uint32(Range(Mix(h, 2), 400.0f, 900.0f));
        p.TurnRad = side * Range(Mix(h, 3), 0.4f, 1.2f);
        p.SideYards = side * Range(Mix(h, 5), 6.0f, 12.0f);
    }
    else
        p.Act = StuckAct::GiveUp;
    return p;
}

IdlePlan PlanIdle(IdleFacts const& f, IdleConfig const& cfg, uint64 botKey, uint32 nowMs)
{
    IdlePlan plan;
    uint64 const h = Mix(botKey, nowMs / 1000, f.StillMs / 1000);

    if (f.Sitting)
    {
        uint32 const hold = cfg.SitMinMs + uint32(Unit(Mix(botKey, 91)) * float(cfg.SitMaxMs - cfg.SitMinMs));
        if (f.Threat || f.SittingMs >= hold)
            plan.Act = IdleAct::StandUp;
        return plan;
    }
    if (f.Threat)
        return plan;   // do not start anything with a hostile around: the aggro behavior owns that

    // standing still too long: walk somewhere
    bool const forced = f.StillMs >= cfg.MaxStillMs;
    if (!forced && (f.StillMs < cfg.MinGapMs || f.SinceActionMs < cfg.MinGapMs))
        return plan;

    float const roll = Unit(h);
    if (forced || roll < 0.30f)
    {
        plan.Act = IdleAct::Wander;
        plan.WanderYards = Range(Mix(h, 1), cfg.WanderMin, cfg.WanderMax);
        plan.WanderBearing = Range(Mix(h, 2), 0.0f, 2.0f * PI);
    }
    else if (roll < 0.30f + (f.HurtOrDrained ? 0.40f : 0.12f))
    {
        plan.Act = IdleAct::Sit;
    }
    else
    {
        plan.Act = IdleAct::Look;
        plan.TurnRad = (Unit(Mix(h, 3)) < 0.5f ? -1.0f : 1.0f) * Range(Mix(h, 4), 0.5f, 2.4f);
    }
    return plan;
}

// ---------------------------------------------------------------------------------------------------------------------
// metrics
// ---------------------------------------------------------------------------------------------------------------------
void Metrics::Sample(uint32 nowMs, float x, float y, bool moving)
{
    if (!_startMs)
        _startMs = nowMs ? nowMs : 1;
    if (_lastMs)
    {
        uint32 const dt = nowMs - _lastMs;
        if (moving)
        {
            _moving += dt;
            _idleRun = 0;
        }
        else
        {
            _idle += dt;
            _idleRun += dt;
            _longestIdle = std::max(_longestIdle, _idleRun);
        }
    }
    _lastMs = nowMs ? nowMs : 1;

    if (_have)
    {
        float const dx = x - _px, dy = y - _py;
        float const d = std::hypot(dx, dy);
        if (moving && d >= 0.5f)
        {
            _dist += d;
            float const heading = std::atan2(dy, dx);
            if (_haveHeading)
            {
                float turn = std::fabs(heading - _heading);
                if (turn > PI)
                    turn = 2.0f * PI - turn;
                _turnSum += turn * RAD2DEG;
                if (turn * RAD2DEG >= 60.0f)
                    ++_sharp;
            }
            _heading = heading;
            _haveHeading = true;
            _px = x;
            _py = y;
        }
        else if (!moving)
        {
            _haveHeading = false;   // a stop ends the run: the next move starts a new heading
            _px = x;
            _py = y;
        }
    }
    else
    {
        _have = true;
        _px = x;
        _py = y;
    }
}

MetricsSnapshot Metrics::Take(uint32 nowMs)
{
    MetricsSnapshot s;
    s.WindowMs = _startMs ? nowMs - _startMs : 0;
    s.MovingMs = _moving;
    s.IdleMs = _idle;
    uint32 const seen = _moving + _idle;
    s.IdleRatio = seen ? float(_idle) / float(seen) : 0.0f;
    s.DistanceYd = _dist;
    s.TurnRateDegPerSec = _moving ? _turnSum / (float(_moving) / 1000.0f) : 0.0f;
    s.SharpTurns = _sharp;
    s.RepeatedLegs = _repeated;
    s.StuckEvents = _stuck;
    s.PathFails = _pathFails;
    s.LongestIdleMs = _longestIdle;

    // restart the window but keep the position so the next sample does not count a jump
    float const px = _px, py = _py;
    bool const have = _have;
    *this = Metrics();
    _have = have;
    _px = px;
    _py = py;
    _startMs = nowMs ? nowMs : 1;
    _lastMs = _startMs;
    return s;
}
}
