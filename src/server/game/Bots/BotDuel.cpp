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

#include "BotDuel.h"
#include "BotAI.h"
#include "BotDuelPlan.h"
#include "BotEngine.h"
#include "BotTravel.h"
#include "Config.h"
#include "DB2Stores.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RestMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <vector>

using Trinity::StringFormat;

namespace BotDuel
{
namespace
{
constexpr uint32 SPELL_DUEL = 7266;
constexpr uint32 ASK_TIMEOUT_MS = 45000;       // a challenge a bot sent and nobody answered is cancelled
constexpr uint32 WALK_UP_MS = 25000;           // longest a bot walks towards a player it wants to challenge
constexpr float WHISPER_MAX_YARDS = 60.0f;     // farther away than this a whispered request is answered with "come closer"

Config s_cfg;
std::once_flag s_cfgOnce;

class DuelCtx : public UntypedValue
{
public:
    explicit DuelCtx(BotAI* ai) : UntypedValue(ai, "duel_ctx", 0) { }

    // a challenge to this bot waits for its answer
    bool Pending = false;
    Verdict Decision = Verdict::Accept;
    uint32 ReplyAtMs = 0;
    ObjectGuid Arbiter;
    ObjectGuid Challenger;
    // this bot challenged someone
    uint32 AskedMs = 0;
    // a running duel
    bool FightOn = false;
    uint32 FightStartMs = 0;
    // the last duel
    bool HasLast = false;
    uint32 LastEndMs = 0;
    // reaction to a finished duel
    bool ReactPending = false;
    Outcome ReactOutcome = Outcome::Won;
    uint32 ReactAtMs = 0;
    // a whispered request: walk up to the player and challenge
    ObjectGuid Want;
    uint32 WantUntilMs = 0;
    uint32 WalkMs = 0;
    // sparring partner search
    uint32 LastLookMs = 0;
    bool Looked = false;
    bool Held = false;      // the module stopped the bot (movement goal cleared) and has not released it yet
};

DuelCtx* Ctx(BotAI* ai) { return ai ? static_cast<DuelCtx*>(ai->GetValueRaw("duel_ctx")) : nullptr; }

bool BotsOn() { return sWorld->getBoolConfig(CONFIG_BOT_ENABLED); }

BotAI* AiOf(Player* p) { return p && p->GetSession() && p->GetSession()->IsBot() ? p->GetSession()->GetBotAI() : nullptr; }

bool Due(uint32 now, uint32 at) { return int32(now - at) >= 0; }

void Emit(BotAI* ai, Player* bot, char const* reason, std::string summary, std::string details = std::string(), uint8 sev = BOTLOG_INFO)
{
    ai->EmitEvent(bot, "decision", sev, reason, std::move(summary), std::move(details));
}

std::string Who(Player* other)
{
    return StringFormat(R"("opponent":"{}","opponent_guid":{},"opponent_bot":{},"opponent_level":{})", other->GetName(), other->GetGUID().GetCounter(),
        other->GetSession() && other->GetSession()->IsBot() ? "true" : "false", uint32(other->GetLevel()));
}

// Anything that makes a duel a bad idea for this bot besides its health and the level gap.
bool IsBusy(BotAI* ai, Player* bot)
{
    return bot->IsInFlight() || bot->InBattleground() || bot->InArena() || bot->IsBeingTeleported() || !bot->GetMap() || bot->GetMap()->Instanceable()
        || bot->GetGroup() || BotTravel::Busy(ai) || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
}

ChallengeFacts FactsOf(BotAI* ai, Player* bot, DuelCtx* ctx, Player* opponent)
{
    ChallengeFacts f;
    f.Alive = bot->IsAlive();
    f.Dueling = bot->duel != nullptr;
    f.InCombat = bot->IsInCombat();
    f.Busy = IsBusy(ai, bot);
    f.HealthPct = uint32(bot->GetHealthPct());
    f.BotLevel = bot->GetLevel();
    f.OpponentLevel = opponent->GetLevel();
    f.SinceLastDuelMs = ctx->HasLast ? ai->GetNowMs() - ctx->LastEndMs : 0xFFFFFFFFu;
    f.OpponentIsBot = opponent->GetSession() && opponent->GetSession()->IsBot();
    f.OwnAlt = bot->GetSession()->IsAltBot() && opponent->GetSession() && opponent->GetSession()->GetAccountId() == bot->GetSession()->GetAccountId();
    return f;
}

// The duel flag spell only works in areas that allow dueling; casting it elsewhere just fails.
bool DuelingAllowed(Player* p)
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId());
    return !area || area->GetFlags().HasFlag(AreaFlags::AllowDueling);
}

