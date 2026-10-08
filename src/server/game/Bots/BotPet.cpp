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

// Hunter bot pets, see BotPet.h. Layout:
//   1. config and small helpers
//   2. per-bot state (pet_ctx value)
//   3. pet upkeep: pet_tick action (summon, revive, feed, mend, commands, autocast set)
//   4. taming: pet_tame action (Taming Rod quests and the first pet with Tame Beast)
//   5. registration of the "pet" strategy

#include "BotPet.h"
#include "BotAI.h"
#include "BotPetLogic.h"
#include "CharmInfo.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace BotPet
{
namespace
{
using Trinity::StringFormat;
namespace Logic = BotPetLogic;

constexpr uint32 SPELL_CALL_PET = 883;
constexpr uint32 SPELL_REVIVE_PET = 982;
constexpr uint32 SPELL_FEED_PET = 6991;
constexpr uint32 SPELL_MEND_PET = 136;            // root of the Mend Pet ranks
constexpr uint32 AURA_FEED_PET_EFFECT = 1539;
constexpr uint32 TAME_RUN_SECONDS = 150;

// ---------------------------------------------------------------------------------------------------------------------
// 1. config and helpers
// ---------------------------------------------------------------------------------------------------------------------
Config s_cfg;
std::once_flag s_cfgOnce;

bool IsHunter(Player const* bot) { return bot->GetClass() == CLASS_HUNTER; }

uint32 HighestRank(Player* bot, uint32 root)
{
    if (!bot->HasSpell(root))
        return 0;
    uint32 id = root;
    for (uint32 guard = 0; guard < 16; ++guard)
    {
        uint32 const next = sSpellMgr->GetNextSpellInChain(id);
        if (!next || !bot->HasSpell(next))
            break;
        id = next;
    }
    return id;
}

PetStable::PetInfo const* SavedPet(Player* bot)
{
    PetStable const* ps = bot->GetPetStable();
    if (!ps)
        return nullptr;
    PetStable::PetInfo const* cur = ps->GetCurrentPet();
    return cur && cur->Type == HUNTER_PET ? cur : nullptr;
}

bool HasAnyHunterPet(Player* bot)
{
    PetStable const* ps = bot->GetPetStable();
    if (!ps)
        return false;
    for (Optional<PetStable::PetInfo> const& p : ps->ActivePets)
        if (p && p->Type == HUNTER_PET)
            return true;
    for (Optional<PetStable::PetInfo> const& p : ps->StabledPets)
        if (p && p->Type == HUNTER_PET)
            return true;
    for (PetStable::PetInfo const& p : ps->UnslottedPets)
        if (p.Type == HUNTER_PET)
            return true;
    return false;
}

Pet* HunterPet(Player* bot)
{
    Pet* pet = bot->GetPet();
    return pet && pet->getPetType() == HUNTER_PET ? pet : nullptr;
}

// the bot's current fight target, if any
Unit* FightTarget(Player* bot)
{
    if (Unit* v = bot->GetVictim())
        return v;
    if (!bot->IsInCombat())
        return nullptr;
    Unit* sel = ObjectAccessor::GetUnit(*bot, bot->GetTarget());
    return sel && sel->IsAlive() && bot->IsValidAttackTarget(sel) ? sel : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// 2. per-bot state
// ---------------------------------------------------------------------------------------------------------------------
class PetCtx : public UntypedValue
{
public:
    explicit PetCtx(BotAI* ai) : UntypedValue(ai, "pet_ctx", 0) { }

    // upkeep
    uint32 LastCallMs = 0, LastReviveMs = 0, LastFeedMs = 0, LastMendMs = 0, CallFails = 0, NextCmdMs = 0;
    uint32 AbilityPet = 0;           // pet number whose autocast set was applied
    uint32 LastDecisionLogMs = 0;

    // taming run
    struct Tame
    {
        bool Active = false;
        bool General = false;        // false: a Taming Rod quest
        uint32 Quest = 0;
        uint32 Spell = 0;            // rod channel or Tame Beast
        char Beast[40] = "";         // name of the wanted beast (rod quests)
        ObjectGuid Target;
        uint32 StartMs = 0, Casts = 0, LastCastMs = 0, LastGoalMs = 0;
        bool WasChannelling = false;
    } T;
    uint32 TameCoolUntilMs = 0;
    uint32 NextScanMs = 0;
    struct Black { ObjectGuid Guid; uint32 UntilMs; };
    std::vector<Black> TameBlack;    // beasts that failed or were busy, skipped for a while (bounded)

    void Reset()
    {
        LastCallMs = LastReviveMs = LastFeedMs = LastMendMs = CallFails = NextCmdMs = 0;
        AbilityPet = 0;
        LastDecisionLogMs = 0;
        T = Tame();
        TameCoolUntilMs = 0;
        NextScanMs = 0;
        TameBlack.clear();
    }

    bool Blacklisted(ObjectGuid g, uint32 now) const
    {
        for (Black const& b : TameBlack)
            if (b.Guid == g && int32(b.UntilMs - now) > 0)
                return true;
        return false;
    }
    void Blacklist(ObjectGuid g, uint32 now, uint32 ms)
    {
        std::erase_if(TameBlack, [now](Black const& b) { return int32(b.UntilMs - now) <= 0; });
        if (TameBlack.size() >= 8)
            TameBlack.erase(TameBlack.begin());
        TameBlack.push_back({ g, now + ms });
    }
};

PetCtx* Ctx(BotAI* ai) { return static_cast<PetCtx*>(ai->GetValueRaw("pet_ctx")); }

void Emit(BotAI* ai, Player* bot, char const* reason, std::string summary, std::string details = std::string(), uint8 sev = BOTLOG_INFO)
{
    ai->EmitEvent(bot, "decision", sev, reason, std::move(summary), std::move(details));
}

// ---------------------------------------------------------------------------------------------------------------------
// 3. upkeep
// ---------------------------------------------------------------------------------------------------------------------
Logic::Config LogicConfig()
{
    Logic::Config lc;
    lc.RetryMs = s_cfg.RetrySec * 1000;
    lc.FeedBelowPct = s_cfg.FeedBelowPct;
    lc.MendBelowPct = s_cfg.MendBelowPct;
    return lc;
}

// Feed Pet candidates from the bags (only called when the pet is hungry and a feed is due).
bool FindFood(Player* bot, Pet* pet, Item*& itemOut)
{
    std::vector<Logic::FoodCandidate> foods;
    std::vector<Item*> items;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && proto->GetClass() == ITEM_CLASS_CONSUMABLE && pet->HaveInDiet(proto))
        {
            foods.push_back({ item->GetEntry(), item->GetCount(), Pet::GetFoodBenefit(pet->GetLevel(), proto->GetBaseItemLevel()), true, uint32(proto->GetBaseItemLevel()) });
            items.push_back(item);
        }
        return ItemSearchCallbackResult::Continue;
    });
    int32 const pick = Logic::PickFood(foods);
    if (pick < 0)
        return false;
    itemOut = items[size_t(pick)];
    return true;
}

