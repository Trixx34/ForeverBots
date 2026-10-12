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

#include "BotStartGear.h"
#include "BotConfigReload.h"
#include "BotGear.h"
#include "Config.h"
#include "DBCEnums.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace BotStartGear
{
namespace
{
struct Settings
{
    bool Enabled = true;        // Bot.StartGear.Enabled
    uint32 MaxQuality = 2;      // Bot.StartGear.MaxQuality: 0 poor, 1 common, 2 uncommon, 3 rare, 4 epic
    int32 LevelOffset = 0;      // Bot.StartGear.LevelOffset: required level of an item may be at most bot level + offset
    uint32 MinLevel = 1;        // Bot.StartGear.MinLevel: bots below this level get nothing
    uint32 LevelStep = 5;       // Bot.StartGear.LevelStep: another pass every N levels (0 = login only)
    bool AllowBop = false;      // Bot.StartGear.AllowBindOnPickup
    bool Armor = true;          // Bot.StartGear.Armor
    bool Weapons = true;        // Bot.StartGear.Weapons
    uint32 MaxItemLevel = 0;    // Bot.StartGear.MaxItemLevel: skip items above this item level (0 = no cap)
};

Settings s_cfg;
BotCfg::Flag s_once;

Settings const& Cfg()
{
    BotCfg::Load(s_once, []()
    {
        Settings s;
        s.Enabled = sConfigMgr->GetBoolDefault("Bot.StartGear.Enabled", true);
        s.MaxQuality = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.StartGear.MaxQuality", 2), 0, 4));
        s.LevelOffset = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.StartGear.LevelOffset", 0), -60, 10);
        s.MinLevel = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.StartGear.MinLevel", 1), 1, 60));
        s.LevelStep = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.StartGear.LevelStep", 5), 0, 60));
        s.AllowBop = sConfigMgr->GetBoolDefault("Bot.StartGear.AllowBindOnPickup", false);
        s.Armor = sConfigMgr->GetBoolDefault("Bot.StartGear.Armor", true);
        s.Weapons = sConfigMgr->GetBoolDefault("Bot.StartGear.Weapons", true);
        s.MaxItemLevel = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.StartGear.MaxItemLevel", 0), 0, 1000));
        s_cfg = s;
    });
    return s_cfg;
}

struct Cand
{
    uint32 Entry = 0;
    uint32 InvType = 0;
    uint32 ArmorSub = 0;      // armor subclass (cloth/leather/mail/plate) for body slots, 0 otherwise
    uint32 ReqLevel = 0;
    double Score = 0.0;
    ItemTemplate const* Proto = nullptr;
};

std::mutex s_mutex;
std::map<uint32, std::vector<Cand>> s_cache;     // key: class, level and the settings that shape the candidate list
bool s_haveAmmo = false;
bool s_ammoKnown = false;

std::string Lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool JunkName(std::string const& n)
{
    std::string const l = Lower(n);
    if (l.empty())
        return true;
    for (char const* bad : { "deprecated", "[ph]", "(old)", "monster -", "unused", "zzold", "(test)", "tim's", "<test", "placeholder" })
        if (l.find(bad) != std::string::npos)
            return true;
    return l.rfind("test", 0) == 0 || l.find(" test ") != std::string::npos || (l.size() > 5 && l.compare(l.size() - 5, 5, " test") == 0);
}

BotGear::ItemFacts FactsOf(ItemTemplate const* proto)
{
    BotGear::ItemFacts f;
    f.InvType = uint32(proto->GetInventoryType());
    f.ItemLevel = proto->GetBaseItemLevel();
    f.Armor = proto->IsArmor() ? proto->GetArmor(f.ItemLevel) : 0;
    f.Dps = proto->IsWeapon() ? proto->GetDPS(f.ItemLevel) : 0.0f;
    f.TwoHand = f.InvType == INVTYPE_2HWEAPON;
    f.RangedWeapon = proto->IsRangedWeapon();
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        if (proto->GetStatPercentEditor(i) > 0)
            f.Stats.emplace_back(proto->GetStatModifierBonusStat(i), proto->GetStatPercentEditor(i));
    return f;
}

bool AmmoWeapon(ItemTemplate const* proto)
{
    if (!proto->IsWeapon())
        return false;
    uint32 const sub = proto->GetSubClass();
    return sub == ITEM_SUBCLASS_WEAPON_BOW || sub == ITEM_SUBCLASS_WEAPON_GUN || sub == ITEM_SUBCLASS_WEAPON_CROSSBOW;
}

// Slots this module fills: armor pieces, back, neck and rings, and the three weapon slots. Shirt, tabard and trinkets are left alone.
bool HandledSlot(uint8 slot, Settings const& s)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD: case EQUIPMENT_SLOT_NECK: case EQUIPMENT_SLOT_SHOULDERS: case EQUIPMENT_SLOT_CHEST:
        case EQUIPMENT_SLOT_WAIST: case EQUIPMENT_SLOT_LEGS: case EQUIPMENT_SLOT_FEET: case EQUIPMENT_SLOT_WRISTS:
        case EQUIPMENT_SLOT_HANDS: case EQUIPMENT_SLOT_FINGER1: case EQUIPMENT_SLOT_FINGER2: case EQUIPMENT_SLOT_BACK:
            return s.Armor;
        case EQUIPMENT_SLOT_MAINHAND: case EQUIPMENT_SLOT_OFFHAND: case EQUIPMENT_SLOT_RANGED:
            return s.Weapons;
        default:
            return false;
    }
}