void Speak(BotAI* ai, Player* bot, Line line, Player* whisperTo = nullptr)
{
    if (!s_cfg.Chat)
        return;
    std::string_view const text = PickLine(line, bot->GetGUID().GetCounter(), ai->GetNowMs());
    if (whisperTo && whisperTo->GetSession() && !whisperTo->GetSession()->IsBot())
        bot->Whisper(text, LANG_UNIVERSAL, whisperTo);
    else
        bot->Say(text, LANG_UNIVERSAL);
}

void DoEmote(Player* bot, Emote e)
{
    switch (e)
    {
        case Emote::Bow:    bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW); break;
        case Emote::Salute: bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE); break;
        case Emote::Cheer:  bot->HandleEmoteCommand(EMOTE_ONESHOT_CHEER); break;
        case Emote::Cry:    bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY); break;
        case Emote::Laugh:  bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH); break;
        case Emote::None:   break;
    }
}

// Stops the bot and keeps it facing `to` (reply pending, countdown, waiting for the answer to its own challenge).
void Hold(BotAI* ai, Player* bot, DuelCtx* ctx, Player* to)
{
    if (!ctx->Held)
    {
        ctx->Held = true;
        ai->Motion().ClearGoal();
        BotMotion::Halt(bot);
    }
    if (to && !bot->isInFront(to, float(M_PI) / 3.0f))
        bot->SetFacingToObject(to);
}

void Release(DuelCtx* ctx) { ctx->Held = false; }

void Respond(BotAI* ai, Player* bot, DuelCtx* ctx)
{
    ctx->Pending = false;
    Player* challenger = ObjectAccessor::FindPlayer(ctx->Challenger);
    bool const accept = ctx->Decision == Verdict::Accept;
    if (!challenger || !bot->duel || bot->duel->State != DUEL_STATE_CHALLENGED || bot->duel->Opponent != challenger)
        return;   // the challenger gave up or the flag expired meanwhile

    std::string const who = Who(challenger);
    if (accept)
    {
        bot->GetSession()->HandleDuelAccepted(ctx->Arbiter);
        Speak(ai, bot, Line::Accept);
        Emit(ai, bot, VerdictCode(Verdict::Accept), StringFormat("accepted the duel with {}", challenger->GetName()), who);
    }
    else
    {
        Speak(ai, bot, LineFor(ctx->Decision), challenger);
        bot->GetSession()->HandleDuelCancelled();
        Emit(ai, bot, VerdictCode(ctx->Decision), StringFormat("declined the duel with {}", challenger->GetName()), who, BOTLOG_WARN);
    }
}

// Casts the duel spell at `target`. False when it cannot be tried (the bot is mounted, the area forbids it ...).
bool Challenge(BotAI* ai, Player* bot, DuelCtx* ctx, Player* target, char const* why)
{
    if (!DuelingAllowed(bot) || !DuelingAllowed(target) || bot->IsMounted() || target->duel || bot->duel || !bot->IsWithinLOSInMap(target))
        return false;
    bot->SetFacingToObject(target);
    bot->CastSpell(target, SPELL_DUEL, false);
    if (!bot->duel)
        return false;   // the spell did not create a flag
    ctx->AskedMs = ai->GetNowMs();
    Speak(ai, bot, Line::Challenge);
    Emit(ai, bot, "DUEL_CHALLENGE_SENT", StringFormat("challenged {} to a duel ({})", target->GetName(), why), Who(target));
    return true;
}

