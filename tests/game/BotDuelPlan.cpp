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

#include "tc_catch2.h"

#include "BotDuelPlan.h"

#include <vector>

using namespace BotDuel;

namespace
{
Config On()
{
    Config c;
    c.Enabled = true;
    return c;
}
}

TEST_CASE("duel: a free healthy bot of similar level accepts", "[bot][duel]")
{
    Config cfg = On();
    cfg.AcceptPct = 100;
    ChallengeFacts f;
    f.BotLevel = f.OpponentLevel = 30;
    for (uint32 t = 0; t < 50; ++t)
        CHECK(Decide(cfg, f, 7 + t, t * 1000).What == Verdict::Accept);
}

TEST_CASE("duel: reasons are checked in order", "[bot][duel]")
{
    Config cfg = On();
    cfg.AcceptPct = 100;
    ChallengeFacts f;
    f.Alive = false;
    f.Dueling = f.InCombat = f.Busy = true;
    f.HealthPct = 10;
    f.BotLevel = 60;
    f.OpponentLevel = 5;
    f.SinceLastDuelMs = 0;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Dead);
    f.Alive = true;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Dueling);
    f.Dueling = false;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Combat);
    f.InCombat = false;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Busy);
    f.Busy = false;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Cooldown);
    f.SinceLastDuelMs = cfg.CooldownSec * 1000;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Hurt);
    f.HealthPct = 100;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::LevelGap);
    f.OpponentLevel = 60;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Accept);
}

TEST_CASE("duel: level gap works in both directions and at the limit", "[bot][duel]")
{
    Config cfg = On();
    cfg.AcceptPct = 100;
    ChallengeFacts f;
    f.BotLevel = 30;
    f.OpponentLevel = 30 + cfg.MaxLevelGap;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Accept);
    f.OpponentLevel = 31 + cfg.MaxLevelGap;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::LevelGap);
    f.OpponentLevel = 30 - cfg.MaxLevelGap;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Accept);
    f.OpponentLevel = 29 - cfg.MaxLevelGap;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::LevelGap);
}

TEST_CASE("duel: the acceptance chance follows AcceptPct", "[bot][duel]")
{
    ChallengeFacts f;
    Config cfg = On();
    cfg.AcceptPct = 0;
    for (uint32 t = 0; t < 100; ++t)
        CHECK(Decide(cfg, f, 3, t * 1000).What == Verdict::Chance);
    cfg.AcceptPct = 50;
    uint32 yes = 0;
    for (uint32 t = 0; t < 1000; ++t)
        yes += Decide(cfg, f, 3 + t, t * 1000).What == Verdict::Accept;
    CHECK(yes > 400);
    CHECK(yes < 600);
}

TEST_CASE("duel: bot against bot and own alts skip the mood roll", "[bot][duel]")
{
    Config cfg = On();
    cfg.AcceptPct = 0;
    ChallengeFacts f;
    f.OpponentIsBot = true;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Accept);
    f.OpponentIsBot = false;
    f.OwnAlt = true;
    f.SinceLastDuelMs = 0;
    f.OpponentLevel = 80;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Accept);
    f.HealthPct = 5;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Hurt);
    f.InCombat = true;
    f.HealthPct = 100;
    CHECK(Decide(cfg, f, 1, 0).What == Verdict::Combat);
}