// Class/level prefilter, cached. Nothing here depends on the individual bot except race and proficiencies, which CanUseItem checks later.
std::vector<Cand> const& Candidates(uint8 classId, uint32 level, Settings const& s)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    uint32 const key = (uint32(classId) << 24) | (level << 16) | (s.MaxQuality << 12) | (uint32(s.AllowBop) << 11) | uint32(s.LevelOffset + 64);
    auto it = s_cache.find(key);
    if (it != s_cache.end())
        return it->second;

    if (!s_ammoKnown)
    {
        s_ammoKnown = true;
        for (auto const& pr : sObjectMgr->GetItemTemplateStore())
            if (pr.second.GetClass() == ITEM_CLASS_PROJECTILE)
            {
                s_haveAmmo = true;
                break;
            }
    }

    BotGear::Role const role = BotGear::RoleForClass(classId);
    int32 const maxReq = int32(level) + s.LevelOffset;
    std::vector<Cand> out;
    for (auto const& pr : sObjectMgr->GetItemTemplateStore())
    {
        ItemTemplate const& p = pr.second;
        if (!(p.IsArmor() || p.IsWeapon()))
            continue;
        if (BotGear::SlotsForInvType(uint32(p.GetInventoryType())).empty())
            continue;
        if (p.GetQuality() > s.MaxQuality || p.GetBaseRequiredLevel() > maxReq)
            continue;
        if (s.MaxItemLevel && p.GetBaseItemLevel() > s.MaxItemLevel)
            continue;
        if (p.GetBaseItemLevel() == 0 || p.GetRequiredSkill() || p.GetRequiredSpell() || p.GetRequiredReputationFaction() || p.GetStartQuest() ||
            p.GetDuration() || p.GetMap() || p.GetArea(0) || p.GetItemSet() || p.GetPlayerLevelToItemLevelCurveId() || p.GetScalingStatContentTuning() ||
            p.GetHolidayID() || p.GetItemLimitCategory())
            continue;
        ItemBondingType const bond = p.GetBonding();
        if (bond != BIND_NONE && bond != BIND_ON_EQUIP && !(s.AllowBop && bond == BIND_ON_ACQUIRE))
            continue;
        int32 const allowed = p.GetAllowableClass();
        if (allowed > 0 && !(allowed & (1 << (classId - 1))))
            continue;
        if (p.IsArmor())
        {
            uint32 const sub = p.GetSubClass();
            if (sub == ITEM_SUBCLASS_ARMOR_COSMETIC || sub == ITEM_SUBCLASS_ARMOR_LIBRAM || sub == ITEM_SUBCLASS_ARMOR_IDOL || sub == ITEM_SUBCLASS_ARMOR_TOTEM ||
                sub == ITEM_SUBCLASS_ARMOR_SIGIL || sub == ITEM_SUBCLASS_ARMOR_RELIC)
                continue;
        }
        else
        {
            uint32 const sub = p.GetSubClass();
            if (sub == ITEM_SUBCLASS_WEAPON_FISHING_POLE || sub == ITEM_SUBCLASS_WEAPON_MISCELLANEOUS || sub == ITEM_SUBCLASS_WEAPON_EXOTIC ||
                sub == ITEM_SUBCLASS_WEAPON_EXOTIC2)
                continue;
            if (AmmoWeapon(&p) && !s_haveAmmo)
                continue;   // a bow or gun with no ammo item in the data is dead weight
        }
        char const* const nm = p.GetName(LOCALE_enUS);
        if (JunkName(nm ? nm : ""))
            continue;
        Cand c;
        c.Entry = p.GetId();
        c.InvType = uint32(p.GetInventoryType());
        c.ReqLevel = uint32(std::max(0, p.GetBaseRequiredLevel()));
        c.Proto = &p;
        c.Score = BotGear::Score(role, FactsOf(&p));
        if (c.Score <= 0.0)
            continue;
        if (p.IsArmor())
        {
            uint32 const sub = p.GetSubClass();
            // Only the body slots have an armor type that matters. A cloak is "cloth" in the data but every class wears it,
            // so tagging it would lock cloaks out for anyone whose best body armor is leather, mail or plate.
            bool const bodySlot = c.InvType == INVTYPE_HEAD || c.InvType == INVTYPE_SHOULDERS || c.InvType == INVTYPE_CHEST ||
                c.InvType == INVTYPE_ROBE || c.InvType == INVTYPE_WAIST || c.InvType == INVTYPE_LEGS || c.InvType == INVTYPE_FEET ||
                c.InvType == INVTYPE_WRISTS || c.InvType == INVTYPE_HANDS;
            if (bodySlot && sub >= ITEM_SUBCLASS_ARMOR_CLOTH && sub <= ITEM_SUBCLASS_ARMOR_PLATE)
                c.ArmorSub = sub;
        }
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](Cand const& a, Cand const& b) { return a.Score != b.Score ? a.Score > b.Score : a.Entry < b.Entry; });
    return s_cache.emplace(key, std::move(out)).first->second;
}

