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

#include "BotChat.h"
#include "BotAI.h"
#include "BotBehavior.h"
#include "BotCombat.h"
#include "BotDummy.h"
#include "BotEngine.h"
#include "BotMount.h"
#include "BotMgr.h"
#include "BotSocial.h"
#include "Chat.h"
#include "Creature.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "ObjectAccessor.h"
#include "Optional.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Util.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

namespace BotChat
{
namespace
{
char const* VerbName(Verb v)
{
    switch (v)
    {
        case Verb::Follow: return "follow";
        case Verb::Stay: return "stay";
        case Verb::Goto: return "goto";
        case Verb::Rest: return "rest";
        case Verb::Release: return "release";
        case Verb::Status: return "status";
        case Verb::Strategy: return "strategy";
        case Verb::Verbose: return "verbose";
        case Verb::Share: return "share";
        case Verb::Stop: return "stop";
        case Verb::Aggressive: return "aggressive";
        case Verb::Passive: return "passive";
        case Verb::Pull: return "pull";
        case Verb::Heal: return "heal";
        case Verb::Mount: return "mount";
        case Verb::Dismount: return "dismount";
        case Verb::Summon: return "summon";
        case Verb::Revive: return "revive";
        case Verb::Dummy: return "dummy";
        default: return "?";
    }
}

char const* ChannelName(Channel c) { return c == Channel::Party ? "party" : c == Channel::Raid ? "raid" : "whisper"; }

struct Cfg
{
    bool Enabled = true;
    bool Orders = false;            // Bot.Chat.Orders.Enabled: stop, aggressive, passive, pull, heal, mount, dismount, summon, revive
    float PullRange = 60.0f;        // Bot.Chat.Orders.PullRangeYards
    float HealRange = 40.0f;        // Bot.Chat.Orders.HealRangeYards (the heal spell's own range still applies)
    float ReviveHealthPct = 35.0f;  // Bot.Chat.Orders.ReviveHealthPct
    bool ReviveSickness = true;     // Bot.Chat.Orders.ReviveSickness
    bool VerboseDefault = false;
    uint32 MaxReplyLines = 4;
    uint32 UnauthLogSec = 30;
    bool LogEvents = true;
    float GotoArrive = 3.0f;
    uint32 TankMask = 0, HealerMask = 0, DpsMask = 0;
};

uint32 ClassMaskOf(std::string const& csv)
{
    uint32 mask = 0;
    for (std::string_view part : Trinity::Tokenize(csv, ',', false))
        if (uint8 classId = BotMgr::ParseClass(std::string(part)))
            mask |= 1u << classId;
    return mask;
}

Cfg const& Config()
{
    static Cfg const cfg = []
    {
        Cfg c;
        c.Enabled = sConfigMgr->GetBoolDefault("Bot.Chat.Enabled", true);
        c.Orders = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.Enabled", false);
        c.PullRange = std::clamp(sConfigMgr->GetFloatDefault("Bot.Chat.Orders.PullRangeYards", 60.0f), 5.0f, 150.0f);
        c.HealRange = std::clamp(sConfigMgr->GetFloatDefault("Bot.Chat.Orders.HealRangeYards", 40.0f), 5.0f, 100.0f);
        c.ReviveHealthPct = std::clamp(sConfigMgr->GetFloatDefault("Bot.Chat.Orders.ReviveHealthPct", 35.0f), 1.0f, 100.0f);
        c.ReviveSickness = sConfigMgr->GetBoolDefault("Bot.Chat.Orders.ReviveSickness", true);
        c.VerboseDefault = sConfigMgr->GetBoolDefault("Bot.Chat.VerboseDefault", false);
        c.MaxReplyLines = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Chat.MaxReplyLines", 4), 1, 10));
        c.UnauthLogSec = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Chat.UnauthorizedLogSec", 30), 0, 3600));
        c.LogEvents = sConfigMgr->GetBoolDefault("Bot.Chat.LogEvents", true);
        c.GotoArrive = std::clamp(sConfigMgr->GetFloatDefault("Bot.Chat.GotoArriveYards", 3.0f), 1.0f, 50.0f);
        c.TankMask = ClassMaskOf(sConfigMgr->GetStringDefault("Bot.Chat.Role.Tank", "warrior"));
        c.HealerMask = ClassMaskOf(sConfigMgr->GetStringDefault("Bot.Chat.Role.Healer", "priest"));
        c.DpsMask = ClassMaskOf(sConfigMgr->GetStringDefault("Bot.Chat.Role.Dps", "rogue,mage,warlock,hunter"));
        return c;
    }();
    return cfg;
}

// ---- cheap, allocation free parsing ----
bool EqI(std::string_view a, char const* b)
{
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && !b[i];
}

std::string Lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::string_view NextToken(std::string_view& s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    size_t n = 0;
    while (n < s.size() && s[n] != ' ' && s[n] != '\t')
        ++n;
    std::string_view tok = s.substr(0, n);
    s.remove_prefix(n);
    return tok;
}

bool ParseSelectorPart(std::string_view p, RoleMasks const& roles, Selector& s, bool allowPlural = true)
{
    if (p.empty())
        return false;
    if (EqI(p, "all"))
    {
        s.All = true;
        return true;
    }
    if ((p[0] == 'g' || p[0] == 'G') && p.size() >= 2 && p[1] >= '1' && p[1] <= '8')
    {
        uint8 lo = uint8(p[1] - '0'), hi = lo;
        if (p.size() > 2)
        {
            size_t i = 2;
            if (p[i] != '-')
                return false;
            ++i;
            if (i < p.size() && (p[i] == 'g' || p[i] == 'G'))
                ++i;
            if (i + 1 != p.size() || p[i] < '1' || p[i] > '8')
                return false;
            hi = uint8(p[i] - '0');
        }
        if (lo > hi)
            std::swap(lo, hi);
        for (uint8 g = lo; g <= hi; ++g)
            s.SubMask |= uint8(1u << (g - 1));
        return true;
    }
    if (EqI(p, "tank")) { s.HasClass = true; s.ClassMask |= roles.Tank; return true; }
    if (EqI(p, "healer") || EqI(p, "heal")) { s.HasClass = true; s.ClassMask |= roles.Healer; return true; }
    if (EqI(p, "dps")) { s.HasClass = true; s.ClassMask |= roles.Dps; return true; }
    for (uint8 classId = 1; classId <= 11; ++classId)
    {
        char const* name = BotMgr::ClassName(classId);
        if (name[0] != '?' && EqI(p, name))
        {
            s.HasClass = true;
            s.ClassMask |= 1u << classId;
            return true;
        }
    }
    // plural forms from the design (healers rest, warriors attack, tanks follow)
    if (allowPlural && p.size() > 2 && (p.back() == 's' || p.back() == 'S'))
    {
        p.remove_suffix(1);
        return ParseSelectorPart(p, roles, s, false);
    }
    return false;
}
}

bool ParseSelector(std::string_view tok, RoleMasks const& roles, Selector& out)
{
    if (tok.empty() || tok.size() > 40)
        return false;
    Selector s;
    size_t pos = 0;
    while (true)
    {
        size_t end = tok.find(',', pos);
        if (end == std::string_view::npos)
            end = tok.size();
        if (!ParseSelectorPart(tok.substr(pos, end - pos), roles, s))
            return false;
        if (end == tok.size())
            break;
        pos = end + 1;
    }
    out = s;
    return true;
}

char const* ParseGotoArgs(std::string_view args, GotoArgs& out)
{
    std::string_view a = Trim(args);
    GotoArgs g;
    if (EqI(a, "here"))
    {
        g.Here = true;
        out = g;
        return nullptr;
    }
    std::string_view t1 = NextToken(a), t2 = NextToken(a), t3 = NextToken(a);
    Optional<float> x = Trinity::StringTo<float>(t1), y = Trinity::StringTo<float>(t2);
    if (!x || !y || !NextToken(a).empty())
        return "BAD_ARGS";
    // StringTo<float> accepts "nan" and "inf"; world coordinates stay within +-17066 (half a map)
    constexpr float maxXY = 17100.0f, maxZ = 5000.0f;
    if (!std::isfinite(*x) || !std::isfinite(*y) || std::fabs(*x) > maxXY || std::fabs(*y) > maxXY)
        return "BAD_ARGS";
    g.X = *x;
    g.Y = *y;
    if (!t3.empty())
    {
        Optional<float> z = Trinity::StringTo<float>(t3);
        if (!z || !std::isfinite(*z) || std::fabs(*z) > maxZ)
            return "BAD_ARGS";
        g.Z = *z;
        g.HasZ = true;
    }
    out = g;
    return nullptr;
}

Verb ParseVerb(std::string_view t, bool orders)
{
    if (t.empty() || t.size() > 10)
        return Verb::None;
    static constexpr std::pair<char const*, Verb> verbs[] = { { "follow", Verb::Follow }, { "stay", Verb::Stay }, { "goto", Verb::Goto },
        { "rest", Verb::Rest }, { "release", Verb::Release }, { "status", Verb::Status }, { "strategy", Verb::Strategy }, { "verbose", Verb::Verbose }, { "share", Verb::Share }, { "dummy", Verb::Dummy } };
    static constexpr std::pair<char const*, Verb> orderVerbs[] = { { "stop", Verb::Stop }, { "aggressive", Verb::Aggressive }, { "passive", Verb::Passive },
        { "pull", Verb::Pull }, { "heal", Verb::Heal }, { "mount", Verb::Mount }, { "dismount", Verb::Dismount }, { "summon", Verb::Summon }, { "revive", Verb::Revive } };
    for (auto const& [name, v] : verbs)
        if (EqI(t, name))
            return v;
    if (orders)
        for (auto const& [name, v] : orderVerbs)
            if (EqI(t, name))
                return v;
    return Verb::None;
}

char const* ValidateOrderArgs(Verb verb, std::string_view args)
{
    switch (verb)
    {
        case Verb::Stop: case Verb::Aggressive: case Verb::Passive: case Verb::Pull: case Verb::Heal:
        case Verb::Mount: case Verb::Dismount: case Verb::Summon: case Verb::Revive:
            return Trim(args).empty() ? nullptr : "BAD_ARGS";
        default:
            return nullptr;
    }
}

char const* RoleGate(Verb verb, uint8 classId, RoleMasks const& roles)
{
    if (classId >= 32)
        return verb == Verb::Pull ? "NOT_TANK" : verb == Verb::Heal ? "NOT_HEALER" : nullptr;
    uint32 const bit = 1u << classId;
    if (verb == Verb::Pull && !(roles.Tank & bit))
        return "NOT_TANK";
    if (verb == Verb::Heal && !(roles.Healer & bit))
        return "NOT_HEALER";
    return nullptr;
}

void SpreadOffset(uint32 index, float& dx, float& dy)
{
    float const angle = float(index) * 2.399963f, radius = index ? 1.5f + 0.9f * std::sqrt(float(index)) : 0.0f;
    dx = radius * std::cos(angle);
    dy = radius * std::sin(angle);
}

char const* SummonCheck(SummonFacts const& f, bool revive)
{
    if (revive)
    {
        if (f.BotAlive)
            return "NOT_DEAD";
    }
    else if (!f.BotAlive)
        return "DEAD";
    if (!f.IssuerAlive)
        return "ISSUER_DEAD";
    if (f.IssuerInCombat)
        return "ISSUER_IN_COMBAT";
    if (f.BotInCombat)
        return "IN_COMBAT";
    if (!f.SameMap && f.InstancedMapInvolved)
        return "INSTANCE";
    if (!revive && f.SameMap && f.Distance < 5.0f)
        return "ALREADY_HERE";
    return nullptr;
}

int32 PickMount(std::span<MountOption const> options)
{
    int32 best = -1;
    for (size_t i = 0; i < options.size(); ++i)
    {
        MountOption const& o = options[i];
        if (o.Flying || !o.SpellId)
            continue;
        if (best < 0 || o.Speed > options[size_t(best)].Speed || (o.Speed == options[size_t(best)].Speed && o.SpellId < options[size_t(best)].SpellId))
            best = int32(i);
    }
    return best;
}

namespace
{
// ---- helpers ----
bool IsBotPlayer(Player* p) { return p && p->GetSession() && p->GetSession()->IsBot() && p->GetSession()->GetBotAI(); }

struct Member
{
    Player* P;
    BotAI* AI;
    uint8 Sub;     // 0-based subgroup
    uint8 Class;
};

std::string JsonEscape(std::string_view s, size_t maxLen = 120)
{
    std::string out;
    for (char c : s.substr(0, maxLen))
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': case '\r': case '\t': out += ' '; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out += ' ';
                else
                    out += c;
        }
    }
    return out;
}

