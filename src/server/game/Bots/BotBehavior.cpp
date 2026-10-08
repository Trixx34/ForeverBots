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

// Phase 3 behaviors of the bot engine (non-combat basics), see docs/playerbots/engine-design.md:
//   goto     (NonCombat)  walk to a goal with mmap pathfinding, arrival, path_fail and stuck detection
//   follow   (NonCombat)  keep within a few yards of a leader on the same map
//   stay     (NonCombat)  hold position: stops walking and forbids every action flagged ACTION_FLAG_MOVES
//   rest     (NonCombat)  sit and eat/drink when health/mana is low (outranks walking)
//   recover  (Dead)       release spirit after a short delay, corpse run or spirit healer, resurrection sickness logged
// Every piece is a strategy built from triggers and actions; they are switched with `bot strategy <bot> +name|-name`.

#include "BotAI.h"
#include "BotCombat.h"
#include "Config.h"
#include "Corpse.h"
#include "BotTownIdlePlan.h"
#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MapUtils.h"
#include "MiscPackets.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "NPCPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "RestMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <atomic>

namespace
{
using Trinity::StringFormat;

std::string Pos3(float x, float y, float z) { return StringFormat(R"({{"x":{:.1f},"y":{:.1f},"z":{:.1f}}})", x, y, z); }

constexpr float CORPSE_RECLAIM_DIST = 35.0f; // the core accepts 39 (CORPSE_RECLAIM_RADIUS), keep a margin

// ---------------------------------------------------------------------------------------------------------------------
// Eat / drink spells. The food and drink spells of the vanilla consumables, picked by the bot's level. Validated against the
// spell data when the registry is built (a missing or wrong spell is dropped and logged, never used).
// ---------------------------------------------------------------------------------------------------------------------
struct Consumable { uint8 MinLevel; uint32 SpellId; };
std::vector<Consumable> g_food, g_drink;

void BuildConsumableTables()
{
    // item -> spell of the basic conjured/vendor food and water per level bracket (Classic data: Tough Jerky 433, Tough Hunk of Bread 434,
    // Freshly Baked Bread 435, Moist Cornbread 1127, Mulgore Spice Bread 1129, Soft Banana Bread 2639; Refreshing Spring Water 430,
    // Ice Cold Milk 431, Melon Juice 432, Sweet Nectar 1133, Moonberry Juice 1135, Morning Glory Dew 1137)
    static constexpr Consumable food[] = { { 1, 433 }, { 5, 434 }, { 15, 435 }, { 25, 1127 }, { 35, 1129 }, { 45, 2639 } };
    static constexpr Consumable drink[] = { { 1, 430 }, { 5, 431 }, { 15, 432 }, { 25, 1133 }, { 35, 1135 }, { 45, 1137 } };

    std::string accepted, rejected;
    auto check = [&](Consumable const& c, bool isFood)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(c.SpellId, DIFFICULTY_NONE);
        bool ok = false;
        if (info)
        {
            if (isFood)
                ok = info->HasAura(SPELL_AURA_MOD_REGEN) || info->HasAura(SPELL_AURA_OBS_MOD_HEALTH) || info->HasAura(SPELL_AURA_PERIODIC_HEAL);
            else
                ok = info->HasAura(SPELL_AURA_MOD_POWER_REGEN) || info->HasAura(SPELL_AURA_OBS_MOD_POWER) || info->HasAura(SPELL_AURA_PERIODIC_ENERGIZE);
        }
        std::string& list = ok ? accepted : rejected;
        std::string effects;
        if (info)
            for (SpellEffectInfo const& effect : info->GetEffects())
                if (effect.IsEffect())
                    effects += StringFormat("{}{}/{}", effects.empty() ? "" : ",", uint32(effect.Effect), uint32(effect.ApplyAuraName));
        list += StringFormat(" {}:{}{}[effect/aura {}]", isFood ? "food" : "drink", c.SpellId, info ? "" : "(missing)", effects);
        if (ok)
            (isFood ? g_food : g_drink).push_back(c);
    };
    for (Consumable const& c : food) check(c, true);
    for (Consumable const& c : drink) check(c, false);
    TC_LOG_INFO("server.worldserver", "Bot AI rest: consumable spells accepted:{} rejected:{}", accepted.empty() ? " none" : accepted, rejected.empty() ? " none" : rejected);
}

uint32 PickConsumable(std::vector<Consumable> const& table, uint8 level)
{
    uint32 spell = 0;
    for (Consumable const& c : table)
        if (c.MinLevel <= level)
            spell = c.SpellId;
    return spell;
}

bool HardcoreRealm()
{
    static bool const hardcore = sConfigMgr->GetBoolDefault("Classic.Hardcore", false); // read once (the core reads it per resurrection)
    return hardcore;
}

// ---------------------------------------------------------------------------------------------------------------------
// Small building blocks
// ---------------------------------------------------------------------------------------------------------------------
class FnTrigger : public Trigger
{
public:
    using Fn = bool (*)(BotAI*, Player*);
    FnTrigger(BotAI* ai, char const* name, uint32 interval, Fn fn) : Trigger(ai, name, interval), _fn(fn) { }
    bool IsActive() override { return _fn(GetAI(), GetBot()); }
private:
    Fn _fn;
};

void AddFnTrigger(BotRegistry& r, char const* name, uint32 interval, FnTrigger::Fn fn)
{
    r.AddTrigger(name, [name, interval, fn](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<FnTrigger>(ai, name, interval, fn); });
}

template<typename T>
BotRegistry::ActionCreator MakeAct() { return [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<T>(ai); }; }

// ---------------------------------------------------------------------------------------------------------------------
// goto
// ---------------------------------------------------------------------------------------------------------------------
class MoveToGoalAction : public Action
{
public:
    explicit MoveToGoalAction(BotAI* ai) : Action(ai, "move_to_goal", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG) { }
    bool IsUseful() override { return GetAI()->Motion().HasGoal(); }
    bool Execute() override
    {
        SetResult("GOTO_STEP");
        return GetAI()->Motion().Step(GetAI(), GetBot()) != BotMotion::Result::Idle;
    }
};

class NaturalIdleAction : public Action
{
public:
    explicit NaturalIdleAction(BotAI* ai) : Action(ai, "natural_idle", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG | ACTION_FLAG_NOISY) { }
    bool Execute() override
    {
        SetResult("IDLE_STEP");
        return GetAI()->Motion().IdleStep(GetAI(), GetBot());
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// follow / stay
// ---------------------------------------------------------------------------------------------------------------------
constexpr float FOLLOW_START_DIST = 10.0f; // start walking when the leader is farther than this
constexpr float FOLLOW_ARRIVE_DIST = 5.0f;  // and stop within this distance

class FollowLeaderAction : public Action
{
public:
    explicit FollowLeaderAction(BotAI* ai) : Action(ai, "follow_leader", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG) { }
    // not while casting: a mount spell (chat order `mount`) is interrupted by movement
    bool IsUseful() override { return !GetAI()->Motion().GetFollow().IsEmpty() && !(GetBot() && GetBot()->IsNonMeleeSpellCast(false)); }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotMotion& motion = ai->Motion();
        SetResult("FOLLOW_STEP");

        Player* leader = ObjectAccessor::GetPlayer(*bot, motion.GetFollow());
        if (!leader || !leader->IsInWorld() || leader == bot)
        {
            if (!_lostLogged)
            {
                _lostLogged = true;
                ai->EmitEvent(bot, "decision", BOTLOG_INFO, "FOLLOW_LEADER_LOST", "follow target not on this map", StringFormat(R"({{"leader":"{}"}})", motion.GetFollow().ToString()));
            }
            return false;
        }
        _lostLogged = false;

        bool const following = motion.HasGoal() && !std::strcmp(motion.GetTag(), "follow");
        float const dist = bot->GetExactDist2d(leader);
        if (!following)
        {
            if (dist <= FOLLOW_START_DIST)
                return false; // close enough, nothing to do
            motion.SetGoal(bot->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), FOLLOW_ARRIVE_DIST, "follow");
        }
        else
        {
            // the leader moved on: retarget (no new GOTO_START row, follow goals are quiet)
            float const dx = leader->GetPositionX() - motion.GoalX(), dy = leader->GetPositionY() - motion.GoalY();
            if (dx * dx + dy * dy > 16.0f)
                motion.SetGoal(bot->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), FOLLOW_ARRIVE_DIST, "follow");
        }
        motion.Step(ai, bot);
        return true;
    }
private:
    bool _lostLogged = false;
};

class HoldPositionMultiplier : public Multiplier
{
public:
    explicit HoldPositionMultiplier(BotAI* ai) : Multiplier(ai, "hold_position") { }
    float GetValue(Action const& action) override { return (action.GetFlags() & ACTION_FLAG_MOVES) ? 0.0f : 1.0f; }
};