// Dry run of the real equip check for this slot: class, race, level, armor and weapon proficiency, dual wield, unique limits.
// Player::CanUseItem(ItemTemplate) alone does not look at proficiencies, so it passes items the bot can never wear.
bool UsableAt(Player* bot, Cand const& c, uint8 slot)
{
    uint16 dest = 0;
    return bot->CanEquipNewItem(slot, dest, c.Entry, false) == EQUIP_ERR_OK;
}

bool FitsSlot(Cand const& c, uint8 slot)
{
    std::vector<uint8> const slots = BotGear::SlotsForInvType(c.InvType);
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

bool Equip(Player* bot, Cand const& c, uint8 slot, Result& res)
{
    uint16 dest = 0;
    if (bot->CanEquipNewItem(slot, dest, c.Entry, false) != EQUIP_ERR_OK)
        return false;
    Item* it = bot->EquipNewItem(dest, c.Entry, ItemContext::NONE, true);
    if (!it)
        return false;
    Granted g;
    g.Slot = slot;
    g.Entry = c.Entry;
    g.ReqLevel = c.ReqLevel;
    g.ItemLevel = c.Proto->GetBaseItemLevel();
    g.Quality = c.Proto->GetQuality();
    char const* n = c.Proto->GetName(LOCALE_enUS);
    g.Name = n ? n : "";
    res.Items.push_back(std::move(g));
    return true;
}

bool Empty(Player* bot, uint8 slot)
{
    return !bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
}
}

bool Enabled() { return Cfg().Enabled; }
uint32 LevelStep() { return Cfg().LevelStep; }
uint32 MinLevel() { return Cfg().MinLevel; }

