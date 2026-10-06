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

#include "BotAI.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace
{
BotAIConfig _config;
std::once_flag _configOnce;
std::atomic<bool> _enabled{true};
std::atomic<bool> _traceAll{false};
BotAIStats _stats;

std::string JsonEscape(std::string_view in)
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

void Truncate(std::string& s, size_t max)
{
    if (s.size() > max)
        s.resize(max);
}

uint64 NowNs()
{
    return uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Default strategy set per engine state (the AI factory policy). Nothing is on by default except what the test switch adds:
// the engine itself (state tracking and its log events) is always active, behaviors come with later phases.
std::vector<std::string> DefaultStrategies(BotState state, Player* /*bot*/)
{
    std::vector<std::string> names;
    if (BotAI::Config().TestStrategy)
    {
        switch (state)
        {
            case BotState::NonCombat: names.push_back("test_idle"); break;
            case BotState::Combat: names.push_back("test_combat"); break;
            case BotState::Dead: names.push_back("test_dead"); break;
        }
    }
    return names;
}
}

BotAIConfig const& BotAI::Config()
{
    std::call_once(_configOnce, []()
    {
        _config.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Enabled", true);
        _config.TickMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TickMs", 500), 50, 10000));
        _config.TestStrategy = sConfigMgr->GetBoolDefault("Bot.AI.TestStrategy", false);
        _config.TestIdleSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Test.IdleSec", 30), 1, 86400));
        _config.TestCombatSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Test.CombatSec", 5), 1, 86400));
        _enabled.store(_config.Enabled, std::memory_order_relaxed);
        TC_LOG_INFO("server.worldserver", "Bot AI: {}, tick {} ms, test strategy {}", _config.Enabled ? "enabled" : "disabled", _config.TickMs,
            _config.TestStrategy ? "on" : "off");
    });
    return _config;
}

bool BotAI::IsEnabled() { return _enabled.load(std::memory_order_relaxed); }
void BotAI::SetEnabled(bool on) { _enabled.store(on, std::memory_order_relaxed); }
BotAIStats& BotAI::Stats() { return _stats; }
void BotAI::SetTraceAll(bool on) { _traceAll.store(on, std::memory_order_relaxed); }
bool BotAI::GetTraceAll() { return _traceAll.load(std::memory_order_relaxed); }

std::unique_ptr<BotAI> BotAI::Create(Player* bot)
{
    Config();
    BotRegistry::instance(); // build the registries on the world thread

    std::unique_ptr<BotAI> ai = std::make_unique<BotAI>(bot);
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
        for (std::string const& name : DefaultStrategies(BotState(s), bot))
            ai->_engines[s]->AddStrategy(name);
    return ai;
}

BotAI::BotAI(Player* bot) : _guid(bot->GetGUID().GetCounter()), _phaseMs(urand(0, Config().TickMs - 1)), _lastBucket(0), _trace(GetTraceAll())
{
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
        _engines[s] = std::make_unique<BotEngine>(this, BotState(s));
}

BotAI::~BotAI() = default;

void BotAI::Update(Player* bot, uint32 diff)
{
    if (!IsEnabled())
        return;

    // Ticks fire when the bot's own time bucket changes: each bot has a fixed random phase, so a long world stall makes every
    // bot tick once but they spread out again afterwards (a reset-to-zero accumulator would keep them in lockstep).
    _nowMs += diff;
    uint32 const bucket = (_nowMs + _phaseMs) / Config().TickMs;
    if (bucket == _lastBucket)
        return;
    _lastBucket = bucket;

    if (bot->IsBeingTeleported())
        return;

    uint64 const start = NowNs();
    Tick(bot);
    uint64 const elapsed = NowNs() - start;

    _stats.Ticks.fetch_add(1, std::memory_order_relaxed);
    _stats.TickNs.fetch_add(elapsed, std::memory_order_relaxed);
    uint64 max = _stats.MaxTickNs.load(std::memory_order_relaxed);
    while (elapsed > max && !_stats.MaxTickNs.compare_exchange_weak(max, elapsed, std::memory_order_relaxed)) { }
}

BotState BotAI::DesiredState(Player* bot, char const*& cause) const
{
    if (_forced)
    {
        cause = "FORCED";
        return *_forced;
    }

    if (!bot->IsAlive())
    {
        cause = "DIED";
        return BotState::Dead;
    }

    if (bot->IsInCombat())
    {
        cause = "COMBAT_START";
        return BotState::Combat;
    }

    cause = _state == BotState::Dead ? "REVIVED" : _state == BotState::Combat ? "COMBAT_END" : "NONE";
    return BotState::NonCombat;
}

void BotAI::Tick(Player* bot)
{
    _tickBot = bot;

    uint16 const mapId = uint16(bot->GetMapId());
    if (mapId != _lastMapId)
    {
        if (_lastMapId != 0xFFFF)
            for (auto& entry : _values) // cached values may refer to the old map
                entry.second->Invalidate();
        _lastMapId = mapId;
    }

    char const* cause = "NONE";
    BotState const desired = DesiredState(bot, cause);
    if (!_started)
    {
        _started = true;
        ChangeState(bot, desired, "AI_START");
    }
    else if (desired != _state)
        ChangeState(bot, desired, (_forcedChanged && !_forced) ? "FORCED_END" : cause);
    _forcedChanged = false;

    _engines[uint32(_state)]->DoNextAction();
    uint64 const posStart = NowNs();
    SamplePosition(bot);
    _stats.PosNs.fetch_add(NowNs() - posStart, std::memory_order_relaxed);
    _tickBot = nullptr;
}

// Position telemetry for the sim console map: one sample on the first tick after login and on map/zone change, then at most one per
// Bot.Log.PosIntervalSec and only when the bot moved more than 2 yards since its last sample. Idle bots cost one interval compare.
void BotAI::SamplePosition(Player* bot)
{
    uint32 const interval = sBotMgr->GetPosIntervalMs();
    if (!interval)
        return;

    uint16 const mapId = uint16(bot->GetMapId());
    uint16 const zoneId = uint16(bot->GetZoneId());
    bool const force = !_posSampled || mapId != _posMapId || zoneId != _posZoneId;
    if (!force)
    {
        if (_nowMs - _posLastMs < interval)
            return;
        float const dx = bot->GetPositionX() - _posX, dy = bot->GetPositionY() - _posY, dz = bot->GetPositionZ() - _posZ;
        if (dx * dx + dy * dy + dz * dz < 4.0f)
            return;
    }

    BotPosSample sample;
    sample.BotGuid = _guid;
    sample.MapId = mapId;
    sample.ZoneId = zoneId;
    sample.X = _posX = bot->GetPositionX();
    sample.Y = _posY = bot->GetPositionY();
    sample.Z = _posZ = bot->GetPositionZ();
    sample.Flags = uint8((bot->isMoving() ? 1 : 0) | (bot->IsInCombat() ? 2 : 0) | (bot->IsAlive() ? 0 : 4));
    _posSampled = true;
    _posMapId = mapId;
    _posZoneId = zoneId;
    _posLastMs = _nowMs;
    _stats.PosSamples.fetch_add(1, std::memory_order_relaxed);
    sBotMgr->LogPosition(std::move(sample));
}

// The one place the engine state changes. Logs the transition; entering Dead also carries the last decisions.
void BotAI::ChangeState(Player* bot, BotState to, char const* cause)
{
    BotState const from = _state;
    _state = to;
    _stateSinceMs = _nowMs;

    _stats.StateChanges.fetch_add(1, std::memory_order_relaxed);

    for (auto& e : _triggers) e.second->OnStateEnter();
    for (auto& e : _actions) e.second->OnStateEnter();
    for (auto& e : _values) e.second->OnStateEnter();

    uint64 const logStart = NowNs();
    std::string details = Trinity::StringFormat(R"({{"kind":"engine","from":"{}","to":"{}","cause":"{}","ai_ms":{})",
        BotStateName(from), BotStateName(to), cause, _nowMs);
    if (to == BotState::Dead)
    {
        details += ",\"death\":true,\"recent_decisions\":";
        details += RecentDecisionsJson();
    }
    details += '}';

    BotEvent event = MakeEvent(bot, "state_change", BOTLOG_INFO, cause, Trinity::StringFormat("engine {} -> {}", BotStateName(from), BotStateName(to)));
    event.Details = details;
    Emit(std::move(event));

    _stats.LogNs.fetch_add(NowNs() - logStart, std::memory_order_relaxed);
}

void BotAI::Emit(BotEvent&& event)
{
    _stats.Events.fetch_add(1, std::memory_order_relaxed);
    sBotMgr->LogEvent(std::move(event));
}

BotEvent BotAI::MakeEvent(Player* bot, char const* type, uint8 severity, std::string reason, std::string summary) const
{
    BotEvent event;
    event.BotGuid = _guid;
    event.Type = type;
    event.Severity = severity;
    Truncate(reason, 63);
    Truncate(summary, 250);
    event.Reason = std::move(reason);
    event.Summary = std::move(summary);
    if (bot)
    {
        event.Level = bot->GetLevel();
        event.MapId = uint16(bot->GetMapId());
        event.ZoneId = uint16(bot->GetZoneId());
        event.X = bot->GetPositionX();
        event.Y = bot->GetPositionY();
        event.Z = bot->GetPositionZ();
    }
    return event;
}

std::string BotAI::RecentDecisionsJson() const
{
    std::string json = "[";
    for (uint32 i = 0; i < _ringCount; ++i)
    {
        // newest first
        Decision const& d = _ring[(_ringNext + DECISION_RING - 1 - i) % DECISION_RING];
        if (i)
            json += ',';
        json += Trinity::StringFormat(R"({{"ms_ago":{},"action":"{}","reason":"{}"}})", _nowMs - d.Ms, d.Act->GetName(), JsonEscape(d.Reason));
    }
    json += ']';
    return json;
}

Trigger* BotAI::GetTrigger(std::string const& name)
{
    auto itr = _triggers.find(name);
    if (itr != _triggers.end())
        return itr->second.get();

    BotRegistry::TriggerCreator const* creator = BotRegistry::instance().FindTrigger(name);
    if (!creator)
    {
        WarnOnce("trigger:" + name, "unknown trigger " + name);
        return nullptr;
    }
    return (_triggers[name] = (*creator)(this)).get();
}

Action* BotAI::GetAction(std::string const& name)
{
    auto itr = _actions.find(name);
    if (itr != _actions.end())
        return itr->second.get();

    BotRegistry::ActionCreator const* creator = BotRegistry::instance().FindAction(name);
    if (!creator)
    {
        WarnOnce("action:" + name, "unknown action " + name);
        return nullptr;
    }
    return (_actions[name] = (*creator)(this)).get();
}

Multiplier* BotAI::GetMultiplier(std::string const& name)
{
    auto itr = _multipliers.find(name);
    if (itr != _multipliers.end())
        return itr->second.get();

    BotRegistry::MultiplierCreator const* creator = BotRegistry::instance().FindMultiplier(name);
    if (!creator)
    {
        WarnOnce("multiplier:" + name, "unknown multiplier " + name);
        return nullptr;
    }
    return (_multipliers[name] = (*creator)(this)).get();
}

UntypedValue* BotAI::GetValueRaw(std::string const& name)
{
    auto itr = _values.find(name);
    if (itr != _values.end())
        return itr->second.get();

    BotRegistry::ValueCreator const* creator = BotRegistry::instance().FindValue(name);
    if (!creator)
    {
        WarnOnce("value:" + name, "unknown value " + name);
        return nullptr;
    }
    return (_values[name] = (*creator)(this)).get();
}

void BotAI::WarnOnce(std::string const& key, std::string const& message)
{
    if (std::find(_warned.begin(), _warned.end(), key) != _warned.end())
        return;
    _warned.push_back(key);

    TC_LOG_ERROR("server.worldserver", "BotAI {}: {}", _guid, message);
    BotEvent event = MakeEvent(_tickBot, "error", BOTLOG_WARN, "ENGINE_UNKNOWN_NAME", message);
    event.Details = Trinity::StringFormat(R"({{"key":"{}"}})", JsonEscape(key));
    Emit(std::move(event));
}

void BotAI::OnActionExecuted(BotState engine, Action& action, Trigger* trigger, float baseRelevance, float effectiveRelevance,
    std::vector<std::string> const& alternatives)
{
    _lastActionMs = _nowMs;
    _stats.ActionsRun.fetch_add(1, std::memory_order_relaxed);

    Decision& d = _ring[_ringNext];
    d.Ms = _nowMs;
    d.Act = &action;
    std::string const& reason = action.GetReason().empty() ? std::string("EXECUTED") : action.GetReason();
    std::strncpy(d.Reason, reason.c_str(), sizeof(d.Reason) - 1);
    d.Reason[sizeof(d.Reason) - 1] = '\0';
    _ringNext = (_ringNext + 1) % DECISION_RING;
    _ringCount = std::min(_ringCount + 1, DECISION_RING);

    if ((action.GetFlags() & ACTION_FLAG_QUIET_LOG) && !_trace)
        return;

    uint64 const logStart = NowNs();
    std::string details = Trinity::StringFormat(R"({{"engine":"{}","trigger":"{}","action":"{}","relevance":{:.0f},"band":"{}","effective":{:.1f},"alternatives":[)",
        BotStateName(engine), trigger ? trigger->GetName() : std::string(), action.GetName(), baseRelevance, BotRelevance::BandName(baseRelevance), effectiveRelevance);
    for (size_t i = 0; i < alternatives.size(); ++i)
    {
        if (i)
            details += ',';
        details += alternatives[i];
    }
    details += ']';
    if (!action.GetDetails().empty())
    {
        details += ",\"detail\":";
        details += action.GetDetails();
    }
    details += '}';

    std::string summary = action.GetSummary().empty() ? action.GetName() + " via " + (trigger ? trigger->GetName() : std::string("?")) : action.GetSummary();
    BotEvent event = MakeEvent(_tickBot, "decision", BOTLOG_INFO, reason, std::move(summary));
    event.Details = std::move(details);
    Emit(std::move(event));
    _stats.LogNs.fetch_add(NowNs() - logStart, std::memory_order_relaxed);
}

void BotAI::TraceEvaluation(BotState /*engine*/, std::string const& summary, std::string const& detailsJson)
{
    if (!_trace)
        return;

    // trace rows are written as INFO so they pass BotLog.MinSeverity: the trace switch is the explicit opt-in
    BotEvent event = MakeEvent(_tickBot, "trace", BOTLOG_INFO, "TRACE_EVAL", summary);
    event.Details = detailsJson;
    Emit(std::move(event));
}

bool BotAI::AddStrategy(Player* bot, std::string const& name, char const* source)
{
    BotRegistry::StrategyEntry const* entry = BotRegistry::instance().FindStrategy(name);
    if (!entry)
        return false;

    std::string engines;
    bool added = false;
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
    {
        if (!(entry->StateMask & BotStateBit(BotState(s))) || !_engines[s]->AddStrategy(name))
            continue;
        added = true;
        engines += (engines.empty() ? "\"" : ",\"") + std::string(BotStateName(BotState(s))) + "\"";
    }

    if (added)
    {
        BotEvent event = MakeEvent(bot, "strategy_change", BOTLOG_INFO, "STRATEGY_ADDED", "strategy +" + name);
        event.Details = Trinity::StringFormat(R"({{"strategy":"{}","source":"{}","engines":[{}]}})", name, source, engines);
        Emit(std::move(event));
    }
    return added;
}

bool BotAI::RemoveStrategy(Player* bot, std::string const& name, char const* source)
{
    std::string engines;
    bool removed = false;
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
    {
        if (!_engines[s]->RemoveStrategy(name))
            continue;
        removed = true;
        engines += (engines.empty() ? "\"" : ",\"") + std::string(BotStateName(BotState(s))) + "\"";
    }

    if (removed)
    {
        BotEvent event = MakeEvent(bot, "strategy_change", BOTLOG_INFO, "STRATEGY_REMOVED", "strategy -" + name);
        event.Details = Trinity::StringFormat(R"({{"strategy":"{}","source":"{}","engines":[{}]}})", name, source, engines);
        Emit(std::move(event));
    }
    return removed;
}

std::vector<std::string> BotAI::DescribeStrategies() const
{
    std::vector<std::string> lines;
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
    {
        std::string line = std::string(BotStateName(BotState(s))) + ":";
        if (_engines[s]->IsEmpty())
            line += " (none)";
        for (std::string const& name : _engines[s]->GetStrategies())
            line += " " + name;
        lines.push_back(std::move(line));
    }
    return lines;
}
