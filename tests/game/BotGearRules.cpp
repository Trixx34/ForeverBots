#include "tc_catch2.h"
#include "BotGearRules.h"

TEST_CASE("Armour a class wears follows the level", "[BotGearRules]")
{
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_WARRIOR, 39) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_WARRIOR, 40) == ITEM_SUBCLASS_ARMOR_PLATE);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_PALADIN, 40) == ITEM_SUBCLASS_ARMOR_PLATE);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_HUNTER, 39) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_SHAMAN, 40) == ITEM_SUBCLASS_ARMOR_MAIL);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_ROGUE, 60) == ITEM_SUBCLASS_ARMOR_LEATHER);
    CHECK(BotGearRules::MaxArmorSubclass(CLASS_DRUID, 60) == ITEM_SUBCLASS_ARMOR_LEATHER);
    for (uint8 classId : { uint8(CLASS_PRIEST), uint8(CLASS_MAGE), uint8(CLASS_WARLOCK) })
        CHECK(BotGearRules::MaxArmorSubclass(classId, 60) == ITEM_SUBCLASS_ARMOR_CLOTH);
}

TEST_CASE("Armour heavier than the class wears is refused in the body slots", "[BotGearRules]")
{
    // a mage never wears leather, mail or plate chest pieces
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_CHEST));
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER, INVTYPE_CHEST));
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_HEAD));
    // a warrior before 40 refuses plate, after 40 wears it; lighter armour is always fine
    CHECK(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 39, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_LEGS));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 40, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_LEGS));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_WARRIOR, 10, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_CLOTH, INVTYPE_LEGS));
}

TEST_CASE("Only body-slot armour is judged by weight", "[BotGearRules]")
{
    // jewellery, cloaks and shields carry armour subclasses of their own and are not judged
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_CLOAK));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_SHIELD));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_FINGER));
    // the miscellaneous subclass and anything that is not armour are never too heavy
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MISCELLANEOUS, INVTYPE_CHEST));
    CHECK_FALSE(BotGearRules::ArmorTooHeavy(CLASS_MAGE, 60, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_ARMOR_PLATE, INVTYPE_CHEST));
}
