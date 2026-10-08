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
#include "BotPet.h"
#include "BotQuest.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Unit.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

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
std::vector<std::string> SplitNames(std::string const& list)
{
    std::vector<std::string> names;
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t end = list.find(',', pos);
        if (end == std::string::npos)
            end = list.size();
        size_t b = pos, e = end;
        while (b < e && list[b] == ' ') ++b;
        while (e > b && list[e - 1] == ' ') --e;
        if (e > b)
            names.push_back(list.substr(b, e - b));
        pos = end + 1;
    }
    return names;
}

std::vector<std::string> DefaultStrategies(BotState state, Player* bot)
{
    std::vector<std::string> names;
    switch (state)
    {
        case BotState::NonCombat:
            // alt bots idle (no quest/grind AI) until their group leader commands them
            if (bot && bot->GetSession() && bot->GetSession()->IsAltBot())
                names = SplitNames(sConfigMgr->GetStringDefault("Bot.Alt.Default.NonCombat", "rest,follow,goto"));
            else
                names = SplitNames(BotAI::Config().DefaultNonCombat);
            break;
        case BotState::Combat: names = SplitNames(BotAI::Config().DefaultCombat); break;
        case BotState::Dead: names = SplitNames(BotAI::Config().DefaultDead); break;
    }
    if (state == BotState::NonCombat && BotMove::Natural().Enabled && BotMove::Natural().Idle
        && std::find(names.begin(), names.end(), "natural_idle") == names.end())
        names.push_back("natural_idle");
    if (BotPet::Cfg().Enabled && bot && bot->GetClass() == CLASS_HUNTER && state != BotState::Dead
        && std::find(names.begin(), names.end(), "pet") == names.end())
        names.push_back("pet");
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
        _config.DefaultNonCombat = sConfigMgr->GetStringDefault("Bot.AI.Default.NonCombat", "rest,goto,follow");
        _config.DefaultCombat = sConfigMgr->GetStringDefault("Bot.AI.Default.Combat", "combat");
        _config.DefaultDead = sConfigMgr->GetStringDefault("Bot.AI.Default.Dead", "recover");
        _config.EatBelowPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Rest.EatBelowPct", 60), 1, 99));
        _config.DrinkBelowPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Rest.DrinkBelowPct", 40), 1, 99));
        _config.RestDonePct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Rest.DonePct", 95), 50, 100));
        _config.FreeFood = sConfigMgr->GetBoolDefault("Bot.AI.Rest.FreeFood", true);
        _config.ReleaseMinSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Release.MinDelaySec", 3), 0, 3600));
        _config.ReleaseMaxSec = std::max(_config.ReleaseMinSec, uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Release.MaxDelaySec", 8), 0, 3600)));
        _config.MaxCorpseRunYards = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Recover.MaxCorpseRunYards", 1200), 50, 100000));
        _config.StuckSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Move.StuckSec", 8), 2, 600));
        _config.StuckRepaths = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Move.StuckRepaths", 3), 1, 20));
        _config.CorpseRunMaxFails = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Death.CorpseRunMaxFails", 3), 1, 20));
        _config.AggroAvoid = sConfigMgr->GetBoolDefault("Bot.AI.AggroAvoid.Enabled", true);
        _config.AggroMarginYd = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.AggroAvoid.MarginYards", 10), 0, 40));
        _config.AggroLevelDiff = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.AggroAvoid.MinLevelDiff", 1), -10, 20);
        _config.AggroElites = sConfigMgr->GetBoolDefault("Bot.AI.AggroAvoid.Elites", true);
        _config.AggroLogSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.AggroAvoid.LogIntervalSec", 15), 1, 3600));
        _config.AggroMaxSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.AggroAvoid.MaxSec", 45), 5, 600));
        _config.TickStatsSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Log.AiTickStatsSec", 60), 0, 3600));
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
        {
            BotRegistry::StrategyEntry const* entry = BotRegistry::instance().FindStrategy(name);
            if (entry && (entry->StateMask & BotStateBit(BotState(s))))
                ai->_engines[s]->AddStrategy(name);
            else
                TC_LOG_ERROR("server.worldserver", "Bot AI: default strategy '{}' for the {} engine is unknown or does not apply to it", name, BotStateName(BotState(s)));
        }
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
        else
            BotPet::OnMapChange(this, bot);
        _lastMapId = mapId;
    }

    _motion.Tick(this, bot);

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

    // Player::Update calls this inside its "can delay teleport" region: teleports requested by an action (release spirit, ...) are
    // delayed to the end of the update like spell teleports and completed by BotMgr::ProcessBotTeleports (a bot has no client).
    if (bot->IsAlive())
    {
        if (!_sampleCount || _nowMs - _sampleLastMs >= 1000)
            SampleVitals(bot);
        UpdateFight(bot);
    }

    uint64 const engStart = NowNs();
    _engines[uint32(_state)]->DoNextAction();
    {
        EngStat& es = _engStat[uint32(_state)];
        uint64 const ns = NowNs() - engStart;
        es.Ns += ns;
        es.MaxNs = std::max(es.MaxNs, ns);
        ++es.N;
    }
    if (!_spellsLogged)
    {
        _spellsLogged = true;
        _levelSinceMs = _nowMs;
        _tickStatsMs = _nowMs;
        EmitSpellsKnown(bot, "LOGIN");
    }
    else if (uint32 const statsSec = Config().TickStatsSec)
        if (_nowMs - _tickStatsMs >= statsSec * 1000)
            EmitTickStats(bot);
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

    if (_rest.Resting())
        BotEndRest(this, bot, to == BotState::Combat ? "COMBAT_START" : to == BotState::Dead ? "DIED" : "STATE_CHANGE", to != BotState::Dead);
    if (to != BotState::NonCombat && _motion.HasGoal() && strcmp(_motion.GetTag(), "corpse_run") != 0)
    {
        BotMotion::Halt(bot);
        _motion.ClearGoal();
    }
    if (from == BotState::Combat && to == BotState::NonCombat && BotMove::Natural().Enabled && BotMove::Natural().Pacing)
        _motion.Pause(_nowMs, BotMove::PauseMs(BotMove::Pause::PostCombat, _guid, _nowMs));
    if (to == BotState::Dead)
    {
        _recover.Reset();
        _recover.DiedMs = _nowMs;
        _recover.ReleaseDelayMs = urand(Config().ReleaseMinSec, Config().ReleaseMaxSec) * 1000;
    }
    _stats.StateChanges.fetch_add(1, std::memory_order_relaxed);

    for (auto& e : _triggers) e.second->OnStateEnter();
    for (auto& e : _actions) e.second->OnStateEnter();
    for (auto& e : _values) e.second->OnStateEnter();

    uint64 const logStart = NowNs();
    std::string details = Trinity::StringFormat(R"({{"kind":"engine","from":"{}","to":"{}","cause":"{}","ai_ms":{})",
        BotStateName(from), BotStateName(to), cause, _nowMs);
    if (to == BotState::Dead)
        details += ",\"death\":true";
    if (from == BotState::Dead && to != BotState::Dead)
    {
        // recovery follow-up: how long the bot was dead and which path brought it back (the corpse run / spirit healer plan is
        // intact until the next Dead entry; "external" = revived by something else, e.g. a GM command)
        char const* path = _recover.Plan == BotRecover::Mode::CorpseRun ? "corpse_run" : _recover.Plan == BotRecover::Mode::Healer ? "spirit_healer" : "external";
        details += Trinity::StringFormat(R"(,"dead_s":{:.1f},"recovery":"{}","plan_reason":"{}","attempts":{})", float(_nowMs - _recover.DiedMs) / 1000.0f, path,
            JsonEscape(_recover.PlanReason), _recover.Attempts);
        _deathSnap = false;
        _deathJson.clear();
    }

    // the engine log row: slim, the enriched payload lives on the death row
    BotEvent event = MakeEvent(bot, "state_change", BOTLOG_INFO, cause, Trinity::StringFormat("engine {} -> {}", BotStateName(from), BotStateName(to)));
    event.Details = details + '}';
    Emit(std::move(event));

    if (to == BotState::Dead && from != BotState::Dead)
    {
        // close the open fight first, with the same fight_id the death row carries
        std::string fightId = _fight.Active ? _fight.Id : std::string();
        if (_fight.Active)
        {
            if (!_fight.StartLogged)
                EmitFightStart(bot);
            EmitFightEnd(bot, "died", _nowMs);
        }

        if (!_forced)
        {
            if (!_deathSnap) // died without passing through Unit::Kill's hook (should not happen): no snapshot
                SnapshotDeath(bot, nullptr);
            BotEvent death = MakeEvent(bot, "death", BOTLOG_WARN, "DIED", Trinity::StringFormat("bot died at L{} (engine was {})", bot->GetLevel(), BotStateName(from)));
            if (_deathKillerEntry)
                death.TargetEntry = _deathKillerEntry;
            std::string d = std::move(details);
            {
                // death loop: consecutive deaths of this login within 5 min and 150 yd of the previous one (same map)
                float const px = bot->GetPositionX(), py = bot->GetPositionY();
                uint32 sinceS = 0;
                float distYd = 0.0f;
                if (_deathSeen && bot->GetMapId() == _lastDeathMap && _nowMs - _lastDeathMs <= 300000
                    && (distYd = std::sqrt((px - _lastDeathX) * (px - _lastDeathX) + (py - _lastDeathY) * (py - _lastDeathY))) <= 150.0f)
                    ++_deathChain;
                else
                    _deathChain = 1;
                if (_deathSeen)
                    sinceS = (_nowMs - _lastDeathMs) / 1000;
                else
                    distYd = 0.0f;
                _deathSeen = true;
                _lastDeathMs = _nowMs;
                _lastDeathMap = bot->GetMapId();
                _lastDeathX = px;
                _lastDeathY = py;
                d += Trinity::StringFormat(R"(,"death_loop":{{"chain":{},"since_prev_s":{},"dist_prev_yd":{:.0f}}})", _deathChain, sinceS, distYd);
            }
            if (!fightId.empty())
                d += Trinity::StringFormat(R"(,"fight_id":"{}")", fightId);
            d += _deathJson;
            std::string decisions = RecentDecisionsJson();
            if (decisions.size() > 2)
                d += ",\"recent_decisions\":" + decisions;
            d += '}';
            death.Details = std::move(d);
            Emit(std::move(death));
        }
        _deathSnap = false;
        _deathJson.clear();
        _deathKillerEntry = 0;
    }
    _stats.LogNs.fetch_add(NowNs() - logStart, std::memory_order_relaxed);
}

