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

#ifndef TRINITY_BOT_LFG_PLAN_H
#define TRINITY_BOT_LFG_PLAN_H

// Looking for group (Bot.LFG.*, default off): a player whispers "lfg" (optionally with a role) to a bot, or types it in party chat as the
// leader, and free bots join the player's group until it is a full five-man party with a tank, a healer and damage dealers. Everything
// here is a pure function over plain data (no Player, no Map) so it is unit tested without a world; the glue is BotLfg.cpp.
// See docs/playerbots/feature-bot-lfg-20261008.md.

#include "BotDungeonPlan.h"
#include "Define.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BotLfg
{
    struct Config
    {
        bool Enabled = false;
        uint32 GroupSize = 5;          // 2-5, the size a search fills up to
        uint32 MaxLevelSpread = 4;     // a bot may be this many levels above or below the player
        uint32 MinLevel = 10;          // lowest bot level that is called
        bool Teleport = true;          // a found bot is moved next to the player; off: only bots on the player's map within MaxDistance
        float MaxDistance = 1500.0f;   // yards, only used without Teleport
        uint32 TimeoutSec = 180;       // a search that is not full after this long ends (the bots found stay)
        uint32 ReleaseSec = 60;        // bots are released this long after the player went offline
    };

    // --- chat request ---
    enum class Action : uint8 { None, Join, Leave, Status };

    struct Request
    {
        Action What = Action::None;
        bool HasRole = false;          // the player named a role
        BotDungeon::Role AsRole = BotDungeon::Role::Dps;
        bool BadArgs = false;          // "lfg" followed by something that is not understood
    };

    // "lfg", "lfg tank|healer|dps", "lfg off|cancel|leave|stop", "lfg status" (case insensitive, nothing else around it). Role aliases:
    // heal, healing, damage, dd. Anything that does not start with the word lfg is None (an ordinary chat line).
    TC_GAME_API Request ParseRequest(std::string_view text);

    // --- filling ---
    struct Candidate
    {
        uint64 Guid = 0;
        uint8 ClassId = 0;
        uint32 Level = 1;
        uint8 Team = 0;                // 0 alliance, 1 horde: only the player's team can join
        float Distance = 0.0f;         // yards to the player on the same map, a large value elsewhere
        bool SameMap = true;
    };

    struct Wanted
    {
        uint8 Team = 0;
        uint32 Level = 1;
        std::vector<BotDungeon::Role> Have;   // roles of everybody already in the group, the player included
    };

    struct Pick
    {
        uint64 Guid = 0;
        BotDungeon::Role AsRole = BotDungeon::Role::Dps;
    };

    struct FillResult
    {
        std::vector<Pick> Picks;
        uint32 MissingTanks = 0;       // places still open after the picks
        uint32 MissingHealers = 0;
        uint32 MissingDps = 0;
        bool Complete() const { return !MissingTanks && !MissingHealers && !MissingDps; }
    };

    // The places a group of `cfg.GroupSize` still needs: one tank and one healer when the group has none, damage dealers for the rest.
    // Extra tanks or healers in the group take a damage place. Zero everywhere when the group is full.
    TC_GAME_API FillResult OpenPlaces(std::span<BotDungeon::Role const> have, Config const& cfg);

    // Bots for the open places. Tanks first, then healers, then damage; within a role a class that prefers it comes before a class that
    // only can (a warrior before a paladin as tank), then the nearer bot, then the lower guid. Bots of the other team or outside the level
    // window around the player (or below cfg.MinLevel) are skipped; without cfg.Teleport so are bots on other maps or beyond MaxDistance.
    // A bot is never used twice. Deterministic for equal input.
    TC_GAME_API FillResult PlanFill(std::span<Candidate const> candidates, Wanted const& want, Config const& cfg);

    // --- life of a search ---
    enum class Verdict : uint8
    {
        Keep,
        Complete,    // the search found everybody: it ends, the bots stay
        Expire,      // the search ran out of time: it ends, the bots stay
        Release      // the bots leave the group (see Outcome::Why)
    };

    struct EntryFacts
    {
        bool Searching = true;
        bool PlayerOnline = true;
        bool PlayerMayLead = true;     // not in somebody else's group, not in a raid, not on a battleground
        uint32 AgeSec = 0;
        uint32 GoneSec = 0;            // seconds in a row the player was offline
        uint32 Missing = 0;            // open places after the last fill
    };

    struct Outcome
    {
        Verdict What = Verdict::Keep;
        char const* Why = "";
    };

    // Release: "offline" (player gone for cfg.ReleaseSec), "not_leader" (the player joined or lost the group). Complete: "full".
    // Expire: "timeout".
    TC_GAME_API Outcome Evaluate(EntryFacts const& f, Config const& cfg);

    // --- texts for the player ---
    // "1 tank, 1 healer and 2 damage dealers", "" when nothing is open.
    TC_GAME_API std::string DescribeMissing(uint32 tanks, uint32 healers, uint32 dps);

    TC_GAME_API char const* RoleText(BotDungeon::Role r);
}

#endif
