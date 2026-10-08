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

#include "BotQuestLog.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <chrono>
#include <limits>
#include <map>
#include <mutex>

namespace
{
thread_local char const* _failCause = nullptr;
thread_local bool _testScope = false;

std::mutex _acceptMutex;
std::map<std::pair<uint64, uint32>, double> _acceptTimes; // (bot guid, quest id) -> unix seconds; lost on restart

double NowSeconds()
{
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Esc(std::string_view in)
{
    std::string out;
    for (char c : in)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (uint8(c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out;
}

bool IsTrace(Player* bot)
{
    BotAI* ai = bot->GetSession()->GetBotAI();
    return ai ? ai->IsTrace() : BotAI::GetTraceAll();
}

BotEvent Make(Player* bot, Quest const* quest, char const* reason, std::string verb, uint8 severity = BOTLOG_INFO)
{
    BotEvent event;
    event.BotGuid = bot->GetGUID().GetCounter();
    event.Type = "quest";
    event.Severity = severity;
    event.Reason = reason;
    std::string summary = verb + " " + quest->GetLogTitle();
    if (summary.size() > 250)
        summary.resize(250);
    event.Summary = std::move(summary);
    event.Level = bot->GetLevel();
    event.MapId = uint16(bot->GetMapId());
    event.ZoneId = uint16(bot->GetZoneId());
    event.X = bot->GetPositionX();
    event.Y = bot->GetPositionY();
    event.Z = bot->GetPositionZ();
    event.QuestId = quest->GetQuestId();
    return event;
}

char const* Source()
{
    return _testScope ? "test_command" : "bot";
}

// Fields shared by most events: title, level data and the group-quest marker later group logic searches for.
std::string Common(Player* bot, Quest const* quest)
{
    uint32 const suggested = quest->GetSuggestedPlayers();
    return Trinity::StringFormat(R"("title":"{}","quest_level":{},"min_level":{},"suggested_players":{},"group_quest":{},"source":"{}")",
        Esc(quest->GetLogTitle()), bot->GetQuestLevel(quest), bot->GetQuestMinLevel(quest), suggested, suggested > 1 ? "true" : "false", Source());
}

std::string GiverJson(Object* giver)
{
    if (!giver)
        return R"({"type":"none"})";

    char const* type = "other";
    switch (giver->GetTypeId())
    {
        case TYPEID_UNIT: type = "creature"; break;
        case TYPEID_GAMEOBJECT: type = "gameobject"; break;
        case TYPEID_ITEM: type = "item"; break;
        case TYPEID_PLAYER: type = "self"; break;
        default: break;
    }
    return Trinity::StringFormat(R"({{"type":"{}","entry":{}}})", type, giver->GetEntry());
}

std::string ItemJson(uint32 itemId, uint32 count)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    return Trinity::StringFormat(R"({{"item":{},"name":"{}","count":{}}})", itemId, proto ? Esc(proto->GetName(DEFAULT_LOCALE)) : std::string(), count);
}

void Send(BotEvent&& event, std::string details)
{
    event.Details = std::move(details);
    sBotMgr->LogEvent(std::move(event));
}
}

void BotQuestLog::OnAccepted(Player* bot, Quest const* quest, Object* giver)
{
    {
        std::lock_guard<std::mutex> lock(_acceptMutex);
        _acceptTimes[{ bot->GetGUID().GetCounter(), quest->GetQuestId() }] = NowSeconds();
    }

    Send(Make(bot, quest, "QUEST_ACCEPTED", "accepted"), Trinity::StringFormat(R"({{{},"giver":{},"flags":{},"objectives":{}}})",
        Common(bot, quest), GiverJson(giver), quest->GetFlags(), quest->GetObjectives().size()));
}

void BotQuestLog::OnObjectiveChange(Player* bot, Quest const* quest, QuestObjective const& objective, int32 oldAmount, int32 newAmount)
{
    // chatty: log the first change of an objective, the change that completes it, and everything when the bot trace is on
    bool const completes = newAmount >= objective.Amount && oldAmount < objective.Amount;
    if (!(oldAmount == 0 && newAmount > 0) && !completes && !IsTrace(bot))
        return;

    Send(Make(bot, quest, "QUEST_PROGRESS", completes ? "objective done:" : "progress:"), Trinity::StringFormat(
        R"({{"title":"{}","objective_id":{},"objective_type":{},"object_id":{},"old":{},"new":{},"required":{},"objective_done":{},"source":"{}"}})",
        Esc(quest->GetLogTitle()), objective.ID, uint32(objective.Type), objective.ObjectID, oldAmount, newAmount, objective.Amount, completes ? "true" : "false", Source()));
}

void BotQuestLog::OnComplete(Player* bot, Quest const* quest)
{
    std::string objectives = "[";
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (objectives.size() > 1)
            objectives += ',';
        objectives += Trinity::StringFormat(R"({{"id":{},"type":{},"object_id":{},"have":{},"required":{}}})",
            obj.ID, uint32(obj.Type), obj.ObjectID, bot->GetQuestObjectiveData(obj), obj.Amount);
    }
    objectives += ']';

    Send(Make(bot, quest, "QUEST_COMPLETE", "ready to turn in"), Trinity::StringFormat(R"({{{},"objectives":{}}})", Common(bot, quest), objectives));
}

void BotQuestLog::OnRewarded(Player* bot, Quest const* quest, LootItemType rewardType, uint32 rewardId, Object* giver, uint32 xp, int32 money)
{
    double acceptedAt = 0.0;
    {
        std::lock_guard<std::mutex> lock(_acceptMutex);
        auto itr = _acceptTimes.find({ bot->GetGUID().GetCounter(), quest->GetQuestId() });
        if (itr != _acceptTimes.end())
        {
            acceptedAt = itr->second;
            _acceptTimes.erase(itr);
        }
    }

    std::string fixed = "[";
    for (uint32 i = 0; i < quest->GetRewItemsCount(); ++i)
    {
        if (!quest->RewardItemId[i])
            continue;
        if (fixed.size() > 1)
            fixed += ',';
        fixed += ItemJson(quest->RewardItemId[i], quest->RewardItemCount[i]);
    }
    fixed += ']';

    std::string choice = "null";
    if (rewardType == LootItemType::Item && rewardId && quest->GetRewChoiceItemsCount())
    {
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            if (quest->RewardChoiceItemId[i] == rewardId && quest->RewardChoiceItemType[i] == LootItemType::Item)
                choice = ItemJson(rewardId, quest->RewardChoiceItemCount[i]);
    }

    std::string reputation = "[";
    for (uint32 i = 0; i < QUEST_REWARD_REPUTATIONS_COUNT; ++i)
    {
        if (!quest->RewardFactionId[i])
            continue;
        if (reputation.size() > 1)
            reputation += ',';
        reputation += Trinity::StringFormat(R"({{"faction":{},"value_id":{},"override":{}}})", quest->RewardFactionId[i], quest->RewardFactionValue[i], quest->RewardFactionOverride[i]);
    }
    reputation += ']';

    std::string seconds = acceptedAt > 0.0 ? Trinity::StringFormat("{:.0f}", NowSeconds() - acceptedAt) : std::string("null");
    Send(Make(bot, quest, "QUEST_REWARDED", "turned in"), Trinity::StringFormat(
        R"({{{},"turn_in":{},"choice_item":{},"fixed_items":{},"xp":{},"money":{},"reputation":{},"accept_to_reward_s":{}}})",
        Common(bot, quest), GiverJson(giver), choice, fixed, xp, money, reputation, seconds));
}

void BotQuestLog::OnAbandoned(Player* bot, Quest const* quest)
{
    {
        std::lock_guard<std::mutex> lock(_acceptMutex);
        _acceptTimes.erase({ bot->GetGUID().GetCounter(), quest->GetQuestId() });
    }
    Send(Make(bot, quest, "QUEST_ABANDONED", "abandoned"), Trinity::StringFormat(R"({{{}}})", Common(bot, quest)));
}

BotQuestLog::Scope::Scope(char const* cause, bool test) : _prevCause(_failCause), _prevTest(_testScope)
{
    _failCause = cause;
    _testScope = _testScope || test;
}

BotQuestLog::Scope::~Scope()
{
    _failCause = _prevCause;
    _testScope = _prevTest;
}

void BotQuestLog::OnFailed(Player* bot, Quest const* quest)
{
    {
        std::lock_guard<std::mutex> lock(_acceptMutex);
        _acceptTimes.erase({ bot->GetGUID().GetCounter(), quest->GetQuestId() });
    }

    // cause: the innermost Scope around the FailQuest call site knows best; without one, a timed quest is assumed to have timed out
    char const* cause = _testScope ? "test_command" : _failCause ? _failCause : quest->GetLimitTime() ? "timeout" : "other";
    BotAI* ai = bot->GetSession()->GetBotAI();
    Send(Make(bot, quest, "QUEST_FAILED", "failed", BOTLOG_WARN), Trinity::StringFormat(
        R"({{{},"cause":"{}","time_limit_s":{},"alive":{},"engine":"{}"}})", Common(bot, quest), cause, quest->GetLimitTime(),
        bot->IsAlive() ? "true" : "false", ai ? BotStateName(ai->GetState()) : "none"));
}

void BotQuestLog::OnLogout(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(_acceptMutex);
    _acceptTimes.erase(_acceptTimes.lower_bound({ botGuid, 0 }), _acceptTimes.upper_bound({ botGuid, std::numeric_limits<uint32>::max() }));
}
