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

#ifndef TRINITY_BOT_LOG_CATEGORY_H
#define TRINITY_BOT_LOG_CATEGORY_H

// Per-category switches of the bot activity log (Bot.Log.Categories, Bot.Log.CategoryMinSeverity). Every bot_event is mapped to one
// category from its type and reason code, and BotMgr::LogEvent drops it when the category is off or below its severity floor.
// Pure (no game types) so it is unit tested. To give a new log tag a category add its prefix to kPrefixes; an unmapped reason falls
// back to its type, then to "other", so a new tag is never lost by accident.

#include "Define.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace BotLogCat
{
    enum Category : uint8
    {
        Lifecycle,  // login/logout, creation, state changes, errors
        Ai,         // strategies, engine traces, tick stats
        Combat,     // fights, casts, auras, flee, aggro, threat
        Dummy,      // training dummy runs
        Movement,   // goto, follow, stuck, path failures, town idling
        Travel,     // long distance travel legs
        Recovery,   // death, corpse run, spirit healer, resting, watchdog
        Quest,      // quest decisions, blocked quests
        Progress,   // xp, levels, talents, known spells, reputation
        Economy,    // auction, trade, vendors, loot, gear, professions, consumables, bank, mail
        Pets,       // hunter and warlock pets
        Social,     // chat orders, alts, group invites, bot parties
        Dungeon,    // dungeon runs
        Other,      // anything no rule matches
        Count
    };

    inline constexpr char const* kNames[Count] =
    {
        "lifecycle", "ai", "combat", "dummy", "movement", "travel", "recovery", "quest",
        "progress", "economy", "pets", "social", "dungeon", "other"
    };

    struct Rule { char const* Prefix; Category Cat; };

    // Reason code prefixes, first match wins (so the longer prefix goes first).
    inline constexpr Rule kPrefixes[] =
    {
        { "BOT_", Lifecycle }, { "CREATE_", Lifecycle }, { "LOG_", Lifecycle }, { "ENGINE_", Ai }, { "STRATEGY_", Ai }, { "TRACE_", Ai },
        { "AI_", Ai }, { "COMBAT_", Combat }, { "CAST_", Combat }, { "AURA_", Combat }, { "FLEE_", Combat }, { "AGGRO_", Combat },
        { "THREAT_", Combat }, { "TARGET_PICKED", Combat }, { "REPAIR_", Economy }, { "DUMMY_", Dummy }, { "TEST_", Dummy },
        { "GOTO_", Movement }, { "FOLLOW_", Movement }, { "MOVE_", Movement }, { "NO_PATH", Movement }, { "NO_PROGRESS", Movement },
        { "PATH_", Movement }, { "ROUTE_", Movement }, { "STAY_", Movement }, { "MMAP_", Movement }, { "UNREACHABLE_", Movement },
        { "IDLE_STEP", Movement }, { "TOWN_", Movement }, { "TRAVEL_", Travel }, { "CORPSE_", Recovery }, { "SPIRIT_", Recovery },
        { "RELEASE_", Recovery }, { "RECLAIM_", Recovery }, { "RECOVER_", Recovery }, { "REST_", Recovery }, { "WATCHDOG_", Recovery },
        { "QUEST_", Quest }, { "POLE_", Quest }, { "XP_", Progress }, { "LEVEL_UP", Progress }, { "TALENTS_", Progress },
        { "SPELLS_", Progress }, { "REP_", Progress }, { "REPUTATION_", Progress }, { "AH_", Economy }, { "TRADE_", Economy },
        { "VENDOR_", Economy }, { "SOLD_", Economy }, { "TRAIN_", Economy }, { "BAG_", Economy }, { "GEAR_", Economy },
        { "LOOT_", Economy }, { "PROF_", Economy }, { "GATHER_", Economy }, { "FISH_", Economy }, { "AMMO_", Economy },
        { "POTION_", Economy }, { "EAT_", Economy }, { "DRINK_", Economy }, { "BANK_", Economy }, { "MAIL_", Economy },
        { "PET_", Pets }, { "ALT_", Social }, { "INVITE_", Social }, { "GROUP_", Social }, { "PARTY_", Social }, { "DUNGEON_", Dungeon }
    };

    // Event type fallback, used when the reason code has no prefix rule.
    inline constexpr Rule kTypes[] =
    {
        { "state_change", Lifecycle }, { "create", Lifecycle }, { "error", Lifecycle }, { "log", Lifecycle },
        { "strategy_change", Ai }, { "trace", Ai }, { "ai_stats", Ai },
        { "combat", Combat }, { "cast", Combat }, { "aura", Combat },
        { "stuck", Movement }, { "path_fail", Movement },
        { "death", Recovery },
        { "quest", Quest }, { "quest_blocked", Quest }, { "quest_done", Quest },
        { "xp", Progress }, { "level_up", Progress }, { "spells", Progress },
        { "chat_command", Social }, { "alt_command", Social }
    };

    inline bool StartsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

    inline Category Classify(std::string_view type, std::string_view reason)
    {
        for (Rule const& r : kPrefixes)
            if (StartsWith(reason, r.Prefix))
                return r.Cat;
        for (Rule const& r : kTypes)
            if (type == r.Prefix)
                return r.Cat;
        return Other;
    }

    struct Config
    {
        bool Enabled[Count];
        uint8 MinSeverity[Count]; // floor per category, on top of BotLog.MinSeverity

        Config() { std::fill(std::begin(Enabled), std::end(Enabled), true); std::fill(std::begin(MinSeverity), std::end(MinSeverity), uint8(0)); }

        bool Allows(Category c, uint8 severity) const { return Enabled[c] && severity >= MinSeverity[c]; }
    };

    inline bool FindCategory(std::string_view name, Category& out)
    {
        std::string n(name);
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        for (uint8 i = 0; i < Count; ++i)
            if (n == kNames[i])
            {
                out = Category(i);
                return true;
            }
        return false;
    }

    inline std::vector<std::string_view> SplitList(std::string_view s, char sep)
    {
        std::vector<std::string_view> out;
        size_t pos = 0;
        while (pos <= s.size())
        {
            size_t end = s.find(sep, pos);
            if (end == std::string_view::npos)
                end = s.size();
            std::string_view tok = s.substr(pos, end - pos);
            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.remove_prefix(1);
            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.remove_suffix(1);
            if (!tok.empty())
                out.push_back(tok);
            pos = end + 1;
        }
        return out;
    }

    // Bot.Log.Categories: comma list read left to right. "all" / "none" set every category, "name" turns one on, "-name" turns one off.
    // A list that starts with a plain name (e.g. "quest,dungeon") begins from "none"; one that starts with "-name" begins from "all".
    // Unknown names are appended to `unknown` and ignored.
    inline void ParseCategories(std::string_view list, Config& cfg, std::vector<std::string>& unknown)
    {
        std::vector<std::string_view> const toks = SplitList(list, ',');
        if (toks.empty())
            return;
        bool const startsAll = toks[0][0] == '-';
        std::fill(std::begin(cfg.Enabled), std::end(cfg.Enabled), startsAll);
        for (std::string_view t : toks)
        {
            std::string lower(t);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
            if (lower == "all" || lower == "none")
            {
                std::fill(std::begin(cfg.Enabled), std::end(cfg.Enabled), lower == "all");
                continue;
            }
            bool const off = t[0] == '-';
            Category c;
            if (FindCategory(off ? t.substr(1) : t, c))
                cfg.Enabled[c] = !off;
            else
                unknown.emplace_back(t);
        }
    }

    // Bot.Log.CategoryMinSeverity: "combat:2,movement:1" (0 trace, 1 info, 2 warn, 3 error). Unknown names / bad numbers go to `unknown`.
    inline void ParseSeverities(std::string_view list, Config& cfg, std::vector<std::string>& unknown)
    {
        for (std::string_view t : SplitList(list, ','))
        {
            size_t const colon = t.find(':');
            Category c;
            if (colon == std::string_view::npos || !FindCategory(t.substr(0, colon), c))
            {
                unknown.emplace_back(t);
                continue;
            }
            std::string_view num = t.substr(colon + 1);
            while (!num.empty() && std::isspace(static_cast<unsigned char>(num.front()))) num.remove_prefix(1);
            if (num.size() != 1 || num[0] < '0' || num[0] > '3')
            {
                unknown.emplace_back(t);
                continue;
            }
            cfg.MinSeverity[c] = uint8(num[0] - '0');
        }
    }
}

#endif