constexpr size_t ISSUER_MAP_CAP = 4096; // per-issuer maps: normally bounded by the players online (erased on logout)
std::unordered_map<uint64, bool> s_verbose;
void SetVerbose(Player* p, bool on)
{
    if (s_verbose.size() >= ISSUER_MAP_CAP && !s_verbose.contains(p->GetGUID().GetCounter()))
        s_verbose.erase(s_verbose.begin());
    s_verbose[p->GetGUID().GetCounter()] = on;
}
bool IsVerbose(Player* p)
{
    auto it = s_verbose.find(p->GetGUID().GetCounter());
    return it != s_verbose.end() ? it->second : Config().VerboseDefault;
}

struct UnauthRec { uint32 LastMs = 0; uint32 Suppressed = 0; bool Seen = false; };
std::unordered_map<uint64, UnauthRec> s_unauth;

// Group bots other than the issuer, with their live subgroup and class.
void CollectBots(Group* group, Player* issuer, std::vector<Member>& out)
{
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == issuer->GetGUID())
            continue;
        Player* p = ObjectAccessor::FindConnectedPlayer(slot.guid);
        if (IsBotPlayer(p))
            out.push_back({ p, p->GetSession()->GetBotAI(), slot.group, slot._class });
    }
}

Player* FirstBot(Group* group, Player* issuer)
{
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == issuer->GetGUID())
            continue;
        Player* p = ObjectAccessor::FindConnectedPlayer(slot.guid);
        if (IsBotPlayer(p))
            return p;
    }
    return nullptr;
}