// Autocast of the pet's abilities, once per pet: damage and threat on, utility and escape off (see BotPetLogic::AutocastPolicy).
void ApplyAbilities(Pet* pet, PetCtx& c)
{
    CharmInfo* ci = pet->GetCharmInfo();
    if (!ci || c.AbilityPet == ci->GetPetNumber())
        return;
    c.AbilityPet = ci->GetPetNumber();
    for (uint8 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
    {
        UnitActionBarEntry const* e = ci->GetActionBarEntry(i);
        if (!e || !e->IsActionBarForSpell() || !e->GetAction())
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(e->GetAction(), DIFFICULTY_NONE);
        if (!si || !si->SpellName)
            continue;
        bool const on = e->GetType() == ACT_ENABLED;
        switch (Logic::AutocastPolicy((*si->SpellName)[DEFAULT_LOCALE]))
        {
            case Logic::Autocast::On:
                if (!on)
                    pet->ToggleAutocast(si, true);
                break;
            case Logic::Autocast::Off:
                if (on)
                    pet->ToggleAutocast(si, false);
                break;
            case Logic::Autocast::Leave:
                break;
        }
    }
}

Logic::Command CommandOf(Pet* pet)
{
    CharmInfo* ci = pet->GetCharmInfo();
    if (!ci)
        return Logic::Command::Other;
    switch (ci->GetCommandState())
    {
        case COMMAND_FOLLOW: return Logic::Command::Follow;
        case COMMAND_STAY: return Logic::Command::Stay;
        case COMMAND_ATTACK: return Logic::Command::Attack;
        default: return Logic::Command::Other;
    }
}

void PetCommand(Player* bot, Pet* pet, uint32 command, Unit* target)
{
    bot->GetSession()->HandlePetActionHelper(pet, pet->GetGUID(), command, ACT_COMMAND, target ? target->GetGUID() : ObjectGuid::Empty,
        target ? target->GetPosition() : Position());
}

class PetTickAction : public Action
{
public:
    explicit PetTickAction(BotAI* ai) : Action(ai, "pet_tick", ACTION_FLAG_QUIET_LOG) { }

    bool IsPossible() override
    {
        Player* bot = GetBot();
        return s_cfg.Enabled && IsHunter(bot) && bot->IsAlive() && bot->GetLevel() >= s_cfg.MinLevel;
    }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        PetCtx* c = Ctx(ai);
        if (!c)
            return false;
        uint32 const now = ai->GetNowMs();
        Pet* pet = HunterPet(bot);
        Unit* target = FightTarget(bot);

        Logic::Facts f;
        f.NowMs = now;
        f.BotAlive = true;
        f.BotInCombat = bot->IsInCombat();
        f.BotMounted = bot->IsMounted() || bot->IsInFlight();
        f.BotCasting = bot->IsNonMeleeSpellCast(false, false, true);
        f.BotHolding = c->T.Active || ai->Rest().Resting();
        f.BotHasTarget = target != nullptr;
        f.KnowsCall = bot->HasSpell(SPELL_CALL_PET);
        f.KnowsRevive = bot->HasSpell(SPELL_REVIVE_PET);
        f.KnowsFeed = s_cfg.Feed && bot->HasSpell(SPELL_FEED_PET);
        uint32 const mendRank = HighestRank(bot, SPELL_MEND_PET);
        f.KnowsMend = mendRank != 0;
        PetStable::PetInfo const* saved = SavedPet(bot);
        f.HasSavedPet = saved != nullptr;
        f.SavedPetDead = saved && saved->Health == 0;
        f.PetSummoned = pet != nullptr;
        f.LastCallMs = c->LastCallMs;
        f.LastReviveMs = c->LastReviveMs;
        f.LastFeedMs = c->LastFeedMs;
        f.LastMendMs = c->LastMendMs;
        f.CallFails = c->CallFails;
        if (pet)
        {
            c->CallFails = 0;
            f.CallFails = 0;
            f.PetAlive = pet->IsAlive();
            f.PetInCombat = pet->IsInCombat();
            f.PetCommand = CommandOf(pet);
            f.PetDistance = bot->GetDistance(pet);
            f.PetHealthPct = int32(pet->GetHealthPct());
            f.PetFeeding = pet->HasAura(AURA_FEED_PET_EFFECT);
            f.PetAttacksTarget = target && pet->GetVictim() == target;
            f.HappinessPct = pet->HasHappiness() ? Logic::HappinessPct(pet->GetPower(POWER_HAPPINESS), pet->GetMaxPower(POWER_HAPPINESS)) : -1;
            if (s_cfg.Abilities && pet->IsAlive())
                ApplyAbilities(pet, *c);
        }

        // food is looked for only when a feed would be the answer
        Item* food = nullptr;
        if (pet && f.PetAlive && f.KnowsFeed && f.HappinessPct >= 0 && f.HappinessPct < int32(s_cfg.FeedBelowPct) && !f.BotInCombat
            && Logic::Elapsed(now, c->LastFeedMs, LogicConfig().FeedIntervalMs))
            f.HasFood = FindFood(bot, pet, food);

        Logic::Decision const d = Logic::Decide(f, LogicConfig());
        switch (d.Act)
        {
            case Logic::Action::None:
                return false;
            case Logic::Action::Call:
                return Cast(ai, bot, *c, d, SPELL_CALL_PET, bot, c->LastCallMs, "PET_CALL", "calling the pet", &c->CallFails);
            case Logic::Action::Revive:
                return Cast(ai, bot, *c, d, SPELL_REVIVE_PET, bot, c->LastReviveMs, "PET_REVIVE", "reviving the pet", nullptr);
            case Logic::Action::Mend:
                return Cast(ai, bot, *c, d, mendRank, pet, c->LastMendMs, "PET_MEND", "mending the pet", nullptr);
            case Logic::Action::Dismiss:
            {
                bot->RemovePet(pet, PET_SAVE_AS_CURRENT);
                c->LastCallMs = 0;
                SetResult("PET_DISMISS", "dismissed a pet that was out of reach", StringFormat(R"({{"why":"{}","distance":{:.0f}}})", d.Reason, f.PetDistance));
                return true;
            }
            case Logic::Action::Feed:
            {
                if (!food)
                    return false;
                BotMotion::Halt(bot);
                c->LastFeedMs = now ? now : 1;
                SpellCastResult const res = bot->CastSpell(CastSpellTargetArg(food), SPELL_FEED_PET, CastSpellExtraArgs(TRIGGERED_NONE));
                SetResult("PET_FEED", res == SPELL_CAST_OK ? "feeding the pet" : "feeding the pet failed",
                    StringFormat(R"({{"item":{},"result":{},"happiness_pct":{}}})", food->GetEntry(), uint32(res), f.HappinessPct));
                return res == SPELL_CAST_OK;
            }
            case Logic::Action::Attack:
                if (now < c->NextCmdMs)
                    return false;
                c->NextCmdMs = now + 1500;
                PetCommand(bot, pet, COMMAND_ATTACK, target);
                return false;   // a command takes no time: the combat strategy still acts this tick
            case Logic::Action::Follow:
                if (now < c->NextCmdMs)
                    return false;
                c->NextCmdMs = now + 3000;
                PetCommand(bot, pet, COMMAND_FOLLOW, nullptr);
                return false;
            case Logic::Action::Stay:
                if (now < c->NextCmdMs)
                    return false;
                c->NextCmdMs = now + 3000;
                PetCommand(bot, pet, COMMAND_STAY, nullptr);
                return false;
        }
        return false;
    }

private:
    bool Cast(BotAI* /*ai*/, Player* bot, PetCtx& c, Logic::Decision const& d, uint32 spell, Unit* target, uint32& lastMs, char const* reason, char const* what, uint32* fails)
    {
        uint32 const now = GetAI()->GetNowMs();
        lastMs = now ? now : 1;
        if (!spell)
            return false;
        BotMotion::Halt(bot);   // Call Pet and Revive Pet have a cast time, Mend Pet is a channel
        SpellCastResult const res = bot->CastSpell(target, spell, CastSpellExtraArgs(TRIGGERED_NONE));
        if (res != SPELL_CAST_OK && fails)
            ++*fails;
        SetResult(reason, res == SPELL_CAST_OK ? what : StringFormat("{} failed", what),
            StringFormat(R"({{"spell":{},"result":{},"why":"{}","fails":{}}})", spell, uint32(res), d.Reason, fails ? *fails : 0));
        (void)c;
        return res == SPELL_CAST_OK;
    }
};