class StopMovingAction : public Action
{
public:
    explicit StopMovingAction(BotAI* ai) : Action(ai, "stop_moving") { }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        bool const hadGoal = ai->Motion().HasGoal();
        ai->Motion().ClearGoal();
        BotMotion::Halt(bot);
        SetResult("STAY_STOP", "holding position", StringFormat(R"({{"had_goal":{}}})", hadGoal ? "true" : "false"));
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// aggro awareness (Bot.AI.AggroAvoid.*): helpers, the public entry points are defined after this namespace
// ---------------------------------------------------------------------------------------------------------------------
bool IsAggroThreat(Player* bot, Creature* c, BotAIConfig const& cfg)
{
    if (!c->IsAlive() || c->IsPet() || c->IsTotem() || c->IsCritter() || c->IsCivilian() || c->IsTrigger())
        return false;
    if (!bot->IsHostileTo(c) || !bot->IsValidAttackTarget(c))
        return false;
    bool const elite = c->IsElite() || c->isWorldBoss();
    return (cfg.AggroElites && elite) || int32(c->GetLevel()) - int32(bot->GetLevel()) >= cfg.AggroLevelDiff;
}

// Refreshes the bot's threat cache (one grid scan per second at most, one per bot).
void RefreshThreats(BotAI* ai, Player* bot)
{
    BotAggro& a = ai->Motion().Aggro();
    uint32 const now = ai->GetNowMs();
    if (a.Scanned && now - a.ScanMs < 1000)
        return;
    a.Scanned = true;
    a.ScanMs = now;
    a.List.clear();
    BotAIConfig const& cfg = BotAI::Config();
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    std::vector<Creature*> list;
    bot->GetCreatureListWithOptionsInGrid(list, 40.0f + float(cfg.AggroMarginYd), options);
    for (Creature* c : list)
    {
        if (a.List.size() >= 12 || !IsAggroThreat(bot, c, cfg))
            continue;
        float const aggro = c->GetAttackDistance(bot);
        if (aggro > 0.0f)
            a.List.push_back({ c->GetGUID(), c->GetEntry(), uint8(c->GetLevel()), c->IsElite() || c->isWorldBoss(), aggro });
    }
}

// A walkable point `radius` yards from (mx,my) in direction `angle`, false when there is no usable navmesh path to it.
bool AggroWaypoint(Player* bot, float mx, float my, float angle, float radius, float& x, float& y, float& z)
{
    x = mx + std::cos(angle) * radius;
    y = my + std::sin(angle) * radius;
    z = bot->GetPositionZ();
    bot->UpdateGroundPositionZ(x, y, z);
    BotPathInfo const pi = BotMotion::QueryPath(bot, x, y, z);
    if (!pi.Valid || pi.GoalOffMesh || (pi.Partial && pi.EndGap > 4.0f))
        return false;
    return pi.Length <= 2.5f * bot->GetExactDist2d(x, y) + 10.0f; // a long winding route leads through the danger
}

// A point away from the mob outside its aggro radius plus margin.
bool AggroRetreatPoint(Player* bot, BotAggroHit const& hit, float& x, float& y, float& z)
{
    float const mx = hit.Mob->GetPositionX(), my = hit.Mob->GetPositionY();
    float const base = std::atan2(bot->GetPositionY() - my, bot->GetPositionX() - mx);
    for (float off : { 0.0f, 0.6f, -0.6f, 1.2f, -1.2f })
        if (AggroWaypoint(bot, mx, my, base + off, hit.Radius + 4.0f, x, y, z))
            return true;
    return false;
}

// Rest is refused (and an eat/drink session ended) while such a mob has the bot inside its aggro radius plus margin.
bool AggroBlocksRest(BotAI* ai, Player* bot, char const* action)
{
    BotAggroHit hit;
    if (!BotAggroNear(ai, bot, 0, hit))
        return false;
    BotAggroLog(ai, bot, action, hit, "rest");
    return true;
}

class AggroRetreatAction : public Action
{
public:
    explicit AggroRetreatAction(BotAI* ai) : Action(ai, "aggro_retreat", ACTION_FLAG_QUIET_LOG) { }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotAggroHit hit;
        if (!BotAggroNear(ai, bot, ai->Motion().QuestEntry(), hit))
            return false;
        SetResult("AGGRO_RETREAT");
        float x, y, z;
        if (!AggroRetreatPoint(bot, hit, x, y, z))
        {
            BotAggroLog(ai, bot, "no_retreat_route", hit, "idle");
            return false;
        }
        bool const wasResting = ai->Rest().Resting();
        if (wasResting)
            BotEndRest(ai, bot, "AGGRO_AVOID");
        else if (bot->GetStandState() == UNIT_STAND_STATE_SIT)
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        ai->Motion().SetGoal(bot->GetMapId(), x, y, z, 3.0f, "aggro_retreat");
        BotAggroLog(ai, bot, wasResting ? "retreat_from_rest" : "retreat_idle", hit, "idle");
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// rest (eat / drink)
// ---------------------------------------------------------------------------------------------------------------------
float PowerPct(Player* bot)
{
    uint32 const max = bot->GetMaxPower(POWER_MANA);
    return max ? 100.0f * float(bot->GetPower(POWER_MANA)) / float(max) : 100.0f;
}

bool UsesMana(Player* bot) { return bot->GetPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) > 0; }

bool NeedEat(BotAI* ai, Player* bot)
{
    return !(ai->Rest().Bits & BotRest::EAT) && bot->IsAlive() && !bot->IsInCombat() && bot->GetHealthPct() < float(BotAI::Config().EatBelowPct) &&
        !AggroBlocksRest(ai, bot, "rest_refused");
}

bool NeedDrink(BotAI* ai, Player* bot)
{
    return !(ai->Rest().Bits & BotRest::DRINK) && bot->IsAlive() && !bot->IsInCombat() && UsesMana(bot) &&
        PowerPct(bot) < float(std::max(BotAI::Config().DrinkBelowPct, BotCombatPrePullManaPct())) && !AggroBlocksRest(ai, bot, "rest_refused");
}

bool RestingNow(BotAI* ai, Player*) { return ai->Rest().Resting(); }

void StartRestSession(BotAI* ai, Player* bot)
{
    BotRest& rest = ai->Rest();
    if (!rest.Resting())
    {
        BotMotion::Halt(bot); // the goal (if any) stays and continues after the meal
        rest.SinceMs = ai->GetNowMs();
    }
    if (bot->GetStandState() != UNIT_STAND_STATE_SIT)
        bot->SetStandState(UNIT_STAND_STATE_SIT);
}

class EatAction : public Action
{
public:
    EatAction(BotAI* ai, char const* name, bool food) : Action(ai, name), _food(food) { }
    bool IsUseful() override
    {
        Player* bot = GetBot();
        return _food ? NeedEat(GetAI(), bot) : NeedDrink(GetAI(), bot);
    }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotRest& rest = ai->Rest();
        bool const free = BotAI::Config().FreeFood;
        uint32 const spell = free ? PickConsumable(_food ? g_food : g_drink, bot->GetLevel()) : 0;
        char const* reason = spell ? (_food ? "EAT_START" : "DRINK_START") : "REST_SIT";

        StartRestSession(ai, bot);
        if (_food)
        {
            rest.Bits |= BotRest::EAT;
            rest.FoodSpell = spell;
            rest.FoodCasts = 0;
        }
        else
        {
            rest.Bits |= BotRest::DRINK;
            rest.DrinkSpell = spell;
            rest.DrinkCasts = 0;
        }
        if (spell)
        {
            bot->CastSpell(bot, spell, true);
            (_food ? rest.FoodCasts : rest.DrinkCasts) = 1;
        }

        SetResult(reason, _food ? "sit and eat" : "sit and drink", StringFormat(R"({{"hp_pct":{:.0f},"mana_pct":{:.0f},"spell":{},"free_consumable":{},"had_goal":{}}})",
            bot->GetHealthPct(), PowerPct(bot), spell, free ? "true" : "false", ai->Motion().HasGoal() ? "true" : "false"));
        return true;
    }
private:
    bool _food;
};

class RestTickAction : public Action
{
public:
    explicit RestTickAction(BotAI* ai) : Action(ai, "rest_tick", ACTION_FLAG_QUIET_LOG) { }
    bool IsUseful() override { return GetAI()->Rest().Resting(); }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotRest& rest = ai->Rest();
        BotAI::Config();
        uint32 const donePct = BotAI::Config().RestDonePct;
        uint32 const elapsed = ai->GetNowMs() - rest.SinceMs;
        SetResult("REST_TICK");

        if (bot->GetStandState() != UNIT_STAND_STATE_SIT)
        {
            BotEndRest(ai, bot, "INTERRUPTED", false);
            return true;
        }
        if (AggroBlocksRest(ai, bot, "rest_interrupted"))
        {
            BotEndRest(ai, bot, "AGGRO_AVOID", true);
            return true;
        }

        if (rest.Bits & BotRest::EAT)
        {
            if (bot->GetHealthPct() >= float(donePct))
                Finish(ai, bot, BotRest::EAT, "EAT_DONE", elapsed);
            else if (rest.FoodSpell && !bot->HasAura(rest.FoodSpell))
            {
                if (rest.FoodCasts >= 12)
                    Finish(ai, bot, BotRest::EAT, "REST_GIVE_UP", elapsed);
                else
                {
                    bot->CastSpell(bot, rest.FoodSpell, true);
                    ++rest.FoodCasts;
                }
            }
            else if (!rest.FoodSpell && elapsed > 120000)
                Finish(ai, bot, BotRest::EAT, "REST_GIVE_UP", elapsed);
        }
        if (rest.Bits & BotRest::DRINK)
        {
            if (!UsesMana(bot) || PowerPct(bot) >= float(donePct))
                Finish(ai, bot, BotRest::DRINK, "DRINK_DONE", elapsed);
            else if (rest.DrinkSpell && !bot->HasAura(rest.DrinkSpell))
            {
                if (rest.DrinkCasts >= 12)
                    Finish(ai, bot, BotRest::DRINK, "REST_GIVE_UP", elapsed);
                else
                {
                    bot->CastSpell(bot, rest.DrinkSpell, true);
                    ++rest.DrinkCasts;
                }
            }
            else if (!rest.DrinkSpell && elapsed > 120000)
                Finish(ai, bot, BotRest::DRINK, "REST_GIVE_UP", elapsed);
        }
        if (!rest.Resting())
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "REST_END", "stood up", StringFormat(R"({{"seconds":{},"hp_pct":{:.0f},"mana_pct":{:.0f}}})", elapsed / 1000, bot->GetHealthPct(), PowerPct(bot)));
        }
        return true;
    }
private:
    static void Finish(BotAI* ai, Player* bot, uint8 kind, char const* reason, uint32 elapsed)
    {
        BotRest& rest = ai->Rest();
        uint32 const spell = kind == BotRest::EAT ? rest.FoodSpell : rest.DrinkSpell;
        if (spell)
            bot->RemoveAurasDueToSpell(spell);
        rest.Bits &= ~kind;
        ai->EmitEvent(bot, "decision", std::strcmp(reason, "REST_GIVE_UP") ? BOTLOG_INFO : BOTLOG_WARN, reason,
            kind == BotRest::EAT ? "finished eating" : "finished drinking",
            StringFormat(R"({{"seconds":{},"hp_pct":{:.0f},"mana_pct":{:.0f},"casts":{}}})", elapsed / 1000, bot->GetHealthPct(), PowerPct(bot), kind == BotRest::EAT ? rest.FoodCasts : rest.DrinkCasts));
    }
};

class RestingHoldMultiplier : public Multiplier
{
public:
    explicit RestingHoldMultiplier(BotAI* ai) : Multiplier(ai, "resting_hold") { }
    float GetValue(Action const& action) override { return ((action.GetFlags() & ACTION_FLAG_MOVES) && GetAI()->Rest().Resting()) ? 0.0f : 1.0f; }
};

// ---------------------------------------------------------------------------------------------------------------------
// recover (dead engine)
// ---------------------------------------------------------------------------------------------------------------------
bool IsGhost(Player* bot) { return bot->HasPlayerFlag(PLAYER_FLAGS_GHOST); }

// Decides corpse run versus spirit healer from the game state (derived again every time, only timers are stored).
void PlanRecovery(BotAI* ai, Player* bot)
{
    BotRecover& r = ai->Recover();
    auto decide = [&](BotRecover::Mode mode, char const* reason, std::string const& details)
    {
        r.Plan = mode;
        std::strncpy(r.PlanReason, reason, sizeof(r.PlanReason) - 1);
        r.PlanReason[sizeof(r.PlanReason) - 1] = '\0';
        ai->EmitEvent(bot, "decision", BOTLOG_INFO, mode == BotRecover::Mode::CorpseRun ? "CORPSE_RUN_START" : "SPIRIT_HEALER_PLAN",
            mode == BotRecover::Mode::CorpseRun ? "running back to the corpse" : "will use the spirit healer", details);
    };

    if (!bot->HasCorpse())
    {
        decide(BotRecover::Mode::Healer, "NO_CORPSE", R"({"reason":"NO_CORPSE"})");
        return;
    }

    WorldLocation const& corpse = bot->GetCorpseLocation();
    std::string const corpseJson = StringFormat(R"("corpse":{{"map":{},"x":{:.1f},"y":{:.1f},"z":{:.1f}}})", corpse.GetMapId(), corpse.GetPositionX(), corpse.GetPositionY(), corpse.GetPositionZ());
    if (corpse.GetMapId() != bot->GetMapId())
    {
        decide(BotRecover::Mode::Healer, "CORPSE_OTHER_MAP", StringFormat(R"({{"reason":"CORPSE_OTHER_MAP",{}}})", corpseJson));
        return;
    }

    float const dist = bot->GetExactDist2d(corpse.GetPositionX(), corpse.GetPositionY());
    if (dist <= CORPSE_RECLAIM_DIST)
    {
        decide(BotRecover::Mode::CorpseRun, "CORPSE_NEAR", StringFormat(R"({{"reason":"CORPSE_NEAR","distance":{:.0f},{}}})", dist, corpseJson));
        return;
    }
    if (dist > float(BotAI::Config().MaxCorpseRunYards))
    {
        decide(BotRecover::Mode::Healer, "CORPSE_TOO_FAR", StringFormat(R"({{"reason":"CORPSE_TOO_FAR","distance":{:.0f},"max":{},{}}})", dist, BotAI::Config().MaxCorpseRunYards, corpseJson));
        return;
    }

    BotPathInfo const path = BotMotion::QueryPath(bot, corpse.GetPositionX(), corpse.GetPositionY(), corpse.GetPositionZ());
    std::string const pathJson = StringFormat(R"("path":{{"length":{:.0f},"partial":{},"end_gap":{:.0f},"type":{}}})", path.Length, path.Partial ? "true" : "false", path.EndGap, path.Type);
    if (path.NoPath || path.MmapMissing || (path.Partial && path.EndGap > 40.0f))
    {
        char const* reason = path.MmapMissing ? "MMAP_MISSING" : path.OffMesh ? "CORPSE_OFF_NAVMESH" : "CORPSE_UNREACHABLE";
        decide(BotRecover::Mode::Healer, reason, StringFormat(R"({{"reason":"{}","distance":{:.0f},{},{}}})", reason, dist, pathJson, corpseJson));
        return;
    }
    decide(BotRecover::Mode::CorpseRun, "CORPSE_PATH_OK", StringFormat(R"({{"reason":"CORPSE_PATH_OK","distance":{:.0f},{},{}}})", dist, pathJson, corpseJson));
}