void SendReply(Player* issuer, ReplySink const* sink, std::vector<std::string> const& lines)
{
    for (std::string const& l : lines)
    {
        std::string line = l.size() > 240 ? l.substr(0, 237) + "..." : l;
        if (sink)
            (*sink)(line);
        else if (issuer->GetSession() && !issuer->GetSession()->IsBot())
            ChatHandler(issuer->GetSession()).SendSysMessage(line);
    }
}

void LogCommand(Member const& m, Player* issuer, Channel ch, Verb v, std::string_view args, std::string const& match, char const* code,
    bool accepted, uint8 severity, std::string const& extraJson = std::string())
{
    if (!Config().LogEvents)
        return;
    std::string summary = Trinity::StringFormat("{} {} by {} via {}: {}", VerbName(v), std::string(args.substr(0, 40)), issuer->GetName(), ChannelName(ch), code);
    std::string details = Trinity::StringFormat("{{\"issuer\":\"{}\",\"issuer_guid\":{},\"channel\":\"{}\",\"match\":\"{}\",\"cmd\":\"{}\",\"args\":\"{}\",\"outcome\":\"{}\"{}}}",
        JsonEscape(issuer->GetName()), issuer->GetGUID().GetCounter(), ChannelName(ch), JsonEscape(match), VerbName(v), JsonEscape(args),
        accepted ? "accepted" : "refused", extraJson.empty() ? std::string() : (std::string(",") + extraJson));
    m.AI->EmitEvent(m.P, "chat_command", severity, code, summary, details);
}

// Parsed once per command, applied to every matched bot.
struct Ctx
{
    Verb V = Verb::None;
    std::string_view Args;
    char const* PreflightError = nullptr;    // refusal code for every bot (bad arguments, unknown strategy)
    bool Off = false;
    bool GotoHere = false;
    float Gx = 0, Gy = 0, Gz = 0;
    bool HasZ = false;
    std::vector<std::pair<bool, std::string>> Strat;  // add?, name
    uint32 QuestId = 0;                               // share
    RoleMasks Roles;                                  // role gate of pull / heal
    Unit* Target = nullptr;                           // pull, heal: the issuer's selected target (live during Handle only)
    uint32 DummySec = 0;                              // dummy: run length, 0 = default
};

