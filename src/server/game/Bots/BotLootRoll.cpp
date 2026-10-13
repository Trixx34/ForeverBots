/*
 * Group loot rolls for bots. See BotLootRoll.h.
 */

#include "BotLootRoll.h"
#include "Config.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Loot.h"
#include "Player.h"
#include "WorldSession.h"
#include <algorithm>

namespace
{
// The equipment slots an item of this inventory type competes for; 0 slots for a type that is not worn.
struct SlotSet
{
    uint8 Slots[2] = { NULL_SLOT, NULL_SLOT };
    bool TwoHand = false;
};

SlotSet SlotsFor(InventoryType type)
{
    SlotSet set;
    switch (type)
    {
        case INVTYPE_HEAD: set.Slots[0] = EQUIPMENT_SLOT_HEAD; break;
        case INVTYPE_NECK: set.Slots[0] = EQUIPMENT_SLOT_NECK; break;
        case INVTYPE_SHOULDERS: set.Slots[0] = EQUIPMENT_SLOT_SHOULDERS; break;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: set.Slots[0] = EQUIPMENT_SLOT_CHEST; break;
        case INVTYPE_WAIST: set.Slots[0] = EQUIPMENT_SLOT_WAIST; break;
        case INVTYPE_LEGS: set.Slots[0] = EQUIPMENT_SLOT_LEGS; break;
        case INVTYPE_FEET: set.Slots[0] = EQUIPMENT_SLOT_FEET; break;
        case INVTYPE_WRISTS: set.Slots[0] = EQUIPMENT_SLOT_WRISTS; break;
        case INVTYPE_HANDS: set.Slots[0] = EQUIPMENT_SLOT_HANDS; break;
        case INVTYPE_FINGER: set.Slots[0] = EQUIPMENT_SLOT_FINGER1; set.Slots[1] = EQUIPMENT_SLOT_FINGER2; break;
        case INVTYPE_TRINKET: set.Slots[0] = EQUIPMENT_SLOT_TRINKET1; set.Slots[1] = EQUIPMENT_SLOT_TRINKET2; break;
        case INVTYPE_CLOAK: set.Slots[0] = EQUIPMENT_SLOT_BACK; break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND: set.Slots[0] = EQUIPMENT_SLOT_MAINHAND; break;
        case INVTYPE_2HWEAPON: set.Slots[0] = EQUIPMENT_SLOT_MAINHAND; set.TwoHand = true; break;
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE: set.Slots[0] = EQUIPMENT_SLOT_OFFHAND; break;
        case INVTYPE_RANGED:
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT: set.Slots[0] = EQUIPMENT_SLOT_RANGED; break;
        default: break;
    }
    return set;
}

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

    SlotSet const slots = SlotsFor(InventoryType(item->GetInventoryType()));
    if (slots.Slots[0] == NULL_SLOT)
        return false;

    uint32 worn = WornItemLevel(player, slots.Slots[0]);
    if (slots.Slots[1] != NULL_SLOT)
        worn = std::min(worn, WornItemLevel(player, slots.Slots[1]));
    if (slots.TwoHand && player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        return false; // a two-hander would cost the off hand: not an automatic upgrade
    return item->GetBaseItemLevel() > worn;
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