std::string ReviveDetails(BotAI* ai, Player* bot, char const* via)
{
    // resurrection sickness (race specific spell, applied only from the configured level)
    uint32 sickness = 0;
    int32 remainingSec = 0;
    if (ChrRacesEntry const* race = sChrRacesStore.LookupEntry(bot->GetRace()))
        if (Aura const* aura = bot->GetAura(race->ResSicknessSpellID))
        {
            sickness = race->ResSicknessSpellID;
            remainingSec = aura->GetDuration() / 1000;
        }
    return StringFormat(R"({{"via":"{}","dead_seconds":{},"level":{},"hp_pct":{:.0f},"resurrection_sickness":{},"sickness_spell":{},"sickness_seconds":{},"attempts":{},"plan_reason":"{}"}})",
        via, (ai->GetNowMs() - ai->Recover().DiedMs) / 1000, bot->GetLevel(), bot->GetHealthPct(), sickness ? "true" : "false", sickness, remainingSec,
        ai->Recover().Attempts, ai->Recover().PlanReason);
}

bool HardcoreBlocked(BotAI* ai, Player* bot)
{
    if (!HardcoreRealm())
        return false;
    BotRecover& r = ai->Recover();
    if (!r.HardcoreLogged)
    {
        r.HardcoreLogged = true;
        ai->EmitEvent(bot, "decision", BOTLOG_WARN, "RECOVER_REFUSED_HARDCORE", "hardcore realm: death is permanent, the bot stays dead", R"({"reason":"Classic.Hardcore"})");
    }
    return true;
}

class ReleaseSpiritAction : public Action
{
public:
    explicit ReleaseSpiritAction(BotAI* ai) : Action(ai, "release_spirit") { }
    bool IsPossible() override { return !GetBot()->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION) && !HardcoreBlocked(GetAI(), GetBot()); }
    bool IsUseful() override
    {
        Player* bot = GetBot();
        BotRecover const& r = GetAI()->Recover();
        return !bot->IsAlive() && !IsGhost(bot) && GetAI()->GetStateAgeMs() >= r.ReleaseDelayMs;
    }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        WorldPackets::Misc::RepopRequest packet{ WorldPacket(CMSG_REPOP_REQUEST) };
        uint32 const mapBefore = bot->GetMapId();
        float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
        ++ai->Recover().Attempts;
        bot->GetSession()->HandleRepopRequest(packet);

        if (!IsGhost(bot))
        {
            if (!ai->Recover().ReleaseFailedLogged)
            {
                ai->Recover().ReleaseFailedLogged = true;
                ai->EmitEvent(bot, "decision", BOTLOG_WARN, "RELEASE_FAILED", "release spirit did not turn the bot into a ghost", R"({"reason":"NO_GHOST_FLAG"})");
            }
            SetResult("RELEASE_FAILED");
            return false;
        }
        SetResult("RELEASE_SPIRIT", "released spirit", StringFormat(R"({{"dead_seconds":{},"death_map":{},"death_pos":{},"has_corpse":{}}})",
            (ai->GetNowMs() - ai->Recover().DiedMs) / 1000, mapBefore, Pos3(x, y, z), bot->HasCorpse() ? "true" : "false"));
        return true;
    }
};

// Last resort of the death code: resurrect the ghost at the closest graveyard (release and respawn) instead of retrying forever.
static void RespawnAtGraveyard(BotAI* ai, Player* bot, char const* reason, uint32 fails)
{
    WorldSafeLocsEntry const* grave = sObjectMgr->GetClosestGraveyard(bot->GetWorldLocation(), bot->GetTeam(), bot);
    ai->Motion().ClearGoal();
    ai->EmitEvent(bot, "decision", BOTLOG_WARN, reason, "recovery gave up, respawning at the closest graveyard",
        StringFormat(R"({{"reason":"{}","fails":{},"graveyard":{},"pos":{}}})", reason, fails, grave ? grave->ID : 0u, Pos3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ())));
    bot->ResurrectPlayer(0.5f);
    bot->SpawnCorpseBones();
    if (grave)
        bot->TeleportTo(grave->Loc);
}

// true when the ghost has been dead longer than Bot.AI.Death.GiveUpSec (0 = never)
static bool GhostTimedOut(BotAI* ai)
{
    uint32 const cap = BotAI::Config().DeathGiveUpSec;
    BotRecover const& r = ai->Recover();
    return cap && r.DiedMs && ai->GetNowMs() - r.DiedMs >= cap * 1000;
}

class CorpseRunAction : public Action
{
public:
    explicit CorpseRunAction(BotAI* ai) : Action(ai, "corpse_run", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return !HardcoreBlocked(GetAI(), GetBot()); }
    bool IsUseful() override
    {
        Player* bot = GetBot();
        BotRecover const& r = GetAI()->Recover();
        if (bot->IsAlive() || !IsGhost(bot) || bot->IsBeingTeleported() || r.Plan == BotRecover::Mode::Healer)
            return false;
        if (r.Plan == BotRecover::Mode::CorpseRun && bot->HasCorpse() && bot->GetCorpseLocation().GetMapId() == bot->GetMapId() &&
            bot->GetExactDist(bot->GetCorpseLocation()) <= CORPSE_RECLAIM_DIST)
            return false; // there: reclaim_corpse takes over
        return true;
    }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotRecover& r = ai->Recover();
        SetResult("CORPSE_RUN_STEP");

        if (GhostTimedOut(ai))
        {
            RespawnAtGraveyard(ai, bot, "CORPSE_RUN_TIMEOUT", r.CorpseFails);
            return true;
        }

        if (r.Plan == BotRecover::Mode::None)
        {
            PlanRecovery(ai, bot);
            return true;
        }

        BotMotion& motion = ai->Motion();
        if (!motion.HasGoal() || std::strcmp(motion.GetTag(), "corpse_run"))
        {
            WorldLocation const& c = bot->GetCorpseLocation();
            motion.SetGoal(c.GetMapId(), c.GetPositionX(), c.GetPositionY(), c.GetPositionZ(), 20.0f, "corpse_run");
        }

        BotMotion::Result result = motion.Step(ai, bot);
        if (result == BotMotion::Result::Failed)
        {
            // retry (the goal is re-issued) until the cap, then fall back to the spirit healer, logged once
            if (++r.CorpseFails >= BotAI::Config().CorpseRunMaxFails)
            {
                r.Plan = BotRecover::Mode::Healer;
                std::strcpy(r.PlanReason, "CORPSE_RUN_GAVE_UP");
                ai->EmitEvent(bot, "decision", BOTLOG_WARN, "CORPSE_RUN_GAVE_UP", "corpse run failed repeatedly, falling back to the spirit healer",
                    StringFormat(R"({{"reason":"CORPSE_RUN_GAVE_UP","fails":{}}})", r.CorpseFails));
            }
        }
        return true;
    }
};

class ReclaimCorpseAction : public Action
{
public:
    explicit ReclaimCorpseAction(BotAI* ai) : Action(ai, "reclaim_corpse") { }
    bool IsPossible() override { return !HardcoreBlocked(GetAI(), GetBot()); }
    bool IsUseful() override
    {
        Player* bot = GetBot();
        BotRecover const& r = GetAI()->Recover();
        if (bot->IsAlive() || !IsGhost(bot) || bot->IsBeingTeleported() || !bot->HasCorpse())
            return false;
        WorldLocation const& c = bot->GetCorpseLocation();
        return c.GetMapId() == bot->GetMapId() && bot->GetExactDist(c) <= CORPSE_RECLAIM_DIST && GetAI()->GetNowMs() >= r.NextTryMs;
    }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotRecover& r = ai->Recover();
        ++r.Attempts;
        BotMotion::Halt(bot);
        ai->Motion().ClearGoal();
        bot->GetSession()->HandleReclaimCorpse(*_packet());
        if (bot->IsAlive())
        {
            SetResult("CORPSE_RECLAIMED", "resurrected at the corpse", ReviveDetails(ai, bot, "corpse_run"));
            return true;
        }

        r.NextTryMs = ai->GetNowMs() + 3000; // the reclaim delay after release (up to 30 s) has not elapsed yet
        if (!r.WaitLogged)
        {
            r.WaitLogged = true;
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "RECLAIM_WAIT", "at the corpse, waiting for the reclaim delay", StringFormat(R"({{"distance":{:.0f}}})", bot->GetExactDist(bot->GetCorpseLocation())));
        }
        SetResult("RECLAIM_WAIT");
        return false;
    }
private:
    static WorldPackets::Misc::ReclaimCorpse* _packet()
    {
        static thread_local WorldPackets::Misc::ReclaimCorpse packet{ WorldPacket(CMSG_RECLAIM_CORPSE) };
        return &packet;
    }
};

class SpiritHealAction : public Action
{
public:
    explicit SpiritHealAction(BotAI* ai) : Action(ai, "spirit_heal", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return !HardcoreBlocked(GetAI(), GetBot()); }
    bool IsUseful() override
    {
        Player* bot = GetBot();
        BotRecover const& r = GetAI()->Recover();
        // reclaim_corpse has priority when the corpse is in reach; this one is for the healer plan (and as the fallback after a failed run)
        return !bot->IsAlive() && IsGhost(bot) && !bot->IsBeingTeleported() && r.Plan == BotRecover::Mode::Healer && GetAI()->GetNowMs() >= r.NextTryMs;
    }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BotRecover& r = ai->Recover();
        BotMotion& motion = ai->Motion();
        uint32 const now = ai->GetNowMs();
        SetResult("SPIRIT_HEAL_STEP");

        if (GhostTimedOut(ai))
        {
            RespawnAtGraveyard(ai, bot, "SPIRIT_HEAL_TIMEOUT", r.HealerFails);
            return true;
        }