void BotAI::Emit(BotEvent&& event)
{
    _stats.Events.fetch_add(1, std::memory_order_relaxed);
    sBotMgr->LogEvent(std::move(event));
}

void BotAI::EmitEvent(Player* bot, char const* type, uint8 severity, std::string reason, std::string summary, std::string detailsJson)
{
    BotEvent event = MakeEvent(bot, type, severity, std::move(reason), std::move(summary));
    event.Details = std::move(detailsJson);
    Emit(std::move(event));
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

// Newest first, only decisions younger than maxAgeMs, at most maxCount ("[]" when none).
std::string BotAI::RecentDecisionsJson(uint32 maxAgeMs, uint32 maxCount) const
{
    std::string json = "[";
    uint32 written = 0;
    for (uint32 i = 0; i < _ringCount && written < maxCount; ++i)
    {
        Decision const& d = _ring[(_ringNext + DECISION_RING - 1 - i) % DECISION_RING];
        if (_nowMs - d.Ms > maxAgeMs)
            break;
        if (written++)
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Combat and death telemetry (docs/playerbots/progress.md, "Death and combat events"). The hooks only append to small ring buffers
// and update fight totals; everything heavier (auras, grid searches, JSON) runs once per death or once per fight start/end.
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
char const* HitKindName(uint8 kind)
{
    static char const* const names[] = { "melee", "spell", "dot", "env" };
    return names[kind < 4 ? kind : 0];
}

char const* EnvName(uint8 env)
{
    switch (env)
    {
        case DAMAGE_EXHAUSTED: return "fatigue";
        case DAMAGE_DROWNING: return "drowning";
        case DAMAGE_FALL: return "fall";
        case DAMAGE_LAVA: return "lava";
        case DAMAGE_SLIME: return "slime";
        case DAMAGE_FIRE: return "fire";
        case DAMAGE_FALL_TO_VOID: return "fall_to_void";
        default: return "other";
    }
}

char const* PowerName(uint32 power)
{
    switch (power)
    {
        case POWER_MANA: return "mana";
        case POWER_RAGE: return "rage";
        case POWER_FOCUS: return "focus";
        case POWER_ENERGY: return "energy";
        default: return "other";
    }
}

char const* RankName(CreatureClassifications c)
{
    switch (c)
    {
        case CreatureClassifications::Normal: return "normal";
        case CreatureClassifications::Elite: return "elite";
        case CreatureClassifications::RareElite: return "rareelite";
        case CreatureClassifications::Rare: return "rare";
        case CreatureClassifications::Trivial: return "trivial";
        case CreatureClassifications::MinusMob: return "minus";
        default: return "other";
    }
}

// The creature or player this bot is fighting: its victim, else an attacker, else any PvE combat reference.
Unit* FindOpponent(Player* bot)
{
    if (Unit* v = bot->GetVictim())
        return v;
    if (!bot->getAttackers().empty())
        return *bot->getAttackers().begin();
    for (auto const& pr : bot->GetCombatManager().GetPvECombatRefs())
        if (Unit* o = pr.second->GetOther(bot))
            return o;
    return nullptr;
}

std::string SpellNameOf(uint32 id)
{
    if (SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE))
        if (si->SpellName)
            return JsonEscape((*si->SpellName)[DEFAULT_LOCALE]);
    return std::string();
}

std::string CreatureNameOf(uint32 entry)
{
    if (CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry))
        return JsonEscape(t->Name);
    return std::string();
}

char const* FirstName(uint8 first)
{
    return first == 1 ? "mob" : first == 2 ? "bot" : "unknown";
}

// process-lifetime death counters: per bot, and per (killer entry, zone); a per-AI member would reset on every respawn
std::mutex _repeatLock;
constexpr size_t DEATH_MAP_CAP = 50000;
std::unordered_map<uint64, uint32> _botDeaths;
std::map<std::pair<uint32, uint32>, uint32> _killerZoneDeaths;

uint64 UnixMs()
{
    return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
}

void BotAI::NoteTestCommand(char const* command)
{
    _testCmd.store(command, std::memory_order_relaxed);
    _testAtMs.store(GameTime::GetGameTimeMS(), std::memory_order_relaxed);
}

bool BotAI::IsTestSource() const
{
    return _testCmd.load(std::memory_order_relaxed) && GameTime::GetGameTimeMS() - _testAtMs.load(std::memory_order_relaxed) <= 120000;
}

void BotAI::SampleVitals(Player* bot)
{
    VitalSample& s = _samples[_sampleNext];
    s.Ms = _nowMs;
    s.Hp = uint32(bot->GetHealth());
    s.Power = uint32(std::max<int32>(0, bot->GetPower(bot->GetPowerType())));
    _sampleNext = (_sampleNext + 1) % SAMPLE_RING;
    _sampleCount = std::min(_sampleCount + 1, SAMPLE_RING);
    _sampleLastMs = _nowMs;
}

void BotAI::OpenFight(Player* /*bot*/, uint8 first, Unit* other, uint32 hp)
{
    _fight = Fight();
    _fight.Active = true;
    _fight.First = first;
    _fight.StartMs = _fight.LastCombatMs = _nowMs;
    _fight.HpStart = _fight.HpMin = hp;
    _fight.StartUnixMs = UnixMs();
    _fight.Id = Trinity::StringFormat("{}-{}", _guid, _fight.StartUnixMs);
    if (other)
    {
        _fight.Target = other->GetGUID();
        _fight.TargetEntry = other->GetEntry();
        _fight.TargetLevel = other->GetLevel();
    }
    _stats.Fights.fetch_add(1, std::memory_order_relaxed);
}

void BotAI::OnDamageTaken(Player* bot, Unit* attacker, uint32 damage, uint32 hpBefore, uint8 type, SpellInfo const* spell)
{
    uint64 const t0 = NowNs();
    bool const env = type == SELF_DAMAGE; // only Player::EnvironmentalDamage uses SELF_DAMAGE for players

    HitRec& h = _hits[_hitNext];
    h.Ms = _nowMs;
    h.Dmg = damage;
    h.HpBefore = hpBefore;
    h.Spell = spell ? spell->Id : 0;
    h.Kind = env ? 3 : type == DOT ? 2 : spell ? 1 : 0;
    h.Env = env ? _pendingEnv : 0xFF;
    _pendingEnv = 0xFF;
    if (attacker && attacker != bot)
    {
        h.Src = attacker->GetGUID();
        h.Entry = attacker->GetEntry();
        h.Level = uint8(attacker->GetLevel());
    }
    else
    {
        h.Src = ObjectGuid::Empty;
        h.Entry = 0;
        h.Level = 0;
    }
    _hitNext = (_hitNext + 1) % HIT_RING;
    _hitCount = std::min(_hitCount + 1, HIT_RING);

    if (!env && attacker && attacker != bot)
    {
        if (!_fight.Active)
            OpenFight(bot, 1, attacker, hpBefore);
        _fight.LastCombatMs = _nowMs;
        _fight.Taken += damage;
        _fight.HpMin = std::min(_fight.HpMin, damage >= hpBefore ? 0u : hpBefore - damage);
    }

    _stats.HookCalls.fetch_add(1, std::memory_order_relaxed);
    _stats.HookNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);
}

