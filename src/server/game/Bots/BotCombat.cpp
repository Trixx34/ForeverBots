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

// Basic combat of the bot engine (strategy "combat", Combat engine only), see docs/playerbots/engine-design.md.
//   combat_flee   (Emergency)  runs away from a mob that is too strong (level difference, elite, world boss) with the goto motion
//   combat_heal   (High)       self-heal below a health threshold (classes that have a heal)
//   combat_engage (Move)       keeps/picks the target, attacks, faces and closes to melee or spell range (MoveChase)
//   combat_cast   (Normal)     one table spell per tick (buff, dots, direct damage, finisher), highest known rank
// The per-bot state lives in the cached value "combat_ctx" (BotCombatCtx). Every actions returns false when it has nothing to do,
// so the engine falls through to the next one (one action per tick). Successful casts are only counted; the decisions that
// matter (target picked, flee, give up, cast failure, ranged to melee, heal) are logged once, and a per-fight summary is
// logged when the Combat engine is left.

#include "BotAI.h"
#include "BotCombat.h"
#include "CombatManager.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace
{
using Trinity::StringFormat;

// ---------------------------------------------------------------------------------------------------------------------
// configuration (Bot.AI.Combat.*, see worldserver.conf.dist)
// ---------------------------------------------------------------------------------------------------------------------
struct CombatConfig
{
    bool FleeEnabled = true;       // Bot.AI.Combat.Flee.Enabled
    int32 FleeLevelDiff = 4;       // Bot.AI.Combat.Flee.LevelDiff: effective mob level minus bot level above this = too strong
    int32 EliteLevelBonus = 3;     // Bot.AI.Combat.EliteLevelBonus: elites count as this many levels higher
    uint32 FleeMaxSec = 10;        // Bot.AI.Combat.Flee.MaxSec: after this long the bot stops running and fights back
    uint32 FleeYards = 35;         // Bot.AI.Combat.Flee.Yards: distance of the flee destination
    uint32 ApproachSec = 15;       // Bot.AI.Combat.ApproachTimeoutSec: give up a target that cannot be reached
    uint32 HealBelowPct = 45;      // Bot.AI.Combat.HealBelowPct: self-heal under this health percent
    uint32 CasterRangePct = 80;    // Bot.AI.Combat.CasterRangePct: casters stand at this percent of the spell range
};

CombatConfig const& Cfg()
{
    static CombatConfig cfg;
    static std::once_flag once;
    std::call_once(once, []()
    {
        cfg.FleeEnabled = sConfigMgr->GetBoolDefault("Bot.AI.Combat.Flee.Enabled", true);
        cfg.FleeLevelDiff = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.Flee.LevelDiff", 4), 0, 60);
        cfg.EliteLevelBonus = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.EliteLevelBonus", 3), 0, 20);
        cfg.FleeMaxSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.Flee.MaxSec", 10), 1, 120));
        cfg.FleeYards = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.Flee.Yards", 35), 10, 100));
        cfg.ApproachSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.ApproachTimeoutSec", 15), 3, 120));
        cfg.HealBelowPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.HealBelowPct", 45), 0, 100));
        cfg.CasterRangePct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Combat.CasterRangePct", 80), 30, 100));
    });
    return cfg;
}

std::string Json(std::string const& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (uint8(c) >= 0x20)
            out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// per-class spell table. Vanilla Classic rank-1 spell ids (the highest known rank is found by walking the rank chain).
// An entry whose id does not exist or whose name differs from the expected one is dropped at startup and logged.
// ---------------------------------------------------------------------------------------------------------------------
enum class Role : uint8 { Melee, Caster, Hunter };
enum class Kind : uint8
{
    SelfBuff,   // cast on self while the aura is missing (Battle Shout, Seal of Righteousness)
    Direct,     // damage spell/ability on the target (melee ability or ranged spell, decided by the spell range)
    Dot,        // cast while the target does not carry our aura
    Finisher,   // needs combo points
    AutoShot,   // auto-repeat ranged attack (hunter)
    Heal        // self-heal (combat_heal)
};

struct SpellDef
{
    Classes Class;
    uint32 Root;
    char const* Name;
    Kind Type;
    uint32 ReqAura = 0;        // aura (spell id) the bot must carry first (Judgement needs a seal), 0 none
    uint8 MinCombo = 0;        // Finisher: combo points needed
};

constexpr SpellDef SPELLS[] =
{
    // Warrior: Battle Shout, Rend (needs Battle Stance), Heroic Strike (next melee swing)
    { CLASS_WARRIOR, 6673,  "Battle Shout",         Kind::SelfBuff },
    { CLASS_WARRIOR, 772,   "Rend",                 Kind::Dot },
    { CLASS_WARRIOR, 78,    "Heroic Strike",        Kind::Direct },
    // Rogue
    { CLASS_ROGUE,   2098,  "Eviscerate",           Kind::Finisher, 0, 3 },
    { CLASS_ROGUE,   1752,  "Sinister Strike",      Kind::Direct },
    // Paladin
    { CLASS_PALADIN, 20154, "Seal of Righteousness", Kind::SelfBuff },
    { CLASS_PALADIN, 20271, "Judgement",            Kind::Direct, 20154 },
    { CLASS_PALADIN, 635,   "Holy Light",           Kind::Heal },
    // Hunter
    { CLASS_HUNTER,  1978,  "Serpent Sting",        Kind::Dot },
    { CLASS_HUNTER,  3044,  "Arcane Shot",          Kind::Direct },
    { CLASS_HUNTER,  2973,  "Raptor Strike",        Kind::Direct },
    { CLASS_HUNTER,  75,    "Auto Shot",            Kind::AutoShot },
    // Mage
    { CLASS_MAGE,    2136,  "Fire Blast",           Kind::Direct },
    { CLASS_MAGE,    116,   "Frostbolt",            Kind::Direct },
    { CLASS_MAGE,    133,   "Fireball",             Kind::Direct },
    // Priest
    { CLASS_PRIEST,  589,   "Shadow Word: Pain",    Kind::Dot },
    { CLASS_PRIEST,  585,   "Smite",                Kind::Direct },
    { CLASS_PRIEST,  2050,  "Lesser Heal",          Kind::Heal },
    // Warlock
    { CLASS_WARLOCK, 348,   "Immolate",             Kind::Dot },
    { CLASS_WARLOCK, 172,   "Corruption",           Kind::Dot },
    { CLASS_WARLOCK, 686,   "Shadow Bolt",          Kind::Direct },
    // Shaman
    { CLASS_SHAMAN,  8042,  "Earth Shock",          Kind::Direct },
    { CLASS_SHAMAN,  403,   "Lightning Bolt",       Kind::Direct },
    { CLASS_SHAMAN,  331,   "Healing Wave",         Kind::Heal },
    // Druid
    { CLASS_DRUID,   8921,  "Moonfire",             Kind::Dot },
    { CLASS_DRUID,   5176,  "Wrath",                Kind::Direct },
    { CLASS_DRUID,   5185,  "Healing Touch",        Kind::Heal },
};

Role RoleOf(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_PALADIN:
            return Role::Melee;
        case CLASS_HUNTER:
            return Role::Hunter;
        default:
            return Role::Caster;
    }
}

char const* RoleName(Role r) { return r == Role::Melee ? "melee" : r == Role::Hunter ? "hunter" : "caster"; }

bool g_tableOk[std::size(SPELLS)];

std::string SpellNameOf(SpellInfo const* si)
{
    return si && si->SpellName ? std::string((*si->SpellName)[DEFAULT_LOCALE]) : std::string();
}

void ValidateTable()
{
    std::string dropped;
    uint32 ok = 0;
    for (size_t i = 0; i < std::size(SPELLS); ++i)
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(SPELLS[i].Root, DIFFICULTY_NONE);
        std::string const name = SpellNameOf(si);
        g_tableOk[i] = si && StringEqualI(name, SPELLS[i].Name);
        if (g_tableOk[i])
            ++ok;
        else
            dropped += StringFormat(" {}:{}(found '{}')", SPELLS[i].Root, SPELLS[i].Name, si ? name : std::string("missing"));
    }
    TC_LOG_INFO("server.worldserver", "Bot AI combat: {} class spells validated, dropped:{}", ok, dropped.empty() ? " none" : dropped);
}

