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
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MapUtils.h"
#include "MiscPackets.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "NPCPackets.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

// ---------------------------------------------------------------------------------------------------------------------
// follow / stay
// ---------------------------------------------------------------------------------------------------------------------
constexpr float FOLLOW_START_DIST = 10.0f; // start walking when the leader is farther than this
constexpr float FOLLOW_ARRIVE_DIST = 5.0f;  // and stop within this distance

class FollowLeaderAction : public Action
{
public:
    explicit FollowLeaderAction(BotAI* ai) : Action(ai, "follow_leader", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG) { }
    bool IsUseful() override { return !GetAI()->Motion().GetFollow().IsEmpty(); }
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
    return !(ai->Rest().Bits & BotRest::EAT) && bot->IsAlive() && !bot->IsInCombat() && bot->GetHealthPct() < float(BotAI::Config().EatBelowPct);
}

bool NeedDrink(BotAI* ai, Player* bot)
{
    return !(ai->Rest().Bits & BotRest::DRINK) && bot->IsAlive() && !bot->IsInCombat() && UsesMana(bot) &&
        PowerPct(bot) < float(std::max(BotAI::Config().DrinkBelowPct, BotCombatPrePullManaPct()));
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
            r.Plan = BotRecover::Mode::Healer;
            std::strcpy(r.PlanReason, "CORPSE_RUN_FAILED");
            ai->EmitEvent(bot, "decision", BOTLOG_WARN, "SPIRIT_HEALER_PLAN", "corpse run failed, falling back to the spirit healer", R"({"reason":"CORPSE_RUN_FAILED"})");
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
void BotMotion::Issue(Player* bot, uint32 now)
{
    _issueMs = now;
    ++_issues;
    if (bot->HasUnitState(UNIT_STATE_NOT_MOVE) || bot->IsMovementPreventedByCasting())
        return; // rooted/stunned/casting: no progress will be made, the stuck detection reports it

    if (bot->GetStandState() != UNIT_STAND_STATE_STAND && !bot->IsInCombat())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    if (bot->GetExactDist(_x, _y, _z) < 1.0f)
        return;

    EnsureGrids(bot, _x, _y);
    PathGenerator path(bot);
    path.CalculatePath(_x, _y, _z, false);
    uint32 const type = uint32(path.GetPathType());
    if ((type & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)) || path.GetPath().size() < 2)
        return;

    Movement::MoveSplineInit init(bot);
    init.MovebyPath(path.GetPath());
    init.Launch();
}

BotMotion::Result BotMotion::Fail(BotAI* ai, Player* bot, char const* type, char const* reason, std::string const& summary, std::string const& extra)
{
    Halt(bot);
    _active = false;
    ai->EmitEvent(bot, type, BOTLOG_WARN, reason, summary, StringFormat(R"({{"tag":"{}","goal":{},"goal_map":{},"distance":{:.0f},"seconds":{},"issues":{}{}}})",
        _tag, Pos3(_x, _y, _z), _mapId, bot->GetExactDist2d(_x, _y), (ai->GetNowMs() - _startMs) / 1000, _issues, extra.empty() ? std::string() : "," + extra));
    return Result::Failed;
}

BotMotion::Result BotMotion::Step(BotAI* ai, Player* bot)
{
    if (!_active)
        return Result::Idle;

    uint32 const now = ai->GetNowMs();
    bool const quiet = !std::strcmp(_tag, "follow") || !std::strcmp(_tag, "quest"); // quest legs are re-issued constantly (BotQuest logs its own decisions) // follow goals retarget constantly: only failures are logged

    if (bot->GetMapId() != _mapId)
        return Fail(ai, bot, "path_fail", "WRONG_MAP", "goal is on another map", StringFormat(R"("bot_map":{})", bot->GetMapId()));

    float const dist = bot->GetExactDist2d(_x, _y);
    if (dist <= _arrive && std::fabs(bot->GetPositionZ() - _z) < 25.0f)
    {
        if (!quiet)
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "GOTO_ARRIVED", StringFormat("arrived at the {} goal", _tag),
                StringFormat(R"({{"tag":"{}","goal":{},"seconds":{},"issues":{},"distance":{:.1f}}})", _tag, Pos3(_x, _y, _z), (now - _startMs) / 1000, _issues, dist));
        Halt(bot);
        _active = false;
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

        BotPathInfo const pi = QueryPath(bot, _x, _y, _z);
        if (!quiet)
            ai->EmitEvent(bot, "decision", BOTLOG_INFO, "GOTO_START", StringFormat("walking to the {} goal", _tag),
                StringFormat(R"({{"tag":"{}","goal":{},"distance":{:.0f},"path_length":{:.0f},"partial":{},"end_gap":{:.0f},"end_gap_3d":{:.0f},"goal_far_from_poly":{},"path_type":{}}})",
                    _tag, Pos3(_x, _y, _z), dist, pi.Length, pi.Partial ? "true" : "false", pi.EndGap, pi.EndGap3D, pi.GoalOffMesh ? "true" : "false", pi.Type));
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

    bool const moving = !bot->movespline->Finalized();
    if (moving)
        _everMoved = true;
    if (dist < _bestDist - 2.0f)
    {
        _bestDist = dist;
        _bestMs = now;
    }

    if (!moving && (_issueMs == 0 || now - _issueMs >= 2000))
        Issue(bot, now ? now : 1);

    BotAIConfig const& cfg = BotAI::Config();
    if (now - _bestMs >= cfg.StuckSec * 1000)
    {
        ++_episodes;
        std::string const info = StringFormat(R"("rooted":{},"moving":{},"best_distance":{:.0f},"episodes":{},"pos":{})", bot->HasUnitState(UNIT_STATE_NOT_MOVE) ? "true" : "false",
            moving ? "true" : "false", _bestDist, _episodes, Pos3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
        if (_episodes >= cfg.StuckRepaths)
            return Fail(ai, bot, "stuck", "UNREACHABLE_TARGET", StringFormat("no progress to the {} goal after {} attempts", _tag, _episodes), info);

        if (_episodes == 1)
            ai->EmitEvent(bot, "stuck", BOTLOG_WARN, "NO_PROGRESS", StringFormat("no progress to the {} goal for {} s", _tag, cfg.StuckSec),
                StringFormat(R"({{"tag":"{}","goal":{},"distance":{:.0f},{}}})", _tag, Pos3(_x, _y, _z), dist, info));
        _bestMs = now;
        Halt(bot);
        Issue(bot, now);
    }
    return Result::Moving;
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

    // actions
    r.AddAction("move_to_goal", MakeAct<MoveToGoalAction>());
    r.AddAction("follow_leader", MakeAct<FollowLeaderAction>());
    r.AddAction("stop_moving", MakeAct<StopMovingAction>());
    r.AddAction("eat", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<EatAction>(ai, "eat", true); });
    r.AddAction("drink", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<EatAction>(ai, "drink", false); });
    r.AddAction("rest_tick", MakeAct<RestTickAction>());
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
            m.push_back("resting_hold");
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
