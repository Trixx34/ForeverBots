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

#include "BotGuild.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotMovePlan.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <unordered_map>

namespace BotGuild
{
namespace
{
using namespace BotGuildPlan;
using Trinity::StringFormat;

struct Pending
{
    enum Kind : uint8 { GuildChat, Whisper } What = GuildChat;
    ObjectGuid Bot;
    ObjectGuid::LowType GuildId = 0;   // GuildChat: the guild the line goes to; the bot must still be in it
    ObjectGuid Target;                 // Whisper
    std::string Text;
    uint32 LeftMs = 0;
};

std::vector<Pending> s_pending;
std::unordered_map<uint64, int64> s_spoke;        // bot guid counter -> game time of its last line
std::unordered_map<uint64, int64> s_whisperSeen;  // bot ^ sender -> game time of the last invite whisper answered
uint64 s_seq = 0;
uint32 s_idleMs = 0, s_autoMs = 0;

uint64 Seed()
{
    return BotMove::Mix(++s_seq, uint64(GameTime::GetGameTime()), 0x67756c64);
}

int64 Now() { return int64(GameTime::GetGameTime()); }

bool IsBotSession(Player* p) { return p && p->GetSession() && p->GetSession()->IsBot(); }
bool IsAlt(Player* p) { return p && p->GetSession() && p->GetSession()->IsAltBot(); }

void Log(uint64 botGuid, char const* code, std::string summary, uint8 sev = BOTLOG_INFO)
{
    BotEvent ev;
    ev.BotGuid = botGuid;
    ev.Type = "decision";
    ev.Severity = sev;
    ev.Reason = code;
    ev.Summary = std::move(summary);
    sBotMgr->LogEvent(std::move(ev));
}

uint32 SinceSpoke(uint64 guid)
{
    auto it = s_spoke.find(guid);
    if (it == s_spoke.end())
        return 1000000;
    return uint32(std::max<int64>(0, Now() - it->second));
}

// Online world bots of the guild that may speak in it (alt bots are the player's own characters and stay quiet).
std::vector<Player*> GuildBots(Guild* guild, Player* except = nullptr)
{
    std::vector<Player*> out;
    guild->BroadcastWorker([&](Player* p)
    {
        if (p != except && IsBotSession(p) && !IsAlt(p))
            out.push_back(p);
    });
    return out;
}

bool HumanOnline(Guild* guild, Player* except = nullptr)
{
    bool found = false;
    guild->BroadcastWorker([&](Player* p)
    {
        if (p != except && !IsBotSession(p))
            found = true;
    });
    return found;
}

void Queue(Pending::Kind what, Player* bot, ObjectGuid::LowType guildId, ObjectGuid target, std::string text, uint32 delayMs)
{
    if (text.empty())
        return;
    Pending p;
    p.What = what;
    p.Bot = bot->GetGUID();
    p.GuildId = guildId;
    p.Target = target;
    p.Text = std::move(text);
    p.LeftMs = delayMs;
    s_spoke[bot->GetGUID().GetCounter()] = Now();
    s_pending.push_back(std::move(p));
}

uint32 Delay(uint64 seed, uint64 key, uint32 baseMs = 0)
{
    Config const& c = Cfg();
    uint32 const hi = std::max(c.DelayMaxMs, c.DelayMinMs);
    return baseMs + c.DelayMinMs + uint32(BotMove::Unit(BotMove::Mix(seed, key, 9)) * float(hi - c.DelayMinMs));
}

void Flush(Pending const& p)
{
    Player* bot = ObjectAccessor::FindPlayer(p.Bot);
    if (!bot || !bot->IsInWorld() || !bot->GetSession())
        return;
    if (p.What == Pending::GuildChat)
    {
        if (bot->GetGuildId() != p.GuildId)
            return;
        if (Guild* guild = sGuildMgr->GetGuildById(p.GuildId))
            guild->BroadcastToGuild(bot->GetSession(), false, p.Text, LANG_UNIVERSAL);
    }
    else if (Player* target = ObjectAccessor::FindPlayer(p.Target))
        bot->Whisper(p.Text, LANG_UNIVERSAL, target);
}

// The bots that react to `intent`, with their lines, queued for guild chat.
void Answer(Guild* guild, Player* subject, Intent intent, std::vector<Player*> const& bots, std::string_view text, uint32 level, uint32 extraMs = 0)
{
    Config const& cfg = Cfg();
    std::vector<Candidate> cand;
    for (Player* b : bots)
    {
        Candidate c;
        c.Key = b->GetGUID().GetCounter();
        c.Named = !text.empty() && Mentions(text, b->GetName());
        c.SinceSpokeSec = SinceSpoke(c.Key);
        cand.push_back(c);
    }
    uint64 const seed = Seed();
    for (Responder const& r : PickResponders(cand, intent, cfg, seed))
    {
        auto it = std::find_if(bots.begin(), bots.end(), [&](Player* b) { return b->GetGUID().GetCounter() == r.Key; });
        if (it == bots.end())
            continue;
        std::string name = subject ? subject->GetName() : std::string();
        Queue(Pending::GuildChat, *it, guild->GetId(), ObjectGuid::Empty, PickLine(intent, BotMove::Mix(seed, r.Key), name, level, guild->GetName()), r.DelayMs + extraMs);
    }
}

void IdleStep()
{
    Config const& cfg = Cfg();
    if (!cfg.ChatEnabled)
        return;
    std::unordered_map<ObjectGuid::LowType, bool> spoke;   // one idle line per guild and step
    for (Player* bot : sBotMgr->GetOnlineBotPlayers())
    {
        if (IsAlt(bot) || !bot->IsInWorld() || !bot->GetGuildId() || spoke[bot->GetGuildId()])
            continue;
        uint64 const key = bot->GetGUID().GetCounter();
        if (!s_spoke.count(key))
        {
            s_spoke[key] = Now() - int64(urand(0, 20 * 60));   // first sight: start part way into the interval so bots do not all speak together
            continue;
        }
        if (!IdleDue(SinceSpoke(key), key, cfg))
            continue;
        Guild* guild = bot->GetGuild();
        if (!guild || !HumanOnline(guild))
            continue;
        spoke[guild->GetId()] = true;
        Queue(Pending::GuildChat, bot, guild->GetId(), ObjectGuid::Empty, IdleLine(Seed()), Delay(Seed(), key));
    }
}

void AutoStep()
{
    Config const& cfg = Cfg();
    if (!cfg.AutoEnabled || GameTime::GetUptime() < 300)   // wait for the population to log in: guilds are counted by their online leader
        return;

    std::vector<BotGuildPlan::BotInfo> bots;
    std::unordered_map<uint64, Player*> byGuid;
    for (Player* p : sBotMgr->GetOnlineBotPlayers())
    {
        if (IsAlt(p) || !p->IsInWorld())
            continue;
        BotGuildPlan::BotInfo b;
        b.Guid = p->GetGUID().GetCounter();
        b.Faction = p->GetTeam() == ALLIANCE ? 0 : 1;
        b.Level = p->GetLevel();
        b.InGuild = p->GetGuildId() != 0;
        Map const* m = p->GetMap();
        b.Free = p->IsAlive() && !p->IsInCombat() && !p->IsInFlight() && !p->GetGroup() && !p->IsBeingTeleported() && m && !m->Instanceable();
        bots.push_back(b);
        byGuid[b.Guid] = p;
    }

    std::vector<BotGuildPlan::GuildInfo> guilds;
    for (auto const& [id, guild] : sGuildMgr->GetGuildStore())
    {
        BotGuildPlan::GuildInfo g;
        g.Id = id;
        g.Members = guild->GetMembersCount();
        Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
        g.BotLed = leader && IsBotSession(leader) && !IsAlt(leader);
        g.Faction = leader && leader->GetTeam() == HORDE ? 1 : 0;
        guilds.push_back(g);
    }

    uint64 const salt = Seed();
    for (BotGuildPlan::Action const& a : PlanAuto(bots, guilds, cfg, salt))
    {
        Player* bot = byGuid[a.Bot];
        if (!bot || bot->GetGuildId())
            continue;
        if (a.What == BotGuildPlan::Action::Join)
        {
            Guild* guild = sGuildMgr->GetGuildById(a.Guild);
            if (!guild)
                continue;
            CharacterDatabaseTransaction trans(nullptr);
            if (!guild->AddMember(trans, bot->GetGUID()))
                continue;
            Log(a.Bot, "GUILD_BOT_JOINED", StringFormat("joins the guild '{}' ({} members)", guild->GetName(), guild->GetMembersCount()));
            Queue(Pending::GuildChat, bot, guild->GetId(), ObjectGuid::Empty, PickLine(Intent::Joined, Seed(), guild->GetName(), 0, guild->GetName()), Delay(salt, a.Bot, 2000));
        }
        else
        {
            std::string name;
            for (uint32 attempt = 0; attempt < 10 && name.empty(); ++attempt)
            {
                std::string const candidate = GuildName(salt ^ a.Bot, attempt);
                if (!sGuildMgr->GetGuildByName(candidate))
                    name = candidate;
            }
            if (name.empty())
                continue;
            Guild* guild = new Guild;
            if (!guild->Create(bot, name))
            {
                delete guild;
                Log(a.Bot, "GUILD_BOT_FOUND_FAILED", StringFormat("could not found the guild '{}'", name), BOTLOG_WARN);
                continue;
            }
            sGuildMgr->AddGuild(guild);
            guild->HandleSetMOTD(bot->GetSession(), StringFormat("Welcome to {}! Say hello and stay a while.", name));
            Log(a.Bot, "GUILD_BOT_FOUNDED", StringFormat("founds the guild '{}'", name));
        }
    }
}
}

Config const& Cfg()
{
    static Config const cfg = []
    {
        Config c;
        auto num = [](char const* k, int32 def, int32 lo, int32 hi) { return uint32(std::clamp<int32>(sConfigMgr->GetIntDefault(k, def), lo, hi)); };
        c.Enabled = sConfigMgr->GetBoolDefault("Bot.Guild.Enabled", false);
        c.InviteEnabled = sConfigMgr->GetBoolDefault("Bot.Guild.Invite.Enabled", true);
        c.AcceptPct = num("Bot.Guild.Invite.AcceptPct", 100, 0, 100);
        c.InviteMinLevel = num("Bot.Guild.Invite.MinLevel", 1, 1, 80);
        c.ChatEnabled = sConfigMgr->GetBoolDefault("Bot.Guild.Chat.Enabled", true);
        c.GreetPct = num("Bot.Guild.Chat.GreetPct", 60, 0, 100);
        c.ReplyPct = num("Bot.Guild.Chat.ReplyPct", 85, 0, 100);
        c.SmallTalkPct = num("Bot.Guild.Chat.SmallTalkPct", 35, 0, 100);
        c.CongratsPct = num("Bot.Guild.Chat.CongratsPct", 60, 0, 100);
        c.CongratsEvery = num("Bot.Guild.Chat.CongratsEvery", 5, 0, 80);
        c.CooldownSec = num("Bot.Guild.Chat.CooldownSec", 20, 0, 3600);
        c.DelayMinMs = num("Bot.Guild.Chat.DelayMinMs", 1500, 0, 60000);
        c.DelayMaxMs = std::max(c.DelayMinMs, num("Bot.Guild.Chat.DelayMaxMs", 6000, 0, 120000));
        c.IdleMinMin = num("Bot.Guild.Chat.IdleMinMin", 25, 1, 1440);
        c.IdleMaxMin = std::max(c.IdleMinMin, num("Bot.Guild.Chat.IdleMaxMin", 70, 1, 1440));
        c.WhisperInvite = sConfigMgr->GetBoolDefault("Bot.Guild.WhisperInvite", true);
        c.AutoEnabled = sConfigMgr->GetBoolDefault("Bot.Guild.Auto.Enabled", false);
        c.AutoCheckSec = num("Bot.Guild.Auto.CheckSec", 120, 10, 3600);
        c.AutoMaxGuilds = num("Bot.Guild.Auto.MaxGuilds", 10, 1, 500);
        c.AutoMaxMembers = num("Bot.Guild.Auto.MaxMembers", 25, 2, 200);
        c.AutoMinLevel = num("Bot.Guild.Auto.MinLevel", 10, 1, 80);
        c.AutoFoundMinLevel = num("Bot.Guild.Auto.FoundMinLevel", 15, 1, 80);
        c.AutoJoinPct = num("Bot.Guild.Auto.JoinPct", 40, 0, 100);
        c.AutoFoundPct = num("Bot.Guild.Auto.FoundPct", 15, 0, 100);
        c.AutoMaxActions = num("Bot.Guild.Auto.MaxActions", 2, 1, 20);
        return c;
    }();
    return cfg;
}

void OnGuildInvite(Guild* guild, Player* inviter, Player* invitee)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !guild || !inviter || !IsBotSession(invitee) || !invitee->GetGuildIdInvited())
        return;

    InviteFacts f;
    f.InviterIsBot = IsBotSession(inviter);
    f.BotIsAlt = IsAlt(invitee);
    f.InviterIsOwner = inviter->GetSession()->GetAccountId() == invitee->GetSession()->GetAccountId();
    f.BotLevel = invitee->GetLevel();
    f.Roll = urand(0, 99);
    uint64 const key = invitee->GetGUID().GetCounter();

    if (char const* refuse = InviteCheck(f, cfg))
    {
        invitee->SetGuildIdInvited(0);
        Log(key, refuse, StringFormat("declines the invite of {} to '{}'", inviter->GetName(), guild->GetName()));
        if (!f.InviterIsBot && cfg.ChatEnabled && std::string_view(refuse) != "GUILD_INVITE_REFUSED_DISABLED")
            Queue(Pending::Whisper, invitee, 0, inviter->GetGUID(), PickLine(Intent::Declined, Seed(), inviter->GetName()), Delay(Seed(), key));
        return;
    }

    guild->HandleAcceptMember(invitee->GetSession());
    if (invitee->GetGuildId() != guild->GetId())
    {
        invitee->SetGuildIdInvited(0);
        Log(key, "GUILD_INVITE_ACCEPT_FAILED", StringFormat("could not join '{}' after the invite of {}", guild->GetName(), inviter->GetName()), BOTLOG_WARN);
        return;
    }
    Log(key, "GUILD_INVITE_ACCEPTED", StringFormat("joins '{}' on the invite of {}", guild->GetName(), inviter->GetName()));
    if (cfg.ChatEnabled && !f.BotIsAlt)
        Queue(Pending::GuildChat, invitee, guild->GetId(), ObjectGuid::Empty, PickLine(Intent::Joined, Seed(), inviter->GetName(), 0, guild->GetName()), Delay(Seed(), key, 2000));
}

