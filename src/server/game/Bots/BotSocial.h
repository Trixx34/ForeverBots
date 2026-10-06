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

#ifndef TRINITY_BOT_SOCIAL_H
#define TRINITY_BOT_SOCIAL_H

// Bot reactions to player-initiated social packets (step A3 follow-up). Bot sessions have no socket, so they never receive an
// invite, a trade window or a quest push; the core handlers call the hooks below on the WORLD thread (these handlers are
// PROCESS_THREADUNSAFE) and the bot answers by driving its own session handler with a hand-built packet. No database access here.
//
//  Invites: Bot.Invite.Enabled (1). Alt bots accept only the owner account, world bots accept anyone the core did not refuse.
//  Trade:   Bot.Trade.Enabled (1). Alt bots trade only with the owner account; world bots refuse every trade (see below).
//           The bot never offers anything: the player gives, the bot accepts a non-empty offer. Core checks (CanBeTraded,
//           soulbound, distance, money, bag space) are the only item rules, the bot adds none.
//  Quests:  a quest pushed to a bot is accepted when the core checks pass (level, class/race, log space, not held/done) and then
//           worked by BotQuest like any quest of its log. The `share` chat verb makes bots push their own quests.

#include "Define.h"
#include <string>

class Player;
class WorldSession;

namespace BotSocial
{
// GroupHandler.cpp, end of HandlePartyInviteOpcode: the invite to `bot` was queued. Accepts or declines on behalf of the bot.
TC_GAME_API void OnPartyInvite(Player* inviter, Player* bot);

// GroupHandler.cpp, "already in a group / invited" refusal: logs INVITE_REFUSED_ALREADY_GROUPED (one group per bot).
TC_GAME_API void OnPartyInviteAlreadyGrouped(Player* inviter, Player* bot);

// TradeHandler.cpp, end of HandleInitiateTradeOpcode: the trade window was opened towards `bot`.
TC_GAME_API void OnTradeInitiated(Player* initiator, Player* bot);

// TradeHandler.cpp, HandleAcceptTradeOpcode: `player` pressed accept while the other side is `bot` who has not accepted yet.
TC_GAME_API void OnTradePlayerAccepted(Player* player, Player* bot);

// TradeHandler.cpp, just before the core executes a validated trade: logs the gold/item flow when a bot is a party to it.
TC_GAME_API void OnTradeExecuting(Player* player, Player* other);

// World thread, BotMgr::Update: cancels trades left open by the player.
TC_GAME_API void Update(uint32 diff);

// QuestHandler.cpp, HandlePushQuestToParty success tail: `receiver` is a bot that passed every core check for the push.
// Replaces the per-receiver part of the core loop for bot receivers: same checks in the same order, the reason goes to the sender and
// to bot_event (QUEST_SHARE_ACCEPTED / QUEST_SHARE_REFUSED_*), an accepted quest is added to the log and then worked by BotQuest.
TC_GAME_API void OnQuestPushed(Player* sender, Player* receiver, uint32 questId);

// Makes `bot` push its own quest to its group through the normal handler. Returns a reason code ("OK" or why not).
TC_GAME_API char const* ShareQuest(Player* bot, uint32 questId);
}

#endif
