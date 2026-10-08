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

#include "BotGear.h"

namespace BotGear
{
namespace
{
    struct Weights
    {
        double Str, Agi, Int, Spi, Sta, Ap, Rap, SpellPower, Regen, Rating, Armor, Dps;
    };

    //                                  Str  Agi  Int  Spi  Sta  AP   RAP  SP   Regen Rating Armor  Dps
    constexpr Weights WMeleeStr   = { 1.0, 0.5, 0.0, 0.0, 0.5, 0.5, 0.0, 0.0, 0.0,  0.4,  0.010, 8.0 };
    constexpr Weights WMeleeAgi   = { 0.7, 1.0, 0.0, 0.0, 0.4, 0.5, 0.0, 0.0, 0.0,  0.4,  0.006, 8.0 };
    constexpr Weights WRanged     = { 0.1, 1.0, 0.3, 0.0, 0.4, 0.4, 0.5, 0.0, 0.0,  0.4,  0.006, 8.0 };
    constexpr Weights WCaster     = { 0.0, 0.0, 1.0, 0.5, 0.3, 0.0, 0.0, 1.5, 0.5,  0.4,  0.003, 1.0 };

    Weights const& For(Role r)
    {
        switch (r)
        {
            case Role::MeleeAgi: return WMeleeAgi;
            case Role::Ranged: return WRanged;
            case Role::Caster: return WCaster;
            default: return WMeleeStr;
        }
    }

    double StatWeight(Weights const& w, int32 stat)
    {
        switch (stat)
        {
            case STAT_AGILITY: return w.Agi;
            case STAT_STRENGTH: return w.Str;
            case STAT_INTELLECT: return w.Int;
            case STAT_SPIRIT: return w.Spi;
            case STAT_STAMINA: return w.Sta;
            case STAT_ATTACK_POWER: return w.Ap;
            case STAT_RANGED_ATTACK_POWER: return w.Rap;
            case STAT_SPELL_HEALING:
            case STAT_SPELL_DAMAGE:
            case STAT_SPELL_POWER: return w.SpellPower;
            case STAT_MANA_REGEN: return w.Regen;
            default: break;
        }
        // hit, crit, haste and other ratings (ItemModType 12..37 and 44..) count a little for everyone
        if ((stat >= 12 && stat <= 37) || stat == 44)
            return w.Rating;
        return 0.0;
    }
}

Role RoleForClass(uint8 classId)
{
    switch (classId)
    {
        case 3: return Role::Ranged;                    // hunter
        case 4: case 10: case 11: return Role::MeleeAgi; // rogue, monk, druid (feral)
        case 5: case 8: case 9: return Role::Caster;    // priest, mage, warlock
        default: return Role::MeleeStr;                 // warrior, paladin, death knight, shaman
    }
}

double Score(Role role, ItemFacts const& item)
{
    Weights const& w = For(role);
    double const budget = double(item.ItemLevel);
    double score = 0.0;
    for (auto const& [stat, pct] : item.Stats)
        if (pct > 0)
            score += StatWeight(w, stat) * (double(pct) / 10000.0) * budget;
    score += w.Armor * double(item.Armor);
    if (item.Dps > 0.0f)
    {
        double dpsWeight = w.Dps;
        // a ranged weapon is dead weight to melee classes and a wand is the only ranged slot a caster has
        if (item.RangedWeapon && role != Role::Ranged && role != Role::Caster)
            dpsWeight = 0.0;
        else if (item.RangedWeapon && role == Role::Caster)
            dpsWeight = 2.0;
        score += dpsWeight * double(item.Dps);
    }
    return score;
}

bool IsUpgrade(double candidate, double worn)
{
    if (candidate <= 0.0)
        return false;
    if (worn <= 0.0)
        return true;
    return candidate > worn * 1.10 + 0.5;
}

std::vector<uint8> SlotsForInvType(uint32 invType)
{
    switch (invType)
    {
        case 1: return { 0 };               // head
        case 2: return { 1 };               // neck
        case 3: return { 2 };               // shoulders
        case 5: case 20: return { 4 };      // chest, robe
        case 6: return { 5 };               // waist
        case 7: return { 6 };               // legs
        case 8: return { 7 };               // feet
        case 9: return { 8 };               // wrists
        case 10: return { 9 };              // hands
        case 11: return { 10, 11 };         // finger
        case 12: return { 12, 13 };         // trinket
        case 13: case 21: case 17: return { 15 };   // one-hand, main hand, two-hand: main hand only (no dual wield in v1)
        case 14: case 22: case 23: return { 16 };   // shield, off hand, holdable
        case 15: case 25: case 26: return { 17 };   // ranged, thrown, ranged right (wand)
        case 16: return { 14 };             // cloak
        default: return {};
    }
}
}
