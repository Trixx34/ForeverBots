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

#include "BotEngine.h"
#include "BotAI.h"
#include "StringFormat.h"
#include <algorithm>
#include <cstring>

char const* BotStateName(BotState state)
{
    switch (state)
    {
        case BotState::NonCombat: return "noncombat";
        case BotState::Combat: return "combat";
        case BotState::Dead: return "dead";
    }
    return "unknown";
}

char const* BotRelevance::BandName(float relevance)
{
    if (relevance >= PullHigh) return "pull_high";
    if (relevance >= PullMid) return "pull_mid";
    if (relevance >= PullLow) return "pull_low";
    if (relevance >= Emergency) return "emergency";
    if (relevance >= Raid) return "raid";
    if (relevance >= Dispel) return "dispel";
    if (relevance >= Interrupt) return "interrupt";
    if (relevance >= Rest) return "rest";
    if (relevance >= Move) return "move";
    if (relevance >= High) return "high";
    if (relevance >= Normal) return "normal";
    return "default";
}

Player* BotAiObject::GetBot() const
{
    return _ai->GetTickBot();
}

bool UntypedValue::NeedsRefresh()
{
    uint32 const now = GetAI()->GetNowMs();
    if (_valid && now - _lastMs < _interval)
        return false;

    _valid = true;
    _lastMs = now;
    return true;
}

bool Trigger::Check()
{
    uint32 const now = GetAI()->GetNowMs();
    if (_interval && _checkedOnce && now - _lastMs < _interval)
        return false;

    _checkedOnce = true;
    _lastMs = now;
    return IsActive();
}

BotRegistry::BotRegistry()
{
    RegisterBuiltinBotObjects(*this);
}

BotRegistry const& BotRegistry::instance()
{
    static BotRegistry registry;
    return registry;
}

std::vector<std::string> BotRegistry::StrategyNames() const
{
    std::vector<std::string> names;
    for (auto const& pair : _strategies)
        names.push_back(pair.first);
    return names;
}

bool BotEngine::AddStrategy(std::string const& name)
{
    if (HasStrategy(name))
        return false;

    BotRegistry::StrategyEntry const* entry = BotRegistry::instance().FindStrategy(name);
    if (!entry)
        return false;

    _strategyNames.push_back(name);
    _strategies.push_back(entry->Create());
    _dirty = true;
    return true;
}

bool BotEngine::RemoveStrategy(std::string const& name)
{
    auto itr = std::find(_strategyNames.begin(), _strategyNames.end(), name);
    if (itr == _strategyNames.end())
        return false;

    size_t const index = itr - _strategyNames.begin();
    _strategyNames.erase(itr);
    _strategies.erase(_strategies.begin() + index);
    _dirty = true;
    return true;
}

bool BotEngine::HasStrategy(std::string const& name) const
{
    return std::find(_strategyNames.begin(), _strategyNames.end(), name) != _strategyNames.end();
}

void BotEngine::Rebuild()
{
    _dirty = false;
    _nodes.clear();
    _multipliers.clear();

    std::map<std::string, size_t> nodeIndex;
    std::vector<std::string> multiplierNames;

    for (std::unique_ptr<Strategy> const& strategy : _strategies)
    {
        std::vector<BotTriggerNode> triggers;
        strategy->InitTriggers(triggers);
        for (BotTriggerNode const& tn : triggers)
        {
            Trigger* trigger = _ai->GetTrigger(tn.Trigger);
            if (!trigger)
                continue;

            auto [itr, inserted] = nodeIndex.try_emplace(tn.Trigger, _nodes.size());
            if (inserted)
                _nodes.push_back(Node{ trigger, {} });

            for (BotActionDef const& def : tn.Actions)
            {
                Action* action = _ai->GetAction(def.Action);
                if (!action)
                    continue;

                // the same action twice under one trigger keeps the higher relevance
                auto& actions = _nodes[itr->second].Actions;
                auto existing = std::find_if(actions.begin(), actions.end(), [action](auto const& p) { return p.first == action; });
                if (existing == actions.end())
                    actions.emplace_back(action, def.Relevance);
                else
                    existing->second = std::max(existing->second, def.Relevance);
            }
        }

        strategy->InitMultipliers(multiplierNames);
    }

    std::sort(multiplierNames.begin(), multiplierNames.end());
    multiplierNames.erase(std::unique(multiplierNames.begin(), multiplierNames.end()), multiplierNames.end());
    for (std::string const& name : multiplierNames)
        if (Multiplier* m = _ai->GetMultiplier(name))
            _multipliers.push_back(m);
}

