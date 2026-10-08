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

#ifndef TRINITY_BOT_PET_LOGIC_H
#define TRINITY_BOT_PET_LOGIC_H

// Decision logic of the hunter bot pet (BotPet.cpp). Everything here is a pure function over plain data so it can be unit tested
// without a map, a database or a Player: the glue in BotPet.cpp fills the Facts structs from the game state, calls a function and
// carries out the answer. See docs/playerbots/feature-bot-pets-movement-loot-20261008.md.

#include "Define.h"
#include <span>
#include <string_view>
#include <vector>

namespace BotPetLogic
{
    enum class Command : uint8 { Follow, Stay, Attack, Other };

    enum class Action : uint8
    {
        None,
        Call,      // Call Pet
        Revive,    // Revive Pet
        Dismiss,   // Dismiss Pet
        Feed,      // Feed Pet
        Mend,      // Mend Pet
        Attack,    // pet command: attack the bot's target
        Follow,    // pet command: follow the bot
        Stay       // pet command: stay where it is
    };

    struct Config
    {
        uint32 RetryMs = 8000;           // wait between two attempts of the same summon/revive (grows with failures)
        uint32 FeedBelowPct = 66;        // feed when happiness is below this percent (66 = anything under "happy")
        uint32 FeedIntervalMs = 30000;   // minimum time between two Feed Pet casts
        uint32 MendBelowPct = 60;        // Mend Pet below this pet health percent (in combat), 80 out of combat
        uint32 MendIntervalMs = 15000;
        float FeedRange = 10.0f;         // the pet must be this close for Feed Pet
        float LostDistance = 150.0f;     // a pet this far away (stuck, other level) is dismissed and called again
        uint32 MaxCallFails = 6;         // consecutive failed summons before the bot stops trying for a long while
        uint32 LongWaitMs = 120000;      // wait after MaxCallFails
    };

    struct Facts
    {
        // the bot
        bool BotAlive = true;
        bool BotInCombat = false;
        bool BotMounted = false;         // mounted, in flight or on a taxi
        bool BotCasting = false;
        bool BotHolding = false;         // must stand still (taming channel, eating): the pet stays too
        bool BotHasTarget = false;       // hostile target of the bot's fight
        bool KnowsCall = false;
        bool KnowsRevive = false;
        bool KnowsFeed = false;
        bool KnowsMend = false;
        bool HasFood = false;            // at least one usable food item in the bags
        // the pet
        bool HasSavedPet = false;        // the stable data holds a current pet
        bool SavedPetDead = false;       // ... and it has no health
        bool PetSummoned = false;        // a pet unit is in the world
        bool PetAlive = false;
        bool PetFeeding = false;         // the feed aura is on the pet
        bool PetInCombat = false;
        bool PetAttacksTarget = false;   // the pet's victim is the bot's target
        Command PetCommand = Command::Follow;
        float PetDistance = 0.0f;
        int32 PetHealthPct = 100;
        int32 HappinessPct = -1;         // 0..100, -1 = unknown
        // clocks (AI clock, ms) and counters kept by the caller
        uint32 NowMs = 0;
        uint32 LastCallMs = 0;           // 0 = never
        uint32 LastReviveMs = 0;
        uint32 LastFeedMs = 0;
        uint32 LastMendMs = 0;
        uint32 CallFails = 0;
    };

    struct Decision
    {
        Action Act = Action::None;
        char const* Reason = "";
    };

    // True when `lastMs` was long enough ago (0 = never done).
    TC_GAME_API bool Elapsed(uint32 nowMs, uint32 lastMs, uint32 waitMs);

    // The one thing the bot should do for its pet right now. Called about once a second, out of combat and in combat.
    TC_GAME_API Decision Decide(Facts const& f, Config const& cfg);

    // Cases that end a pet's stay in the world on purpose, answered by the caller's event (logout, map change, death). Pure so the
    // table is tested: a pet is only ever dismissed (saved as the current pet) for these.
    enum class Event : uint8 { Logout, MapChange, BotDied, EnteredBattleground, LevelBelowMinimum };
    TC_GAME_API bool DismissOn(Event e, bool petSummoned);

    // --- feeding ---
    struct FoodCandidate
    {
        uint32 Entry = 0;
        uint32 Count = 0;
        int32 Benefit = 0;       // happiness per Feed Pet tick (Pet::GetFoodBenefit), 0 = too low level
        bool InDiet = false;
        uint32 ItemLevel = 0;
    };
    // The food to give: highest benefit, then the lowest item level (cheapest), then the largest stack. -1 when none is usable.
    TC_GAME_API int32 PickFood(std::span<FoodCandidate const> foods);

    // happiness in percent from the raw power value (Pet::HAPPINESS_MAX = 1000000)
    TC_GAME_API int32 HappinessPct(int32 power, int32 maxPower);

    // --- pet abilities ---
    enum class Autocast : uint8 { On, Off, Leave };
    // Policy for a pet spell by its English name: damage and threat abilities on, utility and escape abilities off, unknown ones untouched.
    TC_GAME_API Autocast AutocastPolicy(std::string_view spellName);

    // --- taming ---
    struct TamingQuest
    {
        uint32 Quest;
        uint32 RodSpell;          // channel spell of the Taming Rod (20 s)
        char const* BeastName;    // creature the rod works on
    };
    // The Classic taming quests the bots support: the Taming Rod quests of the data (docs/playerbots/quest-design.md, NEEDS_EVENT).
    TC_GAME_API std::span<TamingQuest const> TamingQuests();
    TC_GAME_API TamingQuest const* FindTamingQuest(uint32 questId);
    // Tame Beast (1515): general taming of a beast for a hunter that has no pet.
    constexpr uint32 SPELL_TAME_BEAST = 1515;

    struct TameCandidate
    {
        uint64 Id = 0;
        uint8 Level = 0;
        float Distance = 0.0f;
        bool Alive = true;
        bool InCombat = false;
        bool Elite = false;
        bool NameMatches = true;     // the wanted beast (rod quests); true when any tameable beast will do
        bool Tameable = true;        // beast family the bot may tame
        bool Reachable = true;       // the caller's navmesh check, when it ran one
    };
    // Index of the target to tame, -1 when none qualifies: alive, idle, not elite, level not above the bot, matching, nearest first.
    TC_GAME_API int32 PickTameTarget(std::span<TameCandidate const> candidates, uint8 botLevel);

    enum class TameStep : uint8 { Approach, Cast, Channel, Done, Abort };
    struct TameConfig
    {
        float CastRange = 25.0f;
        uint32 MaxMs = 120000;       // whole attempt
        uint32 MaxCasts = 4;
        float MaxTargetDrift = 45.0f;
    };
    struct TameFacts
    {
        bool TargetValid = false;
        bool TargetAlive = true;
        bool TargetInCombatWithOther = false;
        float Distance = 0.0f;
        bool LineOfSight = true;
        bool ChannelActive = false;  // the bot is channelling the tame spell
        bool Tamed = false;          // pet appeared / quest credit
        bool BotInCombat = false;
        uint32 ElapsedMs = 0;
        uint32 Casts = 0;
    };
    struct TameDecision
    {
        TameStep Step = TameStep::Abort;
        char const* Reason = "";
    };
    TC_GAME_API TameDecision DecideTame(TameFacts const& f, TameConfig const& cfg);
}

#endif