class PetDueTrigger : public Trigger
{
public:
    explicit PetDueTrigger(BotAI* ai) : Trigger(ai, "pet_due", 1000) { }
    bool IsActive() override { return s_cfg.Enabled; }
};

// ---------------------------------------------------------------------------------------------------------------------
// 4. taming
// ---------------------------------------------------------------------------------------------------------------------
struct BeastSpawn
{
    uint32 Map;
    float X, Y, Z;
};
std::unordered_map<uint32, std::vector<BeastSpawn>> s_beastSpawns;   // taming quest -> spawn points of its beast (built once at start-up)
std::once_flag s_indexOnce;

bool ItemHasSpell(Item const* item, uint32 spell)
{
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return false;
    for (ItemEffectEntry const* eff : proto->Effects)
        if (eff && uint32(eff->SpellID) == spell)
            return true;
    return false;
}

Item* FindRod(Player* bot, uint32 spell)
{
    Item* found = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (ItemHasSpell(item, spell))
        {
            found = item;
            return ItemSearchCallbackResult::Stop;
        }
        return ItemSearchCallbackResult::Continue;
    });
    return found;
}

void EndTame(BotAI* ai, Player* bot, PetCtx& c, char const* reason, bool success)
{
    uint32 const now = ai->GetNowMs();
    if (bot && bot->IsInWorld())
    {
        if (c.T.WasChannelling || bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            bot->InterruptNonMeleeSpells(false, c.T.Spell);
        if (ai->Motion().HasGoal() && !std::strcmp(ai->Motion().GetTag(), "tame"))
            ai->Motion().ClearGoal();
        BotMotion::Halt(bot);
    }
    Emit(ai, bot, success ? "PET_TAME_DONE" : "PET_TAME_ABORT", StringFormat("taming {}: {}", success ? "finished" : "stopped", reason),
        StringFormat(R"({{"why":"{}","quest":{},"spell":{},"casts":{},"seconds":{},"general":{}}})", reason, c.T.Quest, c.T.Spell, c.T.Casts,
            (now - c.T.StartMs) / 1000, c.T.General ? "true" : "false"), success ? BOTLOG_INFO : BOTLOG_WARN);
    if (!success)
    {
        if (!c.T.Target.IsEmpty())
            c.Blacklist(c.T.Target, now, 5 * 60 * 1000);
        c.TameCoolUntilMs = now + s_cfg.TameCooldownSec * 1000;
    }
    c.T = PetCtx::Tame();
}

// What to tame: a Taming Rod quest in the log first, else the first pet of a hunter that has none.
bool PlanTame(Player* bot, PetCtx& c)
{
    if (s_cfg.Taming)
        for (Logic::TamingQuest const& q : Logic::TamingQuests())
            if (bot->GetQuestStatus(q.Quest) == QUEST_STATUS_INCOMPLETE && bot->GetLevel() >= s_cfg.MinLevel)
            {
                c.T.Active = true;
                c.T.General = false;
                c.T.Quest = q.Quest;
                c.T.Spell = q.RodSpell;
                std::strncpy(c.T.Beast, q.BeastName, sizeof(c.T.Beast) - 1);
                return true;
            }
    if (s_cfg.TameFirstPet && bot->GetLevel() >= s_cfg.MinLevel && bot->HasSpell(Logic::SPELL_TAME_BEAST) && !HasAnyHunterPet(bot) && !bot->GetPet())
    {
        c.T.Active = true;
        c.T.General = true;
        c.T.Quest = 0;
        c.T.Spell = Logic::SPELL_TAME_BEAST;
        c.T.Beast[0] = '\0';
        return true;
    }
    return false;
}

class PetTameAction : public Action
{
public:
    explicit PetTameAction(BotAI* ai) : Action(ai, "pet_tame", ACTION_FLAG_MOVES) { }

    bool IsPossible() override
    {
        Player* bot = GetBot();
        return s_cfg.Enabled && (s_cfg.Taming || s_cfg.TameFirstPet) && IsHunter(bot) && bot->IsAlive() && !bot->GetSession()->IsAltBot();
    }

    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        PetCtx* cp = Ctx(ai);
        if (!cp)
            return false;
        PetCtx& c = *cp;
        uint32 const now = ai->GetNowMs();

        if (!c.T.Active)
        {
            if (now < c.TameCoolUntilMs || now < c.NextScanMs || bot->IsInCombat() || bot->IsMounted() || bot->IsInFlight() || ai->Rest().Resting())
                return false;
            // another movement owner (goto, follow, flee) keeps the slot
            if (ai->Motion().HasGoal() || !ai->Motion().GetFollow().IsEmpty())
                return false;
            if (!PlanTame(bot, c))
            {
                c.NextScanMs = now + 5000;
                return false;
            }
            c.T.StartMs = now ? now : 1;
            Emit(ai, bot, "PET_TAME_START", c.T.General ? "taming a first pet with Tame Beast" : StringFormat("taming '{}' for quest {}", c.T.Beast, c.T.Quest),
                StringFormat(R"({{"quest":{},"spell":{},"general":{}}})", c.T.Quest, c.T.Spell, c.T.General ? "true" : "false"));
        }
        return Run(ai, bot, c, now);
    }

private:
    // Finds the beast: the nearest matching, idle one in sight; null when none is near.
    Creature* FindBeast(Player* bot, PetCtx& c, uint32 now)
    {
        FindCreatureOptions options;
        options.IsAlive = FindCreatureAliveState::Alive;
        std::vector<Creature*> list;
        bot->GetCreatureListWithOptionsInGrid(list, 90.0f, options);
        std::vector<Logic::TameCandidate> cands;
        std::vector<Creature*> creatures;
        bool const canExotic = bot->CanTameExoticPets();
        for (Creature* cr : list)
        {
            if (c.Blacklisted(cr->GetGUID(), now) || cr->IsPet() || cr->IsSummon())
                continue;
            CreatureTemplate const* tmpl = cr->GetCreatureTemplate();
            CreatureDifficulty const* diff = cr->GetCreatureDifficulty();
            if (!tmpl || !diff || !tmpl->IsTameable(canExotic, diff))
                continue;
            Logic::TameCandidate t;
            t.Id = cr->GetGUID().GetCounter();
            t.Level = uint8(cr->GetLevel());
            t.Distance = bot->GetDistance(cr);
            t.Alive = cr->IsAlive();
            t.InCombat = cr->IsInCombat();
            t.Elite = cr->IsElite() || cr->isWorldBoss();
            t.NameMatches = c.T.General || StringEqualI(cr->GetName(), c.T.Beast);
            t.Tameable = true;
            cands.push_back(t);
            creatures.push_back(cr);
        }
        int32 const pick = Logic::PickTameTarget(cands, uint8(bot->GetLevel()));
        return pick < 0 ? nullptr : creatures[size_t(pick)];
    }

    bool Run(BotAI* ai, Player* bot, PetCtx& c, uint32 now)
    {
        BotMotion& motion = ai->Motion();
        if (bot->IsInCombat())
        {
            EndTame(ai, bot, c, "bot_in_combat", false);
            return false;
        }
        if (now - c.T.StartMs > TAME_RUN_SECONDS * 1000)
        {
            EndTame(ai, bot, c, "timeout", false);
            return false;
        }

        Unit* target = c.T.Target.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, c.T.Target);
        if (!target)
        {
            // no beast chosen yet (or it is gone): look nearby, else walk to the nearest known spawn of the quest beast
            if (!c.T.Target.IsEmpty())
            {
                if (c.T.Casts)
                {
                    // the beast vanished after a cast: the pet or the quest credit may just have arrived (checked below)
                    c.T.Target = ObjectGuid::Empty;
                }
                else
                {
                    EndTame(ai, bot, c, "target_gone", false);
                    return false;
                }
            }
            if (Creature* beast = FindBeast(bot, c, now))
            {
                c.T.Target = beast->GetGUID();
                target = beast;
            }
        }
        bool const tamed = c.T.General ? HunterPet(bot) != nullptr : bot->GetQuestStatus(c.T.Quest) != QUEST_STATUS_INCOMPLETE;

        if (!target)
        {
            if (tamed)
            {
                EndTame(ai, bot, c, c.T.General ? "pet_acquired" : "quest_progressed", true);
                return false;
            }
            return WalkToKnownSpawn(ai, bot, c, now, motion);
        }

        Logic::TameFacts f;
        f.TargetValid = true;
        f.TargetAlive = target->IsAlive();
        f.TargetInCombatWithOther = target->IsInCombat() && !bot->IsInCombatWith(target);
        f.Distance = bot->GetDistance(target);
        f.LineOfSight = bot->IsWithinLOSInMap(target);
        Spell const* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        Spell const* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        f.ChannelActive = (channel && channel->GetSpellInfo()->Id == c.T.Spell) || (cast && cast->GetSpellInfo()->Id == c.T.Spell);
        f.Tamed = tamed;
        f.BotInCombat = bot->IsInCombat();
        f.ElapsedMs = now - c.T.StartMs;
        f.Casts = c.T.Casts;
        c.T.WasChannelling = f.ChannelActive;

        Logic::TameDecision const d = Logic::DecideTame(f, Logic::TameConfig());
        switch (d.Step)
        {
            case Logic::TameStep::Done:
                EndTame(ai, bot, c, d.Reason, true);
                return false;
            case Logic::TameStep::Abort:
                EndTame(ai, bot, c, d.Reason, false);
                return false;
            case Logic::TameStep::Approach:
                // retarget about every 2 s (the beast wanders); the goto strategy walks, this tick stays free for it
                if (!motion.HasGoal() || now - c.T.LastGoalMs >= 2000)
                {
                    c.T.LastGoalMs = now;
                    motion.SetGoal(bot->GetMapId(), target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 14.0f, "tame");
                }
                return false;
            case Logic::TameStep::Channel:
                // 20 seconds of standing still: nothing from the movement layer may interrupt
                if (motion.HasGoal() && !std::strcmp(motion.GetTag(), "tame"))
                    motion.ClearGoal();
                if (bot->isMoving())
                    BotMotion::Halt(bot);
                SetResult("PET_TAME_CHANNEL");
                return true;
            case Logic::TameStep::Cast:
                break;
        }

        if (now - c.T.LastCastMs < 2500)
            return true;   // a failed cast is retried after a short wait; nothing else runs meanwhile
        if (motion.HasGoal() && !std::strcmp(motion.GetTag(), "tame"))
            motion.ClearGoal();
        BotMotion::Halt(bot);
        bot->SetFacingToObject(target);
        c.T.LastCastMs = now ? now : 1;
        ++c.T.Casts;
        SpellCastResult res = SPELL_FAILED_ITEM_NOT_FOUND;
        if (c.T.General)
            res = bot->CastSpell(target, c.T.Spell, CastSpellExtraArgs(TRIGGERED_NONE));
        else if (Item* rod = FindRod(bot, c.T.Spell))
            res = bot->CastSpell(target, c.T.Spell, CastSpellExtraArgs(TRIGGERED_NONE).SetCastItem(rod));
        if (res != SPELL_CAST_OK)
        {
            Emit(ai, bot, "PET_TAME_CAST_FAILED", StringFormat("tame cast failed ({})", uint32(res)),
                StringFormat(R"({{"spell":{},"result":{},"casts":{},"distance":{:.0f},"rod_found":{}}})", c.T.Spell, uint32(res), c.T.Casts, f.Distance,
                    (!c.T.General && res == SPELL_FAILED_ITEM_NOT_FOUND) ? "false" : "true"), BOTLOG_WARN);
            if (!c.T.General && res == SPELL_FAILED_ITEM_NOT_FOUND)
            {
                EndTame(ai, bot, c, "no_rod", false);
                return false;
            }
        }
        else
            Emit(ai, bot, "PET_TAME_CAST", "tame channel started", StringFormat(R"({{"spell":{},"target_entry":{},"distance":{:.0f}}})", c.T.Spell, target->GetEntry(), f.Distance));
        SetResult("PET_TAME_CAST");
        return true;
    }

    // No beast near: walk to the nearest known spawn of the quest beast on this map. The general first pet looks around where the bot is.
    bool WalkToKnownSpawn(BotAI* ai, Player* bot, PetCtx& c, uint32 now, BotMotion& motion)
    {
        if (motion.HasGoal())
            return false;   // already walking
        if (!c.T.General)
        {
            auto it = s_beastSpawns.find(c.T.Quest);
            if (it != s_beastSpawns.end())
            {
                BeastSpawn const* best = nullptr;
                float bd = 1.0e9f;
                for (BeastSpawn const& s : it->second)
                {
                    if (s.Map != bot->GetMapId())
                        continue;
                    float const d = std::hypot(s.X - bot->GetPositionX(), s.Y - bot->GetPositionY());
                    if (d < bd && d > 25.0f)
                    {
                        bd = d;
                        best = &s;
                    }
                }
                if (best)
                {
                    c.T.LastGoalMs = now;
                    motion.SetGoal(bot->GetMapId(), best->X, best->Y, best->Z, 40.0f, "tame");
                    return false;
                }
            }
        }
        // nothing to walk to and nothing near
        EndTame(ai, bot, c, "no_beast_found", false);
        return false;
    }
};

