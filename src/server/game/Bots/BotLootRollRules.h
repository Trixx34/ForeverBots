/*
 * Pure rules of the bot loot-roll vote (see BotLootRoll.h): which equipment slots an item competes for, and whether its item level beats
 * what is worn. No player or item objects, so they can be tested alone.
 */

#ifndef TRINITY_BOT_LOOT_ROLL_RULES_H
#define TRINITY_BOT_LOOT_ROLL_RULES_H

#include "Define.h"
#include "ItemTemplate.h"
#include <algorithm>

namespace BotLootRollRules
{
// Equipment slot numbers, the same as EQUIPMENT_SLOT_* in Player.h and NULL_SLOT in Unit.h (BotLootRoll.cpp asserts it). They are repeated
// here so these rules, and their tests, need no player headers.
namespace Slot
{
    inline constexpr uint8 None = 255, Head = 0, Neck = 1, Shoulders = 2, Chest = 4, Waist = 5, Legs = 6, Feet = 7, Wrists = 8, Hands = 9,
        Finger1 = 10, Finger2 = 11, Trinket1 = 12, Trinket2 = 13, Back = 14, MainHand = 15, OffHand = 16, Ranged = 17;
}

// The equipment slots an item of this inventory type competes for; Slot::None in the first place for a type that is not worn.
struct SlotSet
{
    uint8 Slots[2] = { Slot::None, Slot::None };
    bool TwoHand = false;
};

inline SlotSet SlotsFor(InventoryType type)
{
    SlotSet set;
    switch (type)
    {
        case INVTYPE_HEAD: set.Slots[0] = Slot::Head; break;
        case INVTYPE_NECK: set.Slots[0] = Slot::Neck; break;
        case INVTYPE_SHOULDERS: set.Slots[0] = Slot::Shoulders; break;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: set.Slots[0] = Slot::Chest; break;
        case INVTYPE_WAIST: set.Slots[0] = Slot::Waist; break;
        case INVTYPE_LEGS: set.Slots[0] = Slot::Legs; break;
        case INVTYPE_FEET: set.Slots[0] = Slot::Feet; break;
        case INVTYPE_WRISTS: set.Slots[0] = Slot::Wrists; break;
        case INVTYPE_HANDS: set.Slots[0] = Slot::Hands; break;
        case INVTYPE_FINGER: set.Slots[0] = Slot::Finger1; set.Slots[1] = Slot::Finger2; break;
        case INVTYPE_TRINKET: set.Slots[0] = Slot::Trinket1; set.Slots[1] = Slot::Trinket2; break;
        case INVTYPE_CLOAK: set.Slots[0] = Slot::Back; break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND: set.Slots[0] = Slot::MainHand; break;
        case INVTYPE_2HWEAPON: set.Slots[0] = Slot::MainHand; set.TwoHand = true; break;
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE: set.Slots[0] = Slot::OffHand; break;
        case INVTYPE_RANGED:
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT: set.Slots[0] = Slot::Ranged; break;
        default: break;
    }
    return set;
}

// Whether an item of `itemLevel` is an upgrade for slots that now hold `worn0` and `worn1` (0 = empty; worn1 only counts for a two-slot
// item, where the worse of the two decides). A two-hander is never an automatic upgrade while an off hand is worn.
inline bool BeatsWorn(SlotSet const& slots, uint32 itemLevel, uint32 worn0, uint32 worn1, bool offHandWorn)
{
    if (slots.Slots[0] == Slot::None)
        return false;
    uint32 worn = worn0;
    if (slots.Slots[1] != Slot::None)
        worn = std::min(worn, worn1);
    if (slots.TwoHand && offHandWorn)
        return false;
    return itemLevel > worn;
}
}

#endif