bool InCity(Player* bot) { return bot->GetRestMgr().HasRestFlag(REST_FLAG_IN_CITY); }

// Bot against bot: a free bot in a city picks a free neighbour and challenges it.
bool Sparring(BotAI* ai, Player* bot, DuelCtx* ctx, uint32 now)
{
    if (!s_cfg.BotVsBot || !InCity(bot) || bot->IsInCombat() || IsBusy(ai, bot) || bot->duel || !bot->IsAlive())
        return false;
    uint32 const sinceLook = ctx->Looked ? now - ctx->LastLookMs : 0xFFFFFFFFu;
    if (sinceLook < s_cfg.BotIntervalSec * 1000)
        return false;
    ctx->Looked = true;
    ctx->LastLookMs = now;

    uint64 const key = bot->GetGUID().GetCounter();
    std::vector<Player*> near;
    bot->GetPlayerListInGrid(near, std::max(s_cfg.BotSearchYards, s_cfg.BotWatchYards));
    bool watcher = false;
    for (Player* p : near)
        if (p != bot && p->GetSession() && !p->GetSession()->IsBot() && p->GetDistance(bot) <= s_cfg.BotWatchYards)
        {
            watcher = true;
            break;
        }
    if (!WantsSparring(s_cfg, sinceLook, watcher, key, now))
        return false;

    ChallengeFacts self = FactsOf(ai, bot, ctx, bot);
    self.OpponentIsBot = true;
    if (Decide(s_cfg, self, key, now).What != Verdict::Accept)
        return false;

    std::vector<Player*> pool;
    std::vector<Partner> partners;
    for (Player* p : near)
    {
        BotAI* pai = AiOf(p);
        if (!pai || p == bot || p->GetDistance(bot) > s_cfg.BotSearchYards || !InCity(p))
            continue;
        DuelCtx* pctx = Ctx(pai);
        if (!pctx)
            continue;
        Partner q;
        q.Level = p->GetLevel();
        q.Yards = p->GetDistance(bot);
        q.HealthPct = uint32(p->GetHealthPct());
        q.SinceLastDuelMs = pctx->HasLast ? pai->GetNowMs() - pctx->LastEndMs : 0xFFFFFFFFu;
        q.Free = p->IsAlive() && !p->IsInCombat() && !p->duel && !IsBusy(pai, p) && !pctx->Pending && pctx->Want.IsEmpty();
        pool.push_back(p);
        partners.push_back(q);
    }
    int32 const pick = PickPartner(s_cfg, bot->GetLevel(), partners, key, now);
    if (pick < 0)
        return false;
    return Challenge(ai, bot, ctx, pool[size_t(pick)], "sparring");
}

// A whispered request: walk up to the player and challenge.
bool Wanted(BotAI* ai, Player* bot, DuelCtx* ctx, uint32 now)
{
    Player* p = ObjectAccessor::FindPlayer(ctx->Want);
    if (!p || !p->IsInWorld() || p->GetMap() != bot->GetMap() || !p->IsAlive() || p->duel || Due(now, ctx->WantUntilMs))
    {
        ctx->Want = ObjectGuid::Empty;
        Release(ctx);
        return false;
    }
    ChallengeFacts f = FactsOf(ai, bot, ctx, p);
    f.OpponentIsBot = true;   // the player asked for it: no mood roll
    if (Verdict const v = Decide(s_cfg, f, bot->GetGUID().GetCounter(), now).What; v != Verdict::Accept)
    {
        Speak(ai, bot, LineFor(v), p);
        Emit(ai, bot, VerdictCode(v), StringFormat("will not challenge {}", p->GetName()), Who(p), BOTLOG_WARN);
        ctx->Want = ObjectGuid::Empty;
        Release(ctx);
        return false;
    }
    float const dist = bot->GetDistance(p);
    if (dist > s_cfg.ChallengeYards)
    {
        if (Due(now, ctx->WalkMs))
        {
            ctx->WalkMs = now + 2000;
            ai->Motion().SetGoal(bot->GetMapId(), p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), std::max(2.0f, s_cfg.ChallengeYards * 0.5f), "duel");
        }
        ctx->Held = true;
        return true;
    }
    ai->Motion().ClearGoal();
    BotMotion::Halt(bot);
    ctx->Want = ObjectGuid::Empty;
    ctx->Held = false;
    if (!Challenge(ai, bot, ctx, p, "asked by whisper"))
    {
        Speak(ai, bot, Line::DeclineBusy, p);
        return false;
    }
    return true;
}