void Preflight(Ctx& c)
{
    std::string_view a = Trim(c.Args);
    switch (c.V)
    {
        case Verb::Follow:
        case Verb::Stay:
        case Verb::Rest:
            if (a.empty() || EqI(a, "on"))
                c.Off = false;
            else if (EqI(a, "off"))
                c.Off = true;
            else
                c.PreflightError = "BAD_ARGS";
            break;
        case Verb::Goto:
        {
            GotoArgs g;
            if (char const* err = ParseGotoArgs(a, g))
            {
                c.PreflightError = err;
                break;
            }
            c.GotoHere = g.Here;
            c.Gx = g.X;
            c.Gy = g.Y;
            c.Gz = g.Z;
            c.HasZ = g.HasZ;
            break;
        }
        case Verb::Strategy:
        {
            size_t pos = 0;
            while (pos < a.size())
            {
                while (pos < a.size() && (a[pos] == ' ' || a[pos] == ','))
                    ++pos;
                size_t end = pos;
                while (end < a.size() && a[end] != ' ' && a[end] != ',')
                    ++end;
                std::string_view tok = a.substr(pos, end - pos);
                pos = end;
                if (tok.empty())
                    continue;
                bool add = true;
                if (tok[0] == '+' || tok[0] == '-')
                {
                    add = tok[0] == '+';
                    tok.remove_prefix(1);
                }
                std::string name = Lower(tok);
                if (name.empty() || !BotRegistry::instance().FindStrategy(name))
                {
                    c.PreflightError = "UNKNOWN_STRATEGY";
                    return;
                }
                c.Strat.emplace_back(add, std::move(name));
            }
            break;
        }
        case Verb::Share:
        {
            // share <questId>: the bot pushes that quest of its log to the group (the core handler validates each receiver)
            Optional<uint32> q = Trinity::StringTo<uint32>(NextToken(a));
            if (!q || !*q || !NextToken(a).empty())
                c.PreflightError = "BAD_ARGS";
            else
                c.QuestId = *q;
            break;
        }
        case Verb::Dummy:
        {
            // dummy [seconds] | dummy off: the bot fights a training dummy and logs DUMMY_SUMMARY
            if (a.empty())
                break;
            if (EqI(a, "off"))
            {
                c.Off = true;
                break;
            }
            Optional<uint32> sec = Trinity::StringTo<uint32>(NextToken(a));
            if (!sec || !*sec || !NextToken(a).empty())
                c.PreflightError = "BAD_ARGS";
            else
                c.DummySec = *sec;
            break;
        }
        case Verb::Release:
        case Verb::Status:
            if (!a.empty())
                c.PreflightError = "BAD_ARGS";
            break;
        case Verb::Stop: case Verb::Aggressive: case Verb::Passive: case Verb::Pull: case Verb::Heal:
        case Verb::Mount: case Verb::Dismount: case Verb::Summon: case Verb::Revive:
            c.PreflightError = ValidateOrderArgs(c.V, a);
            break;
        default:
            break;
    }
}

