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

// Bot mounts, see BotMount.h.

#include "BotMount.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotEngine.h"
#include "BotMountPlan.h"
#include "Config.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace BotMount
{
namespace
{
using Trinity::StringFormat;

Config s_cfg;
std::once_flag s_cfgOnce;

// A ride ordered in chat is protected from the automatic dismount; the flag falls when the bot is still on foot this long after the order.
constexpr uint32 ORDER_GRACE_MS = 10000;
constexpr uint32 MOUNT_CHECK_MS = 30000;

// per-bot state (value "mount_ctx")
class MountCtx : public UntypedValue
{
public:
    explicit MountCtx(BotAI* ai) : UntypedValue(ai, "mount_ctx", 0) { }
    uint32 LeaderMountedSince = 0;   // 0 = leader not mounted (or not seen)
    uint32 LeaderFootSince = 0;      // 0 = leader mounted (or not seen)
    uint32 FarSince = 0;
    uint32 LastActMs = 0;
    uint32 LastFailMs = 0;
    uint32 OrderedMs = 0;
    uint32 MountCheckMs = 0;         // when HasMount was last read (the spell book is walked at most every 30 s)
    bool HasMount = false;
    bool Ordered = false;
};

uint32 Since(uint32 stamp, uint32 now) { return stamp ? now - stamp : 0; }
uint32 SinceOr(uint32 stamp, uint32 now) { return stamp ? now - stamp : 0xFFFFFFFFu; }

std::vector<MountOption> CollectMounts(Player* bot)
{
    std::vector<MountOption> options;
    for (auto const& [id, spell] : bot->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || !spell.active)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!si || !si->HasAura(SPELL_AURA_MOUNTED))
            continue;
        MountOption o;
        o.SpellId = id;
        for (SpellEffectInfo const& e : si->GetEffects())
        {
            if (e.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                o.Speed = std::max(o.Speed, e.CalcValueAsInt(bot));
            else if (e.ApplyAuraName == SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED || e.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)
                o.Flying = true;
        }
        options.push_back(o);
    }
    return options;
}

class MountTickAction : public Action
{
public:
    explicit MountTickAction(BotAI* ai) : Action(ai, "mount_tick", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        MountCtx* c = static_cast<MountCtx*>(ai->GetValueRaw("mount_ctx"));
        if (!c || !bot->IsInWorld())
            return false;
        Config const& cfg = Cfg();
        uint32 const now = std::max<uint32>(ai->GetNowMs(), 1);   // 0 is the "not set" stamp

        Facts f;
        f.BotAlive = bot->IsAlive();
        f.BotInCombat = bot->IsInCombat() || ai->GetState() == BotState::Combat;
        f.BotBusy = bot->IsNonMeleeSpellCast(false) || bot->IsInFlight() || bot->IsBeingTeleported();
        f.BotMounted = bot->IsMounted();
        if (!f.BotMounted && c->Ordered && now - c->OrderedMs > ORDER_GRACE_MS)
            c->Ordered = false;
        f.Ordered = c->Ordered;

        Map const* map = bot->GetMap();
        f.CanMountHere = map && !map->IsDungeon() && !map->IsBattlegroundOrArena() && bot->IsOutdoors() && !bot->IsInWater()
            && !bot->IsInDisallowedMountForm();

        Player* leader = ai->Motion().GetFollow().IsEmpty() ? nullptr : ObjectAccessor::GetPlayer(*bot, ai->Motion().GetFollow());
        if (leader && (!leader->IsInWorld() || leader == bot || leader->GetMap() != bot->GetMap()))
            leader = nullptr;
        f.Follows = leader != nullptr;
        if (leader)
        {
            f.LeaderInCombat = leader->IsInCombat();
            f.LeaderMounted = leader->IsMounted();
            f.Distance = bot->GetExactDist2d(leader);
            if (f.LeaderMounted)
            {
                if (!c->LeaderMountedSince)
                    c->LeaderMountedSince = now;
                c->LeaderFootSince = 0;
            }
            else
            {
                if (!c->LeaderFootSince)
                    c->LeaderFootSince = now;
                c->LeaderMountedSince = 0;
            }
            if (f.Distance > cfg.CatchUpYards)
            {
                if (!c->FarSince)
                    c->FarSince = now;
            }
            else
                c->FarSince = 0;
        }
        else
        {
            c->LeaderMountedSince = c->LeaderFootSince = c->FarSince = 0;
        }
        f.LeaderMountedMs = Since(c->LeaderMountedSince, now);
        f.LeaderOnFootMs = Since(c->LeaderFootSince, now);
        f.FarForMs = Since(c->FarSince, now);
        f.SinceActMs = SinceOr(c->LastActMs, now);
        f.SinceFailMs = SinceOr(c->LastFailMs, now);
        if (!f.BotMounted && f.Follows && f.CanMountHere && (!c->MountCheckMs || now - c->MountCheckMs >= MOUNT_CHECK_MS))
        {
            c->MountCheckMs = now;
            c->HasMount = PickMatching(CollectMounts(bot), 0) >= 0;
        }
        f.HasMount = c->HasMount;

        Decision const d = Decide(f, cfg);
        switch (d.What)
        {
            case Act::None:
                return false;
            case Act::Mount:
            {
                c->LastActMs = now;
                int32 const want = leader ? SpeedPctFromRate(leader->GetSpeedRate(MOVE_RUN)) : 0;
                char const* res = MountUp(ai, bot, want);
                bool const ok = !std::strcmp(res, "OK");
                if (!ok)
                    c->LastFailMs = now;
                SetResult(ok ? "MOUNT_UP" : "MOUNT_REFUSED", StringFormat("{}: {}", d.Why, res),
                    StringFormat(R"({{"why":"{}","result":"{}","leader_speed_pct":{},"dist":{:.0f}}})", d.Why, res, want, f.Distance));
                return ok;
            }
            case Act::Dismount:
                c->LastActMs = now;
                bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                bot->Dismount();
                c->Ordered = false;
                SetResult("MOUNT_DOWN", d.Why, StringFormat(R"({{"why":"{}","dist":{:.0f}}})", d.Why, f.Distance));
                return true;
        }
        return false;
    }
};

class MountDueTrigger : public Trigger
{
public:
    explicit MountDueTrigger(BotAI* ai) : Trigger(ai, "mount_due", 500) { }
    bool IsActive() override { return Cfg().Enabled; }
};

class MountStrategy : public Strategy
{
public:
    MountStrategy() : Strategy("mount") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // above the follow leg (Move) so a mount cast is not cut off by the next follow step; the action returns false unless it mounted
        // or dismounted, so the other actions still run this tick
        t.push_back({ "mount_due", { { "mount_tick", BotRelevance::Move + 5.0f } } });
    }
};
} // namespace

Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Mount.Enabled", false);
        s_cfg.MountDelayMs = 1000u * uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.MountDelaySec", 2), 0, 60));
        s_cfg.DismountDelayMs = 1000u * uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.DismountDelaySec", 3), 0, 60));
        s_cfg.DismountYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.DismountYards", 15), 3, 100));
        s_cfg.CatchUpYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.CatchUpYards", 60), 20, 500));
        s_cfg.CatchUpMs = 1000u * uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.CatchUpSec", 8), 0, 300));
        s_cfg.FailBackoffMs = 1000u * uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Mount.RetrySec", 30), 5, 600));
    });
    return s_cfg;
}

