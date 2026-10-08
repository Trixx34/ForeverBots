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

#ifndef TRINITY_BOT_BANK_H
#define TRINITY_BOT_BANK_H

// Bot bank and mail (Bot.Bank.*, default off): bots with tight bags deposit their materials, gems and recipes at a banker and fetch
// wanted gear upgrades back; a bot with gold above its reserve mails the surplus to a configured character. The decisions are in
// BotBankPlan.cpp; the map-thread AI only decides and posts, the bank and mail calls run on the world thread (BotMgr::PostWorldTask).
// See docs/playerbots/feature-bot-bank-mail-20261008.md.

#include "BotBankPlan.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <functional>

class Item;
class Player;

namespace BotBank
{
    // True when Bot.Bank.Enabled is set (bank trips). Mail trips need MailEnabled().
    TC_GAME_API bool Enabled();
    // True when Bot.Bank.Mail.Enabled is set and Bot.Bank.Mail.Recipient names a character.
    TC_GAME_API bool MailEnabled();
    TC_GAME_API uint32 VisitCooldownMs();
    TC_GAME_API BotBankPlan::Config const& PlanConfig();

    // `wanted` says which bank items the bot wants back (a gear upgrade it can use).
    using WantFn = std::function<bool(Player*, Item*)>;

    // What a bank trip would be for right now, from the bags and the bank contents.
    TC_GAME_API BotBankPlan::Visit PlanVisit(Player* bot, WantFn const& wanted);

    // Copper the bot would mail now (0 = no mail trip due).
    TC_GAME_API uint64 MailGold(Player* bot);

    // World-thread tasks. Both re-check everything (the bot may have moved, logged out or lost the item) and log BANK_* events.
    TC_GAME_API void PostBank(ObjectGuid bot, ObjectGuid banker, WantFn wanted);
    TC_GAME_API void PostMailGold(ObjectGuid bot, ObjectGuid mailbox);
}

#endif