class TameDueTrigger : public Trigger
{
public:
    explicit TameDueTrigger(BotAI* ai) : Trigger(ai, "tame_due", 500) { }
    bool IsActive() override
    {
        if (!s_cfg.Enabled)
            return false;
        Player* bot = GetBot();
        return IsHunter(bot) && bot->IsAlive();
    }
};

// While a tame run owns the bot, eating and the other upkeep must not interrupt it (and the goto walk of the run still needs its slot).
class TameStrategy : public Strategy
{
public:
    TameStrategy() : Strategy("pet") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // above rest (35) so a channel is never interrupted by eating; below the emergency bands. The action returns false when no
        // run is active, so it costs nothing otherwise.
        t.push_back({ "tame_due", { { "pet_tame", BotRelevance::Rest + 5.0f } } });
        // upkeep: between the quest layer (31) and rest (35); commands return false so the combat actions still run in the same tick
        t.push_back({ "pet_due", { { "pet_tick", BotRelevance::Move + 2.0f } } });
    }
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------------------------------------------------
Config const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Pet.Enabled", false);
        s_cfg.Feed = sConfigMgr->GetBoolDefault("Bot.AI.Pet.Feed", true);
        s_cfg.Abilities = sConfigMgr->GetBoolDefault("Bot.AI.Pet.Abilities", true);
        s_cfg.Taming = sConfigMgr->GetBoolDefault("Bot.AI.Pet.Taming", true);
        s_cfg.TameFirstPet = sConfigMgr->GetBoolDefault("Bot.AI.Pet.TameFirstPet", true);
        s_cfg.MinLevel = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Pet.MinLevel", 10), 1, 80));
        s_cfg.FeedBelowPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Pet.FeedBelowPct", 66), 1, 100));
        s_cfg.MendBelowPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Pet.MendBelowPct", 60), 1, 99));
        s_cfg.RetrySec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Pet.RetrySec", 8), 1, 600));
        s_cfg.TameCooldownSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Pet.TameCooldownSec", 300), 10, 86400));
    });
    return s_cfg;
}