void OnGuildMessage(Player* speaker, Guild* guild, std::string_view text)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !cfg.ChatEnabled || !guild || !speaker || IsBotSession(speaker))
        return;
    std::vector<Player*> bots = GuildBots(guild, speaker);
    if (bots.empty())
        return;

    Intent intent = Classify(text);
    bool named = false;
    for (Player* b : bots)
        named |= Mentions(text, b->GetName());
    if (intent == Intent::None)
    {
        if (!named)
            return;
        intent = Intent::Mention;
    }
    Answer(guild, speaker, intent, bots, text, 0);
}

void OnMemberLogin(Player* player)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !cfg.ChatEnabled || !player || IsBotSession(player))
        return;
    Guild* guild = player->GetGuild();
    if (!guild)
        return;
    std::vector<Player*> bots = GuildBots(guild, player);
    if (!bots.empty())
        Answer(guild, player, Intent::Login, bots, {}, 0, 3000);   // the client is still loading: hold the first line back a little
}

void OnLevelUp(Player* player)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !cfg.ChatEnabled || !player || IsAlt(player) || !CongratsLevel(player->GetLevel(), cfg))
        return;
    Guild* guild = player->GetGuild();
    if (!guild)
        return;
    bool const bot = IsBotSession(player);
    if (bot && !HumanOnline(guild))   // bots do not cheer each other on for nobody
        return;
    std::vector<Player*> bots = GuildBots(guild, player);
    if (!bots.empty())
        Answer(guild, player, Intent::LevelUp, bots, {}, player->GetLevel());
}