// One bot, one command. Returns the code logged ("OK" or the refusal reason).
char const* Exec(Ctx const& c, Player* issuer, Member const& m, uint32 index)
{
    Player* bot = m.P;
    BotAI* ai = m.AI;
    if (c.PreflightError)
        return c.PreflightError;
    if (char const* gate = RoleGate(c.V, m.Class, c.Roles))
        return gate;

    bool const alive = bot->IsAlive();
    switch (c.V)
    {
        case Verb::Follow:
            if (c.Off)
            {
                ai->Motion().SetFollow(ObjectGuid::Empty);
                ai->RemoveStrategy(bot, "follow", "chat");
                return "OK";
            }
            if (!alive)
                return "DEAD";
            if (!issuer->IsInWorld() || issuer->GetMapId() != bot->GetMapId())
                return "DIFFERENT_MAP";
            ai->RemoveStrategy(bot, "stay", "chat");
            ai->AddStrategy(bot, "follow", "chat");
            ai->Motion().SetFollow(issuer->GetGUID());
            return "OK";
        case Verb::Stay:
            if (c.Off)
            {
                ai->RemoveStrategy(bot, "stay", "chat");
                return "OK";
            }
            if (!alive)
                return "DEAD";
            ai->AddStrategy(bot, "stay", "chat");
            ai->Motion().SetFollow(ObjectGuid::Empty);
            if (ai->Motion().HasGoal())
                ai->Motion().ClearGoal();
            return "OK";
        case Verb::Goto:
        {
            if (!alive)
                return "DEAD";
            if (ai->GetState() == BotState::Combat)
                return "IN_COMBAT";
            float x = c.Gx, y = c.Gy, z = c.HasZ ? c.Gz : bot->GetPositionZ();
            if (c.GotoHere)
            {
                if (!issuer->IsInWorld() || issuer->GetMapId() != bot->GetMapId())
                    return "DIFFERENT_MAP";
                // spread the bots on a spiral around the issuer so they do not all try to stand on one point
                float dx, dy;
                SpreadOffset(index, dx, dy);
                x = issuer->GetPositionX() + dx;
                y = issuer->GetPositionY() + dy;
                z = issuer->GetPositionZ();
            }
            ai->RemoveStrategy(bot, "stay", "chat");
            ai->AddStrategy(bot, "goto", "chat");
            ai->Motion().SetFollow(ObjectGuid::Empty);
            BotMotion::EnsureGrids(bot, x, y);
            bot->UpdateGroundPositionZ(x, y, z);
            ai->Motion().SetGoal(bot->GetMapId(), x, y, z, Config().GotoArrive, "goto");
            return "OK";
        }
        case Verb::Rest:
            // The rest strategy is trigger driven (eat/drink below the Bot.AI.Rest thresholds); there is no forced rest.
            if (c.Off)
            {
                ai->RemoveStrategy(bot, "rest", "chat");
                if (ai->Rest().Resting())
                    BotEndRest(ai, bot, "COMMAND", true);
                return "OK";
            }
            if (!alive)
                return "DEAD";
            if (ai->GetState() == BotState::Combat)
                return "IN_COMBAT";
            ai->AddStrategy(bot, "rest", "chat");
            return "OK";
        case Verb::Share:
            if (!alive)
                return "DEAD";
            return BotSocial::ShareQuest(bot, c.QuestId, IsVerbose(issuer));
        case Verb::Dummy:
            return c.Off ? BotDummy::Stop(ai, bot) : BotDummy::Start(ai, bot, c.DummySec);
        case Verb::Release:
            if (alive)
                return "NOT_DEAD";
            if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
                return "ALREADY_GHOST";
            ai->Recover().ReleaseDelayMs = 0;  // one shot: the next death rolls a new delay (ChangeState)
            return "OK";
        case Verb::Stop:
            if (!alive)
                return "DEAD";
            ai->Motion().SetFollow(ObjectGuid::Empty);
            ai->Motion().ClearGoal();
            ai->RemoveStrategy(bot, "goto", "chat");
            if (ai->GetState() == BotState::NonCombat)
            {
                bot->AttackStop();
                bot->StopMoving();
            }
            return "OK";
        case Verb::Aggressive:
            ai->SetPassive(false);
            return "OK";
        case Verb::Passive:
            ai->SetPassive(true);
            return "OK";
        case Verb::Pull:
        {
            if (!alive)
                return "DEAD";
            Creature* mob = c.Target ? c.Target->ToCreature() : nullptr;
            if (!c.Target)
                return "NO_TARGET";
            if (!mob || !mob->IsAlive() || !mob->IsInWorld() || !bot->IsValidAttackTarget(mob) || mob->IsFriendlyTo(bot))
                return "NOT_HOSTILE";
            if (mob->GetMap() != bot->GetMap())
                return "DIFFERENT_MAP";
            if (ai->GetState() == BotState::Combat || bot->IsInCombat())
                return "IN_COMBAT";
            if (bot->GetDistance(mob) > Config().PullRange)
                return "OUT_OF_RANGE";
            // the same opening as the quest pull (BotQuest.cpp): the Combat engine takes over on the next tick
            bot->SetFacingToObject(mob);
            bot->Attack(mob, true);
            mob->EngageWithTarget(bot);
            bot->SetInCombatWith(mob);
            return "OK";
        }
        case Verb::Heal:
        {
            if (!alive)
                return "DEAD";
            if (!c.Target)
                return "NO_TARGET";
            if (!c.Target->IsAlive())
                return "TARGET_DEAD";
            if (!c.Target->IsInWorld() || c.Target->GetMap() != bot->GetMap())
                return "DIFFERENT_MAP";
            if (!bot->IsFriendlyTo(c.Target))
                return "NOT_FRIENDLY";
            if (bot->GetDistance(c.Target) > Config().HealRange)
                return "OUT_OF_RANGE";
            return BotCombatHealUnit(bot, c.Target);
        }
        case Verb::Mount:
        {
            if (!alive)
                return "DEAD";
            if (bot->IsMounted())
                return "ALREADY_MOUNTED";
            if (ai->GetState() == BotState::Combat || bot->IsInCombat())
                return "IN_COMBAT";
            char const* const res = BotMount::MountUp(ai, bot, 0);
            if (!std::strcmp(res, "OK"))
                BotMount::NoteOrder(ai, true);
            return res;
        }
        case Verb::Dismount:
            if (!alive)
                return "DEAD";
            if (!bot->IsMounted() && !bot->HasAuraType(SPELL_AURA_MOUNTED))
                return "NOT_MOUNTED";
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            bot->Dismount();
            BotMount::NoteOrder(ai, false);
            return "OK";
        case Verb::Summon:
        case Verb::Revive:
        {
            bool const revive = c.V == Verb::Revive;
            SummonFacts f;
            f.BotAlive = alive;
            f.BotInCombat = bot->IsInCombat() || ai->GetState() == BotState::Combat;
            f.IssuerAlive = issuer->IsAlive();
            f.IssuerInCombat = issuer->IsInCombat();
            f.SameMap = issuer->IsInWorld() && bot->IsInWorld() && issuer->GetMap() == bot->GetMap();
            f.InstancedMapInvolved = issuer->IsInWorld() && ((bot->IsInWorld() && bot->GetMap()->Instanceable()) || issuer->GetMap()->Instanceable());
            f.Distance = f.SameMap ? bot->GetDistance(issuer) : 0.0f;
            if (char const* refusal = SummonCheck(f, revive))
                return refusal;
            if (!issuer->IsInWorld())
                return "ISSUER_DEAD";
            static bool const hardcore = sConfigMgr->GetBoolDefault("Classic.Hardcore", false);
            if (revive && hardcore)
                return "HARDCORE";
            float dx, dy;
            SpreadOffset(index, dx, dy);
            if (revive)
            {
                // a ghost is brought back to life where it stands, then moved to the issuer like a summon
                bot->ResurrectPlayer(Config().ReviveHealthPct / 100.0f, Config().ReviveSickness);
                ai->Recover().Reset();
            }
            float const x = issuer->GetPositionX() + dx, y = issuer->GetPositionY() + dy;
            ai->Motion().ClearGoal();
            bot->TeleportTo(WorldLocation(issuer->GetMapId(), x, y, issuer->GetPositionZ(), issuer->GetOrientation()));
            return "OK";
        }
        case Verb::Strategy:
            for (auto const& [add, name] : c.Strat)
                if (add)
                    ai->AddStrategy(bot, name, "chat");
                else
                    ai->RemoveStrategy(bot, name, "chat");
            return "OK";
        default:
            return "OK";   // status and the strategy listing are read only
    }
}