        Creature* healer = r.Healer.IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*bot, r.Healer);
        if (!healer)
        {
            r.Healer.Clear();
            std::vector<Creature*> list;
            FindCreatureOptions options;
            options.IgnorePhases = true;
            bot->GetCreatureListWithOptionsInGrid(list, 400.0f, options);
            float best = 0.0f;
            for (Creature* c : list)
            {
                if (!c->HasNpcFlag(UNIT_NPC_FLAG_SPIRIT_HEALER))
                    continue;
                float const d = bot->GetExactDist2d(c);
                if (!healer || d < best)
                {
                    healer = c;
                    best = d;
                }
            }
            if (!healer)
            {
                if (!r.NoHealerLogged)
                {
                    r.NoHealerLogged = true;
                    ai->EmitEvent(bot, "stuck", BOTLOG_WARN, "NO_SPIRIT_HEALER", "no spirit healer found near the ghost",
                        StringFormat(R"({{"searched_yards":400,"pos":{},"plan_reason":"{}"}})", Pos3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()), r.PlanReason));
                }
                r.NextTryMs = now + 30000;
                return false;
            }
            r.Healer = healer->GetGUID();
            r.HealerX = healer->GetPositionX();
            r.HealerY = healer->GetPositionY();
            r.HealerZ = healer->GetPositionZ();
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "SPIRIT_HEALER_FOUND", "walking to the spirit healer",
                StringFormat(R"({{"entry":{},"distance":{:.0f},"healer":{}}})", healer->GetEntry(), bot->GetExactDist2d(healer), Pos3(r.HealerX, r.HealerY, r.HealerZ)));
        }

        float const dist = bot->GetExactDist(healer);
        if (dist > 4.0f)
        {
            if (!motion.HasGoal() || std::strcmp(motion.GetTag(), "spirit_healer"))
                motion.SetGoal(bot->GetMapId(), r.HealerX, r.HealerY, r.HealerZ, 3.0f, "spirit_healer");
            if (motion.Step(ai, bot) == BotMotion::Result::Failed)
            {
                r.Healer.Clear();
                r.NextTryMs = now + 20000;
                if (++r.HealerFails >= BotAI::Config().CorpseRunMaxFails)
                {
                    // nothing reachable: respawn at the closest graveyard instead of looping forever
                    RespawnAtGraveyard(ai, bot, "SPIRIT_HEAL_GAVE_UP", r.HealerFails);
                }
            }
            return true;
        }

        BotMotion::Halt(bot);
        motion.ClearGoal();
        ++r.Attempts;
        WorldPackets::NPC::SpiritHealerActivate packet{ WorldPacket(CMSG_SPIRIT_HEALER_ACTIVATE) };
        packet.Healer = healer->GetGUID();
        bot->GetSession()->HandleSpiritHealerActivate(packet);
        if (bot->IsAlive())
        {
            SetResult("SPIRIT_HEALED", "resurrected by the spirit healer", ReviveDetails(ai, bot, "spirit_healer"));
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "SPIRIT_HEALED", "resurrected by the spirit healer", ReviveDetails(ai, bot, "spirit_healer"));
            return true;
        }
        r.NextTryMs = now + 10000;
        ai->EmitEvent(bot, "decision", BOTLOG_WARN, "SPIRIT_HEAL_FAILED", "spirit healer did not resurrect the bot", StringFormat(R"({{"distance":{:.1f}}})", dist));
        return false;
    }
};
}