bool Enabled()
{
    return Cfg().Enabled;
}

char const* MountUp(BotAI* ai, Player* bot, int32 wantSpeed)
{
    std::vector<MountOption> options = CollectMounts(bot);
    int32 const pick = PickMatching(options, wantSpeed);
    if (pick < 0)
        return "NO_MOUNT";
    // stop the follow / goto leg: movement interrupts the cast
    if (ai->Motion().HasGoal())
        ai->Motion().ClearGoal();
    bot->StopMoving();
    return bot->CastSpell(bot, options[size_t(pick)].SpellId, CastSpellExtraArgs(TRIGGERED_NONE)) == SPELL_CAST_OK ? "OK" : "CANT_MOUNT";
}

void NoteOrder(BotAI* ai, bool mounted)
{
    if (MountCtx* c = static_cast<MountCtx*>(ai->GetValueRaw("mount_ctx")))
    {
        c->Ordered = mounted;
        c->OrderedMs = std::max<uint32>(ai->GetNowMs(), 1);
    }
}
} // namespace BotMount

void RegisterMountBotObjects(BotRegistry& r)
{
    BotMount::Enabled();
    r.AddValue("mount_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotMount::MountCtx>(ai); });
    r.AddTrigger("mount_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotMount::MountDueTrigger>(ai); });
    r.AddAction("mount_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotMount::MountTickAction>(ai); });
    r.AddStrategy("mount", BotStateBit(BotState::NonCombat) | BotStateBit(BotState::Combat),
        []() -> std::unique_ptr<Strategy> { return std::make_unique<BotMount::MountStrategy>(); });
}
