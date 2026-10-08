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

// Training dummy mode, see BotDummy.h. The run itself is a value (dummy_ctx) of the bot; the "dummy" strategy ticks it once a second:
// keeps the bot engaged with the dummy, refills its power, and ends the run on time, on death or when the dummy is gone. The fight is
// the normal combat strategy: the dummy is in the bot's combat references, so target choice, approach, cast and engage work unchanged.

#include "BotDummy.h"
#include "BotAI.h"
#include "BotDummyStats.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>

namespace BotDummy
{
namespace
{
using Trinity::StringFormat;

Config s_cfg;
std::once_flag s_cfgOnce;

class DummyCtx : public UntypedValue
{
public:
    explicit DummyCtx(BotAI* ai) : UntypedValue(ai, "dummy_ctx", 0) { }

    bool Active = false;
    ObjectGuid Dummy;
    uint32 EndMs = 0;
    uint32 StartMs = 0;
    uint32 Reengaged = 0;
    BotDummyStats::Run Stats;
};

DummyCtx* Ctx(BotAI* ai) { return static_cast<DummyCtx*>(ai->GetValueRaw("dummy_ctx")); }

Creature* FindDummy(Player* bot, DummyCtx const& c)
{
    return ObjectAccessor::GetCreature(*bot, c.Dummy);
}

std::string SpellLabel(uint32 id)
{
    if (!id)
        return "melee";
    SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
    return si && si->SpellName ? std::string((*si->SpellName)[DEFAULT_LOCALE]) : StringFormat("spell {}", id);
}

// Names go into JSON: keep quotes and backslashes out.
std::string JsonSafe(std::string s)
{
    std::replace_if(s.begin(), s.end(), [](char ch) { return ch == '"' || ch == '\\'; }, '\'');
    return s;
}

void EngageDummy(Player* bot, Creature* dummy)
{
    bot->SetInCombatWith(dummy);
    dummy->SetInCombatWith(bot);
    bot->SetTarget(dummy->GetGUID());
}

void Finish(BotAI* ai, Player* bot, DummyCtx& c, char const* why)
{
    uint32 const now = ai->GetNowMs();
    c.Stats.End(now);
    c.Active = false;

    std::vector<BotDummyStats::SpellRow> rows = c.Stats.Rows();
    std::sort(rows.begin(), rows.end(), [](auto const& a, auto const& b) { return a.Damage > b.Damage; });
    std::string spells;
    for (BotDummyStats::SpellRow const& r : rows)
        spells += StringFormat(R"({}{{"spell":{},"name":"{}","hits":{},"damage":{},"pct":{:.1f}}})", spells.empty() ? "" : ",", r.SpellId, JsonSafe(SpellLabel(r.SpellId)), r.Hits, r.Damage,
            BotDummyStats::Percent(r.Damage, c.Stats.TotalDamage()));

    Creature* dummy = FindDummy(bot, c);
    uint32 const dummyLevel = dummy ? dummy->GetLevel() : 0;
    ai->EmitEvent(bot, "decision", BOTLOG_INFO, "DUMMY_SUMMARY",
        StringFormat("dummy run ended ({}): {} damage in {} s, {:.1f} dps", why, c.Stats.TotalDamage(), c.Stats.DurationMs() / 1000, c.Stats.Dps()),
        StringFormat(R"({{"end":"{}","level":{},"dummy_level":{},"class":{},"duration_ms":{},"damage":{},"hits":{},"dps":{:.2f},"first_hit_ms":{},"longest_gap_ms":{},"reengaged":{},"rotation":{},"spells":[{}]}})",
            why, uint32(bot->GetLevel()), dummyLevel, uint32(bot->GetClass()), c.Stats.DurationMs(), c.Stats.TotalDamage(), c.Stats.TotalHits(), c.Stats.Dps(), c.Stats.FirstHitMs(),
            c.Stats.LongestGapMs(), c.Reengaged, sConfigMgr->GetBoolDefault("Bot.AI.Rotation.Enabled", false) ? "true" : "false", spells));

    if (dummy)
        dummy->DespawnOrUnsummon();
    c.Dummy = ObjectGuid::Empty;
    bot->AttackStop();
    bot->CombatStop();
    ai->RemoveStrategy(bot, "dummy", "dummy");
}

class DummyTickAction : public Action
{
public:
    explicit DummyTickAction(BotAI* ai) : Action(ai, "dummy_tick", ACTION_FLAG_QUIET_LOG) { }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        DummyCtx* c = Ctx(ai);
        if (!c || !c->Active)
            return false;
        uint32 const now = ai->GetNowMs();
        Creature* dummy = FindDummy(bot, *c);
        if (!bot->IsAlive())
            Finish(ai, bot, *c, "bot_died");
        else if (!dummy || !dummy->IsAlive() || dummy->GetMap() != bot->GetMap())
            Finish(ai, bot, *c, "dummy_gone");
        else if (int32(now - c->EndMs) >= 0)
            Finish(ai, bot, *c, "done");
        else
        {
            if (Cfg().FullPower)
            {
                bot->SetFullHealth();
                bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
                bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));
            }
            // the dummy script ends combat after five seconds without damage (a bot that cannot reach it, a long cooldown gap): engage again
            auto const& refs = bot->GetCombatManager().GetPvECombatRefs();
            if (refs.find(dummy->GetGUID()) == refs.end())
            {
                EngageDummy(bot, dummy);
                ++c->Reengaged;
            }
        }
        return false;   // never takes the tick: the combat actions run in the same tick
    }
};