// ---------------------------------------------------------------------------------------------------------------------
// aggro awareness, public entry points
// ---------------------------------------------------------------------------------------------------------------------
bool BotAggroNear(BotAI* ai, Player* bot, uint32 ignoreEntry, BotAggroHit& out)
{
    BotAIConfig const& cfg = BotAI::Config();
    if (!cfg.AggroAvoid || !bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        return false;
    RefreshThreats(ai, bot);
    float deepest = 0.0f;
    bool found = false;
    for (BotAggro::Threat const& t : ai->Motion().Aggro().List)
    {
        if (ignoreEntry && t.Entry == ignoreEntry)
            continue;
        Creature* c = ObjectAccessor::GetCreature(*bot, t.Guid);
        if (!c || !c->IsAlive())
            continue;
        float const radius = t.Aggro + float(cfg.AggroMarginYd);
        float const d = bot->GetExactDist(c);
        if (d >= radius || (found && d - radius >= deepest))
            continue;
        deepest = d - radius;
        found = true;
        out = { c, t.Entry, t.Level, t.Elite, d, t.Aggro, radius };
    }
    return found;
}

bool BotAggroGuarded(BotAI* ai, Player* bot, Creature const* target)
{
    BotAIConfig const& cfg = BotAI::Config();
    if (!cfg.AggroAvoid)
        return false;
    RefreshThreats(ai, bot);
    bool const targetElite = target->IsElite() || target->isWorldBoss();
    for (BotAggro::Threat const& t : ai->Motion().Aggro().List)
    {
        // only mobs more dangerous than the target itself count, so a pack of equals does not block its own kills
        if (t.Guid == target->GetGUID() || !((t.Elite && !targetElite) || int32(t.Level) >= int32(target->GetLevel()) + 2))
            continue;
        Creature* c = ObjectAccessor::GetCreature(*bot, t.Guid);
        if (!c || !c->IsAlive())
            continue;
        float const d = target->GetExactDist2d(c);
        if (d < t.Aggro + float(cfg.AggroMarginYd) * 0.5f)
        {
            BotAggroLog(ai, bot, "target_skipped", { c, t.Entry, t.Level, t.Elite, bot->GetExactDist(c), t.Aggro, t.Aggro + float(cfg.AggroMarginYd) }, "target");
            return true;
        }
    }
    return false;
}

void BotAggroLog(BotAI* ai, Player* bot, char const* action, BotAggroHit const& hit, char const* tag)
{
    BotAggro& a = ai->Motion().Aggro();
    uint32 const now = ai->GetNowMs();
    if (a.LogMs && now - a.LogMs < BotAI::Config().AggroLogSec * 1000)
        return;
    a.LogMs = now ? now : 1;
    BotEvent ev = ai->MakeEvent(bot, "decision", BOTLOG_INFO, "AGGRO_AVOID", StringFormat("avoiding {} (level {}{}): {}", hit.Mob->GetName(), hit.Level, hit.Elite ? ", elite" : "", action));
    ev.TargetEntry = hit.Entry;
    ev.Details = StringFormat(R"({{"action":"{}","entry":{},"mob_level":{},"bot_level":{},"elite":{},"distance":{:.1f},"aggro_radius":{:.1f},"margin":{},"tag":"{}","has_goal":{}}})",
        action, hit.Entry, hit.Level, bot->GetLevel(), hit.Elite ? "true" : "false", hit.Dist, hit.Aggro, BotAI::Config().AggroMarginYd, tag, ai->Motion().HasGoal() ? "true" : "false");
    sBotMgr->LogEvent(std::move(ev));
}

// ---------------------------------------------------------------------------------------------------------------------
// natural movement support
// ---------------------------------------------------------------------------------------------------------------------
namespace BotMove
{
NaturalConfig const& Natural()
{
    static NaturalConfig cfg;
    static std::once_flag once;
    std::call_once(once, []()
    {
        cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Enabled", false);
        cfg.Shortcut = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Shortcut", true);
        cfg.RoundCorners = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.RoundCorners", true);
        cfg.SpeedVariation = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.SpeedVariation", true);
        cfg.Pacing = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Pacing", true);
        cfg.Spread = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Spread", true);
        cfg.Idle = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Idle", true);
        cfg.Metrics = sConfigMgr->GetBoolDefault("Bot.AI.Move.Natural.Metrics", true);
        cfg.MetricsSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Move.Natural.MetricsSec", 60), 10, 3600));
        cfg.IdleMaxStandSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Move.Natural.IdleMaxStandSec", 25), 5, 600));
        cfg.MaxShortcutYards = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Move.Natural.MaxShortcutYards", 45), 10, 120));
    });
    return cfg;
}
}

namespace BotTownIdle
{
Config const& Cfg()
{
    static Config cfg;
    static std::once_flag once;
    std::call_once(once, []()
    {
        cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.TownIdle.Enabled", false);
        cfg.SearchYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TownIdle.SearchYards", 70), 20, 150));
        cfg.LingerMinSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TownIdle.LingerMinSec", 15), 5, 600));
        cfg.LingerMaxSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TownIdle.LingerMaxSec", 70), int32(cfg.LingerMinSec), 1800));
        cfg.EmotePct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TownIdle.EmotePct", 22), 0, 60));
        cfg.SitPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.TownIdle.SitPct", 10), 0, 30));
    });
    return cfg;
}
}

namespace
{
std::atomic<BotDestinationVeto> s_destVeto{ nullptr };

bool ShapedTag(char const* tag) { return std::strcmp(tag, "follow") && std::strcmp(tag, "aggro_retreat"); }  // follow goals retarget constantly
bool PacedTag(char const* tag) { return !std::strcmp(tag, "quest") || !std::strcmp(tag, "goto"); }
BotMove::Vec3 ToVec(G3D::Vector3 const& v) { return { v.x, v.y, v.z }; }

// A straight walk from a to b: nothing blocks the line (terrain, models, doors) and the ground stays close to the straight line every
// few yards, so a shortcut never cuts over a ledge or a gap. Heights come from the terrain and model floors, not from the navmesh.
bool WalkLos(Player* bot, BotMove::Vec3 const& a, BotMove::Vec3 const& b)
{
    Map* map = bot->GetMap();
    PhaseShift const& ps = bot->GetPhaseShift();
    if (!map->isInLineOfSight(ps, a.x, a.y, a.z + 1.5f, b.x, b.y, b.z + 1.5f, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        return false;
    float const len = BotMove::Dist2D(a, b);
    int32 const n = int32(len / 3.0f);
    for (int32 i = 1; i <= n; ++i)
    {
        float const t = float(i) / float(n + 1);
        float const x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t, z = a.z + (b.z - a.z) * t;
        float const h = map->GetHeight(ps, x, y, z + 2.0f, true, 6.0f);
        if (h <= INVALID_HEIGHT || std::fabs(h - z) > 2.0f)
            return false;
    }
    return true;
}
}

void BotSetDestinationVeto(BotDestinationVeto fn) { s_destVeto.store(fn, std::memory_order_release); }

bool BotDestinationVetoed(Player* bot, float x, float y, float z)
{
    BotDestinationVeto fn = s_destVeto.load(std::memory_order_acquire);
    return fn && fn(bot, bot->GetMapId(), x, y, z);
}

// ---------------------------------------------------------------------------------------------------------------------
// BotMotion
// ---------------------------------------------------------------------------------------------------------------------
void BotMotion::SetGoal(uint32 mapId, float x, float y, float z, float arriveDist, char const* tag)
{
    bool const sameTag = _active && !std::strcmp(_tag, tag);
    _mapId = mapId;
    _x = x;
    _y = y;
    _z = z;
    _arrive = arriveDist;
    if (sameTag)
    {
        // retarget of a running goal (follow): keep the history, force a new path
        _issueMs = 0;
        _bestDist = 1.0e9f;
        return;
    }
    std::strncpy(_tag, tag, sizeof(_tag) - 1);
    _tag[sizeof(_tag) - 1] = '\0';
    _active = true;
    _fresh = true;
    _detour = false;
    _aggro.SinceMs = 0;
    // a new kind of goal drops the quest context of the previous one (the entry stays: aggro avoidance reads it)
    _questId = 0;
    _task = nullptr;
}

void BotMotion::Halt(Player* bot)
{
    if (bot->IsInWorld() && !bot->movespline->Finalized())
        bot->StopMoving();
}

// The navmesh tile of a grid is loaded together with the grid, and PathGenerator needs the tiles of the start and the destination
// (a destination in an unloaded grid looks like "no navmesh"). Loads the grids along the straight line to the target (the core does the
// same when a player or an active object is there; they unload again when idle).
void BotMotion::EnsureGrids(Player* bot, float x, float y)
{
    Map* map = bot->GetMap();
    float const sx = bot->GetPositionX(), sy = bot->GetPositionY();
    float const dist = std::sqrt((x - sx) * (x - sx) + (y - sy) * (y - sy));
    int32 const steps = std::clamp<int32>(int32(dist / 250.0f) + 1, 1, 14);
    for (int32 i = steps; i >= 1; --i)
    {
        float const px = sx + (x - sx) * float(i) / float(steps);
        float const py = sy + (y - sy) * float(i) / float(steps);
        if (Trinity::IsValidMapCoord(px, py) && !map->IsGridLoaded(px, py))
            map->LoadGrid(px, py);
    }
}

BotPathInfo BotMotion::QueryPath(Player* bot, float x, float y, float z)
{
    EnsureGrids(bot, x, y);
    BotPathInfo info;
    PathGenerator path(bot);
    path.CalculatePath(x, y, z, false);
    info.Type = uint32(path.GetPathType());
    info.NoPath = (info.Type & PATHFIND_NOPATH) != 0;
    // NOT_USING_PATH together with FARFROMPOLY means the navmesh exists but the start or destination is not on it (water, inside rock):
    // that is a path failure, not a missing navmesh
    info.OffMesh = (info.Type & PATHFIND_NOT_USING_PATH) && (info.Type & PATHFIND_FARFROMPOLY);
    info.GoalOffMesh = (info.Type & PATHFIND_FARFROMPOLY_END) != 0;
    if (info.Type == PATHFIND_NOPATH)
    {
        // no polygon at all near the start or the goal ("hole in the mesh"): a path from the bot to its own position tells which one
        PathGenerator probe(bot);
        probe.CalculatePath(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), false);
        if (uint32(probe.GetPathType()) & PATHFIND_NOPATH)
            info.StartOffMesh = true;
        else
            info.GoalOffMesh = true;
    }
    info.OffMesh = info.OffMesh || info.StartOffMesh || (info.NoPath && info.GoalOffMesh);
    info.NoPath = info.NoPath || info.OffMesh;
    info.MmapMissing = (info.Type & PATHFIND_NOT_USING_PATH) && !(info.Type & PATHFIND_FARFROMPOLY);
    info.Partial = (info.Type & PATHFIND_INCOMPLETE) != 0;
    info.Length = path.GetPathLength();
    G3D::Vector3 const end = path.GetActualEndPosition();
    info.End = end;
    info.EndGap = std::sqrt((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y));
    info.EndGap3D = std::sqrt((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) + (end.z - z) * (end.z - z));
    info.Valid = !info.NoPath && !info.MmapMissing;
    return info;
}

// Computes the path with the core's PathGenerator (mmaps) and launches it as a spline; the same sequence MotionMaster uses.
// A path that does not exist is never walked as a straight line (the core's MovePoint would do that).
BotMotion::Launch BotMotion::Issue(Player* bot, uint32 now)
{
    _issueMs = now;
    ++_issues;
    if (bot->HasUnitState(UNIT_STATE_NOT_MOVE) || bot->IsMovementPreventedByCasting())
        return Launch::Skipped; // rooted/stunned/casting: no progress will be made, the stuck detection reports it

    if (bot->GetStandState() != UNIT_STAND_STATE_STAND && !bot->IsInCombat())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    float tx = _x + _ox, ty = _y + _oy, tz = _z;
    if (_detour)
    {
        tx = _dx; ty = _dy; tz = _dz;
    }
    else if (_sideOn)
    {
        tx = _sx; ty = _sy; tz = _sz;
    }
    if (bot->GetExactDist(tx, ty, tz) < 1.0f)
        return Launch::Skipped;

    EnsureGrids(bot, tx, ty);
    PathGenerator path(bot);
    path.CalculatePath(tx, ty, tz, false);
    uint32 const type = uint32(path.GetPathType());
    if ((type & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)) || path.GetPath().size() < 2)
        return Launch::NoPath;

    BotMove::NaturalConfig const& nat = BotMove::Natural();
    bool const shape = nat.Enabled && ShapedTag(_tag);
    Movement::PointsArray pts = path.GetPath();
    bool smooth = false;
    if (shape && pts.size() >= 3 && (nat.Shortcut || nat.RoundCorners))
    {
        std::vector<BotMove::Vec3> v;
        v.reserve(pts.size());
        for (G3D::Vector3 const& p : pts)
            v.push_back(ToVec(p));
        BotMove::LosFn const los = [bot](BotMove::Vec3 const& a, BotMove::Vec3 const& b) { return WalkLos(bot, a, b); };
        if (nat.Shortcut)
        {
            BotMove::ShortcutConfig sc;
            sc.MaxSegment = float(nat.MaxShortcutYards);
            v = BotMove::ShortcutPath(v, los, sc);
        }
        if (nat.RoundCorners)
            v = BotMove::RoundCorners(v, los);
        pts.clear();
        for (BotMove::Vec3 const& p : v)
            pts.emplace_back(p.x, p.y, p.z);
        smooth = nat.RoundCorners && pts.size() >= 4;
    }

    Movement::MoveSplineInit init(bot);
    init.MovebyPath(pts);
    if (smooth)
        init.SetSmooth();
    if (shape && nat.SpeedVariation)
        init.SetVelocity(bot->GetSpeed(bot->IsWalking() ? MOVE_WALK : MOVE_RUN) * BotMove::SpeedFactor(bot->GetGUID().GetCounter(), now, 0.05f));
    init.Launch();
    return Launch::Launched;
}

bool BotMotion::RepeatFail(char const* reason, uint32 now)
{
    uint32 key = 2166136261u;
    for (char const* s = _tag; *s; ++s)
        key = (key ^ uint8(*s)) * 16777619u;
    for (char const* s = reason; *s; ++s)
        key = (key ^ uint8(*s)) * 16777619u;
    if (key == _lastFailKey && _lastFailMs && now - _lastFailMs < 60000)
        return true;
    _lastFailKey = key;
    _lastFailMs = now ? now : 1;
    return false;
}

void BotMotion::EmitMotion(BotAI* ai, Player* bot, char const* type, uint8 severity, char const* reason, std::string const& summary, std::string const& details)
{
    BotEvent ev = ai->MakeEvent(bot, type, severity, reason, summary);
    if (_questId)
        ev.QuestId = _questId;
    if (_questEntry)
        ev.TargetEntry = _questEntry;
    ev.Details = details;
    if (_task && ev.Details.size() > 1 && ev.Details.front() == '{' && ev.Details.back() == '}' && ev.Details.find("\"task\":") == std::string::npos)
        ev.Details.insert(ev.Details.size() - 1, StringFormat(R"(,"task":"{}")", _task));
    sBotMgr->LogEvent(std::move(ev));
}

BotMotion::Result BotMotion::Fail(BotAI* ai, Player* bot, char const* type, char const* reason, std::string const& summary, std::string const& extra)
{
    Halt(bot);
    _active = false;
    _lastEndMs = ai->GetNowMs();
    if (!std::strcmp(type, "path_fail"))
        _metrics.NotePathFail();
    if (!std::strcmp(_tag, "idle"))
        return Result::Failed; // an idle wander that does not work is not worth a row
    if (RepeatFail(reason, ai->GetNowMs()))
        return Result::Failed;
    std::string const details = StringFormat(R"({{"tag":"{}","quest_id":{},"goal":{},"goal_map":{},"distance":{:.0f},"seconds":{},"issues":{},"path_us":{}{}}})",
        _tag, _questId, Pos3(_x, _y, _z), _mapId, bot->GetExactDist2d(_x, _y), (ai->GetNowMs() - _startMs) / 1000, _issues, _pathUs, extra.empty() ? std::string() : "," + extra);
    EmitMotion(ai, bot, type, BOTLOG_WARN, reason, summary, details);
    if (_questId && !std::strcmp(_tag, "quest"))
    {
        EmitMotion(ai, bot, "decision", BOTLOG_INFO, "QUEST_WALK_ABORT", StringFormat("quest walk aborted: {}", reason),
            StringFormat(R"({{"quest_id":{},"abort_reason":"{}","distance":{:.0f},"seconds":{},"issues":{}}})", _questId, reason, bot->GetExactDist2d(_x, _y), (ai->GetNowMs() - _startMs) / 1000, _issues));
    }
    _walkLogged = false;
    return Result::Failed;
}

// Aggro steering of a goto/quest goal around hostile elites and higher-level mobs (see BotAggroNear): inside a mob's aggro radius the
// bot backs off, in the margin zone it detours around the mob when its heading closes in, and when no detour exists it waits outside
// the radius. A goal held back longer than Bot.AI.AggroAvoid.MaxSec fails with AGGRO_BLOCKED so the quest layer picks another goal.
std::optional<BotMotion::Result> BotMotion::Steer(BotAI* ai, Player* bot, uint32 now)
{
    BotAIConfig const& cfg = BotAI::Config();
    if (_detour)
    {
        if (bot->GetExactDist2d(_dx, _dy) < 4.0f || now - _detourMs > 10000)
        {
            _detour = false;
            _issueMs = 0;
        }
        else
        {
            _bestMs = now; // the detour counts as progress
            return std::nullopt;
        }
    }

    bool const isQuest = !std::strcmp(_tag, "quest");
    BotAggroHit hit;
    if (!cfg.AggroAvoid || (!isQuest && std::strcmp(_tag, "goto")) || bot->IsInCombat() || !BotAggroNear(ai, bot, isQuest ? _questEntry : 0, hit))
    {
        if (!_aggro.ClearMs)
            _aggro.ClearMs = now ? now : 1;
        else if (_aggro.SinceMs && now - _aggro.ClearMs > 6000)
            _aggro.SinceMs = 0;
        return std::nullopt;
    }
    _aggro.ClearMs = 0;
    if (!_aggro.SinceMs)
        _aggro.SinceMs = now ? now : 1;
    if (now - _aggro.SinceMs > cfg.AggroMaxSec * 1000)
    {
        _aggro.SinceMs = 0;
        return Fail(ai, bot, "path_fail", "AGGRO_BLOCKED", StringFormat("goal held back by {} (level {}) for {} s", hit.Mob->GetName(), hit.Level, cfg.AggroMaxSec),
            StringFormat(R"("mob_entry":{},"mob_level":{},"elite":{},"mob_distance":{:.0f},"aggro_radius":{:.0f})", hit.Entry, hit.Level, hit.Elite ? "true" : "false", hit.Dist, hit.Aggro));
    }

    float const mx = hit.Mob->GetPositionX(), my = hit.Mob->GetPositionY();
    float const bx = bot->GetPositionX(), by = bot->GetPositionY();
    float const d2 = std::max(0.1f, bot->GetExactDist2d(mx, my));
    float const base = std::atan2(by - my, bx - mx);
    float x, y, z;
    auto startDetour = [&](char const* action)
    {
        _detour = true;
        _dx = x;
        _dy = y;
        _dz = z;
        _detourMs = now;
        _bestMs = now;
        Halt(bot);
        BotAggroLog(ai, bot, action, hit, _tag);
        Issue(bot, now ? now : 1);
        return std::optional<Result>(Result::Moving);
    };

    if (hit.Dist < hit.Aggro)
    {
        // already inside the aggro radius: back off first
        if (AggroRetreatPoint(bot, hit, x, y, z))
            return startDetour("retreat");
        BotAggroLog(ai, bot, "no_retreat_route", hit, _tag);
        return std::nullopt;
    }

    // margin zone: only a heading that closes in on the mob is a problem (the next path node, else the goal; at least 25 and at most 40 yards ahead)
    float hx = _x, hy = _y;
    bool const moving = !bot->movespline->Finalized();
    if (moving)
    {
        G3D::Vector3 const& next = bot->movespline->CurrentDestination();
        hx = next.x;
        hy = next.y;
    }
    float const len = std::sqrt((hx - bx) * (hx - bx) + (hy - by) * (hy - by));
    if (len < 0.5f)
        return std::nullopt;
    float const dirx = (hx - bx) / len, diry = (hy - by) / len;
    float const reach = std::clamp(len, moving ? 25.0f : 0.0f, 40.0f);
    float const along = std::clamp((mx - bx) * dirx + (my - by) * diry, 0.0f, reach);
    float const closest = std::sqrt((bx + dirx * along - mx) * (bx + dirx * along - mx) + (by + diry * along - my) * (by + diry * along - my));
    if (closest >= d2 - 0.5f)
        return std::nullopt;

    // closing in: detour on the side the heading already leans to, unless the goal itself lies inside the aggro radius
    if (std::hypot(_x - mx, _y - my) >= hit.Aggro + 2.0f)
    {
        float const side = ((bx - mx) * diry - (by - my) * dirx) >= 0.0f ? 1.0f : -1.0f;
        for (float off : { 0.6f * side, 1.0f * side, -0.6f * side })
            if (AggroWaypoint(bot, mx, my, base + off, hit.Radius + 3.0f, x, y, z))
                return startDetour("detour");
    }
    Halt(bot);
    _bestMs = now;
    BotAggroLog(ai, bot, "wait", hit, _tag);
    return Result::Moving;
}

BotMotion::Result BotMotion::Step(BotAI* ai, Player* bot)
{
    if (!_active)
        return Result::Idle;

    uint32 const now = ai->GetNowMs();
    bool const quiet = !std::strcmp(_tag, "follow") || !std::strcmp(_tag, "quest") || !std::strcmp(_tag, "aggro_retreat") || !std::strcmp(_tag, "idle"); // quest legs are re-issued constantly (BotQuest logs its own decisions) // follow goals retarget constantly: only failures are logged

    if (bot->GetMapId() != _mapId)
        return Fail(ai, bot, "path_fail", "WRONG_MAP", "goal is on another map", StringFormat(R"("bot_map":{})", bot->GetMapId()));

    float const dist = bot->GetExactDist2d(_x, _y);
    if (dist <= _arrive && std::fabs(bot->GetPositionZ() - _z) < 25.0f)
    {
        if (!quiet)
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "GOTO_ARRIVED", StringFormat("arrived at the {} goal", _tag),
                StringFormat(R"({{"tag":"{}","goal":{},"seconds":{},"issues":{},"distance":{:.1f}}})", _tag, Pos3(_x, _y, _z), (now - _startMs) / 1000, _issues, dist));
        if (quiet && _walkLogged && _questId)
            EmitMotion(ai, bot, "decision", BOTLOG_INFO, "QUEST_WALK_ARRIVE", "arrived at the quest walk goal",
                StringFormat(R"({{"quest_id":{},"seconds":{},"issues":{},"distance":{:.1f}}})", _questId, (now - _startMs) / 1000, _issues, dist));
        _walkLogged = false;
        Halt(bot);
        _active = false;
        _lastEndMs = now;
        if (BotMove::Natural().Enabled && BotMove::Natural().Pacing && PacedTag(_tag))
            Pause(now, BotMove::PauseMs(BotMove::Pause::Arrive, bot->GetGUID().GetCounter(), _issues));
        return Result::Arrived;
    }

    if (_fresh)
    {
        _fresh = false;
        _startMs = now;
        _bestDist = dist;
        _bestMs = now;
        _issues = 0;
        _episodes = 0;
        _everMoved = false;

        _ox = _oy = 0.0f;
        _sideOn = false;
        _waitUntilMs = 0;
        _legFails = 0;
        _trailN = 0;
        _legs.Reset();
        BotMove::NaturalConfig const& nat = BotMove::Natural();
        if (nat.Enabled)
        {
            uint64 const botKey = bot->GetGUID().GetCounter();
            if (_lastEndMs && now - _lastEndMs > 300000)
                _fails.Clear();
            // another approach point per bot and per goal, inside the arrival radius, on the side the bot comes from
            if (nat.Spread && PacedTag(_tag) && _arrive >= 3.0f)
            {
                BotMove::ApproachParams ap;
                ap.BotKey = botKey;
                ap.TargetKey = BotMove::Mix(uint64(int32(_x)), uint64(int32(_y)), _mapId);
                ap.Radius = std::min(2.5f, _arrive * 0.6f);
                ap.BearingToBot = std::atan2(bot->GetPositionY() - _y, bot->GetPositionX() - _x);
                BotMove::Vec3 const off = BotMove::ApproachOffset(ap);
                _ox = off.x;
                _oy = off.y;
            }
            // a real start (not the next leg of a running walk) begins with a short pause
            if (nat.Pacing && PacedTag(_tag) && (!_lastEndMs || now - _lastEndMs > 8000))
                Pause(now, BotMove::PauseMs(BotMove::Pause::StartDelay, botKey, uint32(_x)));
        }

        auto const pathT0 = std::chrono::steady_clock::now();
        BotPathInfo const pi = QueryPath(bot, _x + _ox, _y + _oy, _z);
        _pathUs = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pathT0).count());
        if (quiet && _questId && !std::strcmp(_tag, "quest") && (!_lastWalkLogMs || now - _lastWalkLogMs >= 10000))
        {
            _lastWalkLogMs = now ? now : 1;
            _walkLogged = true;
            EmitMotion(ai, bot, "decision", BOTLOG_INFO, "QUEST_WALK_START", "quest walk started",
                StringFormat(R"({{"quest_id":{},"goal":{},"distance":{:.0f},"path_length":{:.0f},"partial":{},"path_type":{},"path_us":{}}})",
                    _questId, Pos3(_x, _y, _z), dist, pi.Length, pi.Partial ? "true" : "false", pi.Type, _pathUs));
        }
        if (!quiet)
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "GOTO_START", StringFormat("walking to the {} goal", _tag),
                StringFormat(R"({{"tag":"{}","goal":{},"distance":{:.0f},"path_length":{:.0f},"partial":{},"end_gap":{:.0f},"end_gap_3d":{:.0f},"goal_far_from_poly":{},"path_type":{},"path_us":{}}})",
                    _tag, Pos3(_x, _y, _z), dist, pi.Length, pi.Partial ? "true" : "false", pi.EndGap, pi.EndGap3D, pi.GoalOffMesh ? "true" : "false", pi.Type, _pathUs));
        if (pi.NoPath)
            return Fail(ai, bot, "path_fail", "NO_PATH", pi.StartOffMesh ? "the bot stands off the navmesh (no polygon near its position)" :
                pi.OffMesh ? "goal is not on the navmesh (water, inside terrain or under a structure)" : "no path to the goal (regions not connected)",
                StringFormat(R"("path_type":{},"off_navmesh":{},"start_off_navmesh":{},"goal_off_navmesh":{})", pi.Type, pi.OffMesh ? "true" : "false",
                    pi.StartOffMesh ? "true" : "false", pi.GoalOffMesh ? "true" : "false"));
        if (pi.MmapMissing)
            return Fail(ai, bot, "path_fail", "MMAP_MISSING", "no navmesh for this map or area", StringFormat(R"("path_type":{})", pi.Type));
        // a partial path whose goal is far from every walkable polygon (ledge below or above, roof, island): the bot would walk to the closest
        // point and never arrive, so refuse it now
        if (pi.Partial && pi.GoalOffMesh)
            return Fail(ai, bot, "path_fail", "PATH_PARTIAL_FAR", "the goal is far from the walkable area; the path only reaches a point near it",
                StringFormat(R"("path_type":{},"end_gap":{:.0f},"end_gap_3d":{:.0f},"path_length":{:.0f},"nav_end":{})", pi.Type, pi.EndGap, pi.EndGap3D, pi.Length,
                    Pos3(pi.End.x, pi.End.y, pi.End.z)));
        // the goal z comes from the terrain, which can differ from the walkable surface (bridges, ramps, interiors): arrive against the
        // navmesh height of the goal
        if (!pi.Partial && pi.EndGap3D < 10.0f)
            _z = pi.End.z;
        _issueMs = 0;
    }

    // coming back after a pause (resting, holding): do not count the pause as lack of progress
    if (_lastStepMs && now - _lastStepMs > 3000)
    {
        _bestMs = now;
        _issueMs = 0;
    }
    _lastStepMs = now;

    if (std::optional<Result> steered = Steer(ai, bot, now))
        return *steered;

    bool const moving = !bot->movespline->Finalized();
    if (moving)
        _everMoved = true;
    if (dist < _bestDist - 2.0f)
    {
        _bestDist = dist;
        _bestMs = now;
        _legFails = 0;
    }

    bool const natural = BotMove::Natural().Enabled && ShapedTag(_tag);
    if (natural)
    {
        if (std::optional<Result> r = NaturalReissue(ai, bot, now, moving))
            return *r;
    }
    else if (!moving && (_issueMs == 0 || now - _issueMs >= 2000))
        Issue(bot, now ? now : 1);

    BotAIConfig const& cfg = BotAI::Config();
    if (now - _bestMs >= cfg.StuckSec * 1000)
    {
        ++_episodes;
        std::string const info = StringFormat(R"("rooted":{},"moving":{},"best_distance":{:.0f},"episodes":{},"pos":{})", bot->HasUnitState(UNIT_STATE_NOT_MOVE) ? "true" : "false",
            moving ? "true" : "false", _bestDist, _episodes, Pos3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
        if (_episodes >= cfg.StuckRepaths)
            return Fail(ai, bot, "stuck", "UNREACHABLE_TARGET", StringFormat("no progress to the {} goal after {} attempts", _tag, _episodes), info);

        if (_episodes == 1 && !RepeatFail("NO_PROGRESS", now))
            EmitMotion(ai, bot, "stuck", BOTLOG_WARN, "NO_PROGRESS", StringFormat("no progress to the {} goal for {} s", _tag, cfg.StuckSec),
                StringFormat(R"({{"tag":"{}","quest_id":{},"goal":{},"distance":{:.0f},"path_us":{},{}}})", _tag, _questId, Pos3(_x, _y, _z), dist, _pathUs, info));
        _bestMs = now;
        if (natural && NaturalStuck(ai, bot, now))
            return Result::Moving;
        Halt(bot);
        Issue(bot, now);
    }
    return Result::Moving;
}

