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


#ifndef TRINITY_BOT_LOG_SUMMARY_H
#define TRINITY_BOT_LOG_SUMMARY_H

// Summary rows for the bot log (Bot.Log.Summary.*). High-volume events (casts, auras, walking decisions) are counted in memory per
// (window, bot, type, reason, severity, summary key, map, zone, quest, target) and written as one row of bot_event_rollup per key and
// window instead of one bot_event row each. Rare or important events stay detailed: anything at or above the keep severity and every
// reason with a keep prefix (DUNGEON_, TRAVEL_, DUMMY_ ...). Pure (no game types, no database) so it is unit tested.

#include "Define.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace BotLogSummary
{
    struct Policy
    {
        bool Enabled = false;
        uint32 WindowSec = 60;
        uint8 KeepSeverity = 2;                    // events at or above this severity are never summarized (4 = no exception)
        std::vector<std::string> Types;            // event types that are summarized (lower case)
        std::vector<std::string> ReasonPrefixes;   // reason code prefixes that are summarized (upper case)
        std::vector<std::string> KeepPrefixes;     // reason code prefixes that always stay detailed, win over Types and ReasonPrefixes
        std::vector<std::string> KeyBySummary;     // types whose summary text is part of the key (e.g. cast: one row per spell)
    };

    inline std::string Lower(std::string_view s) { std::string r(s); std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return r; }
    inline std::string Upper(std::string_view s) { std::string r(s); std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return char(std::toupper(c)); }); return r; }

    // "a, b ,c" -> {"a","b","c"}; empty entries dropped; every entry passed through `norm`.
    inline std::vector<std::string> ParseList(std::string_view list, std::string (*norm)(std::string_view))
    {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos <= list.size())
        {
            size_t end = list.find(',', pos);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view tok = list.substr(pos, end - pos);
            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.remove_prefix(1);
            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.remove_suffix(1);
            if (!tok.empty())
                out.push_back(norm(tok));
            pos = end + 1;
        }
        return out;
    }

    inline bool HasPrefix(std::string_view s, std::vector<std::string> const& prefixes)
    {
        for (std::string const& p : prefixes)
            if (s.size() >= p.size() && s.compare(0, p.size(), p) == 0)
                return true;
        return false;
    }

    inline bool Contains(std::vector<std::string> const& v, std::string_view s)
    {
        return std::find(v.begin(), v.end(), s) != v.end();
    }

    inline bool ShouldSummarize(Policy const& p, std::string_view type, std::string_view reason, uint8 severity)
    {
        if (!p.Enabled || severity >= p.KeepSeverity)
            return false;
        std::string const r = Upper(reason);
        if (HasPrefix(r, p.KeepPrefixes))
            return false;
        return Contains(p.Types, Lower(type)) || (!r.empty() && HasPrefix(r, p.ReasonPrefixes));
    }

    struct Key
    {
        uint64 Window = 0;        // window start, unix seconds
        uint64 BotGuid = 0;
        std::string Type, Reason, SummaryKey;
        uint8 Severity = 0;
        uint32 MapId = 0, ZoneId = 0, QuestId = 0, TargetEntry = 0;

        bool operator<(Key const& o) const
        {
            return std::tie(Window, BotGuid, Type, Reason, SummaryKey, Severity, MapId, ZoneId, QuestId, TargetEntry)
                 < std::tie(o.Window, o.BotGuid, o.Type, o.Reason, o.SummaryKey, o.Severity, o.MapId, o.ZoneId, o.QuestId, o.TargetEntry);
        }
    };

    struct Row
    {
        Key K;
        uint32 Count = 0;
        double FirstTs = 0.0, LastTs = 0.0;
        uint8 Level = 0;                  // of the last event
        uint64 SessionSeq = 0;            // of the last event
        std::string SampleSummary;        // of the first event
        std::string SampleDetails;        // of the first event
    };

    struct Input
    {
        uint64 BotGuid = 0;
        std::string Type, Reason, Summary, Details;
        uint8 Severity = 1, Level = 0;
        uint32 MapId = 0, ZoneId = 0, QuestId = 0, TargetEntry = 0;
        double Timestamp = 0.0;
        uint64 SessionSeq = 0;
    };

    class Aggregator
    {
    public:
        // false = not stored (table full), the caller writes the event in detail.
        bool Add(Policy const& p, Input const& in, size_t maxKeys)
        {
            Key k;
            uint32 const win = std::max<uint32>(1, p.WindowSec);
            k.Window = uint64(std::floor(in.Timestamp / win)) * win;
            k.BotGuid = in.BotGuid;
            k.Type = in.Type;
            k.Reason = in.Reason;
            k.Severity = in.Severity;
            k.MapId = in.MapId; k.ZoneId = in.ZoneId; k.QuestId = in.QuestId; k.TargetEntry = in.TargetEntry;
            if (Contains(p.KeyBySummary, Lower(in.Type)))
                k.SummaryKey = in.Summary.substr(0, 64);
            auto it = _rows.find(k);
            if (it == _rows.end())
            {
                if (_rows.size() >= maxKeys)
                    return false;
                Row r;
                r.K = k;
                r.FirstTs = in.Timestamp;
                r.SampleSummary = in.Summary.substr(0, 255);
                r.SampleDetails = in.Details;
                it = _rows.emplace(k, std::move(r)).first;
            }
            Row& r = it->second;
            ++r.Count;
            r.FirstTs = std::min(r.FirstTs, in.Timestamp);
            r.LastTs = std::max(r.LastTs, in.Timestamp);
            r.Level = in.Level;
            r.SessionSeq = in.SessionSeq;
            return true;
        }

        // Rows of windows that ended before `now` (all rows when `all`), removed from the table.
        std::vector<Row> Drain(double now, uint32 windowSec, bool all)
        {
            std::vector<Row> out;
            uint32 const win = std::max<uint32>(1, windowSec);
            for (auto it = _rows.begin(); it != _rows.end();)
            {
                if (all || double(it->first.Window + win) <= now)
                {
                    out.push_back(std::move(it->second));
                    it = _rows.erase(it);
                }
                else
                    ++it;
            }
            return out;
        }

        size_t Size() const { return _rows.size(); }

    private:
        std::map<Key, Row> _rows;
    };
}

#endif