char const* CastResultName(uint32 r)
{
    switch (r)
    {
        case SPELL_FAILED_AFFECTING_COMBAT: return "AFFECTING_COMBAT";
        case SPELL_FAILED_BAD_TARGETS: return "BAD_TARGETS";
        case SPELL_FAILED_CASTER_AURASTATE: return "CASTER_AURASTATE";
        case SPELL_FAILED_EQUIPPED_ITEM: return "EQUIPPED_ITEM";
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS: return "EQUIPPED_ITEM_CLASS";
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND: return "EQUIPPED_ITEM_CLASS_MAINHAND";
        case SPELL_FAILED_LINE_OF_SIGHT: return "LINE_OF_SIGHT";
        case SPELL_FAILED_MOVING: return "MOVING";
        case SPELL_FAILED_NEED_AMMO: return "NEED_AMMO";
        case SPELL_FAILED_NO_AMMO: return "NO_AMMO";
        case SPELL_FAILED_NOT_BEHIND: return "NOT_BEHIND";
        case SPELL_FAILED_NOT_INFRONT: return "NOT_INFRONT";
        case SPELL_FAILED_UNIT_NOT_INFRONT: return "UNIT_NOT_INFRONT";
        case SPELL_FAILED_NOT_READY: return "NOT_READY";
        case SPELL_FAILED_NOT_STANDING: return "NOT_STANDING";
        case SPELL_FAILED_NO_COMBO_POINTS: return "NO_COMBO_POINTS";
        case SPELL_FAILED_NO_POWER: return "NO_POWER";
        case SPELL_FAILED_ONLY_SHAPESHIFT: return "ONLY_SHAPESHIFT";
        case SPELL_FAILED_NOT_SHAPESHIFT: return "NOT_SHAPESHIFT";
        case SPELL_FAILED_ONLY_STEALTHED: return "ONLY_STEALTHED";
        case SPELL_FAILED_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case SPELL_FAILED_SPELL_IN_PROGRESS: return "SPELL_IN_PROGRESS";
        case SPELL_FAILED_SPELL_UNAVAILABLE: return "SPELL_UNAVAILABLE";
        case SPELL_FAILED_STUNNED: return "STUNNED";
        case SPELL_FAILED_SILENCED: return "SILENCED";
        case SPELL_FAILED_TARGET_AURASTATE: return "TARGET_AURASTATE";
        case SPELL_FAILED_TOO_CLOSE: return "TOO_CLOSE";
        case SPELL_FAILED_INTERRUPTED: return "INTERRUPTED";
        case SPELL_FAILED_UNKNOWN: return "UNKNOWN";
        default: return "OTHER";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// per-bot state
// ---------------------------------------------------------------------------------------------------------------------
struct Resolved
{
    SpellDef const* Def = nullptr;
    uint32 Id = 0;               // highest known rank
    SpellInfo const* Info = nullptr;
    float MaxRange = 0.0f;
    float MinRange = 0.0f;
    bool MeleeRange = false;     // ability that needs melee range
};

// fight scope: reset as a whole when a Combat engine period starts
struct FightData
{
    bool InFight = false;
    uint32 StartMs = 0;
    ObjectGuid Target;
    uint32 TargetSinceMs = 0;
    bool InRangeSeen = false;
    bool Chasing = false;
    ObjectGuid ChaseGuid;
    bool ChaseRanged = false;
    float ChaseRange = 0.0f;
    bool MeleeFallback = false;      // a caster/hunter that cannot cast any more fights in melee
    bool Fleeing = false;
    bool FleeGaveUp = false;
    bool FleeNoPath = false;
    uint32 FleeStartMs = 0;
    ObjectGuid Ignored;              // unreachable target given up
    uint32 IgnoredUntilMs = 0;
    uint32 Picked = 0, TargetsDead = 0, Casts = 0, CastFails = 0, Heals = 0;
    uint32 LastHealMs = 0;
    uint32 LastBuffMs = 0;           // self buffs are not recast within 25 s (the aura id may differ from the spell id)
    uint32 NoPowerSkips = 0;
    bool NoPowerLogged = false;
    bool NoTargetLogged = false;
    struct CastCount { uint32 Id; uint32 N; };
    std::vector<CastCount> CastLog;
    struct FailKey { uint32 Spell; uint32 Result; };
    std::vector<FailKey> FailLogged; // dedupe of cast failure logs within the fight
};

class BotCombatCtx : public UntypedValue, public FightData
{
public:
    explicit BotCombatCtx(BotAI* ai) : UntypedValue(ai, "combat_ctx", 0) { }

    uint32 Seq = 0;
    // spell resolution (valid for ResolvedLevel)
    std::vector<Resolved> Spells;
    uint8 ResolvedLevel = 0;
    Role BotRole = Role::Melee;
    bool RangedBroken = false;       // no ammo / no ranged weapon: never worth chasing at range
    uint32 RangedBrokenLevel = 0;

    void OnStateEnter() override;
    void Begin(Player* bot);
    void End(Player* bot);
    void Resolve(Player* bot);
    void CountCast(uint32 id)
    {
        for (CastCount& c : CastLog)
            if (c.Id == id) { ++c.N; return; }
        if (CastLog.size() < 8)
            CastLog.push_back({ id, 1 });
    }
    bool FailSeen(uint32 spell, uint32 result)
    {
        for (FailKey const& k : FailLogged)
            if (k.Spell == spell && k.Result == result)
                return true;
        if (FailLogged.size() < 12)
            FailLogged.push_back({ spell, result });
        return false;
    }
    Resolved const* Find(Kind kind) const
    {
        for (Resolved const& r : Spells)
            if (r.Def->Type == kind)
                return &r;
        return nullptr;
    }

protected:
    // never recalculated through Get(); the value is used as a plain container
};

BotCombatCtx* Ctx(BotAI* ai)
{
    return static_cast<BotCombatCtx*>(ai->GetValueRaw("combat_ctx"));
}

// the context with the fight started and the spell list current
BotCombatCtx* FightCtx(BotAI* ai, Player* bot)
{
    BotCombatCtx* ctx = Ctx(ai);
    if (!ctx->InFight)
        ctx->Begin(bot);
    else if (ctx->ResolvedLevel != bot->GetLevel())
        ctx->Resolve(bot);
    return ctx;
}

void BotCombatCtx::Resolve(Player* bot)
{
    Spells.clear();
    ResolvedLevel = bot->GetLevel();
    BotRole = RoleOf(bot->GetClass());
    for (size_t i = 0; i < std::size(SPELLS); ++i)
    {
        SpellDef const& def = SPELLS[i];
        if (!g_tableOk[i] || def.Class != bot->GetClass() || !bot->HasSpell(def.Root))
            continue;
        uint32 id = def.Root;
        for (uint32 guard = 0; guard < 16; ++guard)
        {
            uint32 const next = sSpellMgr->GetNextSpellInChain(id);
            if (!next || !bot->HasSpell(next))
                break;
            id = next;
        }
        SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!si)
            continue;
        Resolved r;
        r.Def = &def;
        r.Id = id;
        r.Info = si;
        r.MaxRange = si->GetMaxRange(false, bot);
        r.MinRange = si->GetMinRange(false);
        r.MeleeRange = def.Type != Kind::SelfBuff && def.Type != Kind::Heal && r.MaxRange <= 6.0f;
        Spells.push_back(r);
    }
}

void BotCombatCtx::Begin(Player* bot)
{
    ++Seq;
    static_cast<FightData&>(*this) = FightData();
    if (RangedBrokenLevel != bot->GetLevel())
        RangedBroken = false;
    InFight = true;
    StartMs = GetAI()->GetNowMs();
    Resolve(bot);
}

// Removes the chase generator and the victim; called on every way out of the fight.
void StopChase(Player* bot, BotCombatCtx* ctx)
{
    if (ctx->Chasing || bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        bot->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);
    ctx->Chasing = false;
    ctx->ChaseGuid = ObjectGuid::Empty;
}

void BotCombatCtx::End(Player* bot)
{
    BotAI* ai = GetAI();
    if (bot && bot->IsInWorld())
    {
        StopChase(bot, this);
        if (bot->GetVictim())
            bot->AttackStop();
        std::string casts;
        for (CastCount const& c : CastLog)
        {
            SpellInfo const* si = sSpellMgr->GetSpellInfo(c.Id, DIFFICULTY_NONE);
            casts += StringFormat(R"({}"{}":{})", casts.empty() ? "" : ",", Json(SpellNameOf(si)), c.N);
        }
        ai->EmitEvent(bot, "decision", BOTLOG_INFO, "COMBAT_SUMMARY",
            StringFormat("combat strategy: {} target(s), {} cast(s), {} failed", Picked, Casts, CastFails),
            StringFormat(R"({{"fight_seq":{},"role":"{}","seconds":{},"targets_picked":{},"targets_dead":{},"casts":{{{}}},"cast_failed":{},"heals":{},"no_power_skips":{},"melee_fallback":{},"fled":{},"fled_gave_up":{},"ranged_broken":{},"state_now":"{}"}})",
                Seq, RoleName(BotRole), (ai->GetNowMs() - StartMs) / 1000, Picked, TargetsDead, casts, CastFails, Heals, NoPowerSkips,
                MeleeFallback ? "true" : "false", (Fleeing || FleeGaveUp) ? "true" : "false", FleeGaveUp ? "true" : "false", RangedBroken ? "true" : "false",
                BotStateName(ai->GetState())));
    }
    InFight = false;
    Fleeing = false;
}

// Every engine switch calls this on all cached values. Leaving Combat ends the fight: chase generator removed (it would
// keep stopping every later BotMotion path), summary logged.
void BotCombatCtx::OnStateEnter()
{
    if (InFight && GetAI()->GetState() != BotState::Combat)
        End(GetAI()->GetTickBot());
}

// ---------------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------------
bool IsElite(Creature const* c)
{
    CreatureClassifications const cl = c->GetCreatureClassification();
    return cl == CreatureClassifications::Elite || cl == CreatureClassifications::RareElite;
}

// Effective level difference of a mob (elites count higher). World bosses are always too strong.
struct Threat { int32 Diff; bool Elite; bool Boss; bool TooStrong; };

Threat Assess(Player* bot, Unit* mob)
{
    Threat t{ int32(mob->GetLevel()) - int32(bot->GetLevel()), false, false, false };
    if (Creature* c = mob->ToCreature())
    {
        t.Elite = IsElite(c);
        t.Boss = c->isWorldBoss();
        if (t.Elite)
            t.Diff += Cfg().EliteLevelBonus;
    }
    t.TooStrong = t.Boss || t.Diff > Cfg().FleeLevelDiff;
    return t;
}

bool CanFight(Player* bot, Unit* u)
{
    return u && u->IsInWorld() && u->IsAlive() && bot->IsValidAttackTarget(u);
}

bool CastingNow(Player* bot)
{
    return bot->IsNonMeleeSpellCast(false, false, true);
}

uint32 CostOf(Player* bot, SpellInfo const* si)
{
    uint32 cost = 0;
    Powers const pt = bot->GetPowerType();
    for (SpellPowerCost const& p : si->CalcPowerCost(bot, si->GetSchoolMask()))
        if (p.Power == pt && p.Amount > 0)
            cost += uint32(p.Amount);
    return cost;
}

bool Affordable(Player* bot, SpellInfo const* si)
{
    return bot->GetPower(bot->GetPowerType()) >= CostOf(bot, si);
}

// Decision log helper (the fight id is added by the plumber's combat events, the seq ties our rows to the summary).
void LogDecision(BotAI* ai, Player* bot, BotCombatCtx* ctx, char const* reason, std::string summary, std::string extra, uint8 sev = BOTLOG_INFO)
{
    ai->EmitEvent(bot, "decision", sev, reason, std::move(summary),
        StringFormat(R"({{"kind":"combat","fight_seq":{}{}{}}})", ctx->Seq, extra.empty() ? "" : ",", extra));
}

std::string MobJson(Player* bot, Unit* mob, Threat const& t)
{
    std::string s = StringFormat(R"("target":{{"guid":"{}","entry":{},"name":"{}","level":{},"lvl_diff":{},"eff_diff":{},"elite":{},"boss":{},"dist":{:.1f},"hp_pct":{:.0f}}})",
        mob->GetGUID().ToString(), mob->GetEntry(), Json(mob->GetName()), uint32(mob->GetLevel()), int32(mob->GetLevel()) - int32(bot->GetLevel()),
        t.Diff, t.Elite ? "true" : "false", t.Boss ? "true" : "false", bot->GetDistance(mob), mob->GetHealthPct());
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// triggers
// ---------------------------------------------------------------------------------------------------------------------
class InCombatTrigger : public Trigger
{
public:
    explicit InCombatTrigger(BotAI* ai) : Trigger(ai, "combat_engaged", 0) { }
    bool IsActive() override { return GetAI()->GetState() == BotState::Combat; }
};

class NeedHealTrigger : public Trigger
{
public:
    explicit NeedHealTrigger(BotAI* ai) : Trigger(ai, "combat_need_heal", 0) { }
    bool IsActive() override
    {
        Player* bot = GetBot();
        return bot && GetAI()->GetState() == BotState::Combat && bot->GetHealthPct() < float(Cfg().HealBelowPct);
    }
};

class FleeingTrigger : public Trigger
{
public:
    explicit FleeingTrigger(BotAI* ai) : Trigger(ai, "combat_fleeing", 0) { }
    bool IsActive() override
    {
        BotCombatCtx* ctx = static_cast<BotCombatCtx*>(GetAI()->GetValueRaw("combat_ctx"));
        return ctx && ctx->InFight && ctx->Fleeing;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// flee
// ---------------------------------------------------------------------------------------------------------------------
// Picks a destination away from `from` that has a complete navmesh path. Returns false when every direction is blocked.
bool FindFleePoint(Player* bot, Unit* from, float& ox, float& oy, float& oz, float& dirOut)
{
    float const away = from->GetAbsoluteAngle(bot);
    static constexpr float offsets[] = { 0.0f, 0.7854f, -0.7854f, 1.5708f, -1.5708f, 2.356f, -2.356f };
    float const yards = float(Cfg().FleeYards);
    for (float off : offsets)
    {
        float const a = away + off;
        float x = bot->GetPositionX() + yards * std::cos(a);
        float y = bot->GetPositionY() + yards * std::sin(a);
        float z = bot->GetPositionZ();
        BotMotion::EnsureGrids(bot, x, y);
        bot->UpdateGroundPositionZ(x, y, z);
        BotPathInfo const pi = BotMotion::QueryPath(bot, x, y, z);
        if (pi.Valid && !pi.NoPath && !pi.Partial && !pi.GoalOffMesh && !pi.OffMesh)
        {
            ox = x; oy = y; oz = z; dirOut = a;
            return true;
        }
    }
    return false;
}

class FleeAction : public Action
{
public:
    explicit FleeAction(BotAI* ai) : Action(ai, "combat_flee", ACTION_FLAG_QUIET_LOG) { }
    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        BotCombatCtx* ctx = FightCtx(ai, bot);
        if (!ctx->Fleeing)
            return false;

        auto giveUp = [&](char const* reason, char const* why)
        {
            ctx->Fleeing = false;
            ctx->FleeGaveUp = true;
            if (ai->Motion().HasGoal() && strcmp(ai->Motion().GetTag(), "flee") == 0)
            {
                BotMotion::Halt(bot);
                ai->Motion().ClearGoal();
            }
            LogDecision(ai, bot, ctx, reason, StringFormat("stops fleeing: {}", why),
                StringFormat(R"("seconds":{},"hp_pct":{:.0f},"attackers":{})", (ai->GetNowMs() - ctx->FleeStartMs) / 1000, bot->GetHealthPct(), bot->getAttackers().size()), BOTLOG_WARN);
        };

        if (ai->GetNowMs() - ctx->FleeStartMs >= Cfg().FleeMaxSec * 1000)
        {
            giveUp("FLEE_GAVE_UP", "the mob is still on us, fights back");
            return false;
        }
        if (!ai->Motion().HasGoal())
        {
            giveUp("FLEE_ENDED", "reached the flee destination while still in combat");
            return false;
        }
        BotMotion::Result const r = ai->Motion().Step(ai, bot);
        if (r == BotMotion::Result::Failed || r == BotMotion::Result::Arrived)
        {
            giveUp(r == BotMotion::Result::Failed ? "FLEE_BLOCKED" : "FLEE_ENDED", r == BotMotion::Result::Failed ? "movement failed" : "reached the flee destination");
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// engage: target selection, attack, approach
// ---------------------------------------------------------------------------------------------------------------------
// Opponents = PvE combat references of the bot (melee attackers and casters alike).
struct Candidate { Unit* Mob; Threat T; float Dist; };

class EngageAction : public Action
{
public:
    explicit EngageAction(BotAI* ai) : Action(ai, "combat_engage", ACTION_FLAG_QUIET_LOG) { }

    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        BotCombatCtx* ctx = FightCtx(ai, bot);
        if (ctx->Fleeing)
            return false;
        uint32 const now = ai->GetNowMs();

        Unit* target = ctx->Target.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, ctx->Target);
        if (target && !CanFight(bot, target))
        {
            if (!target->IsAlive())
                ++ctx->TargetsDead;
            target = nullptr;
        }

        // approach timeout: still not in range after Bot.AI.Combat.ApproachTimeoutSec
        if (target && !ctx->InRangeSeen && now - ctx->TargetSinceMs >= Cfg().ApproachSec * 1000)
        {
            LogDecision(ai, bot, ctx, "APPROACH_TIMEOUT", StringFormat("cannot reach {} within {} s, giving it up", target->GetName(), Cfg().ApproachSec),
                StringFormat(R"({},"pos_mob":{{"x":{:.1f},"y":{:.1f},"z":{:.1f}}},"moving":{},"los":{})", MobJson(bot, target, Assess(bot, target)),
                    target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), bot->isMoving() ? "true" : "false", bot->IsWithinLOSInMap(target) ? "true" : "false"), BOTLOG_WARN);
            ctx->Ignored = target->GetGUID();
            ctx->IgnoredUntilMs = now + 20000;
            target = nullptr;
        }

        bool changed = false;
        if (!target)
        {
            if (!ctx->Target.IsEmpty())
            {
                StopChase(bot, ctx);
                if (bot->GetVictim())
                    bot->AttackStop();
                ctx->Target = ObjectGuid::Empty;
                changed = true;
            }
            if (!PickTarget(bot, ai, ctx, target))
                return changed; // nothing to fight (or the flee started)
            changed = true;
            if (ctx->Fleeing)
                return true;
        }

        // attack
        if (bot->GetVictim() != target)
        {
            if (!bot->Attack(target, true))
                return changed;
            changed = true;
        }

        // approach: melee, or spell range for casters/hunters while they can still cast
        bool wantRanged = false;
        float range = 0.0f;
        if (ctx->BotRole != Role::Melee && !ctx->MeleeFallback && !ctx->RangedBroken)
        {
            if (Resolved const* primary = RangedPrimary(ctx))
            {
                if (!Affordable(bot, primary->Info) && ctx->BotRole == Role::Caster)
                {
                    ctx->MeleeFallback = true;
                    LogDecision(ai, bot, ctx, "RANGED_TO_MELEE", "out of power, fights in melee",
                        StringFormat(R"("power":{},"cost":{},"spell":"{}")", bot->GetPower(bot->GetPowerType()), CostOf(bot, primary->Info), Json(SpellNameOf(primary->Info))));
                }
                else
                {
                    wantRanged = true;
                    range = std::max(8.0f, primary->MaxRange * float(Cfg().CasterRangePct) / 100.0f);
                }
            }
            else if (ctx->BotRole == Role::Caster)
                ctx->MeleeFallback = true; // no ranged spell known at all
        }

        bool const chaseOk = bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE;
        if (!ctx->Chasing || !chaseOk || ctx->ChaseGuid != target->GetGUID() || ctx->ChaseRanged != wantRanged || (wantRanged && std::fabs(ctx->ChaseRange - range) > 0.5f))
        {
            if (chaseOk)
                bot->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);
            if (wantRanged)
                bot->GetMotionMaster()->MoveChase(target, ChaseRange(range));
            else
                bot->GetMotionMaster()->MoveChase(target);
            ctx->Chasing = true;
            ctx->ChaseGuid = target->GetGUID();
            ctx->ChaseRanged = wantRanged;
            ctx->ChaseRange = range;
            changed = true;
        }

        // in range for the first time (approach bookkeeping)
        if (!ctx->InRangeSeen)
        {
            float const d = bot->GetDistance(target);
            if (wantRanged ? d <= range + 3.0f : bot->IsWithinMeleeRange(target))
                ctx->InRangeSeen = true;
        }
        return changed;
    }

private:
    static Resolved const* RangedPrimary(BotCombatCtx* ctx)
    {
        for (Resolved const& r : ctx->Spells)
            if (!r.MeleeRange && (r.Def->Type == Kind::Direct || r.Def->Type == Kind::AutoShot))
                return &r;
        return nullptr;
    }

    // Chooses the nearest opponent that is not too strong. When every opponent is too strong: flee (or fight when fleeing is
    // off, was given up, or has no path). Returns true when `out` is a target to fight.
    bool PickTarget(Player* bot, BotAI* ai, BotCombatCtx* ctx, Unit*& out)
    {
        uint32 const now = ai->GetNowMs();
        std::vector<Candidate> cands;
        for (auto const& [guid, ref] : bot->GetCombatManager().GetPvECombatRefs())
        {
            Unit* mob = ref->GetOther(bot);
            if (!CanFight(bot, mob))
                continue;
            if (mob->GetGUID() == ctx->Ignored && now < ctx->IgnoredUntilMs)
                continue;
            cands.push_back({ mob, Assess(bot, mob), bot->GetDistance(mob) });
            if (cands.size() >= 12)
                break;
        }
        if (cands.empty())
        {
            if (!ctx->NoTargetLogged && now - ctx->StartMs >= 2000)
            {
                ctx->NoTargetLogged = true;
                std::string refs;
                uint32 n = 0;
                for (auto const& [guid, ref] : bot->GetCombatManager().GetPvECombatRefs())
                {
                    Unit* mob = ref->GetOther(bot);
                    if (n++ < 4 && mob)
                        refs += StringFormat(R"({}{{"entry":{},"level":{},"alive":{},"valid_attack":{},"friendly":{},"inworld":{},"see":{},"phase":{},"samemap":{},"dist":{:.0f}}})", refs.empty() ? "" : ",", mob->GetEntry(), uint32(mob->GetLevel()),
                            mob->IsAlive() ? "true" : "false", bot->IsValidAttackTarget(mob) ? "true" : "false", bot->IsFriendlyTo(mob) ? "true" : "false", mob->IsInWorld() ? "true" : "false", bot->CanSeeOrDetect(mob) ? "true" : "false", bot->InSamePhase(mob) ? "true" : "false", bot->GetMap() == mob->GetMap() ? "true" : "false", bot->GetDistance(mob));
                }
                LogDecision(ai, bot, ctx, "NO_TARGET", "in combat but no valid opponent to attack",
                    StringFormat(R"("pve_refs":{},"pvp_refs":{},"attackers":{},"refs":[{}])", bot->GetCombatManager().GetPvECombatRefs().size(), bot->GetCombatManager().GetPvPCombatRefs().size(), bot->getAttackers().size(), refs), BOTLOG_WARN);
            }
            return false;
        }

        auto nearest = [&](bool strongOk) -> Candidate*
        {
            Candidate* best = nullptr;
            for (Candidate& c : cands)
                if ((strongOk || !c.T.TooStrong) && (!best || c.Dist < best->Dist))
                    best = &c;
            return best;
        };

        Candidate* pick = nearest(false);
        std::string considered;
        for (size_t i = 0; i < cands.size() && i < 6; ++i)
            considered += StringFormat(R"({}{{"entry":{},"level":{},"eff_diff":{},"dist":{:.0f},"too_strong":{}}})", i ? "," : "", cands[i].Mob->GetEntry(),
                uint32(cands[i].Mob->GetLevel()), cands[i].T.Diff, cands[i].Dist, cands[i].T.TooStrong ? "true" : "false");

        if (!pick)
        {
            // everything on us is too strong
            Candidate* worst = nearest(true);
            char const* why = "NO_FLEE_PATH";
            if (!Cfg().FleeEnabled)
                why = "FLEE_DISABLED";
            else if (ctx->FleeGaveUp)
                why = "FLEE_ALREADY_TRIED";
            else
            {
                float fx, fy, fz, dir;
                if (FindFleePoint(bot, worst->Mob, fx, fy, fz, dir))
                {
                    StopChase(bot, ctx);
                    if (bot->GetVictim())
                        bot->AttackStop();
                    ai->Motion().SetGoal(bot->GetMapId(), fx, fy, fz, 3.0f, "flee");
                    ctx->Fleeing = true;
                    ctx->FleeStartMs = now;
                    ctx->Target = ObjectGuid::Empty;
                    LogDecision(ai, bot, ctx, "FLEE_LEVEL_DIFF", StringFormat("flees from {} L{} (eff. +{} levels, limit +{})", worst->Mob->GetName(), uint32(worst->Mob->GetLevel()), worst->T.Diff, Cfg().FleeLevelDiff),
                        StringFormat(R"({},"limit":{},"destination":{{"x":{:.1f},"y":{:.1f},"z":{:.1f}}},"considered":[{}])", MobJson(bot, worst->Mob, worst->T), Cfg().FleeLevelDiff, fx, fy, fz, considered), BOTLOG_WARN);
                    return false;
                }
            }
            // fighting back is the only option left
            pick = worst;
            LogDecision(ai, bot, ctx, "FIGHT_TOO_STRONG", StringFormat("fights {} L{} despite eff. +{} levels ({})", pick->Mob->GetName(), uint32(pick->Mob->GetLevel()), pick->T.Diff, why),
                StringFormat(R"({},"limit":{},"why":"{}","considered":[{}])", MobJson(bot, pick->Mob, pick->T), Cfg().FleeLevelDiff, why, considered), BOTLOG_WARN);
        }
        else
        {
            LogDecision(ai, bot, ctx, "TARGET_PICKED", StringFormat("targets {} L{}", pick->Mob->GetName(), uint32(pick->Mob->GetLevel())),
                StringFormat(R"({},"opponents":{},"considered":[{}])", MobJson(bot, pick->Mob, pick->T), cands.size(), considered));
        }

        out = pick->Mob;
        ctx->Target = out->GetGUID();
        ctx->TargetSinceMs = now;
        ctx->InRangeSeen = false;
        ++ctx->Picked;
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// casting
// ---------------------------------------------------------------------------------------------------------------------
// Casts `r` and does the failure bookkeeping (counted, logged once per spell+result and fight). Returns true when cast started.
bool TryCast(BotAI* ai, Player* bot, BotCombatCtx* ctx, Resolved const& r, Unit* target)
{
    if (target && !bot->HasInArc(float(M_PI), target))
        bot->SetInFront(target);
    SpellCastResult const res = bot->CastSpell(target ? target : bot, r.Id, CastSpellExtraArgs(TRIGGERED_NONE));
    if (res == SPELL_CAST_OK)
    {
        ++ctx->Casts;
        ctx->CountCast(r.Id);
        return true;
    }

    ++ctx->CastFails;
    if (res == SPELL_FAILED_NO_AMMO || res == SPELL_FAILED_NEED_AMMO || res == SPELL_FAILED_EQUIPPED_ITEM || res == SPELL_FAILED_EQUIPPED_ITEM_CLASS ||
        res == SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND)
    {
        if (r.Def->Type == Kind::AutoShot)
        {
            ctx->RangedBroken = true;
            ctx->RangedBrokenLevel = bot->GetLevel();
            ctx->MeleeFallback = true;
        }
    }
    // not-ready/range/moving results are the normal rhythm of a fight; the rest is worth a row (once per fight)
    bool const routine = res == SPELL_FAILED_NOT_READY || res == SPELL_FAILED_OUT_OF_RANGE || res == SPELL_FAILED_MOVING || res == SPELL_FAILED_SPELL_IN_PROGRESS ||
        res == SPELL_FAILED_NO_POWER || res == SPELL_FAILED_UNIT_NOT_INFRONT || res == SPELL_FAILED_NOT_INFRONT || res == SPELL_FAILED_INTERRUPTED;
    if (!ctx->FailSeen(r.Id, uint32(res)) && !(routine && res != SPELL_FAILED_OUT_OF_RANGE))
        LogDecision(ai, bot, ctx, "CAST_FAILED", StringFormat("{} failed: {}", SpellNameOf(r.Info), CastResultName(uint32(res))),
            StringFormat(R"("spell":{},"spell_name":"{}","result":{},"result_name":"{}","dist":{:.1f},"power":{},"target":{},"routine":{})", r.Id, Json(SpellNameOf(r.Info)),
                uint32(res), CastResultName(uint32(res)), target ? bot->GetDistance(target) : 0.0f, bot->GetPower(bot->GetPowerType()), target ? target->GetEntry() : 0, routine ? "true" : "false"),
            routine ? BOTLOG_INFO : BOTLOG_WARN);
    return false;
}

bool Ready(Player* bot, Resolved const& r)
{
    return !bot->GetSpellHistory()->HasCooldown(r.Info) && !bot->GetSpellHistory()->HasGlobalCooldown(r.Info);
}

class CastAction : public Action
{
public:
    explicit CastAction(BotAI* ai) : Action(ai, "combat_cast", ACTION_FLAG_QUIET_LOG) { }
    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        BotCombatCtx* ctx = FightCtx(ai, bot);
        if (ctx->Fleeing || ctx->Target.IsEmpty() || ctx->Spells.empty() || CastingNow(bot) || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
            return false;
        Unit* target = ObjectAccessor::GetUnit(*bot, ctx->Target);
        if (!CanFight(bot, target))
            return false;

        float const dist = bot->GetDistance(target);
        bool const inMelee = bot->IsWithinMeleeRange(target);
        bool const moving = bot->isMoving();
        bool skippedForPower = false;

        for (Resolved const& r : ctx->Spells)
        {
            Kind const kind = r.Def->Type;
            if (kind == Kind::Heal)
                continue;
            if (kind == Kind::SelfBuff)
            {
                if (bot->HasAura(r.Id) || !Ready(bot, r) || !Affordable(bot, r.Info) || (ctx->LastBuffMs && ai->GetNowMs() - ctx->LastBuffMs < 25000))
                    continue;
                if (TryCast(ai, bot, ctx, r, nullptr))
                {
                    ctx->LastBuffMs = ai->GetNowMs();
                    return true;
                }
                continue;
            }
            if (r.Def->ReqAura && !bot->HasAura(r.Def->ReqAura))
                continue;
            if (kind == Kind::Finisher && bot->GetPower(POWER_COMBO_POINTS) < r.Def->MinCombo)
                continue;
            if (kind == Kind::Dot && target->HasAura(r.Id, bot->GetGUID()))
                continue;
            if (kind == Kind::AutoShot)
            {
                if (ctx->RangedBroken || bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) || inMelee || dist < r.MinRange || dist > r.MaxRange - 1.0f)
                    continue;
            }
            else if (r.MeleeRange)
            {
                if (!inMelee)
                    continue;
            }
            else
            {
                if (dist > r.MaxRange - 1.0f || dist < r.MinRange)
                    continue;
                if (moving && r.Info->CastTimeEntry && r.Info->CalcCastTime() > 0)
                    continue; // wait until the chase stops (casting while running fails with MOVING)
            }
            if (!Ready(bot, r))
                continue;
            if (!Affordable(bot, r.Info))
            {
                skippedForPower = true;
                continue;
            }
            if (TryCast(ai, bot, ctx, r, target))
                return true;
        }

        if (skippedForPower)
        {
            ++ctx->NoPowerSkips;
            if (!ctx->NoPowerLogged)
            {
                ctx->NoPowerLogged = true;
                LogDecision(ai, bot, ctx, "CAST_NO_POWER", "not enough power for the spell rotation",
                    StringFormat(R"("power_type":{},"power":{},"max":{})", uint32(bot->GetPowerType()), bot->GetPower(bot->GetPowerType()), bot->GetMaxPower(bot->GetPowerType())));
            }
        }
        return false;
    }
};

class HealAction : public Action
{
public:
    explicit HealAction(BotAI* ai) : Action(ai, "combat_heal", ACTION_FLAG_NONE) { }
    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        BotCombatCtx* ctx = FightCtx(ai, bot);
        if (CastingNow(bot) || ai->GetNowMs() - ctx->LastHealMs < 3000 && ctx->Heals)
            return false;
        Resolved const* heal = ctx->Find(Kind::Heal);
        if (!heal || !Ready(bot, *heal) || !Affordable(bot, heal->Info))
            return false;
        float const hpBefore = bot->GetHealthPct();
        if (!TryCast(ai, bot, ctx, *heal, bot))
            return false;
        ctx->LastHealMs = ai->GetNowMs();
        ++ctx->Heals;
        SetResult("SELF_HEAL", StringFormat("casts {} at {:.0f}% health", SpellNameOf(heal->Info), hpBefore),
            StringFormat(R"({{"spell":{},"spell_name":"{}","hp_pct":{:.0f},"threshold":{},"fight_seq":{}}})", heal->Id, Json(SpellNameOf(heal->Info)), hpBefore, Cfg().HealBelowPct, ctx->Seq));
        return true;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// console aid and registration
// ---------------------------------------------------------------------------------------------------------------------
std::vector<std::string> BotCombatDescribeSpells(Player* bot)
{
    std::vector<std::string> lines;
    std::string known;
    uint32 n = 0;
    for (auto const& [id, spell] : bot->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || !spell.active)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        known += StringFormat("{}{}:{}", known.empty() ? "" : ", ", id, si ? SpellNameOf(si) : std::string("?"));
        if (++n % 8 == 0)
        {
            lines.push_back(known);
            known.clear();
        }
    }
    if (!known.empty())
        lines.push_back(known);
    lines.insert(lines.begin(), StringFormat("{} L{} class {}: {} active spells known:", bot->GetName(), uint32(bot->GetLevel()), uint32(bot->GetClass()), n));

    // what the strategy resolves (a throwaway context, the bot's own is untouched)
    BotCombatCtx probe(nullptr);
    probe.Resolve(bot);
    lines.push_back(StringFormat("combat role: {}", RoleName(probe.BotRole)));
    for (size_t i = 0; i < std::size(SPELLS); ++i)
    {
        SpellDef const& def = SPELLS[i];
        if (def.Class != bot->GetClass())
            continue;
        Resolved const* hit = nullptr;
        for (Resolved const& r : probe.Spells)
            if (r.Def == &def)
                hit = &r;
        if (hit)
            lines.push_back(StringFormat("  {} root {} -> rank spell {} (range {:.0f}-{:.0f}, power cost {}, kind {})", def.Name, def.Root, hit->Id, hit->MinRange, hit->MaxRange, CostOf(bot, hit->Info), uint32(def.Type)));
        else
            lines.push_back(StringFormat("  {} root {}: {}", def.Name, def.Root, g_tableOk[i] ? "not known" : "dropped at startup (id/name mismatch)"));
    }
    return lines;
}

void RegisterCombatBotObjects(BotRegistry& r)
{
    ValidateTable();

    r.AddValue("combat_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotCombatCtx>(ai); });
    r.AddTrigger("combat_engaged", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<InCombatTrigger>(ai); });
    r.AddTrigger("combat_need_heal", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<NeedHealTrigger>(ai); });
    r.AddTrigger("combat_fleeing", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<FleeingTrigger>(ai); });
    r.AddAction("combat_flee", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<FleeAction>(ai); });
    r.AddAction("combat_heal", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<HealAction>(ai); });
    r.AddAction("combat_engage", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<EngageAction>(ai); });
    r.AddAction("combat_cast", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<CastAction>(ai); });

    class CombatStrategy : public Strategy
    {
    public:
        CombatStrategy() : Strategy("combat") { }
        void InitTriggers(std::vector<BotTriggerNode>& t) override
        {
            t.push_back({ "combat_fleeing", { { "combat_flee", BotRelevance::Emergency } } });
            t.push_back({ "combat_need_heal", { { "combat_heal", BotRelevance::High + 40.0f } } });
            t.push_back({ "combat_engaged", { { "combat_engage", BotRelevance::Move }, { "combat_cast", BotRelevance::Normal } } });
        }
    };
    r.AddStrategy("combat", BotStateBit(BotState::Combat), []() -> std::unique_ptr<Strategy> { return std::make_unique<CombatStrategy>(); });
}