void BotMotion::StartSideRoute(Player* bot, uint32 now, float yards)
{
    float dx = _x + _ox - bot->GetPositionX(), dy = _y + _oy - bot->GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return;
    dx /= len;
    dy /= len;
    float const ahead = std::min(len, 10.0f) * 0.5f;
    _sx = bot->GetPositionX() + dx * ahead - dy * yards;
    _sy = bot->GetPositionY() + dy * ahead + dx * yards;
    _sz = bot->GetPositionZ();
    _sideOn = true;
    _sideMs = now;
    _issueMs = 0;
}

// Natural movement, the part that decides when a leg is issued: no new leg while paused or backing off, the same leg is never issued
// again and again (a third identical leg within 8 s counts as a failure), a failed leg is followed by another route or a wait.
std::optional<BotMotion::Result> BotMotion::NaturalReissue(BotAI* ai, Player* bot, uint32 now, bool moving)
{
    uint64 const botKey = bot->GetGUID().GetCounter();
    if (_sideOn && (bot->GetExactDist2d(_sx, _sy) < 3.0f || now - _sideMs > 8000))
    {
        _sideOn = false;
        _issueMs = 0;
    }
    if (moving)
    {
        if (!_trailMs || now - _trailMs >= 2000)
        {
            _trailMs = now ? now : 1;
            for (uint32 i = std::min<uint32>(_trailN, 3); i > 0; --i)
                _trail[i] = _trail[i - 1];
            _trail[0] = { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() };
            _trailN = std::min<uint32>(_trailN + 1, 4);
        }
        return std::nullopt;
    }
    if (IsPaused(now) || int32(_waitUntilMs - now) > 0)
    {
        _bestMs = now; // standing on purpose is not lack of progress
        return Result::Moving;
    }
    if (_issueMs != 0 && now - _issueMs < 2000)
        return std::nullopt;

    BotMove::Vec3 const here{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() };
    BotMove::Vec3 const goal{ _x + _ox, _y + _oy, _z };
    uint64 const key = BotMove::LegKey(here, goal);
    if (_fails.Blocked(key, now))
    {
        _waitUntilMs = _fails.UntilMs(key);
        _bestMs = now;
        return Result::Moving;
    }
    uint32 const repeats = _legs.Issue(key, now);
    if (repeats >= 1)
        _metrics.NoteRepeatedLeg();

    bool failed = repeats >= 2;
    if (!failed && Issue(bot, now ? now : 1) == Launch::NoPath)
        failed = true;
    if (!failed)
        return std::nullopt;

    ++_legFails;
    uint32 const wait = _fails.Fail(key, now, botKey);
    switch (BotMove::AdviceFor(_legFails))
    {
        case BotMove::Reroute::Retry:
        case BotMove::Reroute::Wait:
            _waitUntilMs = now + wait;
            _bestMs = now;
            return Result::Moving;
        case BotMove::Reroute::SideRoute:
        {
            float const side = BotMove::Range(BotMove::Mix(botKey, key), 6.0f, 12.0f) * (BotMove::Unit(BotMove::Mix(botKey, key, 7)) < 0.5f ? -1.0f : 1.0f);
            StartSideRoute(bot, now, side);
            Issue(bot, now ? now : 1);
            return std::nullopt;
        }
        case BotMove::Reroute::GiveUp:
            break;
    }
    return Fail(ai, bot, "path_fail", "ROUTE_GIVEUP", StringFormat("no usable route to the {} goal after {} attempts", _tag, _legFails),
        StringFormat(R"("leg_fails":{},"repeated_legs":{})", _legFails, _legs.Repeats()));
}