class DuelTickAction : public Action
{
public:
    explicit DuelTickAction(BotAI* ai) : Action(ai, "duel_tick", ACTION_FLAG_QUIET_LOG) { }

    bool IsPossible() override { return s_cfg.Enabled && GetBot()->IsAlive(); }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        DuelCtx* ctx = Ctx(ai);
        if (!ctx)
            return false;
        uint32 const now = ai->GetNowMs();
        DuelInfo* d = bot->duel.get();

        if (!d)
        {
            if (ctx->Pending || ctx->FightOn)
            {
                ctx->Pending = false;
                ctx->FightOn = false;
            }
            if (ctx->ReactPending && Due(now, ctx->ReactAtMs))
            {
                ctx->ReactPending = false;
                DoEmote(bot, ReactionEmote(ctx->ReactOutcome, bot->GetGUID().GetCounter(), now));
                Speak(ai, bot, ctx->ReactOutcome == Outcome::Won ? Line::Win : Line::Lose);
            }
            if (!ctx->Want.IsEmpty())
                return Wanted(ai, bot, ctx, now);
            if (ctx->Held)
            {
                Release(ctx);
                if (ai->Motion().HasGoal() && !std::strcmp(ai->Motion().GetTag(), "duel"))
                    ai->Motion().ClearGoal();
            }
            if (!bot->IsInCombat())
                Sparring(ai, bot, ctx, now);
            return bot->duel != nullptr;   // a challenge was just sent: hold from the next tick on
        }

        Player* opp = d->Opponent;
        switch (d->State)
        {
            case DUEL_STATE_CHALLENGED:
                if (d->Initiator == bot)
                {
                    // waiting for the answer to our own challenge
                    if (now - ctx->AskedMs > ASK_TIMEOUT_MS)
                    {
                        Release(ctx);
                        bot->GetSession()->HandleDuelCancelled();
                        return false;
                    }
                    Hold(ai, bot, ctx, opp);
                    return true;
                }
                if (!ctx->Pending)
                    return false;   // a challenge the hook did not see (module switched on meanwhile): the flag expires by itself
                Hold(ai, bot, ctx, opp);
                if (Due(now, ctx->ReplyAtMs))
                    Respond(ai, bot, ctx);
                return true;
            case DUEL_STATE_COUNTDOWN:
                Hold(ai, bot, ctx, opp);
                return true;
            case DUEL_STATE_IN_PROGRESS:
            {
                if (!ctx->FightOn)
                {
                    ctx->FightOn = true;
                    ctx->FightStartMs = now;
                    Release(ctx);
                    Emit(ai, bot, "DUEL_FIGHT_START", StringFormat("fights {} in a duel", opp->GetName()), Who(opp));
                }
                FightFacts ff;
                ff.HealthPct = uint32(bot->GetHealthPct());
                ff.ElapsedMs = now - ctx->FightStartMs;
                ff.OpponentIsBot = opp->GetSession() && opp->GetSession()->IsBot();
                FightAct const act = FightStep(s_cfg, ff);
                if (act == FightAct::Fight)
                    return false;   // the combat engine fights: BotCombat picks ActiveOpponent as the target
                Emit(ai, bot, act == FightAct::Yield ? "DUEL_YIELD" : "DUEL_TIMEOUT",
                    StringFormat("{} the duel with {}", act == FightAct::Yield ? "yields" : "gives up", opp->GetName()),
                    StringFormat(R"({},"hp_pct":{},"elapsed_ms":{})", Who(opp), ff.HealthPct, ff.ElapsedMs), BOTLOG_WARN);
                if (act == FightAct::Yield)
                    Speak(ai, bot, Line::Yield);
                bot->GetSession()->HandleDuelCancelled();
                return true;
            }
            case DUEL_STATE_COMPLETED:
                return false;
        }
        return false;
    }
};