void EnsureIndex()
{
    Cfg();
    if (!s_cfg.Enabled || !s_cfg.Taming)
        return;
    std::call_once(s_indexOnce, []()
    {
        // entries of the beasts by name, then their spawn points
        std::unordered_map<uint32, uint32> questOfEntry;
        for (Logic::TamingQuest const& q : Logic::TamingQuests())
            for (auto const& [entry, tmpl] : sObjectMgr->GetCreatureTemplates())
                if (StringEqualI(tmpl.Name, q.BeastName))
                    questOfEntry[entry] = q.Quest;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            auto it = questOfEntry.find(data.id);
            if (it != questOfEntry.end())
                s_beastSpawns[it->second].push_back({ data.mapId, data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() });
        }
        uint32 spawns = 0;
        for (auto const& kv : s_beastSpawns)
            spawns += uint32(kv.second.size());
        TC_LOG_INFO("server.worldserver", "Bot pets: {} taming quests, {} beast entries, {} beast spawns", uint32(Logic::TamingQuests().size()), uint32(questOfEntry.size()), spawns);
    });
}

void OnLogout(BotAI* ai, Player* bot)
{
    if (!s_cfg.Enabled || !IsHunter(bot))
        return;
    PetCtx* c = Ctx(ai);
    if (c && c->T.Active)
        EndTame(ai, bot, *c, "logout", false);
    if (Pet* pet = HunterPet(bot))
        if (Logic::DismissOn(Logic::Event::Logout, true))
            bot->RemovePet(pet, PET_SAVE_AS_CURRENT);   // the same save mode the core uses at logout: the pet is current again next login
    if (c)
        c->Reset();
}

