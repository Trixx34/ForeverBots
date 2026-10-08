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

#include "BotSocial.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotTradeLink.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "PartyPackets.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "QuestPools.h"
#include "Player.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "StringFormat.h"
#include "World.h"
#include <algorithm>
#include <map>
#include <optional>
#include <vector>
#include "WorldSession.h"

namespace BotSocial
{
namespace
{
bool BotsOn()
{
    return sWorld->getBoolConfig(CONFIG_BOT_ENABLED);
}

std::string JsonEscape(std::string const& s)
{
    std::string out;
    for (char c : s)
    {
        if (uint8(c) < 0x20)
            continue;
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

// One bot_event. `accepted` goes into the details JSON because bot_event.outcome is generated from it.
void Log(Player* bot, char const* type, bool accepted, char const* reason, std::string summary, std::string extra = std::string(), uint32 questId = 0)
{
    BotAI* ai = bot && bot->GetSession() ? bot->GetSession()->GetBotAI() : nullptr;
    if (!ai)
        return;
    BotEvent e = ai->MakeEvent(bot, type, accepted ? BOTLOG_INFO : BOTLOG_WARN, reason, std::move(summary));
    e.Details = Trinity::StringFormat("{{\"outcome\":\"{}\"{}{}}}", accepted ? "accepted" : "refused", extra.empty() ? "" : ",", extra);
    if (questId)
        e.QuestId = questId;
    sBotMgr->LogEvent(std::move(e));
}

std::string Who(Player* inviter)
{
    return Trinity::StringFormat("\"issuer\":\"{}\",\"issuer_guid\":{}", JsonEscape(inviter->GetName()), inviter->GetGUID().GetCounter());
}

// Trades opened towards a bot that wait for the player: cancelled after TradeTimeoutMs (world thread only).
uint32 TradeTimeoutMs()
{
    int32 const sec = sConfigMgr->GetIntDefault("Bot.Trade.TimeoutSeconds", 90);
    return uint32(std::clamp<int32>(sec, 10, 600)) * 1000;
}
struct OpenTrade
{
    ObjectGuid Bot;
    uint32 LeftMs;
};
std::vector<OpenTrade> OpenTrades;

bool IsOwner(Player* other, Player* altBot)
{
    return other->GetSession()->GetAccountId() == altBot->GetSession()->GetAccountId();
}
}

void OnPartyInvite(Player* inviter, Player* bot)
{
    if (!BotsOn() || !bot->GetSession()->IsBot() || !bot->GetGroupInvite())
        return;

    char const* refuse = nullptr;
    if (inviter->GetSession()->IsBot())
        return; // bots never invite bots through this path (the inline accept would run inside the inviter's own handler)
    if (!sConfigMgr->GetBoolDefault("Bot.Invite.Enabled", true))
        refuse = "INVITE_REFUSED_DISABLED";
    else if (bot->GetSession()->IsAltBot() && !IsOwner(inviter, bot))
        refuse = "INVITE_REFUSED_NOT_OWNER";

    WorldPackets::Party::PartyInviteResponse response{WorldPacket(CMSG_PARTY_INVITE_RESPONSE)};
    response.Accept = refuse == nullptr;
    bot->GetSession()->HandlePartyInviteResponseOpcode(response);

    if (refuse)
        Log(bot, "invite", false, refuse, Trinity::StringFormat("declined party invite from {}", inviter->GetName()), Who(inviter));
    else
        Log(bot, "invite", true, "INVITE_ACCEPTED", Trinity::StringFormat("joined the party of {}", inviter->GetName()),
            Who(inviter) + Trinity::StringFormat(",\"in_group\":{}", bot->GetGroup() ? "true" : "false"));
}

void OnPartyInviteAlreadyGrouped(Player* inviter, Player* bot)
{
    if (!BotsOn() || !bot->GetSession()->IsBot() || inviter->GetSession()->IsBot())
        return;
    Log(bot, "invite", false, "INVITE_REFUSED_ALREADY_GROUPED", Trinity::StringFormat("invite from {} refused: already in a group or invited", inviter->GetName()), Who(inviter));
}

// --- trading ---

namespace
{
void RefuseTrade(Player* initiator, Player* bot, char const* reason, char const* what)
{
    // sendback=false: only the other side (the player) is told, the bot has no client
    bot->TradeCancel(false, TRADE_STATUS_PLAYER_BUSY);
    Log(bot, "trade", false, reason, Trinity::StringFormat("trade with {} refused: {}", initiator->GetName(), what), Who(initiator));
}

std::string Offer(TradeData* t, uint32& itemCount)
{
    std::string items;
    itemCount = 0;
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
        if (Item* item = t->GetItem(TradeSlots(i)))
        {
            ++itemCount;
            if (!items.empty())
                items += ',';
            items += Trinity::StringFormat("{{\"entry\":{},\"count\":{}}}", item->GetEntry(), item->GetCount());
        }
    return items;
}
}

void OnTradeInitiated(Player* initiator, Player* bot)
{
    if (!BotsOn() || !bot->GetSession()->IsBot() || !bot->GetTradeData() || initiator->GetSession()->IsBot())
        return;

    if (!sConfigMgr->GetBoolDefault("Bot.Trade.Enabled", true))
        return RefuseTrade(initiator, bot, "TRADE_REFUSED_DISABLED", "bot trading is disabled");
    if (!bot->GetSession()->IsAltBot())
        return RefuseTrade(initiator, bot, "TRADE_REFUSED_WORLD_BOT", "world bots do not trade");
    if (!IsOwner(initiator, bot))
        return RefuseTrade(initiator, bot, "TRADE_REFUSED_NOT_OWNER", "not the owner account of this alt");
    if (bot->IsInCombat())
        return RefuseTrade(initiator, bot, "TRADE_REFUSED_BUSY", "bot is in combat");

    // the bot "presses" begin trade: the window opens on the player's client
    WorldPackets::Trade::BeginTrade begin{WorldPacket(CMSG_BEGIN_TRADE)};
    bot->GetSession()->HandleBeginTradeOpcode(begin);
    OpenTrades.push_back({ bot->GetGUID(), TradeTimeoutMs() });
}

void OnTradePlayerAccepted(Player* player, Player* bot)
{
    if (!BotsOn() || !bot->GetSession()->IsBot() || player->GetSession()->IsBot())
        return;
    TradeData* mine = bot->GetTradeData();
    TradeData* theirs = player->GetTradeData();
    if (!mine || !theirs)
        return;

    uint32 n = 0, nMine = 0;
    Offer(theirs, n);
    Offer(mine, nMine);   // items the bot put in itself (BotTradeLink)
    if (!n && !nMine && !theirs->GetMoney())
        return RefuseTrade(player, bot, "TRADE_REFUSED_EMPTY", "empty offer");

    // The bot accepts with the state index of the player's side (what a client echoes back); on success the core completes the trade
    // and frees both TradeData, so nothing may touch them afterwards.
    WorldPackets::Trade::AcceptTrade accept{WorldPacket(CMSG_ACCEPT_TRADE)};
    accept.StateIndex = theirs->GetServerStateIndex();
    bot->GetSession()->HandleAcceptTradeOpcode(accept);

    if (bot->GetTradeData())
    {
        // the core refused the completion (bag space, range, state ...): do not leave a half-open window
        bot->TradeCancel(false, TRADE_STATUS_FAILED);
        Log(bot, "trade", false, "TRADE_REFUSED_CORE_FAILED", Trinity::StringFormat("trade with {} failed in the core checks and was cancelled", player->GetName()), Who(player));
    }
}

void OnTradeExecuting(Player* player, Player* other)
{
    if (!BotsOn())
        return;
    for (int side = 0; side < 2; ++side)
    {
        Player* bot = side ? other : player;
        Player* partner = side ? player : other;
        if (!bot->GetSession()->IsBot())
            continue;
        TradeData* mine = bot->GetTradeData();
        TradeData* theirs = partner->GetTradeData();
        if (!mine || !theirs)
            continue;
        BotTradeLink::OnTradeExecuting(partner, bot);
        uint32 nOut = 0, nIn = 0;
        std::string const out = Offer(mine, nOut);
        std::string const in = Offer(theirs, nIn);
        Log(bot, "trade", true, "TRADE_ACCEPTED", Trinity::StringFormat("trade with {}: received {} items and {} copper, gave {} items and {} copper", partner->GetName(), nIn, theirs->GetMoney(), nOut, mine->GetMoney()),
            Who(partner) + Trinity::StringFormat(",\"gold_in\":{},\"gold_out\":{},\"items_in\":[{}],\"items_out\":[{}]", theirs->GetMoney(), mine->GetMoney(), in, out));
    }
}

void Update(uint32 diff)
{
    BotTradeLink::Update(diff);
    if (OpenTrades.empty())
        return;
    for (size_t i = 0; i < OpenTrades.size();)
    {
        OpenTrade& t = OpenTrades[i];
        Player* bot = ObjectAccessor::FindPlayer(t.Bot);
        if (!bot || !bot->GetTradeData())
        {
            OpenTrades.erase(OpenTrades.begin() + i);
            continue;
        }
        if (t.LeftMs > diff)
        {
            t.LeftMs -= diff;
            ++i;
            continue;
        }
        Player* partner = bot->GetTradeData()->GetTrader();
        bot->TradeCancel(true, TRADE_STATUS_CANCELLED);
        Log(bot, "trade", false, "TRADE_REFUSED_TIMEOUT", Trinity::StringFormat("trade with {} left open, cancelled", partner ? partner->GetName() : std::string("?")));
        OpenTrades.erase(OpenTrades.begin() + i);
    }
}

// --- quest sharing ---

namespace
{
// Active while a bot pushes its quest (ShareQuest, world thread): receivers are counted instead of logged one by one.
struct ShareAgg
{
    bool Active = false;
    bool LogEach = false;
    uint32 Accepted = 0;
    std::map<std::string, uint32> Refused;
};
ShareAgg s_share;
}

void OnQuestPushed(Player* sender, Player* receiver, uint32 questId)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    struct Refusal { char const* Code; QuestPushReason Reason; };
    std::optional<Refusal> refusal;
    auto refuse = [&](char const* code, QuestPushReason reason) { refusal = Refusal{ code, reason }; };

    if (!receiver->GetPlayerSharingQuest().IsEmpty())
        refuse("QUEST_SHARE_REFUSED_BUSY", QuestPushReason::Busy);
    else if (!receiver->IsAlive())
        refuse("QUEST_SHARE_REFUSED_DEAD", QuestPushReason::Dead);
    else if (receiver->GetQuestStatus(questId) == QUEST_STATUS_REWARDED)
        refuse("QUEST_SHARE_REFUSED_DONE", QuestPushReason::AlreadyDone);
    else if (receiver->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE || receiver->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        refuse("QUEST_SHARE_REFUSED_ON_QUEST", QuestPushReason::OnQuest);
    else if (!receiver->SatisfyQuestLog(false))
        refuse("QUEST_SHARE_REFUSED_LOG_FULL", QuestPushReason::LogFull);
    else if (!receiver->SatisfyQuestDay(quest, false))
        refuse("QUEST_SHARE_REFUSED_DONE", QuestPushReason::AlreadyDone);
    else if (!receiver->SatisfyQuestMinLevel(quest, false))
        refuse("QUEST_SHARE_REFUSED_LEVEL_LOW", QuestPushReason::LowLevel);
    else if (!receiver->SatisfyQuestMaxLevel(quest, false))
        refuse("QUEST_SHARE_REFUSED_LEVEL_HIGH", QuestPushReason::HighLevel);
    else if (!receiver->SatisfyQuestClass(quest, false))
        refuse("QUEST_SHARE_REFUSED_CLASS", QuestPushReason::Class);
    else if (!receiver->SatisfyQuestRace(quest, false))
        refuse("QUEST_SHARE_REFUSED_RACE", QuestPushReason::Race);
    else if (!receiver->SatisfyQuestMinReputation(quest, false))
        refuse("QUEST_SHARE_REFUSED_REP_LOW", QuestPushReason::LowFaction);
    else if (!receiver->SatisfyQuestMaxReputation(quest, false))
        refuse("QUEST_SHARE_REFUSED_REP_HIGH", QuestPushReason::HighFaction);
    else if (!receiver->SatisfyQuestDependentQuests(quest, false))
        refuse("QUEST_SHARE_REFUSED_PREREQ", QuestPushReason::Prerequisite);
    else if (!receiver->SatisfyQuestExpansion(quest, false))
        refuse("QUEST_SHARE_REFUSED_EXPANSION", QuestPushReason::Expansion);
    else if (!receiver->CanTakeQuest(quest, false))
        refuse("QUEST_SHARE_REFUSED_NOT_ELIGIBLE", QuestPushReason::Invalid);
    else if (quest->IsTurnIn() && quest->IsRepeatable() && !quest->IsDailyOrWeekly())
        refuse("QUEST_SHARE_REFUSED_REPEATABLE", QuestPushReason::Invalid); // turn-in share: needs the player's item dialog
    else if (!receiver->CanAddQuest(quest, false))
        refuse("QUEST_SHARE_REFUSED_BAG_FULL", QuestPushReason::LogFull);   // source item does not fit

    std::string const extra = Who(sender) + Trinity::StringFormat(",\"quest\":{}", questId);
    std::string const title = quest->GetLogTitle();
    if (refusal)
    {
        sender->SendPushToPartyResponse(receiver, refusal->Reason);
        if (s_share.Active)
            ++s_share.Refused[refusal->Code];
        if (!s_share.Active || s_share.LogEach)
            Log(receiver, "quest_share", false, refusal->Code, Trinity::StringFormat("declined shared quest '{}' from {}", title, sender->GetName()), extra, questId);
        return;
    }

    sender->SendPushToPartyResponse(receiver, QuestPushReason::Success);
    receiver->AddQuestAndCheckCompletion(quest, nullptr); // nullptr: no DB script run, as for a confirmed shared quest
    if (quest->GetSrcSpell() > 0)
        receiver->CastSpell(receiver, quest->GetSrcSpell(), true);
    sender->SendPushToPartyResponse(receiver, QuestPushReason::Accepted);
    if (s_share.Active)
        ++s_share.Accepted;
    if (!s_share.Active || s_share.LogEach)
        Log(receiver, "quest_share", true, "QUEST_SHARE_ACCEPTED", Trinity::StringFormat("accepted shared quest '{}' from {}", title, sender->GetName()), extra, questId);
}

char const* ShareQuest(Player* bot, uint32 questId, bool verbose)
{
    if (!bot->GetGroup())
        return "NOT_GROUPED";
    if (!bot->IsActiveQuest(questId))
        return "NOT_ON_QUEST";
    if (!bot->CanShareQuest(questId))
        return "NOT_SHAREABLE";

    WorldPackets::Quest::PushQuestToParty push{WorldPacket(CMSG_PUSH_QUEST_TO_PARTY)};
    push.QuestID = questId;
    s_share = ShareAgg();
    s_share.Active = true;
    s_share.LogEach = verbose || sConfigMgr->GetBoolDefault("Bot.Social.QuestShare.LogEach", false);
    bot->GetSession()->HandlePushQuestToParty(push);
    ShareAgg const agg = std::move(s_share);
    s_share = ShareAgg();

    uint32 refusedTotal = 0;
    std::string byReason;
    for (auto const& [code, n] : agg.Refused)
    {
        refusedTotal += n;
        byReason += Trinity::StringFormat("{}\"{}\":{}", byReason.empty() ? "" : ",", code, n);
    }
    Log(bot, "quest_share", true, "QUEST_SHARE_SENT",
        Trinity::StringFormat("shared quest {} with the group: {} accepted, {} refused", questId, agg.Accepted, refusedTotal),
        Trinity::StringFormat("\"quest\":{},\"issuer\":\"{}\",\"accepted\":{},\"refused\":{},\"refused_by_reason\":{{{}}}", questId, JsonEscape(bot->GetName()), agg.Accepted, refusedTotal, byReason), questId);
    return "OK";
}
}