class DummyDueTrigger : public Trigger
{
public:
    explicit DummyDueTrigger(BotAI* ai) : Trigger(ai, "dummy_due", 1000) { }
    bool IsActive() override
    {
        DummyCtx* c = Ctx(GetAI());
        return c && c->Active;
    }
};

class DummyStrategy : public Strategy
{
public:
    DummyStrategy() : Strategy("dummy") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        t.push_back({ "dummy_due", { { "dummy_tick", BotRelevance::Move + 3.0f } } });
    }
};
} // namespace

Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Dummy.Enabled", false);
        s_cfg.Entry = uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.AI.Dummy.Entry", 9900001)));
        s_cfg.DefaultSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Dummy.DefaultSec", 60), 5, 3600));
        s_cfg.MaxSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Dummy.MaxSec", 600), 5, 3600));
        s_cfg.FullPower = sConfigMgr->GetBoolDefault("Bot.AI.Dummy.FullPower", true);
    });
    return s_cfg;
}

bool Active(BotAI* ai)
{
    DummyCtx* c = Ctx(ai);
    return c && c->Active;
}

char const* Start(BotAI* ai, Player* bot, uint32 sec)
{
    if (!Cfg().Enabled)
        return "DUMMY_OFF";
    DummyCtx* c = Ctx(ai);
    if (!c)
        return "NO_CTX";
    if (c->Active)
        return "ALREADY_RUNNING";
    if (!bot->IsAlive())
        return "DEAD";
    if (ai->GetState() != BotState::NonCombat || bot->IsInCombat())
        return "IN_COMBAT";
    if (!sObjectMgr->GetCreatureTemplate(Cfg().Entry))
        return "NO_DUMMY_TEMPLATE";
    if (!sec)
        sec = Cfg().DefaultSec;
    sec = std::min(sec, Cfg().MaxSec);

    float x, y, z;
    bot->GetClosePoint(x, y, z, bot->GetCombatReach(), 6.0f);
    TempSummon* dummy = bot->SummonCreature(Cfg().Entry, x, y, z, bot->GetOrientation() + float(M_PI), TEMPSUMMON_MANUAL_DESPAWN);
    if (!dummy)
        return "SPAWN_FAILED";
    dummy->SetLevel(bot->GetLevel());

    uint32 const now = ai->GetNowMs();
    c->Active = true;
    c->Dummy = dummy->GetGUID();
    c->StartMs = now;
    c->EndMs = now + sec * 1000;
    c->Reengaged = 0;
    c->Stats.Begin(now);
    ai->AddStrategy(bot, "dummy", "dummy");
    EngageDummy(bot, dummy);
    ai->EmitEvent(bot, "decision", BOTLOG_INFO, "DUMMY_START", StringFormat("dummy run of {} s against a level {} dummy", sec, uint32(dummy->GetLevel())),
        StringFormat(R"({{"seconds":{},"dummy_entry":{},"level":{},"class":{}}})", sec, Cfg().Entry, uint32(bot->GetLevel()), uint32(bot->GetClass())));
    return "OK";
}

char const* Stop(BotAI* ai, Player* bot)
{
    DummyCtx* c = Ctx(ai);
    if (!c || !c->Active)
        return "NOT_RUNNING";
    Finish(ai, bot, *c, "stopped");
    return "OK";
}

void OnLogout(BotAI* ai, Player* bot)
{
    DummyCtx* c = Ctx(ai);
    if (!c || !c->Active)
        return;
    if (Creature* dummy = FindDummy(bot, *c))
        dummy->DespawnOrUnsummon();
    c->Active = false;
    c->Dummy = ObjectGuid::Empty;
}

void NoteDamage(Unit* dummy, Unit* attacker, uint32 damage, SpellInfo const* spell)
{
    if (!dummy || !attacker)
        return;
    Player* p = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!p || !p->GetSession())
        return;
    BotAI* ai = p->GetSession()->GetBotAI();
    if (!ai)
        return;
    DummyCtx* c = Ctx(ai);
    if (!c || !c->Active || c->Dummy != dummy->GetGUID())
        return;
    // pet and guardian damage counts under the pet's spell ids; melee swings have no spell (id 0)
    c->Stats.NoteHit(spell ? spell->Id : 0, damage, ai->GetNowMs());
}
} // namespace BotDummy

void RegisterDummyBotObjects(BotRegistry& r)
{
    BotDummy::Cfg();
    r.AddValue("dummy_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotDummy::DummyCtx>(ai); });
    r.AddTrigger("dummy_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotDummy::DummyDueTrigger>(ai); });
    r.AddAction("dummy_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotDummy::DummyTickAction>(ai); });
    r.AddStrategy("dummy", BotStateBit(BotState::NonCombat) | BotStateBit(BotState::Combat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotDummy::DummyStrategy>(); });
}