void OnMapChange(BotAI* ai, Player* bot)
{
    if (!s_cfg.Enabled || !IsHunter(bot))
        return;
    PetCtx* c = Ctx(ai);
    if (!c)
        return;
    if (c->T.Active)
        EndTame(ai, bot, *c, "map_change", false);
    if (Pet* pet = HunterPet(bot))
        if (pet->GetMap() != bot->GetMap() || bot->InBattleground())
            if (Logic::DismissOn(bot->InBattleground() ? Logic::Event::EnteredBattleground : Logic::Event::MapChange, true))
                bot->RemovePet(pet, PET_SAVE_AS_CURRENT);
    // timers belong to the old map's situation; a pet the core unsummoned for the teleport is called again by the upkeep
    c->LastCallMs = c->LastReviveMs = c->NextCmdMs = 0;
    c->CallFails = 0;
    c->AbilityPet = 0;
    c->NextScanMs = 0;
    c->TameBlack.clear();
}

bool Busy(BotAI* ai)
{
    if (!s_cfg.Enabled)
        return false;
    Player* bot = ai->GetTickBot();
    if (!bot || !IsHunter(bot))
        return false;
    PetCtx* c = Ctx(ai);
    return c && c->T.Active;
}

bool SupportsEventQuest(uint32 questId)
{
    Cfg();
    return s_cfg.Enabled && s_cfg.Taming && Logic::FindTamingQuest(questId) != nullptr;
}
} // namespace BotPet

void RegisterPetBotObjects(BotRegistry& r)
{
    BotPet::Cfg();
    r.AddValue("pet_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotPet::PetCtx>(ai); });
    r.AddTrigger("pet_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotPet::PetDueTrigger>(ai); });
    r.AddTrigger("tame_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotPet::TameDueTrigger>(ai); });
    r.AddAction("pet_tick", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotPet::PetTickAction>(ai); });
    r.AddAction("pet_tame", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotPet::PetTameAction>(ai); });
    // NonCombat and Combat: upkeep and assist work in both engines
    r.AddStrategy("pet", BotStateBit(BotState::NonCombat) | BotStateBit(BotState::Combat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotPet::TameStrategy>(); });
}