void BotAI::OnDamageDealt(Player* bot, Unit* victim, uint32 damage, SpellInfo const* spell)
{
    uint64 const t0 = NowNs();
    {
        SpellStat& st = StatFor(spell ? spell->Id : 0);
        ++st.Hits;
        st.Dmg += damage;
    }
    _dealt[_dealtNext] = { _nowMs, damage };
    _dealtNext = (_dealtNext + 1) % HIT_RING;
    _dealtCount = std::min(_dealtCount + 1, HIT_RING);

    if (!_fight.Active)
        OpenFight(bot, 2, victim, uint32(bot->GetHealth()));
    _fight.LastCombatMs = _nowMs;
    _fight.Dealt += damage;

    _stats.HookCalls.fetch_add(1, std::memory_order_relaxed);
    _stats.HookNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);
}

void BotAI::OnKilled(Player* /*bot*/, Unit* /*victim*/)
{
    if (!_fight.Active)
        return;
    ++_fight.Kills;
    _fight.LastKillMs = _nowMs;
    _fight.LastCombatMs = _nowMs;
}

void BotAI::OnDying(Player* bot, Unit* attacker)
{
    uint64 const t0 = NowNs();
    SnapshotDeath(bot, attacker);
    BotQuest::NoteDeath(bot);
    uint64 const elapsed = NowNs() - t0;
    _stats.Deaths.fetch_add(1, std::memory_order_relaxed);
    _stats.DeathNs.fetch_add(elapsed, std::memory_order_relaxed);
    _stats.HookCalls.fetch_add(1, std::memory_order_relaxed);
    _stats.HookNs.fetch_add(elapsed, std::memory_order_relaxed);
}

void BotAI::OnLogout(Player* bot, char const* reason)
{
    if (_fight.Active && _fight.StartLogged)
        EmitFightEnd(bot, reason && !strcmp(reason, "LOGOUT_COMMAND") ? "despawned" : "logout", _nowMs);
    _fight = Fight();
    BotPet::OnLogout(this, bot); // dismisses the pet, drops a taming run
    BotQuest::OnLogout(this); // releases the loot claim of the quest task
}

uint32 BotAI::CountHostiles(Player* bot) const
{
    std::list<Unit*> units;
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, 30.0f);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitAllObjects(bot, searcher, 30.0f);
    uint32 count = 0;
    for (Unit* u : units)
        if (u != bot && u->IsCreature())
            ++count;
    return count;
}

// What the bot is doing right now: the movement goal / follow / rest, for the combat and death rows.
std::string BotAI::ActivityJson(Player* bot) const
{
    if (_rest.Resting())
        return R"({"kind":"rest"})";
    if (_motion.HasGoal())
        return Trinity::StringFormat(R"({{"kind":"goto","tag":"{}","x":{:.0f},"y":{:.0f},"z":{:.0f},"dist":{:.0f}}})", JsonEscape(_motion.GetTag()),
            _motion.GoalX(), _motion.GoalY(), _motion.GoalZ(), bot->GetExactDist(_motion.GoalX(), _motion.GoalY(), _motion.GoalZ()));
    if (!_motion.GetFollow().IsEmpty())
        return Trinity::StringFormat(R"({{"kind":"follow","target":"{}"}})", _motion.GetFollow().ToString());
    return R"({"kind":"none"})";
}

void BotAI::UpdateFight(Player* bot)
{
    bool const inCombat = bot->IsInCombat();
    if (inCombat)
    {
        if (!_fight.Active)
        {
            Unit* other = bot->GetVictim();
            bool const botFirst = other != nullptr;
            if (!other)
                other = FindOpponent(bot);
            OpenFight(bot, botFirst ? 2 : 1, other, uint32(bot->GetHealth()));
        }
        _fight.LastCombatMs = _nowMs;
        _fight.HpMin = std::min(_fight.HpMin, uint32(bot->GetHealth()));
    }

    if (!_fight.Active)
        return;
    if (!_fight.StartLogged)
    {
        if (_fight.Target.IsEmpty())
            if (Unit* other = FindOpponent(bot))
            {
                _fight.Target = other->GetGUID();
                _fight.TargetEntry = other->GetEntry();
                _fight.TargetLevel = other->GetLevel();
            }
        if (_fight.Target.IsEmpty() && inCombat && _nowMs - _fight.StartMs < 2000)
            return; // aggro without a visible opponent yet: retry next tick
        EmitFightStart(bot);
        return;
    }
    // coalesce flapping: the fight ends only after ~2 s without combat state and without a hit in either direction
    if (inCombat || _nowMs - _fight.LastCombatMs < 2000)
        return;

    char const* outcome = "other";
    if (_fight.Kills > 0)
        outcome = "target_killed";
    else if (!_fight.Dealt && !_fight.Taken)
        outcome = "target_left_combat";
    else if (_fight.FleeReason || (_motion.HasGoal() && bot->isMoving()))
        outcome = "bot_fled";
    else if (_fight.Dealt > 0)
        outcome = "target_evaded_or_reset";
    EmitFightEnd(bot, outcome, _fight.LastCombatMs);
}