class DuelDueTrigger : public Trigger
{
public:
    explicit DuelDueTrigger(BotAI* ai) : Trigger(ai, "duel_due", 0) { }
    bool IsActive() override { return s_cfg.Enabled; }
};

class DuelStrategy : public Strategy
{
public:
    DuelStrategy() : Strategy("duel") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // above walking and quest work: a bot that was asked stands still and answers; in a running duel the action returns false so the
        // combat actions fight
        t.push_back({ "duel_due", { { "duel_tick", BotRelevance::Interrupt } } });
    }
};
} // namespace

Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        Config& c = s_cfg;
        c.Enabled = sConfigMgr->GetBoolDefault("Bot.Duel.Enabled", false);
        c.AcceptPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.AcceptPct", 90), 0, 100));
        c.ReplyMinMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.ReplyMinMs", 1500), 0, 20000));
        c.ReplyMaxMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.ReplyMaxMs", 4500), int32(c.ReplyMinMs), 30000));
        c.MaxLevelGap = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.MaxLevelGap", 5), 0, 80));
        c.MinHealthPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.MinHealthPct", 70), 0, 100));
        c.CooldownSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.CooldownSec", 60), 0, 3600));
        c.YieldPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.YieldPct", 0), 0, 50));
        c.Chat = sConfigMgr->GetBoolDefault("Bot.Duel.Chat", true);
        c.Challenge = sConfigMgr->GetBoolDefault("Bot.Duel.Challenge.Enabled", true);
        c.ChallengeYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.Challenge.Yards", 8), 3, 10));
        c.BotVsBot = sConfigMgr->GetBoolDefault("Bot.Duel.BotVsBot.Enabled", false);
        c.BotIntervalSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.BotVsBot.IntervalSec", 240), 30, 7200));
        c.BotChancePct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.BotVsBot.ChancePct", 35), 0, 100));
        c.BotSearchYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.BotVsBot.SearchYards", 25), 5, 60));
        c.BotWatchYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.BotVsBot.WatchYards", 60), 10, 150));
        c.BotNeedWatcher = sConfigMgr->GetBoolDefault("Bot.Duel.BotVsBot.NeedWatcher", true);
        c.BotTimeoutSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Duel.BotVsBot.TimeoutSec", 240), 30, 1800));
    });
    return s_cfg;
}

void OnChallenged(Player* target, Player* challenger, ObjectGuid arbiter)
{
    if (!Cfg().Enabled || !BotsOn())
        return;
    BotAI* ai = AiOf(target);
    DuelCtx* ctx = Ctx(ai);
    if (!ctx)
        return;

    // the duel info exists already, so "dueling" must not count against the new challenge
    ChallengeFacts f = FactsOf(ai, target, ctx, challenger);
    f.Dueling = false;
    uint32 const now = ai->GetNowMs();
    Answer const a = Decide(s_cfg, f, target->GetGUID().GetCounter(), now);
    ctx->Pending = true;
    ctx->Decision = a.What;
    ctx->ReplyAtMs = now + a.DelayMs;
    ctx->Arbiter = arbiter;
    ctx->Challenger = challenger->GetGUID();
    Emit(ai, target, "DUEL_CHALLENGED", StringFormat("{} asks for a duel, answer in {} ms: {}", challenger->GetName(), a.DelayMs, VerdictCode(a.What)),
        StringFormat(R"({},"verdict":"{}","hp_pct":{})", Who(challenger), VerdictCode(a.What), f.HealthPct));
}

