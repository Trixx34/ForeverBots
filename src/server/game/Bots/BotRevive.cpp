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

// Bot resurrection, see BotRevive.h.

#include "BotRevive.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "Group.h"
#include "GroupReference.h"
#include "Item.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace BotRevive
{
namespace
{
using Trinity::StringFormat;

Config s_cfg;
std::once_flag s_cfgOnce;

// Rank 1 spell ids of the class resurrections (Classic): Resurrection, Redemption, Ancestral Spirit. Druids only have Rebirth, which needs
// a reagent and is a combat spell, so they are left out.
uint32 SpellRoot(uint8 cls)
{
    switch (cls)
    {
        case CLASS_PRIEST: return 2006;
        case CLASS_PALADIN: return 7328;
        case CLASS_SHAMAN: return 2008;
        default: return 0;
    }
}

// Highest known rank of the class resurrection, 0 when the bot does not know any.
uint32 KnownSpell(Player* p)
{
    uint32 id = SpellRoot(p->GetClass());
    if (!id || !p->HasSpell(id))
        return 0;
    for (uint32 guard = 0; guard < 8; ++guard)
    {
        uint32 const next = sSpellMgr->GetNextSpellInChain(id);
        if (!next || !p->HasSpell(next))
            break;
        id = next;
    }
    return id;
}

BotAI* AiOf(Player* p)
{
    return p && p->GetSession() ? p->GetSession()->GetBotAI() : nullptr;
}

// The dead member every rezzer of the group agrees on: the lowest guid among those in the same world and not yet offered a rez.
Player* DeadMember(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    Player* best = nullptr;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* m = ref.GetSource();
        if (!m || m == bot || !m->IsInWorld() || m->IsAlive() || m->GetMap() != bot->GetMap() || m->IsResurrectRequested())
            continue;
        if (!best || m->GetGUID().GetCounter() < best->GetGUID().GetCounter())
            best = m;
    }
    return best;
}

// Everybody in the group (the bot itself included when it is able) who could raise the victim, in group order; `who` is parallel.
void Candidates(Player* reference, Player* victim, std::vector<Candidate>& out, std::vector<Player*>& who)
{
    Group* group = reference->GetGroup();
    if (!group)
        return;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* m = ref.GetSource();
        if (!m || m == victim || !m->IsInWorld() || !m->IsAlive() || m->GetMap() != victim->GetMap() || !AiOf(m))
            continue;
        uint32 const spell = KnownSpell(m);
        if (!spell)
            continue;
        Candidate c;
        c.Key = m->GetGUID().GetCounter();
        c.Dist = m->GetDistance(victim);
        uint32 const maxMana = m->GetMaxPower(POWER_MANA);
        c.ManaPct = maxMana ? int32(uint64(m->GetPower(POWER_MANA)) * 100 / maxMana) : 0;
        c.InCombat = m->IsInCombat();
        c.Busy = m->IsNonMeleeSpellCast(false) || m->HasUnitState(UNIT_STATE_CONTROLLED);
        c.Ready = !m->GetSpellHistory()->HasCooldown(spell);
        out.push_back(c);
        who.push_back(m);
    }
}

class ReviveAction : public Action
{
public:
    explicit ReviveAction(BotAI* ai) : Action(ai, "revive_member", ACTION_FLAG_MOVES | ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        uint32 const spell = KnownSpell(bot);
        if (!bot->IsAlive() || bot->IsInCombat() || !spell)
            return false;
        Player* victim = DeadMember(bot);
        if (!victim)
            return false;
        if (victim->GetGUID() != _target)
        {
            _target = victim->GetGUID();
            _attempts = 0;
            _lastCastMs = 0;
        }
        std::vector<Candidate> cands;
        std::vector<Player*> who;
        Candidates(bot, victim, cands, who);
        int32 const pick = PickResurrector(cands, Cfg());
        if (pick < 0 || who[size_t(pick)] != bot)
            return false;

        SpellInfo const* si = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
        if (!si)
            return false;
        Facts f;
        f.Dist = bot->GetDistance(victim);
        f.CastRange = si->GetMaxRange(false, bot);
        f.TargetDead = !victim->IsAlive();
        f.OfferPending = victim->IsResurrectRequested();
        f.Casting = bot->IsNonMeleeSpellCast(false);
        f.SinceCastMs = _lastCastMs ? ai->GetNowMs() - _lastCastMs : 0xFFFFFFFF;
        f.Attempts = _attempts;
        switch (PlanStep(f, Cfg()))
        {
            case Act::Approach:
                // keep one yard outside the spell range so the cast never fails on a step of drift
                ai->Motion().SetGoal(victim->GetMapId(), victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ(), std::max(3.0f, f.CastRange - 6.0f), "revive");
                SetResult("REVIVE_APPROACH", "walks to a dead group member", StringFormat(R"({{"target":{},"distance":{:.0f},"spell":{}}})", victim->GetGUID().GetCounter(), f.Dist, spell));
                return true;
            case Act::Cast:
            {
                if (ai->Motion().HasGoal() && !std::strcmp(ai->Motion().GetTag(), "revive"))
                    ai->Motion().ClearGoal();
                SpellCastResult const res = bot->CastSpell(victim, spell, CastSpellExtraArgs(TRIGGERED_NONE));
                ++_attempts;
                _lastCastMs = ai->GetNowMs();
                SetResult(res == SPELL_CAST_OK ? "REVIVE_CAST" : "REVIVE_CAST_FAILED", res == SPELL_CAST_OK ? "resurrects a group member" : "resurrection failed",
                    StringFormat(R"({{"target":{},"spell":{},"result":{},"attempt":{},"distance":{:.0f}}})", victim->GetGUID().GetCounter(), spell, uint32(res), _attempts, f.Dist));
                return res == SPELL_CAST_OK;
            }
            default:
                return false;
        }
    }
private:
    ObjectGuid _target;
    uint32 _attempts = 0;
    uint32 _lastCastMs = 0;
};

