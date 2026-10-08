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

#ifndef TRINITY_BOT_GEAR_H
#define TRINITY_BOT_GEAR_H

// Gear scoring for the bot quest pipeline (quest reward choice, equip checks). Pure functions over plain item facts, so the
// scoring and the upgrade rule can be unit tested without a database. BotQuest.cpp builds ItemFacts from an ItemTemplate.
//
// The score is a class-role weighted sum: stat points (percent editor x item level, the same budget the core uses), armor,
// and weapon DPS. Absolute values mean nothing; only comparisons between two items for the same role do.

#include "Define.h"
#include <utility>
#include <vector>

namespace BotGear
{
    enum class Role : uint8
    {
        MeleeStr,   // warrior, paladin, shaman, death knight
        MeleeAgi,   // rogue, monk, druid
        Ranged,     // hunter
        Caster      // priest, mage, warlock
    };

    // Class ids as in SharedDefines.h (CLASS_WARRIOR = 1 ...). Unknown classes fall back to MeleeStr.
    TC_GAME_API Role RoleForClass(uint8 classId);

    // Stat type ids mirror ItemModType (ItemTemplate.h)
    enum StatId : int32
    {
        STAT_AGILITY = 3, STAT_STRENGTH = 4, STAT_INTELLECT = 5, STAT_SPIRIT = 6, STAT_STAMINA = 7,
        STAT_ATTACK_POWER = 38, STAT_RANGED_ATTACK_POWER = 39, STAT_SPELL_HEALING = 41, STAT_SPELL_DAMAGE = 42,
        STAT_MANA_REGEN = 43, STAT_SPELL_POWER = 45
    };

    struct ItemFacts
    {
        uint32 InvType = 0;          // InventoryType
        uint32 ItemLevel = 0;
        uint32 Armor = 0;
        float Dps = 0.0f;            // weapons only
        bool TwoHand = false;
        bool RangedWeapon = false;   // bow, gun, crossbow, thrown, wand
        std::vector<std::pair<int32, int32>> Stats;   // (stat type, percent editor); zero-value rows are skipped
    };

    TC_GAME_API double Score(Role role, ItemFacts const& item);

    // Hysteresis so a bot does not swap back and forth on near-equal items: a clear upgrade beats the worn item by 10 percent
    // plus a small floor. An empty slot (worn score 0) takes anything with a positive score.
    TC_GAME_API bool IsUpgrade(double candidate, double worn);

    // Equipment slots (EQUIPMENT_SLOT_*) an inventory type can go to, preferred order. Empty = not equippable gear.
    TC_GAME_API std::vector<uint8> SlotsForInvType(uint32 invType);
}

#endif