static std::string AltJson(std::string const& action, float relevance, char const* outcome)
{
    return Trinity::StringFormat(R"({{"action":"{}","relevance":{:.0f},"result":"{}"}})", action, relevance, outcome);
}

namespace { struct SkippedAlt { Action const* Act; float Relevance; char const* Outcome; }; }

bool BotEngine::DoNextAction()
{
    if (_dirty)
        Rebuild();

    _queue.clear();
    for (Node const& node : _nodes)
    {
        if (!node.Trig->Check())
            continue;

        for (auto const& [action, relevance] : node.Actions)
        {
            auto existing = std::find_if(_queue.begin(), _queue.end(), [action](Slot const& s) { return s.Act == action; });
            if (existing == _queue.end())
                _queue.push_back(Slot{ action, relevance, node.Trig });
            else if (relevance > existing->Relevance)
            {
                existing->Relevance = relevance;
                existing->Trig = node.Trig;
            }
        }
    }

    if (_queue.empty())
    {
        ++_idleNoTrigger;
        return false;
    }

    std::stable_sort(_queue.begin(), _queue.end(), [](Slot const& a, Slot const& b) { return a.Relevance > b.Relevance; });

    // outcomes of the queue entries that did not run; formatted as JSON only when the executed action is logged (cheap per tick)
    std::vector<SkippedAlt> skipped;
    for (size_t i = 0; i < _queue.size(); ++i)
    {
        Slot const& slot = _queue[i];
        float effective = slot.Relevance;
        for (Multiplier* m : _multipliers)
            effective *= m->GetValue(*slot.Act);

        char const* skip = nullptr;
        if (effective <= 0.0f)
            skip = "MULTIPLIED_TO_ZERO";
        else if (!slot.Act->IsPossible())
            skip = "NOT_POSSIBLE";
        else if (!slot.Act->IsUseful())
            skip = "NOT_USEFUL";

        if (!skip)
        {
            slot.Act->ClearResult();
            if (slot.Act->Execute())
            {
                std::vector<std::string> alternatives;
                if (!(slot.Act->GetFlags() & ACTION_FLAG_QUIET_LOG) || _ai->IsTrace())
                {
                    for (SkippedAlt const& a : skipped)
                        alternatives.push_back(AltJson(a.Act->GetName(), a.Relevance, a.Outcome));
                    for (size_t j = i + 1; j < _queue.size(); ++j)
                        alternatives.push_back(AltJson(_queue[j].Act->GetName(), _queue[j].Relevance, "NOT_REACHED"));
                }
                _ai->OnActionExecuted(_state, *slot.Act, slot.Trig, slot.Relevance, effective, alternatives);
                return true;
            }
            skip = "EXECUTE_FAILED";
        }

        skipped.push_back(SkippedAlt{ slot.Act, slot.Relevance, skip });
    }

    if (!skipped.empty())
    {
        char const* top = skipped.front().Outcome;   // the outcome of the highest-ranked queued action explains the idle tick
        if (!std::strcmp(top, "MULTIPLIED_TO_ZERO"))
            ++_idleMultiplied;
        else if (!std::strcmp(top, "NOT_POSSIBLE"))
            ++_idleNotPossible;
        else if (!std::strcmp(top, "NOT_USEFUL"))
            ++_idleNotUseful;
        else
            ++_idleExecFailed;
    }

    if (_ai->IsTrace())
    {
        std::string json = "{\"engine\":\"";
        json += BotStateName(_state);
        json += "\",\"considered\":[";
        for (size_t i = 0; i < skipped.size(); ++i)
        {
            if (i)
                json += ',';
            json += AltJson(skipped[i].Act->GetName(), skipped[i].Relevance, skipped[i].Outcome);
        }
        json += "]}";
        _ai->TraceEvaluation(_state, "no action ran", json);
    }
    return false;
}

std::string BotEngine::TakeIdleJson()
{
    uint32 const total = _idleNoTrigger + _idleMultiplied + _idleNotPossible + _idleNotUseful + _idleExecFailed;
    if (!total)
        return std::string();
    std::string j = Trinity::StringFormat(R"({{"total":{},"no_trigger":{},"multiplied_to_zero":{},"not_possible":{},"not_useful":{},"execute_failed":{}}})",
        total, _idleNoTrigger, _idleMultiplied, _idleNotPossible, _idleNotUseful, _idleExecFailed);
    _idleNoTrigger = _idleMultiplied = _idleNotPossible = _idleNotUseful = _idleExecFailed = 0;
    return j;
}