class ReviveTrigger : public Trigger
{
public:
    explicit ReviveTrigger(BotAI* ai) : Trigger(ai, "revive_due", 2000) { }
    bool IsActive() override
    {
        if (!Cfg().Enabled)
            return false;
        Player* bot = GetBot();
        return bot && bot->IsAlive() && !bot->IsInCombat() && bot->GetGroup() && DeadMember(bot);
    }
};

class ReviveStrategy : public Strategy
{
public:
    ReviveStrategy() : Strategy("revive") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        t.push_back({ "revive_due", { { "revive_member", BotRelevance::High } } });
    }
};

// A dead bot takes a resurrection offer (from a bot or from a player) a moment after it arrived.
class AcceptAction : public Action
{
public:
    explicit AcceptAction(BotAI* ai) : Action(ai, "revive_accept") { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool IsUseful() override { return GetBot()->IsResurrectRequested(); }
    bool Execute() override
    {
        Player* bot = GetBot();
        BotAI* ai = GetAI();
        if (!_seenMs)
            _seenMs = ai->GetNowMs();
        if (!AcceptOffer(ai->GetNowMs() - _seenMs, Cfg()))
            return false;
        _seenMs = 0;
        bot->ResurrectUsingRequestData();
        ai->Recover().Reset();
        SetResult("REVIVE_ACCEPTED", "accepts a resurrection", StringFormat(R"({{"dead_seconds":{}}})", ai->GetStateAgeMs() / 1000));
        return true;
    }
private:
    uint32 _seenMs = 0;
};

class OfferTrigger : public Trigger
{
public:
    explicit OfferTrigger(BotAI* ai) : Trigger(ai, "revive_offer", 500) { }
    bool IsActive() override
    {
        Player* bot = GetBot();
        return Cfg().Enabled && bot && !bot->IsAlive() && bot->IsResurrectRequested();
    }
};

class AcceptStrategy : public Strategy
{
public:
    AcceptStrategy() : Strategy("revive_accept") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        t.push_back({ "revive_offer", { { "revive_accept", BotRelevance::Emergency } } });
    }
};
} // namespace

Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Revive.Enabled", false);
        s_cfg.MaxYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.MaxYards", 60), 10, 300));
        s_cfg.MinManaPct = std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.MinManaPct", 40), 0, 100);
        s_cfg.RetryMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.RetrySec", 20), 5, 120)) * 1000;
        s_cfg.MaxAttempts = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.MaxAttempts", 4), 1, 20));
        s_cfg.WaitBeforeReleaseMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.WaitBeforeReleaseSec", 20), 0, 300)) * 1000;
        s_cfg.AcceptDelayMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Revive.AcceptDelaySec", 2), 0, 30)) * 1000;
    });
    return s_cfg;
}

bool ClassCanRevive(uint8 playerClass)
{
    return SpellRoot(playerClass) != 0;
}

bool HoldRelease(BotAI* ai, Player* deadBot)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !cfg.WaitBeforeReleaseMs || !deadBot->GetGroup() || !deadBot->IsInWorld())
        return false;
    std::vector<Candidate> cands;
    std::vector<Player*> who;
    Candidates(deadBot, deadBot, cands, who);
    return BotRevive::HoldRelease(PickResurrector(cands, cfg) >= 0, ai->GetStateAgeMs(), cfg);
}
} // namespace BotRevive

void RegisterReviveBotObjects(BotRegistry& r)
{
    BotRevive::Cfg();
    r.AddTrigger("revive_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotRevive::ReviveTrigger>(ai); });
    r.AddAction("revive_member", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotRevive::ReviveAction>(ai); });
    r.AddStrategy("revive", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotRevive::ReviveStrategy>(); });
    r.AddTrigger("revive_offer", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotRevive::OfferTrigger>(ai); });
    r.AddAction("revive_accept", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotRevive::AcceptAction>(ai); });
    r.AddStrategy("revive_accept", BotStateBit(BotState::Dead), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotRevive::AcceptStrategy>(); });
}
