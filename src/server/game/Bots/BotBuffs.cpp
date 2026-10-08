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

// Bot group buffs, see BotBuffs.h.

#include "BotBuffs.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotBuffPlan.h"
#include "BotEngine.h"
#include "BotMgr.h"
#include "Config.h"
#include "Group.h"
#include "GroupReference.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Util.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace BotBuff
{
namespace
{
using Trinity::StringFormat;

struct Settings
{
    bool Enabled = false;       // Bot.AI.Buffs.Enabled
    float RangeYards = 30.0f;   // Bot.AI.Buffs.RangeYards: group members farther away are not buffed
    uint32 MinManaPct = 50;     // Bot.AI.Buffs.MinManaPct: buffs are cast with at least this mana percent
    uint32 RetrySec = 30;       // Bot.AI.Buffs.RetrySec: wait between two casts of the same buff on the same target
};
Settings s_cfg;
std::once_flag s_cfgOnce;

Settings const& Cfg()
{
    std::call_once(s_cfgOnce, []()
    {
        s_cfg.Enabled = sConfigMgr->GetBoolDefault("Bot.AI.Buffs.Enabled", false);
        s_cfg.RangeYards = float(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Buffs.RangeYards", 30), 10, 40));
        s_cfg.MinManaPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Buffs.MinManaPct", 50), 0, 100));
        s_cfg.RetrySec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.AI.Buffs.RetrySec", 30), 5, 600));
    });
    return s_cfg;
}

struct BuffDef
{
    Classes Class;
    uint32 Root;             // rank 1; the bot casts the highest rank it knows
    char const* Name;
    Targets For;
    uint8 Exclusive;         // buffs with the same non-zero value replace each other from one caster (paladin blessings): one per target
};

// Rows whose id or name does not match the spell data are dropped at the first use and logged.
constexpr BuffDef BUFFS[] =
{
    { CLASS_PRIEST,  1243,  "Power Word: Fortitude", Targets::All,       0 },
    { CLASS_PRIEST,  14752, "Divine Spirit",         Targets::ManaUsers, 0 },
    { CLASS_MAGE,    1459,  "Arcane Intellect",      Targets::ManaUsers, 0 },
    { CLASS_DRUID,   1126,  "Mark of the Wild",      Targets::All,       0 },
    { CLASS_PALADIN, 19740, "Blessing of Might",     Targets::Melee,     1 },
    { CLASS_PALADIN, 19742, "Blessing of Wisdom",    Targets::ManaUsers, 1 },
};
bool g_ok[std::size(BUFFS)];
std::once_flag s_validateOnce;

void Validate()
{
    std::string dropped;
    uint32 ok = 0;
    for (size_t i = 0; i < std::size(BUFFS); ++i)
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(BUFFS[i].Root, DIFFICULTY_NONE);
        g_ok[i] = si && si->SpellName && StringEqualI((*si->SpellName)[DEFAULT_LOCALE], BUFFS[i].Name);
        if (g_ok[i])
            ++ok;
        else
            dropped += StringFormat(" {}:{}", BUFFS[i].Root, BUFFS[i].Name);
    }
    TC_LOG_INFO("server.worldserver", "Bot AI buffs: {} group buffs validated, dropped:{}", ok, dropped.empty() ? " none" : dropped);
}

uint32 HighestKnownRank(Player* bot, uint32 root)
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

// the target carries some rank of the buff (a higher rank from anyone counts; an exclusive buff only from this caster)
bool HasBuff(Unit* target, Player* caster, uint32 root, bool ownOnly)
{
    uint32 id = root;
    for (uint32 guard = 0; guard < 16 && id; ++guard, id = sSpellMgr->GetNextSpellInChain(id))
        if (ownOnly ? target->HasAura(id, caster->GetGUID()) : target->HasAura(id))
            return true;
    return false;
}

class BuffCtx : public UntypedValue
{
public:
    explicit BuffCtx(BotAI* ai) : UntypedValue(ai, "buff_ctx", 0) { }
    Throttle T;
};

class BuffAction : public Action
{
public:
    explicit BuffAction(BotAI* ai) : Action(ai, "buff_cast", ACTION_FLAG_QUIET_LOG) { }
    bool IsPossible() override { return Cfg().Enabled; }
    bool Execute() override
    {
        BotAI* ai = GetAI();
        Player* bot = GetBot();
        BuffCtx* c = static_cast<BuffCtx*>(ai->GetValueRaw("buff_ctx"));
        if (!c || !bot->IsAlive() || bot->IsInCombat() || bot->IsMounted() || bot->IsInFlight() || bot->IsNonMeleeSpellCast(false, false, true) || ai->Rest().Resting())
            return false;
        uint32 const maxMana = bot->GetMaxPower(POWER_MANA);
        if (bot->GetPowerType() != POWER_MANA || !maxMana || uint64(bot->GetPower(POWER_MANA)) * 100 < uint64(Cfg().MinManaPct) * maxMana)
            return false;
        std::call_once(s_validateOnce, Validate);

        // the bot first, then the members of its group in range
        std::vector<Player*> allies;
        allies.push_back(bot);
        if (Group* group = bot->GetGroup())
            for (GroupReference const& ref : group->GetMembers())
            {
                Player* m = ref.GetSource();
                if (m && m != bot && m->IsInWorld() && m->IsAlive() && m->GetMap() == bot->GetMap() && bot->GetDistance(m) <= Cfg().RangeYards && bot->IsWithinLOSInMap(m))
                    allies.push_back(m);
            }

        uint32 const now = ai->GetNowMs();
        for (size_t i = 0; i < std::size(BUFFS); ++i)
        {
            BuffDef const& b = BUFFS[i];
            if (!g_ok[i] || b.Class != bot->GetClass())
                continue;
            uint32 const spell = HighestKnownRank(bot, b.Root);
            if (!spell || bot->GetSpellHistory()->HasCooldown(spell))
                continue;
            for (Player* a : allies)
            {
                if (!Applies(b.For, a->GetClass()) || HasBuff(a, bot, b.Root, b.Exclusive != 0))
                    continue;
                if (b.Exclusive && ExclusiveHeld(a, bot, b))
                    continue;
                uint64 const key = a->GetGUID().GetCounter();
                if (!c->T.Ready(key, b.Root, now, Cfg().RetrySec * 1000u))
                    continue;
                c->T.Note(key, b.Root, now);
                BotMotion::Halt(bot);
                SpellCastResult const res = bot->CastSpell(a, spell, CastSpellExtraArgs(TRIGGERED_NONE));
                SetResult(res == SPELL_CAST_OK ? "BUFF_CAST" : "BUFF_FAILED", StringFormat("{} on {}", b.Name, a == bot ? "itself" : a->GetName()),
                    StringFormat(R"({{"spell":{},"name":"{}","target_class":{},"self":{},"result":{}}})", spell, b.Name, uint32(a->GetClass()), a == bot ? "true" : "false", uint32(res)));
                return res == SPELL_CAST_OK;
            }
        }
        return false;
    }

private:
    // another buff of the same exclusive group from this caster is already on the target
    static bool ExclusiveHeld(Player* target, Player* caster, BuffDef const& b)
    {
        for (BuffDef const& o : BUFFS)
            if (&o != &b && o.Exclusive == b.Exclusive && o.Class == b.Class && HasBuff(target, caster, o.Root, true))
                return true;
        return false;
    }
};

class BuffDueTrigger : public Trigger
{
public:
    explicit BuffDueTrigger(BotAI* ai) : Trigger(ai, "buff_due", 3000) { }
    bool IsActive() override { return Cfg().Enabled; }
};

class BuffStrategy : public Strategy
{
public:
    BuffStrategy() : Strategy("buffs") { }
    void InitTriggers(std::vector<BotTriggerNode>& t) override
    {
        // between the quest layer and rest; the action returns false when nothing needs a buff, so other actions still run this tick
        t.push_back({ "buff_due", { { "buff_cast", BotRelevance::Move + 1.0f } } });
    }
};
} // namespace

bool Enabled()
{
    return Cfg().Enabled;
}
} // namespace BotBuff

void RegisterBuffBotObjects(BotRegistry& r)
{
    BotBuff::Enabled();
    r.AddValue("buff_ctx", [](BotAI* ai) -> std::unique_ptr<UntypedValue> { return std::make_unique<BotBuff::BuffCtx>(ai); });
    r.AddTrigger("buff_due", [](BotAI* ai) -> std::unique_ptr<Trigger> { return std::make_unique<BotBuff::BuffDueTrigger>(ai); });
    r.AddAction("buff_cast", [](BotAI* ai) -> std::unique_ptr<Action> { return std::make_unique<BotBuff::BuffAction>(ai); });
    r.AddStrategy("buffs", BotStateBit(BotState::NonCombat), []() -> std::unique_ptr<Strategy> { return std::make_unique<BotBuff::BuffStrategy>(); });
}
