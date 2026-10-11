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

#include "BotDuelPlan.h"
#include "BotMovePlan.h"
#include <array>
#include <string>
#include <vector>

namespace BotDuel
{
using BotMove::Mix;
using BotMove::Range;
using BotMove::Unit;

namespace
{
uint32 Gap(uint32 a, uint32 b) { return a > b ? a - b : b - a; }

// The mood checks shared by Decide and PickPartner. Returns Accept when none applies.
Verdict Mood(Config const& cfg, ChallengeFacts const& f)
{
    if (f.SinceLastDuelMs < cfg.CooldownSec * 1000)
        return Verdict::Cooldown;
    if (f.HealthPct < cfg.MinHealthPct)
        return Verdict::Hurt;
    if (Gap(f.BotLevel, f.OpponentLevel) > cfg.MaxLevelGap)
        return Verdict::LevelGap;
    return Verdict::Accept;
}

constexpr size_t VARIANTS = 4;
using Lines = std::array<std::string_view, VARIANTS>;

constexpr std::array<Lines, size_t(Line::Count)> kLines = {{
    /* Accept */          { "Alright, let's do this.", "You're on!", "Challenge accepted. Show me what you've got.", "Ready when you are." },
    /* Challenge */       { "Care for a duel?", "How about a friendly duel?", "Let's see who's better. Duel?", "Fancy a spar?" },
    /* DeclineCombat */   { "Not now, I'm in the middle of something!", "Busy fighting, ask me later.", "Can't right now, something's after me.", "Give me a moment, I'm in a fight." },
    /* DeclineBusy */     { "I can't right now, sorry.", "Not now, I've got somewhere to be.", "Maybe later, I'm busy.", "Bad timing, sorry." },
    /* DeclineHurt */     { "I'm hurt, give me a minute to recover.", "Let me patch myself up first.", "Not like this, I need to rest first.", "Ask me again when I've caught my breath." },
    /* DeclineLevel */    { "That wouldn't be a fair fight.", "We're not evenly matched, sorry.", "Find someone closer to your level.", "I'd rather spar with someone my own size." },
    /* DeclineCooldown */ { "Give me a moment to catch my breath.", "Just fought, ask me again in a bit.", "Let me recover first.", "Not so soon, I'm still sore." },
    /* DeclineChance */   { "Not today, thanks.", "Maybe another time.", "I'll pass this time.", "Not in the mood, sorry." },
    /* Win */             { "Good fight!", "Well fought!", "Better luck next time.", "That was fun, thanks for the duel." },
    /* Lose */            { "Well fought, you got me.", "Ouch. Good duel!", "You win this time.", "Nicely done, I'll get you next time." },
    /* Yield */           { "I yield! Well fought.", "Enough, you win!", "Okay okay, I give up!", "You've got me, I yield." },
    /* TooFar */          { "Come a bit closer and I'll duel you.", "Step over here and we'll see.", "I can't reach you from there.", "Come closer, I'm not walking all that way." },
}};

std::vector<std::string> Tokens(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            cur += c;
        else if (c >= 'A' && c <= 'Z')
            cur += char(c - 'A' + 'a');
        else if (!cur.empty())
        {
            out.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty())
        out.push_back(std::move(cur));
    return out;
}
}

char const* VerdictCode(Verdict v)
{
    switch (v)
    {
        case Verdict::Accept:   return "DUEL_ACCEPTED";
        case Verdict::Dead:     return "DUEL_DECLINED_DEAD";
        case Verdict::Dueling:  return "DUEL_DECLINED_DUELING";
        case Verdict::Combat:   return "DUEL_DECLINED_COMBAT";
        case Verdict::Busy:     return "DUEL_DECLINED_BUSY";
        case Verdict::Hurt:     return "DUEL_DECLINED_HURT";
        case Verdict::LevelGap: return "DUEL_DECLINED_LEVEL";
        case Verdict::Cooldown: return "DUEL_DECLINED_COOLDOWN";
        case Verdict::Chance:   return "DUEL_DECLINED_CHANCE";
    }
    return "DUEL_DECLINED";
}

uint32 ReplyDelayMs(Config const& cfg, uint64 botKey, uint32 nowMs)
{
    uint32 const lo = cfg.ReplyMinMs, hi = cfg.ReplyMaxMs > cfg.ReplyMinMs ? cfg.ReplyMaxMs : lo;
    return lo + uint32(Unit(Mix(botKey, nowMs, 0xD1E1)) * float(hi - lo));
}

Answer Decide(Config const& cfg, ChallengeFacts const& f, uint64 botKey, uint32 nowMs)
{
    Answer a;
    a.DelayMs = ReplyDelayMs(cfg, botKey, nowMs);
    if (!f.Alive)
        a.What = Verdict::Dead;
    else if (f.Dueling)
        a.What = Verdict::Dueling;
    else if (f.InCombat)
        a.What = Verdict::Combat;
    else if (f.Busy)
        a.What = Verdict::Busy;
    else if (f.OwnAlt)
        a.What = f.HealthPct < cfg.MinHealthPct ? Verdict::Hurt : Verdict::Accept;
    else if (Verdict const mood = Mood(cfg, f); mood != Verdict::Accept)
        a.What = mood;
    else if (!f.OpponentIsBot && uint32(Unit(Mix(botKey, nowMs, 0xD1E2)) * 100.0f) >= cfg.AcceptPct)
        a.What = Verdict::Chance;
    return a;
}

FightAct FightStep(Config const& cfg, FightFacts const& f)
{
    if (!f.OpponentIsBot && cfg.YieldPct && f.HealthPct <= cfg.YieldPct)
        return FightAct::Yield;
    if (f.OpponentIsBot && cfg.BotTimeoutSec && f.ElapsedMs >= cfg.BotTimeoutSec * 1000)
        return FightAct::TimeOut;
    return FightAct::Fight;
}

Emote ReactionEmote(Outcome o, uint64 botKey, uint32 nowMs)
{
    uint32 const pick = uint32(Unit(Mix(botKey, nowMs, 0xD1E3)) * 6.0f);
    if (o == Outcome::Won)
    {
        static constexpr Emote won[6] = { Emote::Cheer, Emote::Cheer, Emote::Bow, Emote::Salute, Emote::Laugh, Emote::Bow };
        return won[pick % 6];
    }
    static constexpr Emote lost[6] = { Emote::Bow, Emote::Salute, Emote::Cry, Emote::Bow, Emote::Salute, Emote::None };
    return lost[pick % 6];
}

std::string_view PickLine(Line line, uint64 botKey, uint32 nowMs)
{
    size_t const i = size_t(line) < kLines.size() ? size_t(line) : 0;
    return kLines[i][size_t(Unit(Mix(botKey, nowMs / 1000, 0xD1E4 + i)) * float(VARIANTS)) % VARIANTS];
}

Line LineFor(Verdict v)
{
    switch (v)
    {
        case Verdict::Accept:   return Line::Accept;
        case Verdict::Dead:     return Line::DeclineBusy;
        case Verdict::Dueling:  return Line::DeclineBusy;
        case Verdict::Combat:   return Line::DeclineCombat;
        case Verdict::Busy:     return Line::DeclineBusy;
        case Verdict::Hurt:     return Line::DeclineHurt;
        case Verdict::LevelGap: return Line::DeclineLevel;
        case Verdict::Cooldown: return Line::DeclineCooldown;
        case Verdict::Chance:   return Line::DeclineChance;
    }
    return Line::DeclineBusy;
}

bool IsDuelRequest(std::string_view text)
{
    std::vector<std::string> const t = Tokens(text);
    if (t.empty() || t.size() > 6)
        return false;
    bool ask = false;
    for (size_t i = 0; i < t.size(); ++i)
    {
        std::string const& w = t[i];
        if (w == "no" || w == "not" || w == "nope" || w == "never" || w == "dont" || w == "don" || w == "cant" || w == "wont")
            return false;
        if (w == "duel" || w == "spar" || w == "sparring" || w == "1v1")
            ask = true;
        else if ((w == "fight" || w == "challenge") && i + 1 < t.size() && t[i + 1] == "me")
            ask = true;
    }
    return ask;
}

int32 PickPartner(Config const& cfg, uint32 botLevel, std::span<Partner const> others, uint64 botKey, uint32 nowMs)
{
    int32 best = -1;
    float bestScore = 0.0f;
    for (size_t i = 0; i < others.size(); ++i)
    {
        Partner const& p = others[i];
        if (!p.Free || p.Yards > cfg.BotSearchYards)
            continue;
        ChallengeFacts f;
        f.BotLevel = p.Level;
        f.OpponentLevel = botLevel;
        f.HealthPct = p.HealthPct;
        f.SinceLastDuelMs = p.SinceLastDuelMs;
        f.OpponentIsBot = true;
        if (Mood(cfg, f) != Verdict::Accept)
            continue;
        // the tiny key-dependent term decides between partners at the same distance without favoring the lower index
        float const score = p.Yards + 8.0f * float(Gap(p.Level, botLevel)) + Unit(Mix(botKey, nowMs / 1000, i)) * 0.01f;
        if (best < 0 || score < bestScore)
        {
            best = int32(i);
            bestScore = score;
        }
    }
    return best;
}

bool WantsSparring(Config const& cfg, uint32 sinceLookMs, bool watcherNear, uint64 botKey, uint32 nowMs)
{
    if (!cfg.Enabled || !cfg.BotVsBot || sinceLookMs < cfg.BotIntervalSec * 1000)
        return false;
    if (cfg.BotNeedWatcher && !watcherNear)
        return false;
    return uint32(Unit(Mix(botKey, nowMs / 1000, 0xD1E5)) * 100.0f) < cfg.BotChancePct;
}
}