// Natural movement, no progress for Bot.AI.Stuck.Sec: look around, step back along the way just walked, then try a different route.
// Returns false when the plan is to give up (the caller then fails the goal as before).
bool BotMotion::NaturalStuck(BotAI* /*ai*/, Player* bot, uint32 now)
{
    uint64 const botKey = bot->GetGUID().GetCounter();
    BotMove::StuckPlan const plan = BotMove::PlanStuck(_episodes, botKey, now);
    _metrics.NoteStuck();
    if (plan.Act == BotMove::StuckAct::GiveUp)
        return false;
    Halt(bot);
    bot->SetFacingTo(bot->GetOrientation() + plan.TurnRad);
    uint32 pauseMs = plan.LookPauseMs;
    if (plan.Act == BotMove::StuckAct::StepBack)
    {
        for (uint32 i = 0; i < _trailN; ++i)
        {
            float const d = std::sqrt(bot->GetExactDist2dSq(_trail[i].x, _trail[i].y));
            if (d >= plan.StepYards * 0.6f || i + 1 == _trailN)
            {
                if (d > 1.0f && d < 15.0f)
                {
                    Movement::MoveSplineInit init(bot);
                    init.MoveTo(_trail[i].x, _trail[i].y, _trail[i].z, false);
                    init.SetWalk(true);
                    init.Launch();
                    pauseMs += uint32(d / 2.5f * 1000.0f);
                }
                break;
            }
        }
    }
    else
        StartSideRoute(bot, now, plan.SideYards);
    Pause(now, pauseMs);
    _bestMs = now;
    _issueMs = now; // the next leg follows the pause
    return true;
}

// Once per AI tick (alive bots): movement metrics for the sim, written as MOVE_METRICS rows.
void BotMotion::Tick(BotAI* ai, Player* bot)
{
    BotMove::NaturalConfig const& nat = BotMove::Natural();
    if (!nat.Enabled || !nat.Metrics || !bot->IsAlive())
        return;
    uint32 const now = ai->GetNowMs();
    _metrics.Sample(now, bot->GetPositionX(), bot->GetPositionY(), !bot->movespline->Finalized());
    if (!_metrics.Due(now, nat.MetricsSec * 1000))
        return;
    BotMove::MetricsSnapshot const m = _metrics.Take(now);
    if (!m.WindowMs)
        return;
    ai->EmitEvent(bot, "decision", BOTLOG_INFO, "MOVE_METRICS", "movement metrics of the last window",
        StringFormat(R"({{"window_ms":{},"moving_ms":{},"idle_ms":{},"idle_ratio":{:.2f},"distance":{:.0f},"turn_rate_deg_s":{:.1f},"sharp_turns":{},"repeated_legs":{},"stuck":{},"path_fails":{},"longest_idle_ms":{}}})",
            m.WindowMs, m.MovingMs, m.IdleMs, m.IdleRatio, m.DistanceYd, m.TurnRateDegPerSec, m.SharpTurns, m.RepeatedLegs, m.StuckEvents, m.PathFails, m.LongestIdleMs));
}

// Natural idling: runs when nothing else wants the bot. Looks around now and then, sits when hurt or drained, wanders a few yards, and
// never stands still longer than IdleMaxStandSec.
bool BotMotion::IdleStep(BotAI* ai, Player* bot)
{
    BotMove::NaturalConfig const& nat = BotMove::Natural();
    if (BotTownIdle::Cfg().Enabled && (bot->GetRestMgr().HasRestFlag(REST_FLAG_IN_CITY) || bot->GetRestMgr().HasRestFlag(REST_FLAG_IN_TAVERN)))
        return TownIdleStep(ai, bot);
    if (!nat.Enabled || !nat.Idle)
        return false; // town idling alone adds the strategy; the plain idling stays off
    uint32 const now = ai->GetNowMs();
    if (!_idleStillSince || bot->GetExactDist2dSq(_idleX, _idleY) > 0.25f)
    {
        _idleX = bot->GetPositionX();
        _idleY = bot->GetPositionY();
        _idleStillSince = now ? now : 1;
    }
    BotAggroHit hit;
    BotMove::IdleFacts f;
    f.StillMs = now - _idleStillSince;
    f.SinceActionMs = _idleLastAct ? now - _idleLastAct : 1000000;
    f.Sitting = _idleSat && bot->GetStandState() == UNIT_STAND_STATE_SIT;
    f.SittingMs = f.Sitting ? now - _idleSatMs : 0;
    f.Threat = bot->IsInCombat() || BotAggroNear(ai, bot, 0, hit);
    f.HurtOrDrained = bot->GetHealthPct() < 99.0f;
    if (!f.HurtOrDrained && bot->GetMaxPower(POWER_MANA) > 0)
        f.HurtOrDrained = bot->GetPower(POWER_MANA) * 100 < bot->GetMaxPower(POWER_MANA) * 99;
    if (_idleSat && !f.Sitting)
        _idleSat = false; // something stood the bot up
    BotMove::IdleConfig cfg;
    cfg.MaxStillMs = nat.IdleMaxStandSec * 1000;
    BotMove::IdlePlan const p = BotMove::PlanIdle(f, cfg, bot->GetGUID().GetCounter(), now);
    switch (p.Act)
    {
        case BotMove::IdleAct::Stay:
            return false;
        case BotMove::IdleAct::Look:
            bot->SetFacingTo(bot->GetOrientation() + p.TurnRad);
            break;
        case BotMove::IdleAct::Sit:
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            _idleSat = true;
            _idleSatMs = now;
            break;
        case BotMove::IdleAct::StandUp:
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            _idleSat = false;
            break;
        case BotMove::IdleAct::Wander:
        {
            float const x = bot->GetPositionX() + std::cos(p.WanderBearing) * p.WanderYards;
            float const y = bot->GetPositionY() + std::sin(p.WanderBearing) * p.WanderYards;
            float const h = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, bot->GetPositionZ() + 3.0f, true, 8.0f);
            _idleLastAct = now ? now : 1;
            if (h <= INVALID_HEIGHT || std::fabs(h - bot->GetPositionZ()) > 3.0f || BotDestinationVetoed(bot, x, y, h))
                return true; // not here; the next try picks another bearing
            _idleSat = false;
            SetGoal(bot->GetMapId(), x, y, h, 1.5f, "idle");
            return true;
        }
    }
    _idleLastAct = now ? now : 1;
    return true;
}

