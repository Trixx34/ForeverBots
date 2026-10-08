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

// Bot group loot rolls, see BotLootRoll.h.

#include "BotLootRoll.h"
#include "BotLootRollPlan.h"
#include "BotQuest.h"
#include "Config.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "WorldSession.h"
#include <mutex>

namespace BotLootRoll
{
namespace
{
struct Settings
{
    bool Enabled = false;     // Bot.Loot.Roll.Enabled
    Config Plan;
};
Settings s_cfg;
std::once_flag s_cfgOnce;

Settings const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.Loot.Roll.Enabled", false);
        s_cfg.Plan.GreedOnOther = sConfigMgr->GetBoolDefault("Bot.Loot.Roll.GreedOnOther", true);
    });
    return s_cfg;
}
}

int32 ChooseVote(Player* bot, ItemTemplate const* item, bool needAllowed)
{
    if (!Cfg().Enabled || !bot || !item || !bot->GetSession() || !bot->GetSession()->IsBot())
        return -1;
    Facts f;
    f.NeedAllowed = needAllowed;
    f.QuestItem = bot->HasQuestForItem(item->GetId());
    f.Usable = bot->CanUseItem(item) == EQUIP_ERR_OK;
    f.Upgrade = f.Usable && BotQuest::IsGearUpgrade(bot, item);
    ItemPosCountVec dest;
    f.BagFull = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->GetId(), 1) != EQUIP_ERR_OK;
    switch (Decide(f, Cfg().Plan))
    {
        case Vote::Need:  return 1;
        case Vote::Greed: return 2;
        case Vote::Pass:  return 0;
    }
    return 0;
}
}
