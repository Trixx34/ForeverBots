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

#ifndef TRINITY_BOT_GROUP_ROLES_H
#define TRINITY_BOT_GROUP_ROLES_H

// Group play of the bots in combat (Bot.AI.Roles.*): who a healer heals, with which heal, and which mob the damage dealers attack.
// Pure functions over plain data so they are unit tested without a map or a Player; BotCombat.cpp fills the structs from the group and
// carries out the answer. See docs/playerbots/feature-bot-group-roles-20261008.md.

#include "Define.h"
#include <span>

namespace BotGroupRoles
{
    struct Config
    {
        int32 AllyHealBelowPct = 80;   // heal a group member below this health percent
        int32 TankHealBelowPct = 90;   // ... the tank already below this
        int32 EmergencyPct = 35;       // below this the fastest heal is used and the member comes first
        int32 SavePowerBelowPct = 20;  // below this mana only emergencies are healed
    };

    struct Ally
    {
        uint64 Guid = 0;
        int32 HealthPct = 100;
        uint32 MissingHealth = 0;      // max health minus health
        bool IsTank = false;
        bool IsSelf = false;
        bool InRange = true;           // within the heal's range and in line of sight
        bool Alive = true;
    };

    // The member to heal now: an emergency first (lowest percent), then the tank below TankHealBelowPct, then the lowest percent
    // below AllyHealBelowPct. -1 when nobody needs a heal or nobody who does can be reached. Ties go to the lower guid (stable).
    TC_GAME_API int32 PickHealTarget(std::span<Ally const> allies, Config const& cfg);

    // true when some member needs a heal (cheap check for the trigger; ignores range)
    TC_GAME_API bool AnyoneNeedsHeal(std::span<Ally const> allies, Config const& cfg);

    struct HealOption
    {
        uint32 SpellId = 0;
        uint32 Amount = 0;             // average healing
        uint32 ManaCost = 0;
        uint32 CastMs = 0;
        bool Ready = true;
    };

    // The heal to cast on a member missing `missing` health. Emergency: the most healing per second of cast time among the ready
    // options. Otherwise the cheapest per point healed that covers at least 80 percent of the missing health without more than
    // 2.5 times overhealing, else the largest below that. Only options the healer can pay (`mana`) are considered. -1 for none.
    TC_GAME_API int32 PickHeal(std::span<HealOption const> options, uint32 missing, uint32 mana, bool emergency);

    struct Foe
    {
        uint64 Guid = 0;
        int32 HealthPct = 100;
        bool OnTank = false;           // attacking the tank
        bool OnHealer = false;         // attacking a healer or another non-tank member
        bool IsCaster = false;
        bool InRange = true;
        int32 VictimHealthPct = 100;   // health of the member the mob attacks (OnHealer foes)
        bool VictimIsHealer = false;
        bool Taunted = false;          // a taunt landed on it recently (or it is taunt immune): skip
    };

    // The mob a damage dealer attacks: one that attacks a non-tank member (peel) when it is nearly dead or alone, otherwise the
    // tank's target (so threat builds on one mob), otherwise any mob on the tank, then the lowest health. `tankTarget` is the mob
    // the tank attacks (0 = unknown). -1 when no foe is in range.
    TC_GAME_API int32 PickAssistTarget(std::span<Foe const> foes, uint64 tankTarget);

    // The mob the tank taunts: one that attacks a non-tank member and was not taunted lately. A mob on a healer first, then on the
    // member with the lowest health, then the mob with the most health left (it threatens the longest). -1 when nothing needs a taunt.
    TC_GAME_API int32 PickTauntTarget(std::span<Foe const> foes);

    struct HoldFacts
    {
        bool IsTank = false;
        bool TankKnown = false;        // the group has a tank in range
        bool TankEngaged = false;      // the tank is fighting this mob (or is about to)
        bool MobOnTank = false;        // the mob attacks the tank
        bool MobOnMe = false;          // the mob attacks this bot
        uint32 SinceMs = 0;            // time since the bot picked the mob
    };

    // true = hold fire: a damage dealer or healer waits for the tank to gather threat, up to `holdMs`, and a little after the mob turned
    // to the tank. Never when the bot is the tank, when there is no tank or the tank is not engaged, or when the mob already attacks the bot.
    TC_GAME_API bool HoldFire(HoldFacts const& f, uint32 holdMs, uint32 afterTurnMs = 1500);
}

#endif