std::vector<std::string> BuildStatus(std::vector<Member> const& targets, uint32 maxLines)
{
    uint32 st[3] = { 0, 0, 0 }, goals = 0, follows = 0, resting = 0, low = 0;
    std::vector<std::string> notable;
    for (Member const& m : targets)
    {
        BotState const state = m.AI->GetState();
        ++st[uint32(state) < 3 ? uint32(state) : 0];
        if (m.AI->Motion().HasGoal())
            ++goals;
        if (!m.AI->Motion().GetFollow().IsEmpty())
            ++follows;
        if (m.AI->Rest().Resting())
            ++resting;
        float const hp = float(m.P->GetHealthPct());
        bool const lowHp = m.P->IsAlive() && hp < 50.0f;
        if (lowHp)
            ++low;
        if (state != BotState::NonCombat || lowHp)
            notable.push_back(Trinity::StringFormat("{} ({}): {} hp {}%", m.P->GetName(), BotMgr::ClassName(m.Class), BotStateName(state), uint32(hp)));
    }
    std::vector<std::string> lines;
    lines.push_back(Trinity::StringFormat("Status {} bot(s): NonCombat {}, Combat {}, Dead {}; goal {}, follow {}, resting {}; hp<50% {}",
        targets.size(), st[0], st[1], st[2], goals, follows, resting, low));
    for (size_t i = 0; i < notable.size() && lines.size() < maxLines; ++i)
    {
        if (lines.size() + 1 == maxLines && notable.size() - i > 1)
        {
            lines.push_back(Trinity::StringFormat("{} (+{} more)", notable[i], notable.size() - i - 1));
            break;
        }
        lines.push_back(notable[i]);
    }
    return lines;
}

std::string Aggregate(std::vector<char const*> const& codes)
{
    uint32 ok = 0;
    std::vector<std::pair<std::string, uint32>> refused;
    for (char const* code : codes)
    {
        if (!strcmp(code, "OK"))
        {
            ++ok;
            continue;
        }
        auto it = std::find_if(refused.begin(), refused.end(), [&](auto const& p) { return p.first == code; });
        if (it == refused.end())
            refused.emplace_back(code, 1);
        else
            ++it->second;
    }
    std::string out = Trinity::StringFormat("{} ok", ok);
    if (!refused.empty())
    {
        uint32 total = 0;
        std::string detail;
        for (auto const& [code, n] : refused)
        {
            total += n;
            detail += Trinity::StringFormat("{}{} x{}", detail.empty() ? "" : ", ", code, n);
        }
        out += Trinity::StringFormat(", {} refused ({})", total, detail);
    }
    return out;
}
} // namespace

void OnPlayerLogout(uint64 guidCounter)
{
    s_verbose.erase(guidCounter);
    s_unauth.erase(guidCounter);
}