Result Grant(Player* bot)
{
    Result res;
    Settings const& s = Cfg();
    if (!s.Enabled || !bot || !bot->IsInWorld() || bot->GetLevel() < s.MinLevel)
        return res;
    res.Ran = true;
    static uint8 const slots[] = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
        EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED };
    for (uint8 slot : slots)
        if (HandledSlot(slot, s) && Empty(bot, slot))
            ++res.EmptyBefore;
    if (!res.EmptyBefore)
        return res;

    std::vector<Cand> const& cands = Candidates(bot->GetClass(), bot->GetLevel(), s);
    res.Candidates = uint32(cands.size());

    // Best armor type: the highest cloth/leather/mail/plate class the bot can actually wear (equip dry run), found by testing the
    // first candidate of each type in the slot it fits.
    uint32 bestArmorSub = 0;
    if (s.Armor)
    {
        bool tested[ITEM_SUBCLASS_ARMOR_PLATE + 1] = {};
        for (Cand const& c : cands)
        {
            if (!c.ArmorSub || c.ArmorSub <= bestArmorSub || tested[c.ArmorSub])
                continue;
            tested[c.ArmorSub] = true;
            for (uint8 slot : BotGear::SlotsForInvType(c.InvType))
                if (UsableAt(bot, c, slot))
                {
                    bestArmorSub = c.ArmorSub;
                    break;
                }
        }
    }

    if (s.Armor)
    {
        for (uint8 slot : slots)
        {
            if (slot >= EQUIPMENT_SLOT_MAINHAND || !Empty(bot, slot))
                continue;
            // Pass 0: the best armor type. Pass 1: any wearable piece (a type below the best, when the best type has nothing for this slot).
            for (int pass = 0; pass < 2 && Empty(bot, slot); ++pass)
                for (Cand const& c : cands)
                {
                    if (!c.Proto->IsArmor() || !FitsSlot(c, slot))
                        continue;
                    if (pass == 0 && c.ArmorSub && c.ArmorSub != bestArmorSub)
                        continue;
                    if (Equip(bot, c, slot, res))
                        break;
                }
        }
    }

    if (s.Weapons)
    {
        bool const mhEmpty = Empty(bot, EQUIPMENT_SLOT_MAINHAND);
        bool const ohEmpty = Empty(bot, EQUIPMENT_SLOT_OFFHAND);
        Item* mhWorn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        bool const twoHandWorn = mhWorn && mhWorn->GetTemplate()->GetInventoryType() == INVTYPE_2HWEAPON;
        Cand const* best1 = nullptr;     // one-hand main hand
        Cand const* best2 = nullptr;     // two-hander
        Cand const* bestOh = nullptr;    // off hand (shield, holdable, off-hand weapon, or a second one-hander)
        for (Cand const& c : cands)
        {
            if (c.Proto->IsArmor())
            {
                if (c.Proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_SHIELD || c.InvType == INVTYPE_HOLDABLE)
                    if (!bestOh && ohEmpty && !twoHandWorn && UsableAt(bot, c, EQUIPMENT_SLOT_OFFHAND))
                        bestOh = &c;
                continue;
            }
            if (!c.Proto->IsWeapon())
                continue;
            if (c.InvType == INVTYPE_2HWEAPON)
            {
                if (!best2 && mhEmpty && UsableAt(bot, c, EQUIPMENT_SLOT_MAINHAND))
                    best2 = &c;
            }
            else if (c.InvType == INVTYPE_WEAPON || c.InvType == INVTYPE_WEAPONMAINHAND)
            {
                if (!best1 && mhEmpty && UsableAt(bot, c, EQUIPMENT_SLOT_MAINHAND))
                    best1 = &c;
                if (c.InvType == INVTYPE_WEAPON && !bestOh && ohEmpty && !twoHandWorn && bot->CanDualWield() && UsableAt(bot, c, EQUIPMENT_SLOT_OFFHAND))
                    bestOh = &c;
            }
            else if (c.InvType == INVTYPE_WEAPONOFFHAND)
            {
                if (!bestOh && ohEmpty && !twoHandWorn && bot->CanDualWield() && UsableAt(bot, c, EQUIPMENT_SLOT_OFFHAND))
                    bestOh = &c;
            }
        }
        if (mhEmpty)
        {
            double const s1 = (best1 ? best1->Score : 0.0) + (bestOh ? bestOh->Score : 0.0);
            double const s2 = best2 ? best2->Score : 0.0;
            bool two = s2 > s1 && ohEmpty;   // a worn off hand stays: do not push it out for a two-hander
            Cand const* mh = two ? best2 : best1;
            if (!mh && ohEmpty)
                mh = best2;
            bool const mhDone = mh && Equip(bot, *mh, EQUIPMENT_SLOT_MAINHAND, res);
            // the off hand does not depend on the main hand pick: a bot that kept its starting weapon still gets its shield
            bool const twoNow = mhDone && mh->InvType == INVTYPE_2HWEAPON;
            if (ohEmpty && bestOh && !twoNow && !(mhDone && bestOh->Entry == mh->Entry))
                Equip(bot, *bestOh, EQUIPMENT_SLOT_OFFHAND, res);
        }
        else if (ohEmpty && bestOh && !twoHandWorn)
            Equip(bot, *bestOh, EQUIPMENT_SLOT_OFFHAND, res);

        if (Empty(bot, EQUIPMENT_SLOT_RANGED))
            for (Cand const& c : cands)
            {
                if (!c.Proto->IsWeapon() || !FitsSlot(c, EQUIPMENT_SLOT_RANGED))
                    continue;
                if (Equip(bot, c, EQUIPMENT_SLOT_RANGED, res))
                    break;
            }
    }
    return res;
}
}
