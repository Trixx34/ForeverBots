/*
 * Pure gear rules shared by the bot code and its tests: no player, item or map objects, only ids and numbers.
 */

#ifndef TRINITY_BOT_GEAR_RULES_H
#define TRINITY_BOT_GEAR_RULES_H

#include "Define.h"
#include "ItemTemplate.h"
#include "SharedDefines.h"

namespace BotGearRules
{
// The heaviest armour a class wears at a level (ITEM_SUBCLASS_ARMOR_*): cloth, leather, mail or plate. Lighter armour is allowed too.
inline uint32 MaxArmorSubclass(uint8 classId, uint32 level)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN: return level >= 40 ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
        case CLASS_HUNTER:
        case CLASS_SHAMAN: return level >= 40 ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
        case CLASS_ROGUE:
        case CLASS_DRUID: return ITEM_SUBCLASS_ARMOR_LEATHER;
        default: return ITEM_SUBCLASS_ARMOR_CLOTH; // priest, mage, warlock
    }
}

// True for armour in the body slots that is heavier than the class wears. Jewellery, cloaks and shields are not judged here.
inline bool ArmorTooHeavy(uint8 classId, uint32 level, uint32 itemClass, uint32 subclass, uint32 inventoryType)
{
    if (itemClass != ITEM_CLASS_ARMOR)
        return false;
    switch (inventoryType)
    {
        case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE: case INVTYPE_WAIST: case INVTYPE_LEGS:
        case INVTYPE_FEET: case INVTYPE_WRISTS: case INVTYPE_HANDS:
            return subclass > ITEM_SUBCLASS_ARMOR_MISCELLANEOUS && subclass <= ITEM_SUBCLASS_ARMOR_PLATE && subclass > MaxArmorSubclass(classId, level);
        default: return false;
    }
}
}

#endif
