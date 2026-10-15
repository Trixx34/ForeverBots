#include "tc_catch2.h"
#include "BotLootRollRules.h"

using namespace BotLootRollRules;

TEST_CASE("Items compete for the slots of their inventory type", "[BotLootRollRules]")
{
    CHECK(SlotsFor(INVTYPE_HEAD).Slots[0] == Slot::Head);
    CHECK(SlotsFor(INVTYPE_ROBE).Slots[0] == Slot::Chest);
    CHECK(SlotsFor(INVTYPE_CLOAK).Slots[0] == Slot::Back);
    CHECK(SlotsFor(INVTYPE_WEAPON).Slots[0] == Slot::MainHand);
    CHECK(SlotsFor(INVTYPE_SHIELD).Slots[0] == Slot::OffHand);
    CHECK(SlotsFor(INVTYPE_THROWN).Slots[0] == Slot::Ranged);

    SlotSet const rings = SlotsFor(INVTYPE_FINGER);
    CHECK(rings.Slots[0] == Slot::Finger1);
    CHECK(rings.Slots[1] == Slot::Finger2);
    SlotSet const trinkets = SlotsFor(INVTYPE_TRINKET);
    CHECK(trinkets.Slots[0] == Slot::Trinket1);
    CHECK(trinkets.Slots[1] == Slot::Trinket2);

    SlotSet const twoHand = SlotsFor(INVTYPE_2HWEAPON);
    CHECK(twoHand.Slots[0] == Slot::MainHand);
    CHECK(twoHand.TwoHand);
    CHECK_FALSE(SlotsFor(INVTYPE_WEAPON).TwoHand);
}

TEST_CASE("A type that is not worn has no slot", "[BotLootRollRules]")
{
    CHECK(SlotsFor(INVTYPE_NON_EQUIP).Slots[0] == Slot::None);
    CHECK(SlotsFor(INVTYPE_BAG).Slots[0] == Slot::None);
    CHECK_FALSE(BeatsWorn(SlotsFor(INVTYPE_NON_EQUIP), 100, 0, 0, false));
}

TEST_CASE("An upgrade must beat the item level worn", "[BotLootRollRules]")
{
    SlotSet const head = SlotsFor(INVTYPE_HEAD);
    CHECK(BeatsWorn(head, 20, 0, 0, false));        // empty slot
    CHECK(BeatsWorn(head, 21, 20, 0, false));
    CHECK_FALSE(BeatsWorn(head, 20, 20, 0, false)); // equal is not an upgrade
    CHECK_FALSE(BeatsWorn(head, 19, 20, 0, false));
}

TEST_CASE("For two-slot items the worse worn item decides", "[BotLootRollRules]")
{
    SlotSet const rings = SlotsFor(INVTYPE_FINGER);
    CHECK(BeatsWorn(rings, 15, 30, 10, false));     // it beats the worse ring
    CHECK_FALSE(BeatsWorn(rings, 10, 30, 10, false));
    CHECK(BeatsWorn(rings, 1, 30, 0, false));       // one ring slot is empty
}

TEST_CASE("A two-hander is no automatic upgrade while an off hand is worn", "[BotLootRollRules]")
{
    SlotSet const twoHand = SlotsFor(INVTYPE_2HWEAPON);
    CHECK(BeatsWorn(twoHand, 30, 20, 0, false));
    CHECK_FALSE(BeatsWorn(twoHand, 30, 20, 0, true));
    CHECK(BeatsWorn(SlotsFor(INVTYPE_WEAPON), 30, 20, 0, true)); // a one-hander keeps the off hand
}
