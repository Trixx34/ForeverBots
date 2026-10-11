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

#ifndef TRINITY_BOT_LOOP_GUARD_H
#define TRINITY_BOT_LOOP_GUARD_H

// Pure helpers against the decision loops found in the 800-bot sim run (docs/playerbots/sim-run-800-analysis-20261011.md,
// rules R-V2-01 to R-V2-03): a weapon pick that never chooses a profession tool, the approach-timeout tracker, the cause of an
// approach loop, and the trip guard (cooldown, attempt cap, give-up period). Header-only and free of server types so they can be
// unit tested. All times are the AI clock in milliseconds; comparisons are wrap safe.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace BotLoop
{
    // True when `now` is at or after `t` (wrap safe for the 32 bit AI clock).
    inline bool Reached(uint32_t now, uint32_t t) { return int32_t(now - t) >= 0; }

    // ---- R-V2-01: combat weapon ----------------------------------------------------------------------------------------------

    // Bag weapon that could replace the main hand.
    struct WeaponCand
    {
        uint32_t Entry = 0;
        bool Usable = false;    // the bot can use it and it goes into the main hand slot, and it is not broken
        bool Tool = false;      // profession tool (fishing pole, skinning knife, mining pick)
        double Score = 0.0;     // class role gear score
    };

    inline bool IsToolEntry(std::vector<uint32_t> const& tools, uint32_t entry)
    {
        return entry && std::find(tools.begin(), tools.end(), entry) != tools.end();
    }

    // The main hand needs a combat weapon: it holds a profession tool. An empty main hand also counts when a weapon could fill it.
    inline bool MainHandNeedsWeapon(uint32_t mainEntry, bool mainIsTool)
    {
        return mainEntry == 0 || mainIsTool;
    }

    // Index of the best usable non-tool weapon, -1 when there is none. Ties keep the first candidate.
    inline int PickCombatWeapon(std::vector<WeaponCand> const& cands)
    {
        int best = -1;
        for (size_t i = 0; i < cands.size(); ++i)
        {
            WeaponCand const& c = cands[i];
            if (!c.Usable || c.Tool)
                continue;
            if (best < 0 || c.Score > cands[size_t(best)].Score)
                best = int(i);
        }
        return best;
    }

    // ---- R-V2-02: approach timeout escalation --------------------------------------------------------------------------------

    enum class ApproachCause : uint8_t { Unknown, Weapon, Ammo };

    inline char const* CauseName(ApproachCause c)
    {
        switch (c)
        {
            case ApproachCause::Weapon: return "weapon";
            case ApproachCause::Ammo: return "ammo";
            default: return "unknown";
        }
    }

    // Weapon first: a tool or nothing in the main hand explains a melee bot that stands next to the mob and does not attack.
    inline ApproachCause ClassifyApproach(bool mainHandNeedsWeapon, bool rangedNeedsAmmo)
    {
        if (mainHandNeedsWeapon)
            return ApproachCause::Weapon;
        if (rangedNeedsAmmo)
            return ApproachCause::Ammo;
        return ApproachCause::Unknown;
    }

    // Timestamps of the latest approach timeouts of one bot.
    struct ApproachTracker
    {
        std::array<uint32_t, 16> Ms{};
        uint8_t Count = 0;

        // Records a timeout at `now` and returns how many are inside the window, this one included.
        uint32_t Note(uint32_t now, uint32_t windowMs)
        {
            uint8_t w = 0;
            for (uint8_t i = 0; i < Count; ++i)
                if (uint32_t(now - Ms[i]) <= windowMs)
                    Ms[w++] = Ms[i];
            Count = w;
            if (Count == Ms.size())
            {
                std::copy(Ms.begin() + 1, Ms.end(), Ms.begin());
                --Count;
            }
            Ms[Count++] = now;
            return Count;
        }

        void Clear() { Count = 0; }
    };

    // ---- R-V2-03: trip guard -------------------------------------------------------------------------------------------------

    struct TripConfig
    {
        uint32_t CooldownMs = 0;    // minimum time between two planned trips of the same kind and key
        uint32_t MaxAttempts = 5;   // planned trips without a success before the key is given up
        uint32_t GiveUpMs = 0;      // how long a given up key stays blocked
    };

    struct TripState
    {
        uint32_t Attempts = 0;
        uint32_t LastMs = 0;
        uint32_t BlockedUntilMs = 0;
        bool Blocked = false;
    };

    inline bool TripAllowed(TripState const& s, uint32_t now, TripConfig const& cfg)
    {
        if (s.Blocked && !Reached(now, s.BlockedUntilMs))
            return false;
        if (s.Attempts > 0 && !Reached(now, s.LastMs + cfg.CooldownMs))
            return false;
        return true;
    }

    // Call when a trip is planned. The MaxAttempts-th trip without a success blocks the key for GiveUpMs; the counter then starts over.
    // Returns true when this call gave the key up.
    inline bool NoteTripPlanned(TripState& s, uint32_t now, TripConfig const& cfg)
    {
        if (s.Blocked && Reached(now, s.BlockedUntilMs))
        {
            s.Blocked = false;
            s.Attempts = 0;
        }
        ++s.Attempts;
        s.LastMs = now;
        if (cfg.MaxAttempts && s.Attempts >= cfg.MaxAttempts)
        {
            s.Blocked = true;
            s.BlockedUntilMs = now + cfg.GiveUpMs;
            s.Attempts = 0;
            return true;
        }
        return false;
    }

    // Call when the trip reached its goal and did its job (the skill was trained, the craft went through).
    inline void NoteTripSuccess(TripState& s)
    {
        s = TripState();
    }
}

#endif
