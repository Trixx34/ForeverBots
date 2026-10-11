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

#ifndef TRINITY_BOT_GROUP_CONTROL_H
#define TRINITY_BOT_GROUP_CONTROL_H

// Crowd control, interrupts and dispels of bots in a group (Bot.AI.GroupControl.*, docs/playerbots/feature-bot-group-control-20261008.md).
// Pure decisions over plain data so they are unit tested without a map or a Player: BotCombat.cpp fills the facts from the game state.
//   - ClaimBoard: several bots must not answer the same event (three kicks on one cast, two polymorphs on one mob). The first bot to
//     claim a key owns it for a short hold; if its cast fails the hold runs out and a second bot can take over.
//   - interrupts: is an enemy cast worth a Kick / Counterspell?
//   - dispels: which harmful aura on which group member is worth a dispel, and who gets it first?
//   - crowd control: which enemy gets the control spell. Raid target icons set by the player (moon, square...) mark the mobs to control.

#include "Define.h"
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace BotGroupControl
{
    // ---------------------------------------------------------------------------------------------------------------------
    // claims
    // ---------------------------------------------------------------------------------------------------------------------
    class TC_GAME_API ClaimBoard
    {
    public:
        // True when `claimer` holds the claim on `key` after this call: nobody held it, the holder's hold ran out, or it is the claimer
        // itself (which renews the hold). False when another claimer still holds it. Times are milliseconds on one shared clock;
        // a difference of more than 2^31 is treated as expired (wrap around).
        bool Claim(uint64 key, uint64 claimer, uint32 nowMs, uint32 holdMs);
        // True when another claimer holds a live claim on `key` (a look, nothing is claimed).
        bool HeldByOther(uint64 key, uint64 claimer, uint32 nowMs) const;
        // The claimer gives the claim up (its cast went through and the aura now speaks for itself, or it cannot cast after all).
        void Release(uint64 key, uint64 claimer);
        // Number of live claims (for tests and the console).
        size_t Size() const;

    private:
        struct Entry { uint64 Key; uint64 Claimer; uint32 UntilMs; };
        static constexpr size_t MaxEntries = 64;   // bounded: the oldest entry makes room
        mutable std::mutex _lock;
        std::vector<Entry> _entries;
    };

    TC_GAME_API ClaimBoard& InterruptClaims();
    TC_GAME_API ClaimBoard& DispelClaims();
    TC_GAME_API ClaimBoard& ControlClaims();

    // ---------------------------------------------------------------------------------------------------------------------
    // interrupts
    // ---------------------------------------------------------------------------------------------------------------------
    struct CastFacts
    {
        uint32 TotalMs = 0;          // full cast (or channel) time
        uint32 RemainingMs = 0;      // time left
        bool Interruptible = true;   // the spell does not carry the "cannot be interrupted" flag
        bool Heal = false;           // the spell heals (a healer mob healing the pack)
        bool Control = false;        // the spell stuns, fears, roots, charms or polymorphs
    };

    struct InterruptConfig
    {
        uint32 MinCastMs = 1500;       // Bot.AI.GroupControl.Interrupt.MinCastMs: other spells are let through when shorter
        uint32 MinRemainingMs = 400;   // Bot.AI.GroupControl.Interrupt.MinRemainingMs: a cast about to land is not worth the cooldown
    };

    TC_GAME_API bool WorthInterrupting(CastFacts const& cast, InterruptConfig const& cfg);

    // ---------------------------------------------------------------------------------------------------------------------
    // dispels
    // ---------------------------------------------------------------------------------------------------------------------
    // Bits of the dispel types a bot can remove with the spells it knows.
    constexpr uint32 DISPEL_BIT_MAGIC   = 1u << 0;
    constexpr uint32 DISPEL_BIT_CURSE   = 1u << 1;
    constexpr uint32 DISPEL_BIT_DISEASE = 1u << 2;
    constexpr uint32 DISPEL_BIT_POISON  = 1u << 3;

    // The bit of a spell dispel type (DispelType: 1 magic, 2 curse, 3 disease, 4 poison, anything else 0).
    TC_GAME_API uint32 DispelBitOf(uint8 dispelType);

    struct AuraFact
    {
        uint8 DispelType = 0;
        bool Harmful = true;       // a debuff on a friend
        bool Hard = false;         // takes the member out of the fight: polymorph, stun, fear, root, silence, charm
        bool Damage = false;       // damage over time
        bool Slow = false;         // movement or attack speed reduction
        uint32 RemainingMs = 0xFFFFFFFFu;   // time left, 0xFFFFFFFF for an aura without duration
    };

    constexpr int32 PRIORITY_HARD   = 100;
    constexpr int32 PRIORITY_DAMAGE = 40;
    constexpr int32 PRIORITY_SLOW   = 20;
    constexpr int32 PRIORITY_OTHER  = 10;

    // 0 when the aura is not worth a dispel (not harmful, not removable with `canMask`, runs out in under 2 seconds), else its priority.
    TC_GAME_API int32 AuraPriority(AuraFact const& aura, uint32 canMask);

    struct DispelCandidate
    {
        uint64 Key = 0;         // group member
        int32 Priority = 0;     // highest AuraPriority on the member
        int32 HealthPct = 100;
    };

    // Index of the member that is dispelled first, or -1: highest priority (at least `minPriority`), then lowest health, then lowest key.
    TC_GAME_API int32 PickDispelTarget(std::vector<DispelCandidate> const& members, int32 minPriority);

    // ---------------------------------------------------------------------------------------------------------------------
    // crowd control
    // ---------------------------------------------------------------------------------------------------------------------
    // Raid target icons, index 0..7 as in the client: star, circle, diamond, triangle, moon, square, cross, skull; -1 for none.
    constexpr int8 ICON_NONE = -1;
    constexpr int8 ICON_SKULL = 7;

    // Bot.AI.GroupControl.Control.Marks: comma list of icon names or numbers 1..8 (1 star ... 8 skull) to a bit mask (bit = icon index).
    // Unknown tokens are appended to `unknown` when given and ignored. "" and "none" give 0.
    TC_GAME_API uint32 ParseMarkList(std::string_view list, std::vector<std::string>* unknown = nullptr);
    TC_GAME_API bool MarkInMask(int8 icon, uint32 mask);

    struct ControlCandidate
    {
        uint64 Key = 0;
        int32 Level = 0;
        int8 Mark = ICON_NONE;
        bool Controlled = false;       // already carries a control aura (polymorph, sap, shackle...)
        bool IsFightTarget = false;    // the mob the group is killing
        bool TypeOk = true;            // the bot's control spell works on this creature type
        bool Reachable = true;         // in spell range and line of sight
        bool Tried = false;            // an attempt on it failed or was resisted recently
        bool ClaimedByOther = false;   // another bot holds the claim on it
    };

    struct ControlConfig
    {
        uint32 MarkMask = 0;       // icons that mean "control this mob"
        bool Auto = true;          // without a mark: the strongest extra attacker gets controlled
        uint32 MaxAuto = 1;        // Auto controls at most this many mobs at a time
    };

    // Index of the mob to control, or -1. The fight target is never controlled. Marked mobs (an icon of MarkMask) come first, any number of
    // them, strongest first. Without a mark, Auto controls the strongest unmarked-for-kill mob (not the skull) while fewer than MaxAuto
    // mobs are controlled already.
    TC_GAME_API int32 PickControlTarget(std::vector<ControlCandidate> const& mobs, ControlConfig const& cfg);

    // A mob that the fight-target choice leaves alone as long as something else is left: controlled, or marked for control.
    TC_GAME_API bool ReservedForControl(bool controlled, int8 mark, uint32 markMask);
}

#endif
