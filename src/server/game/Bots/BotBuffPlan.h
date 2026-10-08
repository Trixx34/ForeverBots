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

#ifndef TRINITY_BOT_BUFF_PLAN_H
#define TRINITY_BOT_BUFF_PLAN_H

// Decision helpers of the group buffs (Bot.AI.Buffs.*, BotBuffs.cpp): which targets a buff makes sense on, and a bounded retry throttle
// per target and buff. Pure over plain data so it is unit tested without a map or a Player.
// See docs/playerbots/feature-bot-group-buffs-20261008.md.

#include "Define.h"
#include <vector>

namespace BotBuff
{
    // Who a buff is meant for.
    enum class Targets : uint8
    {
        All,        // every class (Power Word: Fortitude, Mark of the Wild)
        ManaUsers,  // classes that run on mana (Arcane Intellect, Divine Spirit, Blessing of Wisdom)
        Melee       // warrior, paladin, hunter, rogue: classes that fight with melee or ranged attack power (Blessing of Might)
    };

    // `cls` is the Classes id of the target (1 warrior, 2 paladin, 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid).
    TC_GAME_API bool Applies(Targets t, uint8 cls);

    // Wait between two attempts of the same buff on the same target, whether the cast worked or not: a refused cast (no mana, out of line
    // of sight) is not repeated every tick, and a buff that was just cast does not wait for the aura to show up.
    class TC_GAME_API Throttle
    {
    public:
        bool Ready(uint64 targetKey, uint32 buffRoot, uint32 nowMs, uint32 waitMs) const;
        void Note(uint64 targetKey, uint32 buffRoot, uint32 nowMs);
        void Clear() { _v.clear(); }
        size_t Size() const { return _v.size(); }

    private:
        struct Rec { uint64 Key; uint32 Root; uint32 Ms; };
        std::vector<Rec> _v;      // bounded: the oldest record goes first
    };
}

#endif