TEST_CASE("duel: the reply pause stays in its range and varies", "[bot][duel]")
{
    Config cfg = On();
    uint32 lo = ~0u, hi = 0;
    for (uint32 t = 0; t < 200; ++t)
    {
        uint32 const d = ReplyDelayMs(cfg, 100 + t, t * 997);
        CHECK(d >= cfg.ReplyMinMs);
        CHECK(d <= cfg.ReplyMaxMs);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    CHECK(hi - lo > 1000);
    cfg.ReplyMaxMs = 100;   // below the minimum: the minimum is used
    CHECK(ReplyDelayMs(cfg, 1, 1) == cfg.ReplyMinMs);
}

TEST_CASE("duel: a running fight yields, times out or goes on", "[bot][duel]")
{
    Config cfg = On();
    FightFacts f;
    f.HealthPct = 5;
    CHECK(FightStep(cfg, f) == FightAct::Fight);   // YieldPct 0: to the end
    cfg.YieldPct = 10;
    CHECK(FightStep(cfg, f) == FightAct::Yield);
    f.HealthPct = 11;
    CHECK(FightStep(cfg, f) == FightAct::Fight);
    f.HealthPct = 5;
    f.OpponentIsBot = true;
    CHECK(FightStep(cfg, f) == FightAct::Fight);   // bots do not yield to bots
    f.ElapsedMs = cfg.BotTimeoutSec * 1000 - 1;
    CHECK(FightStep(cfg, f) == FightAct::Fight);
    f.ElapsedMs = cfg.BotTimeoutSec * 1000;
    CHECK(FightStep(cfg, f) == FightAct::TimeOut);
    f.OpponentIsBot = false;
    f.HealthPct = 100;
    CHECK(FightStep(cfg, f) == FightAct::Fight);   // against a player there is no time limit
}

TEST_CASE("duel: every line exists and every verdict has one", "[bot][duel]")
{
    for (uint32 l = 0; l < uint32(Line::Count); ++l)
        for (uint32 t = 0; t < 20; ++t)
            CHECK_FALSE(PickLine(Line(l), 5 + t, t * 1000).empty());
    for (uint32 v = 0; v <= uint32(Verdict::Chance); ++v)
    {
        CHECK_FALSE(PickLine(LineFor(Verdict(v)), 1, 0).empty());
        CHECK(std::string_view(VerdictCode(Verdict(v))).substr(0, 5) == "DUEL_");
    }
    CHECK(LineFor(Verdict::Accept) == Line::Accept);
    CHECK(PickLine(Line::Win, 9, 5000) == PickLine(Line::Win, 9, 5000));
}

TEST_CASE("duel: reaction emotes fit the outcome", "[bot][duel]")
{
    bool sawCry = false;
    for (uint32 t = 0; t < 500; ++t)
    {
        Emote const w = ReactionEmote(Outcome::Won, 11, t);
        CHECK(w != Emote::None);
        CHECK(w != Emote::Cry);
        Emote const l = ReactionEmote(Outcome::Lost, 11, t);
        CHECK(l != Emote::Cheer);
        CHECK(l != Emote::Laugh);
        sawCry |= l == Emote::Cry;
    }
    CHECK(sawCry);
}

TEST_CASE("duel: whispers that ask for a duel", "[bot][duel]")
{
    CHECK(IsDuelRequest("duel"));
    CHECK(IsDuelRequest("Duel me!"));
    CHECK(IsDuelRequest("  wanna DUEL?  "));
    CHECK(IsDuelRequest("let's spar"));
    CHECK(IsDuelRequest("1v1?"));
    CHECK(IsDuelRequest("fight me"));
    CHECK(IsDuelRequest("challenge me"));
    CHECK_FALSE(IsDuelRequest(""));
    CHECK_FALSE(IsDuelRequest("hello"));
    CHECK_FALSE(IsDuelRequest("fight"));
    CHECK_FALSE(IsDuelRequest("no duel please"));
    CHECK_FALSE(IsDuelRequest("I don't want to duel"));
    CHECK_FALSE(IsDuelRequest("can you help me with the duel quest in redridge please"));
}

TEST_CASE("duel: the closest fitting partner is picked", "[bot][duel]")
{
    Config cfg = On();
    std::vector<Partner> others(4);
    others[0].Yards = 20.0f;
    others[0].Level = 30;
    others[1].Yards = 10.0f;
    others[1].Level = 30;
    others[2].Yards = 5.0f;
    others[2].Level = 30;
    others[2].Free = false;
    others[3].Yards = 5.0f;
    others[3].Level = 30 + cfg.MaxLevelGap + 1;
    CHECK(PickPartner(cfg, 30, others, 1, 1000) == 1);

    others[1].HealthPct = 10;   // hurt
    CHECK(PickPartner(cfg, 30, others, 1, 1000) == 0);
    others[0].SinceLastDuelMs = 1000;   // just fought
    CHECK(PickPartner(cfg, 30, others, 1, 1000) == -1);
    others[0].SinceLastDuelMs = 0xFFFFFFFFu;
    others[0].Yards = cfg.BotSearchYards + 1.0f;   // too far
    CHECK(PickPartner(cfg, 30, others, 1, 1000) == -1);
    CHECK(PickPartner(cfg, 30, {}, 1, 1000) == -1);
}

TEST_CASE("duel: a level apart counts as 8 yards when picking a partner", "[bot][duel]")
{
    Config cfg = On();
    std::vector<Partner> others(2);
    others[0].Yards = 6.0f;
    others[0].Level = 33;   // 3 apart: 6 + 24
    others[1].Yards = 20.0f;
    others[1].Level = 30;   // same level: 20
    CHECK(PickPartner(cfg, 30, others, 1, 1000) == 1);
}

TEST_CASE("duel: looking for a sparring partner needs the switch, the interval, a watcher and luck", "[bot][duel]")
{
    Config cfg = On();
    cfg.BotVsBot = true;
    cfg.BotChancePct = 100;
    uint32 const iv = cfg.BotIntervalSec * 1000;
    CHECK(WantsSparring(cfg, iv, true, 1, 1000));
    CHECK_FALSE(WantsSparring(cfg, iv - 1, true, 1, 1000));
    CHECK_FALSE(WantsSparring(cfg, iv, false, 1, 1000));
    cfg.BotNeedWatcher = false;
    CHECK(WantsSparring(cfg, iv, false, 1, 1000));
    cfg.BotChancePct = 0;
    CHECK_FALSE(WantsSparring(cfg, iv, true, 1, 1000));
    cfg.BotChancePct = 100;
    cfg.BotVsBot = false;
    CHECK_FALSE(WantsSparring(cfg, iv, true, 1, 1000));
}