void BotAI::EmitFightStart(Player* bot)
{
    uint64 const logStart = NowNs();
    _fight.StartLogged = true;

    Unit* target = _fight.Target.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, _fight.Target);
    if (!target)
        target = bot->GetVictim();
    uint32 const entry = target ? target->GetEntry() : _fight.TargetEntry;
    uint32 const level = target ? target->GetLevel() : _fight.TargetLevel;
    std::string name = target ? JsonEscape(target->GetName()) : CreatureNameOf(entry);

    std::string tj = Trinity::StringFormat(R"({{"guid":"{}","entry":{},"name":"{}","level":{})", _fight.Target.ToString(), entry, name, level);
    if (level)
        tj += Trinity::StringFormat(R"(,"lvl_diff":{})", int32(level) - int32(bot->GetLevel()));
    if (CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry))
        tj += Trinity::StringFormat(R"(,"rank":"{}")", RankName(t->Classification));
    if (target)
        tj += Trinity::StringFormat(R"(,"dist":{:.1f})", bot->GetDistance(target));
    tj += '}';

    Powers const pt = bot->GetPowerType();
    std::string d = Trinity::StringFormat(R"({{"fight_id":"{}","source":"{}","first":"{}","target":{},"hp":{},"hp_pct":{:.0f},"power":{{"type":"{}","v":{},"max":{}}},"hostiles_30yd":{},"activity":{}}})",
        _fight.Id, IsTestSource() ? "test_command" : "bot", FirstName(_fight.First), tj,
        uint32(bot->GetHealth()), bot->GetHealthPct(), PowerName(pt), bot->GetPower(pt), bot->GetMaxPower(pt), CountHostiles(bot), ActivityJson(bot));

    {
        // Crowding: other bots within 40 yd, and whether another bot already tagged or is fighting this mob.
        uint32 nearBots = 0;
        if (Map* map = bot->GetMap())
            for (MapReference const& ref : map->GetPlayers())
            {
                Player* p = ref.GetSource();
                if (p && p != bot && p->GetSession() && p->GetSession()->GetBotAI() && bot->IsWithinDist(p, 40.0f))
                    ++nearBots;
            }
        bool claimed = false;
        ObjectGuid claimer;
        if (Creature* c = target ? target->ToCreature() : nullptr)
        {
            for (ObjectGuid const& g : c->GetTapList())
            {
                if (g == bot->GetGUID())
                    continue;
                Player* p = ObjectAccessor::FindPlayer(g);
                if (p && p->GetSession() && p->GetSession()->GetBotAI())
                {
                    claimed = true;
                    claimer = g;
                    break;
                }
            }
            if (!claimed)
                if (Unit* v = c->GetVictim())
                    if (v != bot && v->IsPlayer() && v->ToPlayer()->GetSession() && v->ToPlayer()->GetSession()->GetBotAI())
                    {
                        claimed = true;
                        claimer = v->GetGUID();
                    }
        }
        d.pop_back();
        d += Trinity::StringFormat(R"(,"nearby_bots":{},"nearby_bots_yd":40,"claimed_by_other_bot":{})", nearBots, claimed ? "true" : "false");
        if (claimed)
            d += Trinity::StringFormat(R"(,"claimed_by":"{}")", claimer.ToString());

        // Pull context: where the mob stands and the pack around it (other hostile creatures within 20 yd of the target).
        if (target)
        {
            d += Trinity::StringFormat(R"(,"mob_pos":{{"x":{:.1f},"y":{:.1f},"z":{:.1f}}})", target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
            std::list<Unit*> around;
            Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, bot, 20.0f);
            Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(target, around, check);
            Cell::VisitAllObjects(target, searcher, 20.0f);
            uint32 packSize = 0, listed = 0;
            std::string pack;
            for (Unit* u : around)
            {
                if (u == target || !u->IsCreature())
                    continue;
                ++packSize;
                if (listed >= 8)
                    continue;
                if (listed++)
                    pack += ',';
                pack += Trinity::StringFormat(R"({{"entry":{},"level":{},"dist":{:.0f}}})", u->GetEntry(), uint32(u->GetLevel()), target->GetDistance(u));
            }
            d += Trinity::StringFormat(R"(,"pack_20yd":{})", packSize);
            if (listed)
                d += ",\"pack\":[" + pack + "]";
        }
        d += '}';
    }

    BotEvent event = MakeEvent(bot, "combat", BOTLOG_INFO, "COMBAT_START", Trinity::StringFormat("combat start vs {} L{}", name, level));
    if (entry)
        event.TargetEntry = entry;
    event.Details = std::move(d);
    Emit(std::move(event));
    _stats.LogNs.fetch_add(NowNs() - logStart, std::memory_order_relaxed);
}

void BotAI::EmitFightEnd(Player* bot, char const* outcome, uint32 endMs)
{
    uint64 const logStart = NowNs();
    uint32 const duration = endMs > _fight.StartMs ? endMs - _fight.StartMs : 0;
    bool const alive = bot && bot->IsAlive();
    uint32 const maxHp = bot ? std::max<uint32>(1, uint32(bot->GetMaxHealth())) : 1;
    std::string d = Trinity::StringFormat(R"({{"fight_id":"{}","source":"{}","outcome":"{}","first":"{}","duration_s":{:.1f},"dealt":{},"taken":{},"kills":{},"hp_start":{},"hp_end":{},"hp_min":{},"hp_min_pct":{:.0f},"target":{{"guid":"{}","entry":{},"level":{}}}}})",
        _fight.Id, IsTestSource() ? "test_command" : "bot", outcome, FirstName(_fight.First), float(duration) / 1000.0f,
        _fight.Dealt, _fight.Taken, _fight.Kills, _fight.HpStart, alive ? uint32(bot->GetHealth()) : 0u, _fight.HpMin, 100.0f * float(_fight.HpMin) / float(maxHp),
        _fight.Target.ToString(), _fight.TargetEntry, _fight.TargetLevel);
    if (_fight.FleeReason)
    {
        d.pop_back();
        d += Trinity::StringFormat(R"(,"flee_reason":"{}"}})", _fight.FleeReason);
    }
    if (alive)
    {
        Powers const pt = bot->GetPowerType();
        d.pop_back();
        d += Trinity::StringFormat(R"(,"power_end":{{"type":"{}","v":{},"max":{}}}}})", PowerName(pt), bot->GetPower(pt), bot->GetMaxPower(pt));
    }

    BotEvent event = MakeEvent(bot, "combat", BOTLOG_INFO, "COMBAT_END", Trinity::StringFormat("combat end: {} ({:.0f}s, dealt {}, taken {})", outcome, float(duration) / 1000.0f, _fight.Dealt, _fight.Taken));
    if (_fight.TargetEntry)
        event.TargetEntry = _fight.TargetEntry;
    event.Details = std::move(d);
    Emit(std::move(event));
    _fight = Fight();
    _stats.LogNs.fetch_add(NowNs() - logStart, std::memory_order_relaxed);
}

