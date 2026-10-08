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

#include "BotPetLogic.h"
#include <algorithm>
#include <array>

namespace BotPetLogic
{
bool Elapsed(uint32 nowMs, uint32 lastMs, uint32 waitMs)
{
    return !lastMs || nowMs - lastMs >= waitMs;
}

namespace
{
// failed summons make the next try wait longer; after MaxCallFails the bot waits LongWaitMs
uint32 CallWait(Facts const& f, Config const& cfg)
{
    if (f.CallFails >= cfg.MaxCallFails)
        return cfg.LongWaitMs;
    return cfg.RetryMs * (1 + std::min<uint32>(f.CallFails, 4));
}

Decision Make(Action a, char const* reason) { return { a, reason }; }
}

Decision Decide(Facts const& f, Config const& cfg)
{
    if (!f.BotAlive)
        return Make(Action::None, "bot_dead");
    if (f.BotMounted)
        return Make(Action::None, "bot_mounted");

    // --- no pet in the world ---
    if (!f.PetSummoned)
    {
        if (!f.HasSavedPet)
            return Make(Action::None, "no_pet");
        if (f.BotInCombat || f.BotCasting)
            return Make(Action::None, "busy");
        if (f.SavedPetDead)
        {
            if (f.KnowsRevive && Elapsed(f.NowMs, f.LastReviveMs, cfg.RetryMs))
                return Make(Action::Revive, "pet_dead");
            return Make(Action::None, f.KnowsRevive ? "revive_wait" : "revive_unknown");
        }
        if (!f.KnowsCall)
            return Make(Action::None, "call_unknown");
        if (Elapsed(f.NowMs, f.LastCallMs, CallWait(f, cfg)))
            return Make(Action::Call, "pet_missing");
        return Make(Action::None, "call_wait");
    }

    // --- a pet unit exists but is dead ---
    if (!f.PetAlive)
    {
        if (f.BotInCombat || f.BotCasting)
            return Make(Action::None, "busy");
        if (f.KnowsRevive && Elapsed(f.NowMs, f.LastReviveMs, cfg.RetryMs))
            return Make(Action::Revive, "pet_dead");
        return Make(Action::None, f.KnowsRevive ? "revive_wait" : "revive_unknown");
    }

    // --- alive ---
    if (!f.BotInCombat && f.PetDistance > cfg.LostDistance)
        return Make(Action::Dismiss, "pet_lost");   // the caller calls it again on a later pass

    // healing first: it is a cast, and the pet keeps the bot alive
    if (f.KnowsMend && !f.BotCasting && f.PetHealthPct > 0 && Elapsed(f.NowMs, f.LastMendMs, cfg.MendIntervalMs))
    {
        int32 const below = f.BotInCombat || f.PetInCombat ? int32(cfg.MendBelowPct) : 80;
        if (f.PetHealthPct < below && f.PetDistance <= 40.0f)
            return Make(Action::Mend, f.BotInCombat ? "pet_hurt_fight" : "pet_hurt");
    }

    if (f.BotHolding)
        return f.PetCommand == Command::Stay ? Make(Action::None, "holding") : Make(Action::Stay, "bot_holds");

    if (f.BotInCombat || f.BotHasTarget)
    {
        if (f.BotHasTarget && !f.PetAttacksTarget)
            return Make(Action::Attack, "assist");
        if (!f.BotHasTarget && f.PetCommand == Command::Attack && !f.PetInCombat)
            return Make(Action::Follow, "fight_over");
        return Make(Action::None, "fighting");
    }

    // out of combat: a pet that still attacks after the fight, or is parked, comes back
    if (f.PetCommand != Command::Follow && !f.PetInCombat)
        return Make(Action::Follow, "regroup");

    // feeding: the pet has to be close, nothing in combat, and a feed in progress is left alone
    if (f.KnowsFeed && f.HasFood && !f.PetFeeding && !f.PetInCombat && !f.BotCasting && f.HappinessPct >= 0
        && f.HappinessPct < int32(cfg.FeedBelowPct) && Elapsed(f.NowMs, f.LastFeedMs, cfg.FeedIntervalMs))
    {
        if (f.PetDistance > cfg.FeedRange)
            return Make(Action::Follow, "feed_close_in");
        return Make(Action::Feed, "unhappy");
    }
    return Make(Action::None, "idle");
}

bool DismissOn(Event e, bool petSummoned)
{
    if (!petSummoned)
        return false;
    switch (e)
    {
        case Event::Logout:
        case Event::MapChange:
        case Event::EnteredBattleground:
        case Event::LevelBelowMinimum:
            return true;
        case Event::BotDied:
            return false;   // the core already unsummons the pet of a dead owner and brings it back with the corpse run
    }
    return false;
}

int32 PickFood(std::span<FoodCandidate const> foods)
{
    int32 best = -1;
    for (size_t i = 0; i < foods.size(); ++i)
    {
        FoodCandidate const& f = foods[i];
        if (!f.InDiet || !f.Count || f.Benefit <= 0)
            continue;
        if (best < 0)
        {
            best = int32(i);
            continue;
        }
        FoodCandidate const& b = foods[size_t(best)];
        if (f.Benefit != b.Benefit ? f.Benefit > b.Benefit : f.ItemLevel != b.ItemLevel ? f.ItemLevel < b.ItemLevel : f.Count > b.Count)
            best = int32(i);
    }
    return best;
}

int32 HappinessPct(int32 power, int32 maxPower)
{
    if (maxPower <= 0)
        return -1;
    return std::clamp<int32>(int32(int64(power) * 100 / maxPower), 0, 100);
}

Autocast AutocastPolicy(std::string_view n)
{
    static constexpr std::array<std::string_view, 8> on = { "Claw", "Bite", "Growl", "Lightning Breath", "Scorpid Poison", "Screech", "Thunderstomp", "Charge" };
    static constexpr std::array<std::string_view, 7> off = { "Cower", "Dash", "Dive", "Prowl", "Furious Howl", "Shell Shield", "Phase Shift" };
    for (std::string_view s : on)
        if (n == s)
            return Autocast::On;
    for (std::string_view s : off)
        if (n == s)
            return Autocast::Off;
    return Autocast::Leave;
}

std::span<TamingQuest const> TamingQuests()
{
    // rod channel spells and beasts as named in classic_spell_scripts.cpp (Taming Rod scripts)
    static constexpr TamingQuest table[] =
    {
        { 94978, 1280003, "Windsong Crawler" },
        { 94979, 1280046, "Ornery Galestrider" },
        { 94013, 1271103, "Vuldren Alpha" },
        { 94792, 1277794, "Rockhide Boar" },
        { 94863, 1278028, "Gray Forest Wolf" },
        { 94864, 1278029, "Young Forest Bear" },
    };
    return table;
}

TamingQuest const* FindTamingQuest(uint32 questId)
{
    for (TamingQuest const& q : TamingQuests())
        if (q.Quest == questId)
            return &q;
    return nullptr;
}

int32 PickTameTarget(std::span<TameCandidate const> c, uint8 botLevel)
{
    int32 best = -1;
    for (size_t i = 0; i < c.size(); ++i)
    {
        TameCandidate const& t = c[i];
        if (!t.Alive || t.InCombat || t.Elite || !t.Tameable || !t.NameMatches || !t.Reachable || t.Level > botLevel)
            continue;
        if (best < 0 || t.Distance < c[size_t(best)].Distance)
            best = int32(i);
    }
    return best;
}

TameDecision DecideTame(TameFacts const& f, TameConfig const& cfg)
{
    if (f.Tamed)
        return { TameStep::Done, "tamed" };
    if (f.ElapsedMs > cfg.MaxMs)
        return { TameStep::Abort, "timeout" };
    if (f.Casts > cfg.MaxCasts && !f.ChannelActive)
        return { TameStep::Abort, "too_many_casts" };
    if (f.BotInCombat)
        return { TameStep::Abort, "bot_in_combat" };   // the combat engine owns the bot; the channel breaks anyway
    if (!f.TargetValid || !f.TargetAlive)
        return { TameStep::Abort, "target_gone" };
    if (f.TargetInCombatWithOther)
        return { TameStep::Abort, "target_busy" };
    if (f.ChannelActive)
        return { TameStep::Channel, "channel" };       // stand still, nothing may interrupt the 20 s
    if (f.Distance > cfg.MaxTargetDrift + cfg.CastRange)
        return { TameStep::Abort, "target_far" };
    if (f.Distance > cfg.CastRange || !f.LineOfSight)
        return { TameStep::Approach, f.LineOfSight ? "out_of_range" : "no_line_of_sight" };
    return { TameStep::Cast, "in_range" };
}
}
