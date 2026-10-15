/*
 * Group loot rolls for bots. See BotLootRoll.h.
 */

#include "BotLootRoll.h"
#include "BotLootRollRules.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Loot.h"
#include "Player.h"
#include "WorldSession.h"
#include <algorithm>

// The slot numbers of the pure rules are copies: they must stay equal to the real ones.
static_assert(BotLootRollRules::Slot::None == NULL_SLOT && BotLootRollRules::Slot::Head == EQUIPMENT_SLOT_HEAD &&
    BotLootRollRules::Slot::Neck == EQUIPMENT_SLOT_NECK && BotLootRollRules::Slot::Shoulders == EQUIPMENT_SLOT_SHOULDERS &&
    BotLootRollRules::Slot::Chest == EQUIPMENT_SLOT_CHEST && BotLootRollRules::Slot::Waist == EQUIPMENT_SLOT_WAIST &&
    BotLootRollRules::Slot::Legs == EQUIPMENT_SLOT_LEGS && BotLootRollRules::Slot::Feet == EQUIPMENT_SLOT_FEET &&
    BotLootRollRules::Slot::Wrists == EQUIPMENT_SLOT_WRISTS && BotLootRollRules::Slot::Hands == EQUIPMENT_SLOT_HANDS &&
    BotLootRollRules::Slot::Finger1 == EQUIPMENT_SLOT_FINGER1 && BotLootRollRules::Slot::Finger2 == EQUIPMENT_SLOT_FINGER2 &&
    BotLootRollRules::Slot::Trinket1 == EQUIPMENT_SLOT_TRINKET1 && BotLootRollRules::Slot::Trinket2 == EQUIPMENT_SLOT_TRINKET2 &&
    BotLootRollRules::Slot::Back == EQUIPMENT_SLOT_BACK && BotLootRollRules::Slot::MainHand == EQUIPMENT_SLOT_MAINHAND &&
    BotLootRollRules::Slot::OffHand == EQUIPMENT_SLOT_OFFHAND && BotLootRollRules::Slot::Ranged == EQUIPMENT_SLOT_RANGED,
    "BotLootRollRules::Slot is out of step with EQUIPMENT_SLOT_*");

namespace
{
// Item level of what the player wears in a slot (0 for an empty slot).
uint32 WornItemLevel(Player const* player, uint8 slot)
{
    Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    return item && item->GetTemplate() ? item->GetTemplate()->GetBaseItemLevel() : 0;
}

// A usable upgrade: the player can use and equip it at its level, and its item level beats what it wears (the worse of two ring or trinket slots).
bool IsUpgrade(Player* player, ItemTemplate const* item)
{
    if (item->GetClass() != ITEM_CLASS_WEAPON && item->GetClass() != ITEM_CLASS_ARMOR)
        return false;
    if (item->GetBaseRequiredLevel() > int32(player->GetLevel()) || player->CanUseItem(item) != EQUIP_ERR_OK)
        return false;

    BotLootRollRules::SlotSet const slots = BotLootRollRules::SlotsFor(InventoryType(item->GetInventoryType()));
    if (slots.Slots[0] == BotLootRollRules::Slot::None)
        return false;

    uint32 const worn0 = WornItemLevel(player, slots.Slots[0]);
    uint32 const worn1 = slots.Slots[1] != BotLootRollRules::Slot::None ? WornItemLevel(player, slots.Slots[1]) : 0;
    bool const offHandWorn = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) != nullptr;
    return BotLootRollRules::BeatsWorn(slots, item->GetBaseItemLevel(), worn0, worn1, offHandWorn);
}
}

Optional<RollVote> BotLootRoll::ChooseVote(Player* player, ItemTemplate const* item, uint32 voteMask)
{
    static bool const enabled = sConfigMgr->GetBoolDefault("Bot.Loot.Roll.Enabled", true);
    static bool const greedOnOther = sConfigMgr->GetBoolDefault("Bot.Loot.Roll.GreedOnOther", false);
    if (!enabled || !player || !item || !player->GetSession() || !player->GetSession()->IsBot())
        return {};

    ItemPosCountVec destination;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item->GetId(), 1) != EQUIP_ERR_OK)
        return RollVote::Pass; // no room

    if ((voteMask & ROLL_FLAG_TYPE_NEED) && IsUpgrade(player, item))
        return RollVote::Need;
    if (greedOnOther && (voteMask & ROLL_FLAG_TYPE_GREED))
        return RollVote::Greed;
    return RollVote::Pass;
}