// Builds the death-row details extension (leading comma, no closing brace) while the bot's auras, power, target and combat references
// still exist: called from Unit::Kill before the death state strips them. Falls back to what is left when called late.
void BotAI::SnapshotDeath(Player* bot, Unit* attacker)
{
    uint32 const now = _nowMs;
    constexpr uint32 WINDOW = 10000;
    HitRec const* last = _hitCount ? &_hits[(_hitNext + HIT_RING - 1) % HIT_RING] : nullptr;
    bool const lethal = last && now - last->Ms <= 1500;

    std::string j;
    j.reserve(2048);
    j += Trinity::StringFormat(R"(,"source":"{}")", IsTestSource() ? "test_command" : "bot");
    if (char const* cmd = _testCmd.load(std::memory_order_relaxed); cmd && IsTestSource())
        j += Trinity::StringFormat(R"(,"test_command":"{}")", cmd);
    if (!_fight.Active)
        j += R"(,"in_fight":false)";

    // --- killer: always resolved, even for non-melee / out-of-window kills (resolved_by says how) ---
    ObjectGuid ksrc;
    uint32 kentry = 0, klevel = 0;
    char const* resolvedBy = "none";
    if (lethal && last->Kind != 3 && !last->Src.IsEmpty())
    {
        ksrc = last->Src;
        kentry = last->Entry;
        klevel = last->Level;
        resolvedBy = "lethal_hit";
    }
    else if (attacker && attacker != bot)
    {
        ksrc = attacker->GetGUID();
        kentry = attacker->GetEntry();
        klevel = attacker->GetLevel();
        resolvedBy = "attacker";
    }
    if (ksrc.IsEmpty())
    {
        // newest non-environment hit with a known source inside the window
        for (uint32 i = 0; i < _hitCount; ++i)
        {
            HitRec const& h = _hits[(_hitNext + HIT_RING - 1 - i) % HIT_RING];
            if (now - h.Ms > WINDOW)
                break;
            if (h.Kind != 3 && !h.Src.IsEmpty())
            {
                ksrc = h.Src;
                kentry = h.Entry;
                klevel = h.Level;
                resolvedBy = "recent_hit";
                break;
            }
        }
    }
    if (ksrc.IsEmpty() && !_fight.Target.IsEmpty())
    {
        ksrc = _fight.Target;
        kentry = _fight.TargetEntry;
        klevel = _fight.TargetLevel;
        resolvedBy = "fight_target";
    }
    if (ksrc.IsEmpty() && !bot->getAttackers().empty())
    {
        Unit* a = *bot->getAttackers().begin();
        ksrc = a->GetGUID();
        kentry = a->GetEntry();
        klevel = a->GetLevel();
        resolvedBy = "attackers_set";
    }
    char const* ktype = lethal && last->Kind == 3 ? "environment" : !ksrc.IsEmpty() ? (ksrc.IsPlayer() ? "player" : "creature") : attacker == bot ? "self" : "unknown";
    Unit* k = ksrc.IsEmpty() ? nullptr : (attacker && attacker->GetGUID() == ksrc ? attacker : ObjectAccessor::GetUnit(*bot, ksrc));
    if (k)
    {
        kentry = k->GetEntry();
        klevel = k->GetLevel();
    }

    std::string kj = Trinity::StringFormat(R"({{"type":"{}")", ktype);
    if (!ksrc.IsEmpty())
    {
        std::string const name = k ? JsonEscape(k->GetName()) : CreatureNameOf(kentry);
        kj += Trinity::StringFormat(R"(,"guid":"{}","entry":{},"name":"{}","level":{},"lvl_diff":{},"resolved_by":"{}")", ksrc.ToString(), kentry, name.empty() ? std::string("?") : name, klevel, int32(klevel) - int32(bot->GetLevel()), resolvedBy);
        if (k)
            kj += Trinity::StringFormat(R"(,"faction":{})", k->GetFaction());
        else if (!ksrc.IsPlayer())
            if (CreatureTemplate const* ft = sObjectMgr->GetCreatureTemplate(kentry))
                kj += Trinity::StringFormat(R"(,"faction":{})", ft->faction);
        if (!ksrc.IsPlayer())
            if (CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(kentry))
                kj += Trinity::StringFormat(R"(,"rank":"{}","elite":{})", RankName(t->Classification),
                    (t->Classification == CreatureClassifications::Elite || t->Classification == CreatureClassifications::RareElite) ? "true" : "false");
        if (k && !k->GetOwnerGUID().IsEmpty())
            kj += Trinity::StringFormat(R"(,"owner":"{}")", k->GetOwnerGUID().ToString());
    }
    if (lethal)
    {
        kj += Trinity::StringFormat(R"(,"hit":"{}")", HitKindName(last->Kind));
        if (last->Spell)
            kj += Trinity::StringFormat(R"(,"spell":{},"spell_name":"{}")", last->Spell, SpellNameOf(last->Spell));
        if (last->Kind == 3)
            kj += Trinity::StringFormat(R"(,"env":"{}")", EnvName(last->Env));
        kj += Trinity::StringFormat(R"(,"dmg":{},"hp_before":{})", last->Dmg, last->HpBefore);
    }
    kj += '}';
    j += ",\"killer\":" + kj;
    {
        // the mob the bot was fighting (fight target, else its victim), so killer and target sit on one row
        ObjectGuid tguid = _fight.Target;
        Unit* tu = tguid.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, tguid);
        if (!tu)
            tu = bot->GetVictim();
        uint32 const tentry = tu ? tu->GetEntry() : _fight.TargetEntry;
        uint32 const tlevel = tu ? tu->GetLevel() : _fight.TargetLevel;
        if (tentry)
        {
            std::string const tname = tu ? JsonEscape(tu->GetName()) : CreatureNameOf(tentry);
            j += Trinity::StringFormat(R"(,"target":{{"guid":"{}","entry":{},"name":"{}","level":{},"lvl_diff":{},"is_killer":{}}})",
                (tu ? tu->GetGUID() : tguid).ToString(), tentry, tname, tlevel, int32(tlevel) - int32(bot->GetLevel()), (!ksrc.IsEmpty() && ksrc == (tu ? tu->GetGUID() : tguid)) ? "true" : "false");
        }
    }
    _deathKillerEntry = ksrc.IsEmpty() || ksrc.IsPlayer() ? 0 : kentry;

    // --- damage taken in the last 10 s ---
    struct SrcAgg { ObjectGuid Guid; uint32 Entry; uint32 Dmg; uint32 Hits; };
    struct SpAgg { uint32 Id; uint8 Kind; uint32 Entry; uint32 Dmg; uint32 Hits; };
    std::vector<SrcAgg> sources;
    std::vector<SpAgg> spells;
    uint32 total = 0, hits = 0, oldestAgo = 0;
    for (uint32 i = 0; i < _hitCount; ++i)
    {
        HitRec const& h = _hits[(_hitNext + HIT_RING - 1 - i) % HIT_RING];
        if (now - h.Ms > WINDOW)
            break;
        total += h.Dmg;
        ++hits;
        oldestAgo = now - h.Ms;
        auto s = std::find_if(sources.begin(), sources.end(), [&](SrcAgg const& a) { return a.Guid == h.Src; });
        if (s == sources.end())
            sources.push_back({ h.Src, h.Entry, h.Dmg, 1 });
        else
        {
            s->Dmg += h.Dmg;
            ++s->Hits;
        }
        uint32 const spellKey = h.Kind == 3 ? uint32(h.Env) : h.Spell;
        auto sp = std::find_if(spells.begin(), spells.end(), [&](SpAgg const& a) { return a.Id == spellKey && a.Kind == h.Kind && a.Entry == h.Entry; });
        if (sp == spells.end())
            spells.push_back({ spellKey, h.Kind, h.Entry, h.Dmg, 1 });
        else
        {
            sp->Dmg += h.Dmg;
            ++sp->Hits;
        }
    }
    std::sort(sources.begin(), sources.end(), [](SrcAgg const& a, SrcAgg const& b) { return a.Dmg > b.Dmg; });
    std::sort(spells.begin(), spells.end(), [](SpAgg const& a, SpAgg const& b) { return a.Dmg > b.Dmg; });

    if (hits)
    {
        std::string dj = Trinity::StringFormat(R"({{"window_s":10,"total":{},"hits":{},"first_hit_s_before_death":{:.1f},"started_by":"{}")", total, hits,
            float(oldestAgo) / 1000.0f, FirstName(_fight.First));
        if (_fight.Active)
            dj += Trinity::StringFormat(R"(,"fight_s":{:.1f})", float(now - _fight.StartMs) / 1000.0f);
        dj += ",\"sources\":[";
        for (size_t i = 0; i < sources.size() && i < 6; ++i)
        {
            SrcAgg const& s = sources[i];
            if (i)
                dj += ',';
            dj += Trinity::StringFormat(R"({{"entry":{},"guid":"{}","name":"{}","dmg":{},"hits":{}}})", s.Entry, s.Guid.ToString(),
                s.Entry ? CreatureNameOf(s.Entry) : std::string("environment"), s.Dmg, s.Hits);
        }
        dj += "],\"spells\":[";
        for (size_t i = 0; i < spells.size() && i < 8; ++i)
        {
            SpAgg const& s = spells[i];
            if (i)
                dj += ',';
            if (s.Kind == 3)
                dj += Trinity::StringFormat(R"({{"env":"{}","dmg":{},"hits":{}}})", EnvName(uint8(s.Id)), s.Dmg, s.Hits);
            else if (s.Id)
                dj += Trinity::StringFormat(R"({{"id":{},"name":"{}","kind":"{}","src":{},"dmg":{},"hits":{}}})", s.Id, SpellNameOf(s.Id), HitKindName(s.Kind), s.Entry, s.Dmg, s.Hits);
            else
                dj += Trinity::StringFormat(R"({{"name":"melee","src":{},"dmg":{},"hits":{}}})", s.Entry, s.Dmg, s.Hits);
        }
        dj += "]}";
        j += ",\"damage\":" + dj;
    }

    // --- the bot's own damage in the window ---
    uint32 dealt = 0;
    for (uint32 i = 0; i < _dealtCount; ++i)
    {
        DealtRec const& r = _dealt[(_dealtNext + HIT_RING - 1 - i) % HIT_RING];
        if (now - r.Ms > WINDOW)
            break;
        dealt += r.Dmg;
    }

    // --- resources: power from the newest 1 s sample (the live value may already be zeroed) and ~10 s earlier ---
    Powers const pt = bot->GetPowerType();
    VitalSample const* newest = _sampleCount ? &_samples[(_sampleNext + SAMPLE_RING - 1) % SAMPLE_RING] : nullptr;
    VitalSample const* oldest = nullptr;
    for (uint32 i = 0; i < _sampleCount; ++i)
    {
        VitalSample const& s = _samples[(_sampleNext + SAMPLE_RING - 1 - i) % SAMPLE_RING];
        if (now - s.Ms > WINDOW + 500)
            break;
        oldest = &s;
    }
    uint32 const powerNow = newest && now - newest->Ms <= 2500 ? newest->Power : uint32(std::max<int32>(0, bot->GetPower(pt)));
    std::string rj = Trinity::StringFormat(R"({{"power":"{}","at_death":{},"max":{})", PowerName(pt), powerNow, bot->GetMaxPower(pt));
    if (oldest && oldest != newest)
        rj += Trinity::StringFormat(R"(,"10s_ago":{},"hp_10s_ago":{})", oldest->Power, oldest->Hp);
    rj += Trinity::StringFormat(R"(,"hp_max":{},"armor":{},"level":{},"class":{},"race":{})", uint32(bot->GetMaxHealth()), bot->GetArmor(), bot->GetLevel(), uint32(bot->GetClass()), uint32(bot->GetRace()));
    {
        uint32 equipped = 0, ilvlSum = 0, broken = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item const* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                ++equipped;
                ilvlSum += item->GetTemplate()->GetBaseItemLevel();
                if (item->IsBroken())
                    ++broken;
            }
        rj += Trinity::StringFormat(R"(,"gear":{{"equipped":{},"avg_ilvl":{:.0f},"broken":{}}}}})", equipped, equipped ? float(ilvlSum) / float(equipped) : 0.0f, broken);
    }
    j += ",\"resources\":" + rj;

    // --- combat context ---
    Unit* victim = bot->GetVictim();
    std::string cj = Trinity::StringFormat(R"({{"attackers":{},"hostiles_30yd":{},"dealt_10s":{})", uint32(bot->getAttackers().size()), CountHostiles(bot), dealt);
    if (victim)
        cj += Trinity::StringFormat(R"(,"target":{{"guid":"{}","entry":{}}})", victim->GetGUID().ToString(), victim->GetEntry());
    else if (!_fight.Target.IsEmpty())
        cj += Trinity::StringFormat(R"(,"target":{{"guid":"{}","entry":{}}})", _fight.Target.ToString(), _fight.TargetEntry);
    cj += ",\"activity\":" + ActivityJson(bot) + ",\"strategies\":{";
    for (uint32 s = 0; s < BOT_STATE_COUNT; ++s)
    {
        if (s)
            cj += ',';
        cj += Trinity::StringFormat("\"{}\":[", BotStateName(BotState(s)));
        bool firstName = true;
        for (std::string const& name : _engines[s]->GetStrategies())
        {
            cj += (firstName ? "\"" : ",\"") + JsonEscape(name) + "\"";
            firstName = false;
        }
        cj += ']';
    }
    cj += "}}";
    j += ",\"combat\":" + cj;

    // --- environment: other bots nearby ---
    {
        uint32 nearBots = 0, fighting = 0;
        if (Map* map = bot->GetMap())
            for (MapReference const& ref : map->GetPlayers())
            {
                Player* p = ref.GetSource();
                if (!p || p == bot || !p->GetSession() || !p->GetSession()->GetBotAI() || !bot->IsWithinDist(p, 30.0f))
                    continue;
                ++nearBots;
                if (p->IsInCombat())
                    ++fighting;
            }
        j += Trinity::StringFormat(R"(,"bots_30yd":{},"bots_30yd_fighting":{})", nearBots, fighting);
    }

    // --- repeat counters (process lifetime) ---
    {
        uint32 botDeaths, killerZone = 0;
        uint32 const zone = bot->GetZoneId();
        {
            std::lock_guard lock(_repeatLock);
            if (_botDeaths.size() >= DEATH_MAP_CAP && !_botDeaths.contains(_guid))
                _botDeaths.clear(); // counters restart; the cap only guards against unbounded growth
            botDeaths = ++_botDeaths[_guid];
            if (_deathKillerEntry)
            {
                if (_killerZoneDeaths.size() >= DEATH_MAP_CAP && !_killerZoneDeaths.contains({ _deathKillerEntry, zone }))
                    _killerZoneDeaths.clear();
                killerZone = ++_killerZoneDeaths[{ _deathKillerEntry, zone }];
            }
        }
        j += Trinity::StringFormat(R"(,"repeat":{{"bot":{})", botDeaths);
        if (killerZone)
            j += Trinity::StringFormat(R"(,"killer_zone":{})", killerZone);
        j += '}';
    }

    // --- auras and crowd control flags, then the hp trajectory (dropped if the row is already large) ---
    {
        std::string auras;
        uint32 listed = 0, shown = 0;
        for (auto const& pr : bot->GetAppliedAuras())
        {
            AuraApplication const* app = pr.second;
            Aura const* aura = app->GetBase();
            SpellInfo const* si = aura->GetSpellInfo();
            if (si->IsPassive())
                continue;
            ++listed;
            if (shown >= 20)
                continue;
            if (shown++)
                auras += ',';
            auras += Trinity::StringFormat(R"({{"id":{},"name":"{}","pos":{},"stacks":{})", si->Id, SpellNameOf(si->Id), app->IsPositive() ? "true" : "false", uint32(aura->GetStackAmount()));
            if (aura->GetDuration() >= 0)
                auras += Trinity::StringFormat(R"(,"ms":{})", aura->GetDuration());
            ObjectGuid const caster = aura->GetCasterGUID();
            if (!caster.IsEmpty() && caster != bot->GetGUID())
                auras += Trinity::StringFormat(R"(,"caster":"{}")", caster.ToString());
            auras += '}';
        }
        if (listed)
            j += Trinity::StringFormat(R"(,"auras":[{}],"auras_total":{})", auras, listed);

        std::string cc;
        auto flag = [&](bool on, char const* name) { if (on) cc += (cc.empty() ? "\"" : ",\"") + std::string(name) + "\""; };
        flag(bot->HasAuraType(SPELL_AURA_MOD_STUN), "stun");
        flag(bot->HasAuraType(SPELL_AURA_MOD_ROOT), "root");
        flag(bot->HasAuraType(SPELL_AURA_MOD_SILENCE), "silence");
        flag(bot->HasAuraType(SPELL_AURA_MOD_FEAR), "fear");
        flag(bot->HasAuraType(SPELL_AURA_MOD_CONFUSE), "confuse");
        flag(bot->HasAuraType(SPELL_AURA_MOD_PACIFY) || bot->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE), "pacify");
        flag(bot->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED), "slow");
        flag(bot->HasBreakableByDamageCrowdControlAura(), "incapacitate");
        if (!cc.empty())
            j += ",\"cc\":[" + cc + "]";

        if (j.size() < 4500)
        {
            std::string traj;
            for (uint32 n = 0; n < _sampleCount; ++n)
            {
                VitalSample const& s = _samples[(_sampleNext + SAMPLE_RING - _sampleCount + n) % SAMPLE_RING];
                if (now - s.Ms > WINDOW + 500)
                    continue;
                if (!traj.empty())
                    traj += ',';
                traj += Trinity::StringFormat("[{},{}]", now - s.Ms, s.Hp);
            }
            if (!traj.empty())
                j += ",\"hp_traj\":[" + traj + "]";
        }
    }

    _deathFightId = _fight.Id;
    _deathJson = std::move(j);
    _deathSnap = true;
}


