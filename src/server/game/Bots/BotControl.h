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

#ifndef TRINITY_BOT_CONTROL_H
#define TRINITY_BOT_CONTROL_H

// Player control panel of the bots (Bot.Chat.Control.*, default off): the group leader sets a bot's role, stance, focus target and
// follow distance through chat and asks what it is doing. Pure functions over plain data (no Player, no config, no world state) so they
// are unit tested; BotChat.cpp parses and applies, BotCombat.cpp / BotBehavior.cpp read the settings.
// See docs/playerbots/feature-bot-control-commands-20261008.md.

#include "Define.h"
#include <string>
#include <string_view>

namespace BotControl
{
    // Role override of one bot. Auto = what the class gives (a warrior is the tank, see BotCombat.cpp GroupTank).
    enum class Role : uint8 { Auto, Tank, Healer, Dps };
    // Stance: Aggressive fights everything the group is in combat with (the default), Defensive only mobs that attack the bot or
    // a group member, Passive only mobs that attack the bot.
    enum class Stance : uint8 { Aggressive, Defensive, Passive };

    TC_GAME_API char const* RoleName(Role r);
    TC_GAME_API char const* StanceName(Stance s);

    // Case insensitive. Role: tank | healer (heals) | dps (dd, damage) | auto (default, class). Stance: aggressive | defensive | passive.
    TC_GAME_API bool ParseRole(std::string_view token, Role& out);
    TC_GAME_API bool ParseStance(std::string_view token, Stance& out);

    struct DistanceArgs
    {
        bool Reset = false;    // "default": back to the built-in follow distance
        float Yards = 0.0f;
    };

    // "<yards>" or "default" (also "reset", "auto"). Returns nullptr when valid, else the refusal code (BAD_ARGS). A number outside
    // [minYards, maxYards], a non finite one or trailing text is refused.
    TC_GAME_API char const* ParseDistance(std::string_view args, float minYards, float maxYards, DistanceArgs& out);

    // Follow thresholds for a follow distance: the bot starts walking when the leader is farther than `start` and stops within
    // `arrive`. distance 0 = the built-in values (10 / 5).
    TC_GAME_API void FollowThresholds(float distance, float& start, float& arrive);

    struct FoeFacts
    {
        bool AttacksSelf = false;     // the mob's victim is the bot
        bool AttacksGroup = false;    // the mob's victim is another member of the bot's group
    };

    // Whether the stance lets the bot fight this mob.
    TC_GAME_API bool StanceAllows(Stance stance, FoeFacts const& foe);

    // Order of the tank candidates: a member with the Tank override first (0), then an Auto warrior (1), nobody else (-1).
    // Healer and Dps overrides take a warrior out of the race.
    TC_GAME_API int32 TankPriority(Role role, bool isWarrior);

    struct ReportFacts
    {
        std::string Name;
        bool Alive = true;
        bool Ghost = false;
        bool InCombat = false;
        std::string FightTarget;      // name of the mob being fought, empty if unknown
        bool Resting = false;
        bool Mounted = false;
        bool Staying = false;         // "stay" strategy
        bool Following = false;       // follows a leader
        bool HasGoal = false;
        std::string GoalTag;          // goto | follow | quest | grind | ...
        Role RoleSet = Role::Auto;
        Stance StanceSet = Stance::Aggressive;
        float FollowYards = 0.0f;     // 0 = default
        std::string Focus;            // name of the focus target, empty = none
        uint32 HealthPct = 100;
        bool UsesMana = false;
        uint32 ManaPct = 100;
    };

    // What the bot is doing right now, as a short phrase ("fighting Defias Thug", "following you", "holding position", "dead").
    TC_GAME_API std::string DescribeDoing(ReportFacts const& f);

    // One reply line for a single bot: "<name>: <doing>; role tank, stance defensive, follow 15y, focus <name>; hp 80%, mana 40%".
    // Settings left at their defaults are not listed.
    TC_GAME_API std::string DescribeReport(ReportFacts const& f);

    // "what are you doing", "what are you up to", "what's up", "report" (case insensitive, a trailing ? . ! is ignored).
    TC_GAME_API bool IsStatusQuestion(std::string_view text);
}

#endif
