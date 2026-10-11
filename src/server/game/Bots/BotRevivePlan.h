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

#ifndef TRINITY_BOT_REVIVE_PLAN_H
#define TRINITY_BOT_REVIVE_PLAN_H

// Resurrecting dead group members (Bot.AI.Revive.*, BotRevive.cpp). A priest, paladin or shaman bot in a group raises a dead member
// with the class resurrection; a dead bot waits a while for such a rezzer before it releases its spirit and runs back to its corpse.
// Pure over plain data so it is unit tested without a map or a Player. See docs/playerbots/feature-bot-revive-repair-20261008.md.

#include "Define.h"
#include <span>

namespace BotRevive
{
    struct Config
    {
        bool Enabled = false;
        float MaxYards = 60.0f;          // a rezzer further away from the dead member than this does not come
        int32 MinManaPct = 40;           // a rezzer needs this much mana (the spell costs a lot)
        uint32 RetryMs = 20000;          // least time between two casts at the same dead member
        uint32 MaxAttempts = 4;          // casts at one dead member before the rezzer gives up
        uint32 WaitBeforeReleaseMs = 20000; // a dead bot holds its spirit this long while a rezzer is on the way (0 = never waits)
        uint32 AcceptDelayMs = 2000;     // a bot takes a resurrection offer this long after it arrived
    };

    struct Candidate
    {
        uint64 Key = 0;                  // guid counter, breaks ties
        float Dist = 0.0f;               // yards to the dead member
        int32 ManaPct = 100;
        bool Ready = true;               // knows the spell, no cooldown, not stunned/silenced
        bool InCombat = false;
        bool Busy = false;               // casting, or taken by a dungeon run
    };

    // Index of the one candidate that goes: the nearest one that is able, ties by key. Every bot computes the same answer from the
    // same group, so exactly one acts. -1 when nobody can.
    TC_GAME_API int32 PickResurrector(std::span<Candidate const> candidates, Config const& cfg);

    enum class Act : uint8 { None, Approach, Cast, Wait };

    struct Facts
    {
        float Dist = 0.0f;
        float CastRange = 30.0f;
        bool TargetDead = true;
        bool OfferPending = false;       // the dead member already holds a resurrection offer
        bool Casting = false;
        uint32 SinceCastMs = 0xFFFFFFFF; // since this bot last cast at the dead member
        uint32 Attempts = 0;
    };

    // Approach until the spell reaches, then cast once per RetryMs; None once the attempts are used up or the member is up again.
    TC_GAME_API Act PlanStep(Facts const& f, Config const& cfg);

    // True while a dead bot should keep its spirit instead of releasing: a rezzer exists and the wait is not over.
    TC_GAME_API bool HoldRelease(bool rezzerAvailable, uint32 deadMs, Config const& cfg);

    // True when a pending resurrection offer should be taken now.
    TC_GAME_API bool AcceptOffer(uint32 offerAgeMs, Config const& cfg);
}

#endif
