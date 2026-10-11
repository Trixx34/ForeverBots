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

#ifndef TRINITY_BOT_PROGRESS_RULES_H
#define TRINITY_BOT_PROGRESS_RULES_H

// Pure decision helpers for the additional rules N-01 to N-09 of docs/playerbots/sim-run-800-analysis-20261011.md (progress stalls,
// zone fence, pull readiness, no-damage fights, bag pressure, crowding, zone leave, class pull margin, log diet). Header-only and free of
// server types so they are unit tested; the glue lives in BotWatchdog.cpp, BotQuest.cpp, BotCombat.cpp, BotBehavior.cpp, BotAI.cpp and
// BotMgr.cpp. Times are milliseconds on a wrapping 32 bit clock unless a name says Sec.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BotRule
{
    inline bool Reached(uint32_t now, uint32_t t) { return int32_t(now - t) >= 0; }

    // ---- N-01: XP based stall detector ---------------------------------------------------------------------------------------

    struct StallConfig
    {
        uint32_t ClearSec = 1200;     // stage 1: clear the task and the blacklists
        uint32_t RelogSec = 2400;     // stage 2: relog the bot
        uint32_t HubSec = 5400;       // stage 3: send the bot to the nearest quest hub for its level
        uint32_t MaxPerHour = 6;      // recoveries per bot and hour; beyond it only the report is written
    };

    enum class StallStep : uint8_t { None, Clear, Relog, Hub };

    inline char const* StepName(StallStep s)
    {
        switch (s)
        {
            case StallStep::Clear: return "clear";
            case StallStep::Relog: return "relog";
            case StallStep::Hub: return "hub";
            default: return "none";
        }
    }

    struct StallVerdict
    {
        StallStep Act = StallStep::None;
        uint32_t StalledSec = 0;
        bool Exhausted = false;       // a step was due but the hourly cap is used up
    };

    // A second watchdog clock that resets only when the progress key changes (experience, level, a rewarded quest, a quest objective
    // counter), so a bot that walks trips in circles still stalls. `busy` (dead, flying, in an instance) holds the clock at zero.
    class XpStall
    {
    public:
        StallVerdict Update(uint32_t nowMs, uint64_t progressKey, bool busy, StallConfig const& cfg)
        {
            StallVerdict v;
            if (!_started || busy || progressKey != _key)
            {
                _started = true;
                _key = progressKey;
                _markMs = nowMs;
                _stage = 0;
                return v;
            }
            uint32_t const stalledSec = (nowMs - _markMs) / 1000;
            v.StalledSec = stalledSec;
            uint32_t due = 0;
            if (cfg.HubSec && stalledSec >= cfg.HubSec)
                due = 3;
            else if (cfg.RelogSec && stalledSec >= cfg.RelogSec)
                due = 2;
            else if (cfg.ClearSec && stalledSec >= cfg.ClearSec)
                due = 1;
            if (due <= _stage)
                return v;
            _stage = due;
            while (!_recoveries.empty() && uint32_t(nowMs - _recoveries.front()) > 3600u * 1000u)
                _recoveries.pop_front();
            if (_recoveries.size() >= cfg.MaxPerHour)
            {
                v.Exhausted = true;
                return v;
            }
            _recoveries.push_back(nowMs);
            v.Act = due == 1 ? StallStep::Clear : due == 2 ? StallStep::Relog : StallStep::Hub;
            if (due == 3)
            {
                _markMs = nowMs;      // the last step was handed out: the clock starts over
                _stage = 0;
            }
            return v;
        }
        uint32_t Stage() const { return _stage; }
        uint32_t RecoveriesInWindow() const { return uint32_t(_recoveries.size()); }

    private:
        bool _started = false;
        uint64_t _key = 0;
        uint32_t _markMs = 0;
        uint32_t _stage = 0;
        std::deque<uint32_t> _recoveries;
    };

    // One tracker per bot guid, outside the BotAI so the clock survives the relog the tracker itself asks for. Thread safe (map
    // threads of different bots call it).
    class StallBook
    {
    public:
        StallVerdict Update(uint64_t guid, uint32_t nowMs, uint64_t key, bool busy, StallConfig const& cfg)
        {
            std::lock_guard<std::mutex> lock(_m);
            return _t[guid].Update(nowMs, key, busy, cfg);
        }
        void Forget(uint64_t guid)
        {
            std::lock_guard<std::mutex> lock(_m);
            _t.erase(guid);
        }
        size_t Size() const
        {
            std::lock_guard<std::mutex> lock(_m);
            return _t.size();
        }

    private:
        mutable std::mutex _m;
        std::unordered_map<uint64_t, XpStall> _t;
    };

    // Folds quest log counters into one number (any change is progress). Order dependent on purpose.
    inline uint64_t FoldKey(uint64_t key, uint64_t value) { return (key ^ (value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2))) * 0x100000001b3ull; }

    // Fixed size ring of the latest decisions of one bot (N-01 writes them into the STALL_XP row).
    class DecisionRing
    {
    public:
        void Add(std::string_view code, std::string_view summary)
        {
            if (_items.size() == kSize)
                _items.pop_front();
            _items.push_back({ std::string(code), std::string(summary.substr(0, 80)) });
        }
        size_t Size() const { return _items.size(); }
        // [{"r":"CODE","s":"summary"},...] oldest first; quotes and backslashes are dropped from the text
        std::string Json() const
        {
            std::string out = "[";
            for (size_t i = 0; i < _items.size(); ++i)
            {
                if (i)
                    out += ',';
                out += "{\"r\":\"" + Clean(_items[i].first) + "\",\"s\":\"" + Clean(_items[i].second) + "\"}";
            }
            return out + "]";
        }
        static constexpr size_t kSize = 10;

    private:
        static std::string Clean(std::string const& s)
        {
            std::string o;
            for (char c : s)
                if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 0x20)
                    o += c;
            return o;
        }
        std::deque<std::pair<std::string, std::string>> _items;
    };

    // ---- N-02: zone band fence -----------------------------------------------------------------------------------------------

    // A zone whose lowest level is more than `margin` above the bot is fenced off. Unknown zones (min level 0) never are.
    inline bool ZoneFenced(int botLevel, int zoneMinLevel, int margin)
    {
        return zoneMinLevel > 0 && zoneMinLevel > botLevel + margin;
    }

    struct Pt2 { float X = 0.0f, Y = 0.0f; };

    // Points along the straight line start -> end, every `stepYd`, the end point always last, at most `maxPoints`. The start is not
    // included (the bot already stands in it).
    inline std::vector<Pt2> LegSamples(float x0, float y0, float x1, float y1, float stepYd, uint32_t maxPoints)
    {
        std::vector<Pt2> out;
        if (!maxPoints)
            return out;
        float const dx = x1 - x0, dy = y1 - y0;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (stepYd < 1.0f)
            stepYd = 1.0f;
        uint32_t n = uint32_t(std::ceil(len / stepYd));
        if (n < 1)
            n = 1;
        if (n > maxPoints)
            n = maxPoints;
        for (uint32_t i = 1; i <= n; ++i)
        {
            float const f = float(i) / float(n);
            out.push_back({ x0 + dx * f, y0 + dy * f });
        }
        return out;
    }

    // ---- N-03: pre-pull readiness gate ---------------------------------------------------------------------------------------

    struct PullConfig
    {
        float HealthPct = 80.0f;      // below this the bot rests before the next pull
        float PowerPct = 50.0f;       // casters: below this the bot rests before the next pull
        uint32_t MaxWaitSec = 120;    // a health or power block that lasts this long is let go (rest could not fix it)
    };

    struct PullState
    {
        bool MainNeedsWeapon = false; // profession tool or nothing in the main hand
        bool RangedNeedsAmmo = false;
        bool UsesMana = false;
        float HealthPct = 100.0f;
        float PowerPct = 100.0f;
    };

    enum class PullBlock : uint8_t { None, Weapon, Ammo, Health, Power };

    inline char const* PullBlockName(PullBlock b)
    {
        switch (b)
        {
            case PullBlock::Weapon: return "weapon";
            case PullBlock::Ammo: return "ammo";
            case PullBlock::Health: return "health";
            case PullBlock::Power: return "power";
            default: return "none";
        }
    }

    inline PullBlock PullGate(PullState const& s, PullConfig const& cfg)
    {
        if (s.MainNeedsWeapon)
            return PullBlock::Weapon;
        if (s.RangedNeedsAmmo)
            return PullBlock::Ammo;
        if (s.HealthPct < cfg.HealthPct)
            return PullBlock::Health;
        if (s.UsesMana && s.PowerPct < cfg.PowerPct)
            return PullBlock::Power;
        return PullBlock::None;
    }

    // Lets a rest block go after MaxWaitSec so a bot that cannot recover does not stand still for ever. Weapon and ammo blocks stay.
    struct PullWait
    {
        uint32_t SinceMs = 0;
        bool Waiting = false;

        // Returns true when the pull must wait.
        bool Blocked(uint32_t nowMs, PullBlock b, PullConfig const& cfg)
        {
            if (b == PullBlock::None)
            {
                Waiting = false;
                return false;
            }
            if (b == PullBlock::Weapon || b == PullBlock::Ammo)
                return true;
            if (!Waiting)
            {
                Waiting = true;
                SinceMs = nowMs;
            }
            return !Reached(nowMs, SinceMs + cfg.MaxWaitSec * 1000);
        }
    };

    // ---- N-04: no-damage fight breaker ---------------------------------------------------------------------------------------

    struct NoDamageConfig
    {
        uint32_t WindowMs = 10000;    // fight time without progress before the breaker acts
        float MinPct = 5.0f;          // damage in percent of the mob's max health that counts as progress
    };

    // True when the bot has fought `fightMs` and took less than MinPct of the mob's health.
    inline bool NoDamage(uint32_t fightMs, uint64_t hpStart, uint64_t hpNow, uint64_t hpMax, NoDamageConfig const& cfg)
    {
        if (fightMs < cfg.WindowMs || !hpMax)
            return false;
        uint64_t const dealt = hpStart > hpNow ? hpStart - hpNow : 0;
        return float(dealt) * 100.0f < cfg.MinPct * float(hpMax);
    }

    enum class NoDmgCause : uint8_t { Unknown, Weapon, Ammo, Power, Los, Range };

    inline char const* NoDmgCauseName(NoDmgCause c)
    {
        switch (c)
        {
            case NoDmgCause::Weapon: return "weapon";
            case NoDmgCause::Ammo: return "ammo";
            case NoDmgCause::Power: return "power";
            case NoDmgCause::Los: return "los";
            case NoDmgCause::Range: return "range";
            default: return "unknown";
        }
    }

    inline NoDmgCause ClassifyNoDamage(bool weaponMissing, bool ammoMissing, bool noPower, bool noLos, bool outOfRange)
    {
        if (weaponMissing)
            return NoDmgCause::Weapon;
        if (ammoMissing)
            return NoDmgCause::Ammo;
        if (noPower)
            return NoDmgCause::Power;
        if (noLos)
            return NoDmgCause::Los;
        if (outOfRange)
            return NoDmgCause::Range;
        return NoDmgCause::Unknown;
    }

    // ---- N-05: bag pressure --------------------------------------------------------------------------------------------------

    struct BagConfig
    {
        uint32_t TripFreeSlots = 4;   // plan the vendor trip at this many free slots or fewer
        float VendorYd = 400.0f;      // ... when a vendor is closer than this
        uint32_t GreyFreeSlots = 4;   // below this many free slots grey items are not looted
    };

    // Free slots at which the vendor trip is planned early. The classic limit (one free slot) is always a trip.
    inline bool BagPressure(uint32_t freeSlots, BagConfig const& cfg) { return freeSlots <= cfg.TripFreeSlots; }

    // Grey (poor quality) loot is left on the corpse when the bags are nearly full; quest items and better quality are always taken.
    inline bool SkipLoot(uint32_t freeSlots, bool poorQuality, bool questItem, BagConfig const& cfg)
    {
        return poorQuality && !questItem && freeSlots < cfg.GreyFreeSlots;
    }

    // ---- N-06: crowd cap per objective ---------------------------------------------------------------------------------------

    // Which bots work on which objective where. One claim per bot; a claim lives `ttlMs` unless it is renewed. Thread safe.
    class CrowdBook
    {
    public:
        void Claim(uint64_t key, uint64_t guid, float x, float y, uint32_t nowMs, uint32_t ttlMs)
        {
            std::lock_guard<std::mutex> lock(_m);
            Erase(guid);
            _byKey[key].push_back({ guid, x, y, nowMs + ttlMs });
            _keyOf[guid] = key;
        }
        void Release(uint64_t guid)
        {
            std::lock_guard<std::mutex> lock(_m);
            Erase(guid);
        }
        // Other bots (not `guid`) with a live claim on `key` within `radius` of (x, y).
        uint32_t Count(uint64_t key, uint64_t guid, float x, float y, float radius, uint32_t nowMs) const
        {
            std::lock_guard<std::mutex> lock(_m);
            auto it = _byKey.find(key);
            if (it == _byKey.end())
                return 0;
            uint32_t n = 0;
            float const r2 = radius * radius;
            for (Entry const& e : it->second)
            {
                if (e.Guid == guid || int32_t(e.ExpireMs - nowMs) <= 0)
                    continue;
                float const dx = e.X - x, dy = e.Y - y;
                if (dx * dx + dy * dy <= r2)
                    ++n;
            }
            return n;
        }
        size_t Size() const
        {
            std::lock_guard<std::mutex> lock(_m);
            return _keyOf.size();
        }

    private:
        struct Entry { uint64_t Guid = 0; float X = 0.0f, Y = 0.0f; uint32_t ExpireMs = 0; };
        // _m held
        void Erase(uint64_t guid)
        {
            auto k = _keyOf.find(guid);
            if (k == _keyOf.end())
                return;
            auto it = _byKey.find(k->second);
            if (it != _byKey.end())
            {
                auto& v = it->second;
                v.erase(std::remove_if(v.begin(), v.end(), [guid](Entry const& e) { return e.Guid == guid; }), v.end());
                if (v.empty())
                    _byKey.erase(it);
            }
            _keyOf.erase(k);
        }
        mutable std::mutex _m;
        std::unordered_map<uint64_t, std::vector<Entry>> _byKey;
        std::unordered_map<uint64_t, uint64_t> _keyOf;
    };

    inline bool Crowded(uint32_t others, uint32_t cap) { return cap && others >= cap; }

    // Score multiplier for a crowded objective: it stays pickable when nothing else is left, but loses against a free one.
    inline float CrowdFactor(uint32_t others, uint32_t cap, float crowdedFactor)
    {
        return Crowded(others, cap) ? crowdedFactor : 1.0f;
    }

    inline uint64_t ObjectiveKey(uint32_t kind, uint32_t id) { return (uint64_t(kind) << 32) | id; }

    // ---- N-07: leave a quest-less zone ---------------------------------------------------------------------------------------

    struct ZoneLeaveConfig
    {
        uint32_t HubNoneRows = 2;     // QUEST_HUB_NONE rows for the same map and level before the bot leaves
        uint32_t GrindMinutes = 10;   // minutes of grinding below MinXpPerKill before the bot leaves
        uint32_t MinXpPerKill = 40;
    };

    enum class LeaveReason : uint8_t { None, NoHub, LowXp };

    inline char const* LeaveReasonName(LeaveReason r)
    {
        return r == LeaveReason::NoHub ? "no_hub" : r == LeaveReason::LowXp ? "low_xp_grind" : "none";
    }

    struct ZoneLeave
    {
        uint32_t Map = 0, Level = 0, HubNone = 0;
        uint32_t LowSinceMs = 0;
        bool LowActive = false;

        // A quest hub scan found nothing. Returns true once the same (map, level) was seen cfg.HubNoneRows times.
        bool NoteHubNone(uint32_t map, uint32_t level, ZoneLeaveConfig const& cfg)
        {
            if (map != Map || level != Level)
            {
                Map = map;
                Level = level;
                HubNone = 0;
            }
            return ++HubNone >= cfg.HubNoneRows;
        }

        // Called after each grind kill with the xp it gave (0 = not a kill). Returns true when the average stayed low long enough.
        bool NoteGrindXp(uint32_t nowMs, uint32_t xp, ZoneLeaveConfig const& cfg)
        {
            if (xp >= cfg.MinXpPerKill)
            {
                LowActive = false;
                return false;
            }
            if (!LowActive)
            {
                LowActive = true;
                LowSinceMs = nowMs;
                return false;
            }
            return Reached(nowMs, LowSinceMs + cfg.GrindMinutes * 60u * 1000u);
        }

        void Reset()
        {
            HubNone = 0;
            LowActive = false;
        }
    };

    // ---- N-08: class pull margin ---------------------------------------------------------------------------------------------

    // Highest level difference a bot of the class may pick (mob level - bot level) while its level is at or below `untilLevel`.
    // Classes: 1 warrior, 2 paladin, 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid. -1 = no class rule.
    inline int ClassPullMargin(uint8_t classId, uint32_t botLevel, uint32_t untilLevel, int weakClassMargin, int sturdyClassMargin)
    {
        if (botLevel > untilLevel)
            return -1;
        switch (classId)
        {
            case 1: case 3: case 4: case 9: return weakClassMargin;       // warrior, hunter, rogue, warlock
            case 2: case 5: case 11: return sturdyClassMargin;            // paladin, priest, druid
            default: return -1;
        }
    }

    // The limit the pick uses: the global cap, lowered by the class rule when there is one.
    inline int EffectivePullCap(int globalCap, int classMargin)
    {
        return classMargin < 0 ? globalCap : std::min(globalCap, classMargin);
    }

    // ---- N-09: log volume diet -----------------------------------------------------------------------------------------------

    // "TARGET_PICKED:60,AGGRO_AVOID:30" -> (reason, cap per bot and hour). Bad tokens are skipped.
    inline std::vector<std::pair<std::string, uint32_t>> ParseHourlyCaps(std::string_view list)
    {
        std::vector<std::pair<std::string, uint32_t>> out;
        size_t pos = 0;
        while (pos <= list.size())
        {
            size_t end = list.find(',', pos);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view tok = list.substr(pos, end - pos);
            pos = end + 1;
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t'))
                tok.remove_prefix(1);
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t'))
                tok.remove_suffix(1);
            size_t const colon = tok.find(':');
            if (colon == std::string_view::npos || colon == 0 || colon + 1 >= tok.size())
                continue;
            std::string_view num = tok.substr(colon + 1);
            uint64_t v = 0;
            bool ok = true;
            for (char c : num)
            {
                if (c < '0' || c > '9' || v > 100000000ull)
                {
                    ok = false;
                    break;
                }
                v = v * 10 + uint64_t(c - '0');
            }
            if (ok)
                out.emplace_back(std::string(tok.substr(0, colon)), uint32_t(v));
        }
        return out;
    }

    // Counts rows per hour for one bot and reason; Hit() is true when this row is over the cap and should be dropped.
    struct HourlyCounter
    {
        uint64_t Hour = ~0ull;
        uint32_t N = 0;

        bool Hit(double nowSec, uint32_t cap)
        {
            uint64_t const hour = uint64_t(nowSec) / 3600u;
            if (hour != Hour)
            {
                Hour = hour;
                N = 0;
            }
            return ++N > cap;
        }
    };
    // ---- R-V2-04: free bag slots ---------------------------------------------------------------------------------------------

    struct BagFreeConfig
    {
        uint32_t TargetFree = 3;         // the extra sell pass stops when this many slots are free
        uint32_t RetryCooldownSec = 300; // after a vendor visit that freed nothing, no forced trip for this long
    };

    // A vendor visit that left the bags full and did not free a slot (freeAfter <= freeBefore); `fullAt` is the largest free count
    // that still counts as full.
    inline bool VendorFreedNothing(uint32_t freeBefore, uint32_t freeAfter, uint32_t fullAt)
    {
        return freeAfter <= fullAt && freeAfter <= freeBefore;
    }

    // Item kinds the extra sell pass may consider (poor or white quality weapons, armor and miscellaneous items; never quest items).
    // itemClass is the ITEM_CLASS_* value of the template (2 weapon, 4 armor, 15 miscellaneous), quality the ITEM_QUALITY_* value.
    inline bool ExtraSellable(uint32_t itemClass, uint32_t quality, bool questItem, bool hasSellPrice)
    {
        if (questItem || !hasSellPrice || quality > 1)
            return false;
        return itemClass == 2 || itemClass == 4 || itemClass == 15;
    }

    // ---- R-V2-05: death loop breaker -----------------------------------------------------------------------------------------

    struct DeathLoopConfig
    {
        uint32_t Deaths = 3;             // deaths to the same killer entry, or in the same area, inside the window
        uint32_t WindowSec = 1800;
        float AreaYd = 150.0f;
        uint32_t BlockSec = 7200;        // how long the killer entry and the area stay blocked for the bot
        uint32_t SharedBots = 10;        // distinct bots that blocked an entry before it goes on the shared avoid list
        uint32_t SharedSec = 7200;
    };

    struct DeathNote
    {
        uint32_t Map = 0;
        float X = 0.0f, Y = 0.0f;
        uint32_t Killer = 0;             // creature entry, 0 when unknown
        uint32_t AtMs = 0;               // 0 = empty slot
    };

    struct DeathLoopHit
    {
        uint32_t Total = 0;              // deaths that share the killer or the area of the newest death (it included)
        uint32_t ByKiller = 0;
        uint32_t ByArea = 0;
    };

    // d[0] is the newest death. Counts the entries inside the window that share its killer entry or its area.
    inline DeathLoopHit DeathLoopCount(DeathNote const* d, size_t n, uint32_t windowMs, float areaYd)
    {
        DeathLoopHit h;
        if (!n || !d[0].AtMs)
            return h;
        for (size_t i = 0; i < n; ++i)
        {
            if (!d[i].AtMs || uint32_t(d[0].AtMs - d[i].AtMs) > windowMs)
                continue;
            bool const k = d[0].Killer && d[i].Killer == d[0].Killer;
            float const dx = d[i].X - d[0].X, dy = d[i].Y - d[0].Y;
            bool const a = d[i].Map == d[0].Map && std::sqrt(dx * dx + dy * dy) <= areaYd;
            if (k)
                ++h.ByKiller;
            if (a)
                ++h.ByArea;
            if (k || a)
                ++h.Total;
        }
        return h;
    }

    // Per-bot block list: killer entries and circles a bot keeps away from. Times are the bot's AI clock.
    struct AvoidBook
    {
        struct Killer { uint32_t Entry; uint32_t Until; };
        struct Area { uint32_t Map; float X, Y, R; uint32_t Until; };
        std::vector<Killer> Killers;
        std::vector<Area> Areas;

        void Prune(uint32_t now)
        {
            Killers.erase(std::remove_if(Killers.begin(), Killers.end(), [now](Killer const& k) { return Reached(now, k.Until); }), Killers.end());
            Areas.erase(std::remove_if(Areas.begin(), Areas.end(), [now](Area const& a) { return Reached(now, a.Until); }), Areas.end());
        }

        void AddKiller(uint32_t entry, uint32_t now, uint32_t durMs)
        {
            Prune(now);
            for (Killer& k : Killers)
                if (k.Entry == entry)
                {
                    k.Until = now + durMs;
                    return;
                }
            if (Killers.size() < 16)
                Killers.push_back({ entry, now + durMs });
        }

        void AddArea(uint32_t map, float x, float y, float r, uint32_t now, uint32_t durMs)
        {
            Prune(now);
            for (Area& a : Areas)
            {
                float const dx = a.X - x, dy = a.Y - y;
                if (a.Map == map && std::sqrt(dx * dx + dy * dy) < r * 0.5f)
                {
                    a.Until = now + durMs;
                    return;
                }
            }
            if (Areas.size() < 8)
                Areas.push_back({ map, x, y, r, now + durMs });
        }

        bool KillerBlocked(uint32_t entry, uint32_t now) const
        {
            for (Killer const& k : Killers)
                if (k.Entry == entry && !Reached(now, k.Until))
                    return true;
            return false;
        }

        bool AreaBlocked(uint32_t map, float x, float y, uint32_t now) const
        {
            for (Area const& a : Areas)
            {
                float const dx = a.X - x, dy = a.Y - y;
                if (a.Map == map && !Reached(now, a.Until) && std::sqrt(dx * dx + dy * dy) <= a.R)
                    return true;
            }
            return false;
        }
    };

    // Shared avoid list: an entry that enough distinct bots blocked after a death loop is avoided by all bots for a while.
    // The clock is one global millisecond clock.
    class ShunBook
    {
    public:
        // Records that `bot` blocked `entry`. True when this call put the entry on the shared list.
        bool Note(uint32_t entry, uint64_t bot, uint32_t now, uint32_t needBots, uint32_t durMs)
        {
            std::lock_guard<std::mutex> lk(_mx);
            Entry& e = _map[entry];
            if (e.Shared && !Reached(now, e.Until))
                return false;
            if (e.Shared)
                e = Entry();
            for (uint64_t b : e.Bots)
                if (b == bot)
                    return false;
            if (e.Bots.size() < 64)
                e.Bots.push_back(bot);
            if (needBots && e.Bots.size() >= needBots)
            {
                e.Shared = true;
                e.Until = now + durMs;
                e.Bots.clear();
                return true;
            }
            return false;
        }

        bool Shared(uint32_t entry, uint32_t now) const
        {
            std::lock_guard<std::mutex> lk(_mx);
            auto it = _map.find(entry);
            return it != _map.end() && it->second.Shared && !Reached(now, it->second.Until);
        }

        size_t SharedCount(uint32_t now) const
        {
            std::lock_guard<std::mutex> lk(_mx);
            size_t n = 0;
            for (auto const& kv : _map)
                if (kv.second.Shared && !Reached(now, kv.second.Until))
                    ++n;
            return n;
        }

    private:
        struct Entry { std::vector<uint64_t> Bots; bool Shared = false; uint32_t Until = 0; };
        mutable std::mutex _mx;
        std::unordered_map<uint32_t, Entry> _map;
    };

    // ---- R-V2-06: flee only with an escape -----------------------------------------------------------------------------------

    struct FleeEscapeConfig
    {
        float MeleeYd = 8.0f;            // a mob this close is on the bot
        uint32_t GraceMs = 3000;         // time a flee gets before it counts as failed
    };

    // True when running cannot work: the mob is within melee distance and moves at least as fast as the bot.
    inline bool NoEscape(float mobDist, float mobSpeed, float botSpeed, FleeEscapeConfig const& cfg)
    {
        return mobDist <= cfg.MeleeYd && mobSpeed >= botSpeed;
    }

    // ---- R-V2-07: grind target pick ------------------------------------------------------------------------------------------

    struct TargetPickConfig
    {
        int32_t MaxAbove = 1;            // never pick a grind mob more than this many levels above the bot
        int32_t GreyBelow = 4;           // a mob this many levels or more below the bot ...
        uint32_t MinXp = 25;             // ... that gives less than this much XP is a low-value pick
    };

    inline bool LowValueTarget(int32_t levelDiff, uint32_t xp, TargetPickConfig const& cfg)
    {
        return levelDiff <= -cfg.GreyBelow && xp < cfg.MinXp;
    }

    // ---- R-V2-08: quest filter -----------------------------------------------------------------------------------------------

    // True when `id` is in the configured list of quest ids that are never offered.
    inline bool QuestListed(std::vector<uint32_t> const& list, uint32_t id)
    {
        return id && std::find(list.begin(), list.end(), id) != list.end();
    }
}

#endif