void OnDuelEnd(Player* winner, Player* loser, DuelCompleteType type)
{
    if (!Cfg().Enabled)
        return;
    for (Player* p : { winner, loser })
    {
        BotAI* ai = AiOf(p);
        DuelCtx* ctx = Ctx(ai);
        if (!ctx)
            continue;
        Player* other = p == winner ? loser : winner;
        uint32 const now = ai->GetNowMs();
        bool const started = ctx->FightOn;
        ctx->Pending = false;
        ctx->FightOn = false;
        ctx->Held = false;
        if (type == DUEL_INTERRUPTED && !started)
        {
            Emit(ai, p, "DUEL_CANCELLED", StringFormat("duel with {} did not take place", other->GetName()), Who(other));
            continue;
        }
        Outcome const o = p == winner ? Outcome::Won : Outcome::Lost;
        ctx->HasLast = true;
        ctx->LastEndMs = now;
        ctx->ReactPending = type != DUEL_INTERRUPTED;
        ctx->ReactOutcome = o;
        ctx->ReactAtMs = now + 1200 + uint32(ReplyDelayMs(s_cfg, p->GetGUID().GetCounter(), now) / 2);
        Emit(ai, p, o == Outcome::Won ? "DUEL_WON" : "DUEL_LOST", StringFormat("{} the duel with {}", o == Outcome::Won ? "won" : "lost", other->GetName()),
            StringFormat(R"({},"type":{},"hp_pct":{})", Who(other), uint32(type), uint32(p->GetHealthPct())));
    }
}

void OnWhisper(Player* sender, Player* receiver, std::string_view text)
{
    if (!Cfg().Enabled || !s_cfg.Challenge || !BotsOn() || !sender || !receiver || sender == receiver)
        return;
    if (sender->GetSession()->IsBot() || !IsDuelRequest(text))
        return;
    BotAI* ai = AiOf(receiver);
    DuelCtx* ctx = Ctx(ai);
    if (!ctx)
        return;

    uint32 const now = ai->GetNowMs();
    uint64 const key = receiver->GetGUID().GetCounter();
    if (!receiver->IsAlive() || sender->GetMap() != receiver->GetMap() || receiver->GetDistance(sender) > WHISPER_MAX_YARDS || sender->duel || receiver->duel)
    {
        Speak(ai, receiver, receiver->IsAlive() && !receiver->duel ? Line::TooFar : Line::DeclineBusy, sender);
        return;
    }
    // the same checks as for a challenge from the player, without the mood roll: asking the bot is already a yes
    ChallengeFacts f = FactsOf(ai, receiver, ctx, sender);
    f.OpponentIsBot = true;
    Answer const a = Decide(s_cfg, f, key, now);
    if (a.What != Verdict::Accept)
    {
        Speak(ai, receiver, LineFor(a.What), sender);
        Emit(ai, receiver, VerdictCode(a.What), StringFormat("will not duel {} on request", sender->GetName()), Who(sender), BOTLOG_WARN);
        return;
    }
    ctx->Want = sender->GetGUID();
    ctx->WantUntilMs = now + WALK_UP_MS;
    ctx->WalkMs = now;
    Emit(ai, receiver, "DUEL_REQUESTED", StringFormat("{} asked for a duel by whisper", sender->GetName()), Who(sender));
}

Player* ActiveOpponent(Player* bot)
{
    if (!s_cfg.Enabled || !bot->duel || bot->duel->State != DUEL_STATE_IN_PROGRESS || !bot->GetSession() || !bot->GetSession()->IsBot())
        return nullptr;
    Player* opp = bot->duel->Opponent;
    return opp && opp->IsInWorld() && opp->IsAlive() ? opp : nullptr;
}

bool Busy(BotAI* ai)
{
    if (!s_cfg.Enabled)
        return false;
    DuelCtx* ctx = Ctx(ai);
    return ctx && ctx->Held;
}
} // namespace BotDuel

void RegisterDuelBotObjects(BotRegistry& r)
{
    BotDuel::Cfg();
    r.AddValue("duel_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotDuel::DuelCtx>(ai); });
    r.AddTrigger("duel_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotDuel::DuelDueTrigger>(ai); });
    r.AddAction("duel_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotDuel::DuelTickAction>(ai); });
    r.AddStrategy("duel", BotStateBit(BotState::NonCombat) | BotStateBit(BotState::Combat),
        []() -> std::unique_ptr<Strategy> { return std::make_unique<BotDuel::DuelStrategy>(); });
}
