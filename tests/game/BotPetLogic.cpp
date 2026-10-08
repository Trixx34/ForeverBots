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

#include "BotPetLogic.h"
#include "Pet.h"
#include <vector>

using namespace BotPetLogic;

namespace
{
Facts Calm()
{
    Facts f;
    f.NowMs = 100000;
    f.HasSavedPet = true;
    f.PetSummoned = true;
    f.PetAlive = true;
    f.KnowsCall = f.KnowsRevive = f.KnowsFeed = f.KnowsMend = true;
    f.HasFood = true;
    f.HappinessPct = 100;
    f.PetDistance = 5.0f;
    return f;
}
Config const cfg;
}

TEST_CASE("BotPet: summon, revive and dismiss", "[BotPet]")
{
    Facts f = Calm();

    SECTION("dead bot and mounted bot do nothing")
    {
        f.BotAlive = false;
        CHECK(Decide(f, cfg).Act == Action::None);
        f = Calm();
        f.BotMounted = true;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("pet not in the world is called")
    {
        f.PetSummoned = false;
        CHECK(Decide(f, cfg).Act == Action::Call);
    }

    SECTION("no saved pet, nothing to call")
    {
        f.PetSummoned = false;
        f.HasSavedPet = false;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("call is not tried in combat, while casting, or before the spell is known")
    {
        f.PetSummoned = false;
        f.BotInCombat = true;
        CHECK(Decide(f, cfg).Act == Action::None);
        f.BotInCombat = false;
        f.BotCasting = true;
        CHECK(Decide(f, cfg).Act == Action::None);
        f.BotCasting = false;
        f.KnowsCall = false;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("failed calls wait longer and longer, then a long while")
    {
        f.PetSummoned = false;
        f.LastCallMs = f.NowMs - cfg.RetryMs;
        CHECK(Decide(f, cfg).Act == Action::Call);          // no failure yet: one retry interval is enough
        f.CallFails = 2;
        CHECK(Decide(f, cfg).Act == Action::None);          // now needs three intervals
        f.LastCallMs = f.NowMs - 3 * cfg.RetryMs;
        CHECK(Decide(f, cfg).Act == Action::Call);
        f.CallFails = cfg.MaxCallFails;
        CHECK(Decide(f, cfg).Act == Action::None);
        f.LastCallMs = f.NowMs - cfg.LongWaitMs;
        CHECK(Decide(f, cfg).Act == Action::Call);
    }

    SECTION("saved pet is dead: revive, with a retry interval")
    {
        f.PetSummoned = false;
        f.SavedPetDead = true;
        CHECK(Decide(f, cfg).Act == Action::Revive);
        f.LastReviveMs = f.NowMs - 1000;
        CHECK(Decide(f, cfg).Act == Action::None);
        f.LastReviveMs = f.NowMs - cfg.RetryMs;
        CHECK(Decide(f, cfg).Act == Action::Revive);
        f.KnowsRevive = false;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("dead pet unit is revived out of combat only")
    {
        f.PetAlive = false;
        CHECK(Decide(f, cfg).Act == Action::Revive);
        f.BotInCombat = true;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("a pet that is far away is dismissed so it can be called again")
    {
        f.PetDistance = cfg.LostDistance + 1.0f;
        CHECK(Decide(f, cfg).Act == Action::Dismiss);
        f.BotInCombat = true;
        CHECK(Decide(f, cfg).Act != Action::Dismiss);
    }
}

TEST_CASE("BotPet: dismiss events", "[BotPet]")
{
    CHECK(DismissOn(Event::Logout, true));
    CHECK(DismissOn(Event::MapChange, true));
    CHECK(DismissOn(Event::EnteredBattleground, true));
    CHECK(DismissOn(Event::LevelBelowMinimum, true));
    CHECK_FALSE(DismissOn(Event::BotDied, true));
    // nothing to dismiss when no pet is out: this is what keeps logout from creating a pet or leaking a state
    CHECK_FALSE(DismissOn(Event::Logout, false));
    CHECK_FALSE(DismissOn(Event::MapChange, false));
}

TEST_CASE("BotPet: assist, follow and stay", "[BotPet]")
{
    Facts f = Calm();

    SECTION("pet attacks the bot's target")
    {
        f.BotInCombat = true;
        f.BotHasTarget = true;
        CHECK(Decide(f, cfg).Act == Action::Attack);
        f.PetAttacksTarget = true;
        f.PetCommand = Command::Attack;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("pet stops attacking when the fight is over")
    {
        f.BotInCombat = true;
        f.BotHasTarget = false;
        f.PetCommand = Command::Attack;
        CHECK(Decide(f, cfg).Act == Action::Follow);
        f.PetInCombat = true;
        CHECK(Decide(f, cfg).Act == Action::None);   // still fighting something: leave it
    }

    SECTION("out of combat a parked or attacking pet comes back")
    {
        f.PetCommand = Command::Stay;
        CHECK(Decide(f, cfg).Act == Action::Follow);
        f.PetCommand = Command::Follow;
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("a holding bot (taming channel) keeps the pet where it is")
    {
        f.BotHolding = true;
        CHECK(Decide(f, cfg).Act == Action::Stay);
        f.PetCommand = Command::Stay;
        CHECK(Decide(f, cfg).Act == Action::None);
    }
}

TEST_CASE("BotPet: feeding and healing", "[BotPet]")
{
    Facts f = Calm();

    SECTION("unhappy pet near the bot is fed")
    {
        f.HappinessPct = 20;
        CHECK(Decide(f, cfg).Act == Action::Feed);
    }

    SECTION("content pet is fed up to the happy threshold only")
    {
        f.HappinessPct = 50;
        CHECK(Decide(f, cfg).Act == Action::Feed);
        f.HappinessPct = int32(cfg.FeedBelowPct);
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("no food, spell unknown, feed in progress, in combat or recently fed: no feeding")
    {
        f.HappinessPct = 10;
        f.HasFood = false;
        CHECK(Decide(f, cfg).Act == Action::None);
        f = Calm(); f.HappinessPct = 10; f.KnowsFeed = false;
        CHECK(Decide(f, cfg).Act == Action::None);
        f = Calm(); f.HappinessPct = 10; f.PetFeeding = true;
        CHECK(Decide(f, cfg).Act == Action::None);
        f = Calm(); f.HappinessPct = 10; f.PetInCombat = true;
        CHECK(Decide(f, cfg).Act != Action::Feed);
        f = Calm(); f.HappinessPct = 10; f.LastFeedMs = f.NowMs - 1000;
        CHECK(Decide(f, cfg).Act == Action::None);
        f = Calm(); f.HappinessPct = -1;   // unknown happiness is never fed blindly
        CHECK(Decide(f, cfg).Act == Action::None);
    }

    SECTION("a far pet is called in first")
    {
        f.HappinessPct = 10;
        f.PetDistance = cfg.FeedRange + 5.0f;
        CHECK(Decide(f, cfg).Act == Action::Follow);
    }

    SECTION("mend when hurt, with a cooldown")
    {
        f.BotInCombat = true;
        f.PetInCombat = true;
        f.PetHealthPct = 40;
        CHECK(Decide(f, cfg).Act == Action::Mend);
        f.LastMendMs = f.NowMs - 2000;
        CHECK(Decide(f, cfg).Act != Action::Mend);
        f.LastMendMs = 0;
        f.PetHealthPct = 90;
        CHECK(Decide(f, cfg).Act != Action::Mend);
    }

    SECTION("out of combat a pet below 80 percent is mended")
    {
        f.PetHealthPct = 70;
        CHECK(Decide(f, cfg).Act == Action::Mend);
        f.PetHealthPct = 85;
        CHECK(Decide(f, cfg).Act == Action::None);
    }
}

TEST_CASE("BotPet: food choice", "[BotPet]")
{
    SECTION("highest benefit first, then cheapest, then biggest stack")
    {
        std::vector<FoodCandidate> foods = {
            { 1, 5, 17000, true, 10 },
            { 2, 5, 35000, true, 30 },
            { 3, 5, 35000, true, 20 },
            { 4, 9, 35000, true, 20 },
        };
        CHECK(PickFood(foods) == 3);
    }

    SECTION("food outside the diet, empty stacks and zero benefit are never picked")
    {
        std::vector<FoodCandidate> foods = {
            { 1, 5, 35000, false, 10 },
            { 2, 0, 35000, true, 10 },
            { 3, 5, 0, true, 10 },
        };
        CHECK(PickFood(foods) == -1);
        CHECK(PickFood({}) == -1);
    }

    SECTION("the benefit tiers of the core")
    {
        // the glue fills Benefit from Pet::GetFoodBenefit; make sure the tiers the tests assume are the real ones
        CHECK(Pet::GetFoodBenefit(20, 15) == 35000);
        CHECK(Pet::GetFoodBenefit(20, 12) == 17000);
        CHECK(Pet::GetFoodBenefit(20, 8) == 8000);
        CHECK(Pet::GetFoodBenefit(40, 10) == 0);
    }
}

TEST_CASE("BotPet: happiness and abilities", "[BotPet]")
{
    CHECK(HappinessPct(0, Pet::HAPPINESS_MAX) == 0);
    CHECK(HappinessPct(Pet::HAPPINESS_MAX, Pet::HAPPINESS_MAX) == 100);
    CHECK(HappinessPct(Pet::HAPPINESS_TAMED, Pet::HAPPINESS_MAX) == 16);
    CHECK(HappinessPct(5, 0) == -1);

    CHECK(AutocastPolicy("Claw") == Autocast::On);
    CHECK(AutocastPolicy("Bite") == Autocast::On);
    CHECK(AutocastPolicy("Growl") == Autocast::On);
    CHECK(AutocastPolicy("Cower") == Autocast::Off);
    CHECK(AutocastPolicy("Dash") == Autocast::Off);
    CHECK(AutocastPolicy("Prowl") == Autocast::Off);
    CHECK(AutocastPolicy("Something Else") == Autocast::Leave);
}

TEST_CASE("BotPet: taming quests and targets", "[BotPet]")
{
    SECTION("the taming quest table")
    {
        TamingQuest const* q = FindTamingQuest(94978);
        REQUIRE(q != nullptr);
        CHECK(q->RodSpell == 1280003);
        CHECK(std::string(q->BeastName) == "Windsong Crawler");
        CHECK(FindTamingQuest(1) == nullptr);
        CHECK(TamingQuests().size() == 6);
        for (TamingQuest const& t : TamingQuests())
            CHECK(FindTamingQuest(t.Quest) == &t);   // no duplicate quest ids
    }

    SECTION("target choice")
    {
        std::vector<TameCandidate> c(5);
        c[0] = { 10, 8, 30.0f };
        c[1] = { 11, 8, 12.0f };
        c[2] = { 12, 8, 5.0f };  c[2].Elite = true;
        c[3] = { 13, 12, 6.0f };                       // above the bot
        c[4] = { 14, 8, 7.0f };  c[4].InCombat = true;
        CHECK(PickTameTarget(c, 10) == 1);

        c[1].NameMatches = false;
        CHECK(PickTameTarget(c, 10) == 0);
        c[0].Tameable = false;
        CHECK(PickTameTarget(c, 10) == -1);
        c[3].Level = 10;
        CHECK(PickTameTarget(c, 10) == 3);
        c[3].Alive = false;
        CHECK(PickTameTarget(c, 10) == -1);
        CHECK(PickTameTarget({}, 10) == -1);
    }

    SECTION("tame steps")
    {
        TameConfig tc;
        TameFacts f;
        f.TargetValid = true;
        f.Distance = 40.0f;
        CHECK(DecideTame(f, tc).Step == TameStep::Approach);
        f.Distance = 20.0f;
        CHECK(DecideTame(f, tc).Step == TameStep::Cast);
        f.LineOfSight = false;
        CHECK(DecideTame(f, tc).Step == TameStep::Approach);
        f.LineOfSight = true;
        f.ChannelActive = true;
        f.Distance = 60.0f;
        CHECK(DecideTame(f, tc).Step == TameStep::Channel);   // the channel is never interrupted by distance logic
        f.BotInCombat = true;
        CHECK(DecideTame(f, tc).Step == TameStep::Abort);
        f.BotInCombat = false;
        f.Tamed = true;
        CHECK(DecideTame(f, tc).Step == TameStep::Done);
        f.Tamed = false;
        f.ElapsedMs = tc.MaxMs + 1;
        CHECK(DecideTame(f, tc).Step == TameStep::Abort);
        f.ElapsedMs = 0;
        f.ChannelActive = false;
        f.Casts = tc.MaxCasts + 1;
        CHECK(DecideTame(f, tc).Step == TameStep::Abort);
        f.Casts = 0;
        f.TargetInCombatWithOther = true;
        CHECK(DecideTame(f, tc).Step == TameStep::Abort);
        f.TargetInCombatWithOther = false;
        f.TargetAlive = false;
        CHECK(DecideTame(f, tc).Step == TameStep::Abort);
    }
}