// ---------------------------------------------------------------------------------------------------------------------
// progression and spell telemetry (docs/playerbots/engine-design.md "Progression and spell events")
// ---------------------------------------------------------------------------------------------------------------------
BotAI::SpellStat& BotAI::StatFor(uint32 id)
{
    for (SpellStat& st : _spellStats)
        if (st.Id == id)
            return st;
    _spellStats.push_back({ id, 0, 0, 0, 0 });
    return _spellStats.back();
}

void BotAI::OnSpellCast(Player* bot, SpellInfo const* spell, uint32 power, Unit* target, bool triggered)
{
    if (!spell)
        return;
    SpellStat& st = StatFor(spell->Id);
    ++st.Casts;
    st.Power += power;

    // CAST_OK row: once per spell per fight, out of a fight once per spell per 10 s; passive spells never
    if (spell->IsPassive())
        return;
    if (_fight.Active && _fight.Id != _castFightId)
    {
        _castFightId = _fight.Id;
        _castSeen.fill({ 0, 0 });
    }
    CastSeen* slot = nullptr;
    for (CastSeen& c : _castSeen)
        if (c.Id == spell->Id)
            slot = &c;
    if (slot && (_fight.Active || _nowMs - slot->Ms < 10000))
        return;
    if (!slot)
        slot = &_castSeen[_castSeenNext++ % _castSeen.size()];
    *slot = { spell->Id, _nowMs };

    std::string d = Trinity::StringFormat(R"({{"spell_id":{},"spell_name":"{}","power":{},"triggered":{},"hp_pct":{:.0f})",
        spell->Id, SpellNameOf(spell->Id), power, triggered ? "true" : "false", bot->GetHealthPct());
    uint32 tentry = 0;
    if (target)
    {
        tentry = target->GetEntry();
        d += Trinity::StringFormat(R"(,"target":{{"kind":"{}","entry":{},"level":{},"dist":{:.1f}}})", target == bot ? "self" : target->IsPlayer() ? "player" : "creature",
            tentry, uint32(target->GetLevel()), bot->GetDistance(target));
    }
    if (_fight.Active)
        d += Trinity::StringFormat(R"(,"fight_id":"{}")", _fight.Id);
    d += '}';
    BotEvent event = MakeEvent(bot, "cast", BOTLOG_INFO, "CAST_OK", Trinity::StringFormat("cast {}", SpellNameOf(spell->Id)));
    if (tentry && target != bot)
        event.TargetEntry = tentry;
    event.Details = std::move(d);
    Emit(std::move(event));
}