void OnWhisper(Player* sender, Player* receiver, std::string_view text)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled || !cfg.WhisperInvite || !sender || !IsBotSession(receiver) || IsBotSession(sender) || !WantsInvite(text))
        return;
    if (IsAlt(receiver) && sender->GetSession()->GetAccountId() != receiver->GetSession()->GetAccountId())
        return;

    uint64 const key = receiver->GetGUID().GetCounter() ^ (sender->GetGUID().GetCounter() << 32);
    if (auto it = s_whisperSeen.find(key); it != s_whisperSeen.end() && Now() - it->second < 60)
        return;
    s_whisperSeen[key] = Now();

    uint64 const seed = Seed();
    uint32 const delay = Delay(seed, key);
    auto say = [&](Intent i, std::string_view guildName)
    {
        Queue(Pending::Whisper, receiver, 0, sender->GetGUID(), PickLine(i, seed, sender->GetName(), 0, guildName), delay);
    };

    Guild* guild = receiver->GetGuild();
    if (sender->GetGuildId())
        return say(Intent::AlreadyInGuild, {});
    if (!guild)
        return say(Intent::NoGuild, {});
    Guild::Member const* member = std::as_const(*guild).GetMember(receiver->GetGUID());
    if (!member || !guild->HasAnyRankRight(member->GetRankId(), GR_RIGHT_INVITE))
        return say(Intent::NoRights, guild->GetName());
    if (sender->GetGuildIdInvited())
        return;

    guild->HandleInviteMember(receiver->GetSession(), sender->GetName());
    if (sender->GetGuildIdInvited() != guild->GetId())
        return;   // the core refused (other faction, ignored ...)
    Log(receiver->GetGUID().GetCounter(), "GUILD_WHISPER_INVITE", StringFormat("invites {} to '{}' on request", sender->GetName(), guild->GetName()));
    say(Intent::InviteSent, guild->GetName());
}

void Update(uint32 diff)
{
    Config const& cfg = Cfg();
    if (!cfg.Enabled)
        return;

    if (!s_pending.empty())
    {
        std::vector<Pending> due;
        for (auto it = s_pending.begin(); it != s_pending.end();)
        {
            if (it->LeftMs <= diff)
            {
                due.push_back(std::move(*it));
                it = s_pending.erase(it);
            }
            else
            {
                it->LeftMs -= diff;
                ++it;
            }
        }
        for (Pending const& p : due)
            Flush(p);
    }

    if ((s_idleMs += diff) >= 15000)
    {
        s_idleMs = 0;
        IdleStep();
    }
    if ((s_autoMs += diff) >= cfg.AutoCheckSec * 1000)
    {
        s_autoMs = 0;
        AutoStep();
    }
}
}