// Town idling: in a city or an inn the bot drifts between the places where players gather and lingers there (looks around, emotes,
// sits). The gathering places are scanned from the nearby creatures and mailboxes and cached for a minute.
bool BotMotion::TownIdleStep(BotAI* ai, Player* bot)
{
    BotTownIdle::Config const& cfg = BotTownIdle::Cfg();
    uint32 const now = ai->GetNowMs();
    uint64 const key = bot->GetGUID().GetCounter();

    if (!_townScanMs || now - _townScanMs > (_townSpots.empty() ? 15000u : 60000u))
    {
        _townScanMs = now ? now : 1;
        std::vector<BotTownIdle::Spot> spots;
        auto add = [&spots](float x, float y, BotTownIdle::SpotKind kind)
        {
            for (BotTownIdle::Spot const& s : spots)
                if ((s.X - x) * (s.X - x) + (s.Y - y) * (s.Y - y) < 64.0f)
                    return; // one spot per cluster of NPCs
            if (spots.size() < 24)
                spots.push_back({ x, y, kind });
        };
        std::vector<Creature*> creatures;
        bot->GetCreatureListWithOptionsInGrid(creatures, cfg.SearchYards, FindCreatureOptions());
        // one pass per kind in weight order, so the most popular kind owns a cluster
        struct { NPCFlags Flag; BotTownIdle::SpotKind Kind; } const kinds[] = {
            { UNIT_NPC_FLAG_INNKEEPER, BotTownIdle::SpotKind::Inn },
            { UNIT_NPC_FLAG_BANKER, BotTownIdle::SpotKind::Bank },
            { UNIT_NPC_FLAG_AUCTIONEER, BotTownIdle::SpotKind::Auction },
            { UNIT_NPC_FLAG_FLIGHTMASTER, BotTownIdle::SpotKind::Flight } };
        for (auto const& k : kinds)
            for (Creature* c : creatures)
                if (c->IsAlive() && !c->IsInCombat() && c->HasNpcFlag(k.Flag))
                    add(c->GetPositionX(), c->GetPositionY(), k.Kind);
        std::vector<GameObject*> boxes;
        FindGameObjectOptions goOptions;
        goOptions.GameObjectType = GAMEOBJECT_TYPE_MAILBOX;
        bot->GetGameObjectListWithOptionsInGrid(boxes, cfg.SearchYards, goOptions);
        for (GameObject* go : boxes)
            add(go->GetPositionX(), go->GetPositionY(), BotTownIdle::SpotKind::Mail);
        for (Creature* c : creatures)
            if (c->IsAlive() && !c->IsInCombat() && c->HasNpcFlag(UNIT_NPC_FLAG_VENDOR_MASK))
                add(c->GetPositionX(), c->GetPositionY(), BotTownIdle::SpotKind::Vendor);
        _townSpots = std::move(spots);
        // keep pointing at the same place across a rescan
        _townSpot = -1;
        for (size_t i = 0; i < _townSpots.size(); ++i)
            if ((_townSpots[i].X - _townSpotX) * (_townSpots[i].X - _townSpotX) + (_townSpots[i].Y - _townSpotY) * (_townSpots[i].Y - _townSpotY) < 64.0f)
                _townSpot = int32(i);
    }

    if (!_idleStillSince || bot->GetExactDist2dSq(_idleX, _idleY) > 0.25f)
    {
        _idleX = bot->GetPositionX();
        _idleY = bot->GetPositionY();
        _idleStillSince = now ? now : 1;
    }
    BotAggroHit hit;
    BotTownIdle::Facts f;
    f.StillMs = now - _idleStillSince;
    f.SinceActionMs = _idleLastAct ? now - _idleLastAct : 1000000;
    f.Sitting = _idleSat && bot->GetStandState() == UNIT_STAND_STATE_SIT;
    f.SittingMs = f.Sitting ? now - _idleSatMs : 0;
    f.Threat = bot->IsInCombat() || BotAggroNear(ai, bot, 0, hit);
    f.HurtOrDrained = bot->GetHealthPct() < 99.0f;
    if (!f.HurtOrDrained && bot->GetMaxPower(POWER_MANA) > 0)
        f.HurtOrDrained = bot->GetPower(POWER_MANA) * 100 < bot->GetMaxPower(POWER_MANA) * 99;
    f.CurrentSpot = _townSpot;
    f.AtSpot = _townSpot >= 0 && bot->GetExactDist2dSq(_townSpotX, _townSpotY) <= (cfg.StandOffMax + 2.0f) * (cfg.StandOffMax + 2.0f);
    if (_idleSat && !f.Sitting)
        _idleSat = false; // something stood the bot up

    BotTownIdle::Plan const p = BotTownIdle::PlanStep(f, cfg, _townSpots, key, now);
    switch (p.What)
    {
        case BotTownIdle::Act::Stay:
            return false;
        case BotTownIdle::Act::Look:
            bot->SetFacingTo(bot->GetOrientation() + p.TurnRad);
            break;
        case BotTownIdle::Act::Emote:
        {
            static constexpr Emote emotes[BotTownIdle::EMOTE_KINDS] = { EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_LAUGH, EMOTE_ONESHOT_YES, EMOTE_ONESHOT_NO, EMOTE_ONESHOT_DANCE };
            bot->HandleEmoteCommand(emotes[uint8(p.Emote)]);
            break;
        }
        case BotTownIdle::Act::Sit:
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            _idleSat = true;
            _idleSatMs = now;
            break;
        case BotTownIdle::Act::StandUp:
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            _idleSat = false;
            break;
        case BotTownIdle::Act::GoSpot:
        case BotTownIdle::Act::Wander:
        {
            float x, y;
            if (p.What == BotTownIdle::Act::GoSpot && p.Spot >= 0 && size_t(p.Spot) < _townSpots.size())
            {
                BotTownIdle::Spot const& s = _townSpots[p.Spot];
                x = s.X + std::cos(p.Bearing) * p.StandOff;
                y = s.Y + std::sin(p.Bearing) * p.StandOff;
            }
            else
            {
                x = bot->GetPositionX() + std::cos(p.Bearing) * p.StandOff;
                y = bot->GetPositionY() + std::sin(p.Bearing) * p.StandOff;
            }
            _idleLastAct = now ? now : 1;
            float const h = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, bot->GetPositionZ() + 3.0f, true, 8.0f);
            if (h <= INVALID_HEIGHT || std::fabs(h - bot->GetPositionZ()) > 6.0f || BotDestinationVetoed(bot, x, y, h))
                return true; // not here; the next try picks another bearing or spot
            if (p.What == BotTownIdle::Act::GoSpot)
            {
                _townSpot = p.Spot;
                _townSpotX = _townSpots[p.Spot].X;
                _townSpotY = _townSpots[p.Spot].Y;
                ai->EmitEvent(bot, "decision", BOTLOG_TRACE, "TOWN_IDLE_GO", "walking to a gathering place",
                    StringFormat(R"({{"spot":{},"kind":{},"spots":{}}})", p.Spot, int(_townSpots[p.Spot].Kind), _townSpots.size()));
            }
            _idleSat = false;
            SetGoal(bot->GetMapId(), x, y, h, 1.5f, "idle");
            return true;
        }
    }
    _idleLastAct = now ? now : 1;
    return true;
}

void BotEndRest(BotAI* ai, Player* bot, char const* reason, bool standUp)
{
    BotRest& rest = ai->Rest();
    if (!rest.Resting())
        return;
    if (bot->IsAlive())
    {
        if (rest.FoodSpell && (rest.Bits & BotRest::EAT))
            bot->RemoveAurasDueToSpell(rest.FoodSpell);
        if (rest.DrinkSpell && (rest.Bits & BotRest::DRINK))
            bot->RemoveAurasDueToSpell(rest.DrinkSpell);
        if (standUp && bot->GetStandState() == UNIT_STAND_STATE_SIT)
            bot->SetStandState(UNIT_STAND_STATE_STAND);
    }
    ai->EmitEvent(bot, "decision", BOTLOG_INFO, "REST_INTERRUPTED", StringFormat("rest ended early: {}", reason),
        StringFormat(R"({{"cause":"{}","seconds":{},"hp_pct":{:.0f},"mana_pct":{:.0f}}})", reason, (ai->GetNowMs() - rest.SinceMs) / 1000, bot->GetHealthPct(), PowerPct(bot)));
    rest = BotRest();
}

// ---------------------------------------------------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------------------------------------------------
void RegisterPhase3BotObjects(BotRegistry& r)
{
    BuildConsumableTables();

    // triggers
    AddFnTrigger(r, "has_goal", 0, [](BotAI* ai, Player*) { return ai->Motion().HasGoal(); });
    AddFnTrigger(r, "follow_active", 0, [](BotAI* ai, Player*) { return !ai->Motion().GetFollow().IsEmpty(); });
    AddFnTrigger(r, "hold_active", 500, [](BotAI* ai, Player* bot) { return ai->Motion().HasGoal() || !bot->movespline->Finalized(); });
    AddFnTrigger(r, "need_eat", 1000, NeedEat);
    AddFnTrigger(r, "need_drink", 1000, NeedDrink);
    AddFnTrigger(r, "resting", 1000, RestingNow);
    AddFnTrigger(r, "recover_tick", 0, [](BotAI* ai, Player*) { return ai->GetState() == BotState::Dead; });
    // idle or resting inside the aggro range of a hostile elite / higher-level mob (walking goals are steered by BotMotion::Steer)
    AddFnTrigger(r, "aggro_near", 1000, [](BotAI* ai, Player* bot)
    {
        if (!BotAI::Config().AggroAvoid || !bot->IsAlive() || bot->IsInCombat() || (ai->Motion().HasGoal() && !ai->Rest().Resting()))
            return false;
        BotAggroHit hit;
        return BotAggroNear(ai, bot, ai->Motion().QuestEntry(), hit) && hit.Dist < hit.Aggro;
    });

    AddFnTrigger(r, "idle_natural", 1000, [](BotAI* ai, Player* bot)
    {
        BotMove::NaturalConfig const& nat = BotMove::Natural();
        return nat.Enabled && nat.Idle && bot->IsAlive() && !bot->IsInCombat() && !ai->Motion().HasGoal() && ai->Motion().GetFollow().IsEmpty()
            && !ai->Rest().Resting() && bot->movespline->Finalized() && !bot->IsNonMeleeSpellCast(false) && !bot->IsInFlight();
    });

    // actions
    r.AddAction("natural_idle", MakeAct<NaturalIdleAction>());
    r.AddAction("move_to_goal", MakeAct<MoveToGoalAction>());
    r.AddAction("follow_leader", MakeAct<FollowLeaderAction>());
    r.AddAction("stop_moving", MakeAct<StopMovingAction>());
    r.AddAction("eat", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<EatAction>(ai, "eat", true); });
    r.AddAction("drink", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<EatAction>(ai, "drink", false); });
    r.AddAction("rest_tick", MakeAct<RestTickAction>());
    r.AddAction("aggro_retreat", MakeAct<AggroRetreatAction>());
    r.AddAction("release_spirit", MakeAct<ReleaseSpiritAction>());
    r.AddAction("corpse_run", MakeAct<CorpseRunAction>());
    r.AddAction("reclaim_corpse", MakeAct<ReclaimCorpseAction>());
    r.AddAction("spirit_heal", MakeAct<SpiritHealAction>());

    // multipliers
    r.AddMultiplier("hold_position", [](BotAI* ai) -> std::unique_ptr<Multiplier> { return std::make_unique<HoldPositionMultiplier>(ai); });
    r.AddMultiplier("resting_hold", [](BotAI* ai) -> std::unique_ptr<Multiplier> { return std::make_unique<RestingHoldMultiplier>(ai); });

    // strategies
    class Fn : public Strategy
    {
    public:
        using Init = void (*)(std::vector<BotTriggerNode>&, std::vector<std::string>&);
        Fn(char const* name, Init init) : Strategy(name), _init(init) { }
        void InitTriggers(std::vector<BotTriggerNode>& t) override { std::vector<std::string> m; _init(t, m); }
        void InitMultipliers(std::vector<std::string>& m) override { std::vector<BotTriggerNode> t; _init(t, m); }
    private:
        Init _init;
    };

    r.AddStrategy("goto", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("goto", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "has_goal", { { "move_to_goal", BotRelevance::Move } } });
        });
    });
    r.AddStrategy("follow", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("follow", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "follow_active", { { "follow_leader", BotRelevance::Move } } });
        });
    });
    r.AddStrategy("stay", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("stay", [](std::vector<BotTriggerNode>& t, std::vector<std::string>& m)
        {
            t.push_back({ "hold_active", { { "stop_moving", BotRelevance::High } } });
            m.push_back("hold_position");
        });
    });
    r.AddStrategy("rest", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("rest", [](std::vector<BotTriggerNode>& t, std::vector<std::string>& m)
        {
            t.push_back({ "need_eat", { { "eat", BotRelevance::Rest } } });
            t.push_back({ "need_drink", { { "drink", BotRelevance::Rest } } });
            t.push_back({ "resting", { { "rest_tick", BotRelevance::Rest - 1.0f } } });
            t.push_back({ "aggro_near", { { "aggro_retreat", BotRelevance::Rest + 1.0f } } });
            m.push_back("resting_hold");
        });
    });
    r.AddStrategy("natural_idle", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("natural_idle", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "idle_natural", { { "natural_idle", BotRelevance::Default } } });
        });
    });
    r.AddStrategy("recover", BotStateBit(BotState::Dead), []() -> std::unique_ptr<Strategy>
    {
        return std::make_unique<Fn>("recover", [](std::vector<BotTriggerNode>& t, std::vector<std::string>&)
        {
            t.push_back({ "recover_tick", { { "release_spirit", BotRelevance::Emergency }, { "reclaim_corpse", BotRelevance::Emergency - 1.0f },
                { "spirit_heal", BotRelevance::Emergency - 2.0f }, { "corpse_run", BotRelevance::Emergency - 3.0f } } });
        });
    });
}