bool Handle(Player* issuer, Channel channel, std::string_view text, Player* whisperTarget /*= nullptr*/, ReplySink const* sink /*= nullptr*/)
{
    Cfg const& cfg = Config();
    if (!cfg.Enabled || !issuer)
        return false;
    if (channel == Channel::Whisper && (!IsBotPlayer(whisperTarget) || whisperTarget == issuer))
        return false;

    // 1. cheap shape check, no allocation: [selector] verb [args]
    std::string_view rest = text;
    std::string_view tok = NextToken(rest);
    if (tok.empty() || tok.size() > 40)
        return false;
    Selector sel;
    RoleMasks const roles{ cfg.TankMask, cfg.HealerMask, cfg.DpsMask };
    bool const hasSel = ParseSelector(tok, roles, sel);
    std::string_view const selText = hasSel ? tok : std::string_view();
    Verb const verb = ParseVerb(hasSel ? NextToken(rest) : tok, cfg.Orders);
    if (verb == Verb::None)
        return false;
    std::string_view const args = Trim(rest);

    // 2. authorization: only the leader of the bot's group
    Group* group = channel == Channel::Whisper ? whisperTarget->GetGroup() : issuer->GetGroup();
    if (!group && channel != Channel::Whisper)
        return false;
    Player* logBot = channel == Channel::Whisper ? whisperTarget : FirstBot(group, issuer);
    if (!logBot)
        return false;   // a group without bots is none of our business

    if (!group || !group->IsLeader(issuer->GetGUID()))
    {
        char const* reason = group ? "UNAUTHORIZED" : "NOT_IN_GROUP";
        if (s_unauth.size() >= ISSUER_MAP_CAP && !s_unauth.contains(issuer->GetGUID().GetCounter()))
            s_unauth.erase(s_unauth.begin()); // entries are also erased on logout; the cap only guards against a flood
        UnauthRec& rec = s_unauth[issuer->GetGUID().GetCounter()];
        uint32 const now = GameTime::GetGameTimeMS();
        if (rec.Seen && now - rec.LastMs < cfg.UnauthLogSec * 1000)
        {
            ++rec.Suppressed;
            return true;
        }
        std::string extra = Trinity::StringFormat("\"suppressed\":{},\"leader\":\"{}\"", rec.Suppressed, group ? JsonEscape(group->GetLeaderName()) : std::string());
        rec.LastMs = now;
        rec.Suppressed = 0;
        rec.Seen = true;
        LogCommand({ logBot, logBot->GetSession()->GetBotAI(), 0, 0 }, issuer, channel, verb, args, std::string(selText), reason, false, BOTLOG_WARN, extra);
        return true;
    }

    bool const verbose = IsVerbose(issuer);
    Member const logMember{ logBot, logBot->GetSession()->GetBotAI(), 0, 0 };

    if (verb == Verb::Verbose)
    {
        bool known = true;
        if (EqI(args, "on"))
            SetVerbose(issuer, true);
        else if (EqI(args, "off"))
            SetVerbose(issuer, false);
        else
            known = false;
        LogCommand(logMember, issuer, channel, verb, args, "-", known ? "OK" : "BAD_ARGS", known, BOTLOG_INFO);
        SendReply(issuer, sink, { known ? Trinity::StringFormat("Bot replies {}.", IsVerbose(issuer) ? "on" : "off") : Trinity::StringFormat("Use: verbose on|off (now {}).", verbose ? "on" : "off") });
        return true;
    }

    // 3. targets, resolved live from the group
    std::string match;
    std::vector<Member> targets;
    if (channel == Channel::Whisper)
    {
        Member m{ whisperTarget, whisperTarget->GetSession()->GetBotAI(), group->GetMemberGroup(whisperTarget->GetGUID()), whisperTarget->GetClass() };
        match = hasSel ? std::string(selText) : "whisper";
        if (!hasSel || sel.Matches(m.Sub, m.Class))
            targets.push_back(m);
    }
    else
    {
        std::vector<Member> bots;
        CollectBots(group, issuer, bots);
        if (hasSel)
        {
            match = std::string(selText);
            bots.erase(std::remove_if(bots.begin(), bots.end(), [&](Member const& m) { return !sel.Matches(m.Sub, m.Class); }), bots.end());
        }
        else if (channel == Channel::Raid)
        {
            LogCommand(logMember, issuer, channel, verb, args, "-", "NEEDS_PREFIX", false, BOTLOG_INFO);
            if (verbose)
                SendReply(issuer, sink, { "Raid chat needs a prefix: all, g1, g3-g4, tank, healer, dps or a class." });
            return true;
        }
        else if (group->isRaidGroup())
        {
            uint8 const sub = group->GetMemberGroup(issuer->GetGUID());
            match = Trinity::StringFormat("party:g{}", sub + 1);
            bots.erase(std::remove_if(bots.begin(), bots.end(), [&](Member const& m) { return m.Sub != sub; }), bots.end());
        }
        else
            match = "party";
        targets = std::move(bots);
    }

    if (targets.empty())
    {
        LogCommand(logMember, issuer, channel, verb, args, match, "NO_MATCH", false, BOTLOG_INFO);
        if (verbose)
            SendReply(issuer, sink, { Trinity::StringFormat("No bot matches '{}'.", match) });
        return true;
    }

    // 4. execute, log per bot, one aggregated reply
    Ctx ctx;
    ctx.V = verb;
    ctx.Args = args;
    ctx.Roles = roles;
    Preflight(ctx);
    if ((verb == Verb::Pull || verb == Verb::Heal) && !ctx.PreflightError)
        ctx.Target = ObjectAccessor::GetUnit(*issuer, issuer->GetTarget());

    std::vector<char const*> codes;
    codes.reserve(targets.size());
    uint32 index = 0;
    for (Member const& m : targets)
    {
        char const* code = Exec(ctx, issuer, m, index++);
        codes.push_back(code);
        LogCommand(m, issuer, channel, verb, args, match, code, !strcmp(code, "OK"), BOTLOG_INFO);
    }

    if (verb == Verb::Status)
        SendReply(issuer, sink, BuildStatus(targets, cfg.MaxReplyLines));
    else if (verb == Verb::Strategy && args.empty())
    {
        std::vector<std::string> lines = targets.front().AI->DescribeStrategies();
        lines.insert(lines.begin(), Trinity::StringFormat("Strategies of {} (first of {} bots):", targets.front().P->GetName(), targets.size()));
        if (lines.size() > cfg.MaxReplyLines)
            lines.resize(cfg.MaxReplyLines);
        SendReply(issuer, sink, lines);
    }
    else if (verbose)
        SendReply(issuer, sink, { Trinity::StringFormat("{} [{}]: {}", VerbName(verb), match, Aggregate(codes)) });
    return true;
}

// ---------------- test path (GM / console commands) ----------------
bool TestSay(ChatHandler* handler, std::string const& issuerName, std::string const& channelName, std::string_view text)
{
    Player* issuer = ObjectAccessor::FindPlayerByName(issuerName);
    if (!issuer)
    {
        handler->PSendSysMessage("Player '%s' is not online.", issuerName.c_str());
        return false;
    }
    Channel channel;
    Player* target = nullptr;
    if (EqI(channelName, "party"))
        channel = Channel::Party;
    else if (EqI(channelName, "raid"))
        channel = Channel::Raid;
    else if (EqI(channelName, "whisper"))
    {
        channel = Channel::Whisper;
        std::string_view rest = text;
        std::string name(NextToken(rest));
        target = ObjectAccessor::FindPlayerByName(name);
        if (!target)
        {
            handler->PSendSysMessage("Whisper target '%s' is not online (text must start with the bot name).", name.c_str());
            return false;
        }
        text = Trim(rest);
    }
    else
    {
        handler->PSendSysMessage("%s", "Channel must be party, raid or whisper.");
        return false;
    }
    Group* group = issuer->GetGroup();
    if (channel != Channel::Whisper && (!group || (channel == Channel::Raid && !group->isRaidGroup())))
    {
        handler->PSendSysMessage("%s is not in a %s.", issuer->GetName().c_str(), channel == Channel::Raid ? "raid" : "group");
        return false;
    }
    ReplySink sink = [handler](std::string const& line) { handler->PSendSysMessage("[reply] %s", line.c_str()); };
    bool const handled = Handle(issuer, channel, text, target, &sink);
    handler->PSendSysMessage("bot say: %s", handled ? "command-shaped (handled, see bot_event)" : "not a command (ignored)");
    return true;
}