void BotAI::OnAuraChange(Player* bot, AuraApplication const* app, bool applied)
{
    Aura const* aura = app->GetBase();
    SpellInfo const* si = aura->GetSpellInfo();
    if (si->IsPassive() || !bot->IsInWorld())
        return;
    uint32 const mode = app->GetRemoveMode();
    if (!applied && mode == AURA_REMOVE_BY_DEATH)
        return;

    // at most 12 aura rows per bot per 10 s; the overflow is counted and reported on the next row
    if (_nowMs - _auraWinMs >= 10000)
    {
        _auraWinMs = _nowMs;
        _auraWinCount = 0;
    }
    if (_auraWinCount >= 12)
    {
        ++_auraSuppressed;
        return;
    }
    ++_auraWinCount;

    static char const* const modes[] = { "none", "default", "interrupt", "cancel", "enemy_spell", "expire", "death" };
    ObjectGuid const caster = aura->GetCasterGUID();
    std::string d = Trinity::StringFormat(R"({{"spell_id":{},"spell_name":"{}","applied":{},"positive":{},"stacks":{})", si->Id, SpellNameOf(si->Id), applied ? "true" : "false",
        app->IsPositive() ? "true" : "false", uint32(aura->GetStackAmount()));
    if (aura->GetDuration() >= 0)
        d += Trinity::StringFormat(R"(,"duration_ms":{})", aura->GetDuration());
    if (!caster.IsEmpty())
        d += Trinity::StringFormat(R"(,"caster":{{"kind":"{}","entry":{}}})", caster == bot->GetGUID() ? "self" : caster.IsPlayer() ? "player" : "creature", caster.IsPlayer() ? 0u : caster.GetEntry());
    if (!applied)
        d += Trinity::StringFormat(R"(,"remove_mode":"{}")", mode < std::size(modes) ? modes[mode] : "other");
    if (_fight.Active)
        d += Trinity::StringFormat(R"(,"fight_id":"{}")", _fight.Id);
    if (_auraSuppressed)
    {
        d += Trinity::StringFormat(R"(,"suppressed_before":{})", _auraSuppressed);
        _auraSuppressed = 0;
    }
    d += '}';
    BotEvent event = MakeEvent(bot, "aura", BOTLOG_INFO, applied ? "AURA_APPLIED" : "AURA_REMOVED", Trinity::StringFormat("{} {}", applied ? "gained" : "lost", SpellNameOf(si->Id)));
    if (!caster.IsEmpty() && !caster.IsPlayer())
        event.TargetEntry = caster.GetEntry();
    event.Details = std::move(d);
    Emit(std::move(event));
}