namespace
{
Player* FindBot(ChatHandler* handler, std::string const& name)
{
    std::vector<Player*> players;
    if (!name.empty() && !EqI(name, "all"))
        players = sBotMgr->GetOnlineBotPlayers(name);
    if (players.size() != 1)
    {
        handler->PSendSysMessage("Bot '%s' is not online (give the exact bot name).", name.c_str());
        return nullptr;
    }
    return players.front();
}

void ListGroup(ChatHandler* handler, Group* group)
{
    handler->PSendSysMessage("Group: %u member(s)%s, leader %s", group->GetMembersCount(), group->isRaidGroup() ? " (raid)" : "", group->GetLeaderName());
    for (uint8 sub = 0; sub < 8; ++sub)
    {
        std::string names;
        uint32 n = 0;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.group == sub)
            {
                names += Trinity::StringFormat("{}{}({})", names.empty() ? "" : " ", slot.name, BotMgr::ClassName(slot._class));
                ++n;
            }
        if (n)
            handler->PSendSysMessage("  g%u [%u]: %s", uint32(sub + 1), n, names.c_str());
    }
}
} // namespace

bool TestGroup(ChatHandler* handler, std::string const& op, std::string_view argText)
{
    std::string_view rest = argText;
    std::string a1(NextToken(rest)), a2(NextToken(rest)), a3(NextToken(rest));

    if (EqI(op, "form"))
    {
        Player* leader = FindBot(handler, a1);
        if (!leader)
            return false;
        if (leader->GetGroup())
        {
            handler->PSendSysMessage("%s is already in a group.", leader->GetName().c_str());
            return false;
        }
        std::vector<Player*> members;
        Optional<uint32> count = Trinity::StringTo<uint32>(a2);
        if (count || EqI(a2, "all"))
        {
            for (Player* p : sBotMgr->GetOnlineBotPlayers())
                if (p != leader && !p->GetGroup() && (!count || members.size() < *count))
                    members.push_back(p);
        }
        else
        {
            for (std::string_view name : Trinity::Tokenize(a2, ',', false))
            {
                Player* p = FindBot(handler, std::string(name));
                if (!p)
                    return false;
                if (p != leader && !p->GetGroup())
                    members.push_back(p);
            }
        }
        if (members.empty() || members.size() + 1 > MAX_RAID_SIZE)
        {
            handler->PSendSysMessage("Need 1 to %u free bots besides the leader (got %zu).", uint32(MAX_RAID_SIZE - 1), members.size());
            return false;
        }
        Group* group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            handler->PSendSysMessage("%s", "Group creation failed.");
            return false;
        }
        sGroupMgr->AddGroup(group);
        if (members.size() + 1 > MAX_GROUP_SIZE || EqI(a3, "raid"))
            group->ConvertToRaid();
        uint32 added = 0;
        for (Player* p : members)
            if (group->AddMember(p))
                ++added;
        handler->PSendSysMessage("Formed %s led by %s with %u member(s) added.", group->isRaidGroup() ? "a raid" : "a party", leader->GetName().c_str(), added);
        ListGroup(handler, group);
        return true;
    }

    if (EqI(op, "disbandall"))
    {
        std::set<Group*> groups;
        for (Player* p : sBotMgr->GetOnlineBotPlayers())
            if (Group* g = p->GetGroup())
                groups.insert(g);
        for (Group* g : groups)
            g->Disband();
        handler->PSendSysMessage("Disbanded %zu group(s) of bots.", groups.size());
        return true;
    }

    Player* player = ObjectAccessor::FindPlayerByName(a1);
    Group* group = player ? player->GetGroup() : nullptr;
    if (!group)
    {
        handler->PSendSysMessage("'%s' is not online or not in a group.", a1.c_str());
        return false;
    }
    if (EqI(op, "disband"))
    {
        group->Disband();
        handler->PSendSysMessage("Disbanded the group of %s.", a1.c_str());
        return true;
    }
    if (EqI(op, "list"))
    {
        ListGroup(handler, group);
        return true;
    }
    if (EqI(op, "move"))
    {
        Optional<uint8> sub = Trinity::StringTo<uint8>(a2);
        if (!sub || *sub < 1 || *sub > 8 || !group->isRaidGroup())
        {
            handler->PSendSysMessage("%s", "Use: bot group move <member> <1-8> (the group must be a raid).");
            return false;
        }
        uint32 inSub = 0;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            inSub += slot.group == *sub - 1;
        if (inSub >= MAX_GROUP_SIZE)
        {
            handler->PSendSysMessage("Subgroup %u is full.", uint32(*sub));
            return false;
        }
        group->ChangeMembersGroup(player->GetGUID(), uint8(*sub - 1));
        handler->PSendSysMessage("%s moved to subgroup %u.", a1.c_str(), uint32(*sub));
        return true;
    }
    handler->PSendSysMessage("%s", "Use: bot group form <leader> <count|name,name|all> [raid] | move <member> <1-8> | list <member> | disband <member> | disbandall");
    return false;
}
}