void BotAI::OnXpGain(Player* bot, uint32 amount, uint32 bonus, Unit* victim)
{
    char const* source = victim ? "kill" : (_xpSource ? _xpSource : "other");
    uint32 const questId = victim ? 0 : _xpQuest;
    _xpSource = nullptr;
    _xpQuest = 0;
    if (victim)
        _killXpTotal += amount;

    std::string d = Trinity::StringFormat(R"({{"source":"{}","amount":{},"bonus":{},"xp_now":{},"xp_max":{},"level":{},"elapsed_s":{})",
        source, amount, bonus, bot->GetXP(), bot->GetXPForNextLevel(), uint32(bot->GetLevel()), _nowMs / 1000);
    if (victim)
        d += Trinity::StringFormat(R"(,"victim":{{"entry":{},"level":{},"name":"{}"}})", victim->GetEntry(), uint32(victim->GetLevel()), JsonEscape(victim->GetName()));
    d += '}';
    BotEvent event = MakeEvent(bot, "xp", BOTLOG_INFO, "XP_GAIN", Trinity::StringFormat("+{} xp ({})", amount + bonus, source));
    if (victim)
        event.TargetEntry = victim->GetEntry();
    if (questId)
        event.QuestId = questId;
    event.Details = std::move(d);
    Emit(std::move(event));
}

void BotAI::OnLevelUp(Player* bot, uint8 oldLevel, uint8 newLevel)
{
    uint32 const sinceLast = _nowMs - _levelSinceMs;
    _levelSinceMs = _nowMs;
    BotEvent event = MakeEvent(bot, "level_up", BOTLOG_INFO, "LEVEL_UP", Trinity::StringFormat("level {} -> {}", uint32(oldLevel), uint32(newLevel)));
    event.Details = Trinity::StringFormat(R"({{"from":{},"to":{},"elapsed_s":{},"level_time_s":{},"kill_xp_total":{}}})",
        uint32(oldLevel), uint32(newLevel), _nowMs / 1000, sinceLast / 1000, _killXpTotal);
    Emit(std::move(event));
    EmitSpellsKnown(bot, "LEVEL_UP");
}

// Highest-rank, non-passive spells with a class spell family: a cheap stand-in for "the class spells this bot could cast".
std::vector<uint32> BotAI::KnownSpellIds(Player* bot) const
{
    std::vector<uint32> ids;
    for (auto const& [id, ps] : bot->GetSpellMap())
    {
        if (ps.state == PLAYERSPELL_REMOVED || ps.disabled || !ps.active)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!si || si->IsPassive() || si->SpellFamilyName == SPELLFAMILY_GENERIC)
            continue;
        uint32 const next = sSpellMgr->GetNextSpellInChain(id);
        if (next && bot->HasSpell(next))
            continue;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void BotAI::EmitSpellsKnown(Player* bot, char const* cause)
{
    std::vector<uint32> const ids = KnownSpellIds(bot);
    std::string list;
    uint32 shown = 0;
    for (uint32 id : ids)
    {
        if (shown++ >= 80)
            break;
        if (!list.empty())
            list += ',';
        list += Trinity::StringFormat(R"({{"id":{},"name":"{}","rank":{}}})", id, SpellNameOf(id), uint32(sSpellMgr->GetSpellRank(id)));
    }
    BotEvent event = MakeEvent(bot, "spells", BOTLOG_INFO, "SPELLS_KNOWN", Trinity::StringFormat("{} class spells known at L{}", ids.size(), uint32(bot->GetLevel())));
    event.Details = Trinity::StringFormat(R"({{"cause":"{}","count":{},"spellbook_total":{},"spells":[{}]}})", cause, ids.size(), bot->GetSpellMap().size(), list);
    Emit(std::move(event));
}

// Returns JSON members (no braces) to append inside the COMBAT_SUMMARY details object, and resets the per-stay counters.
std::string BotAI::TakeSpellBreakdownJson(Player* bot)
{
    std::vector<SpellStat> stats = std::move(_spellStats);
    _spellStats.clear();
    std::sort(stats.begin(), stats.end(), [](SpellStat const& a, SpellStat const& b) { return a.Dmg + a.Casts > b.Dmg + b.Casts; });
    std::string j = "\"spell_stats\":[";
    for (size_t i = 0; i < stats.size() && i < 16; ++i)
    {
        SpellStat const& st = stats[i];
        if (i)
            j += ',';
        j += Trinity::StringFormat(R"({{"id":{},"name":"{}","casts":{},"hits":{},"dmg":{},"power":{}}})", st.Id, st.Id ? SpellNameOf(st.Id) : std::string("melee"), st.Casts, st.Hits, st.Dmg, st.Power);
    }
    j += Trinity::StringFormat(R"(],"power_type":"{}","unused_spells":[)", PowerName(bot->GetPowerType()));
    uint32 shown = 0;
    for (uint32 id : KnownSpellIds(bot))
    {
        bool used = false;
        for (SpellStat const& st : stats)
            if (st.Id == id && st.Casts)
            {
                used = true;
                break;
            }
        if (used)
            continue;
        if (shown >= 15)
            break;
        j += Trinity::StringFormat(R"({}{{"id":{},"name":"{}"}})", shown++ ? "," : "", id, SpellNameOf(id));
    }
    j += ']';
    return j;
}

void BotAI::EmitTickStats(Player* bot)
{
    uint32 const windowS = (_nowMs - _tickStatsMs) / 1000;
    _tickStatsMs = _nowMs;
    std::string engines;
    uint32 total = 0;
    for (uint32 st = 0; st < 3; ++st)
    {
        EngStat& e = _engStat[st];
        total += e.N;
        if (e.N)
            engines += Trinity::StringFormat(R"({}"{}":{{"n":{},"avg_ms":{:.3f},"max_ms":{:.3f}}})", engines.empty() ? "" : ",", BotStateName(BotState(st)), e.N,
                double(e.Ns) / double(e.N) / 1e6, double(e.MaxNs) / 1e6);
        e = EngStat();
    }
    if (!total)
        return;
    BotEvent event = MakeEvent(bot, "ai_stats", BOTLOG_INFO, "AI_TICK_STATS", Trinity::StringFormat("engine tick cost over {} s ({} ticks)", windowS, total));
    event.Details = Trinity::StringFormat(R"({{"window_s":{},"engines":{{{}}}}})", windowS, engines);
    Emit(std::move(event));
}
