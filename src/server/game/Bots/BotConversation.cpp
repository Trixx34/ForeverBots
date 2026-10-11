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

#include "BotConversation.h"
#include "BotAI.h"
#include "BotChat.h"
#include "BotConversationText.h"
#include "BotMgr.h"
#include "ChatPackets.h"
#include "Config.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <vector>

namespace BotConversation
{
namespace
{
using namespace BotConversationText;

struct Cfg
{
    bool Enabled = false;                // Bot.Conversation.Enabled
    bool Whisper = true;                 // Bot.Conversation.Whisper.Enabled
    bool Party = true;                   // Bot.Conversation.Party.Enabled
    uint32 ChatterPct = 35;              // Bot.Conversation.Party.ChatterPct
    bool RandomPersonality = true;       // Bot.Conversation.Personality = random
    Personality Fixed = Personality::Friendly;
    uint32 DelayMinMs = 800;             // Bot.Conversation.Delay.MinMs
    uint32 DelayMaxMs = 3500;            // Bot.Conversation.Delay.MaxMs
    uint32 CooldownMs = 4000;            // Bot.Conversation.CooldownSec
    bool LogEvents = true;               // Bot.Conversation.LogEvents
};

constexpr size_t PENDING_CAP = 64;
constexpr size_t COOLDOWN_MAP_CAP = 4096;

Cfg const& Config()
{
    static Cfg const cfg = []
    {
        Cfg c;
        c.Enabled = sConfigMgr->GetBoolDefault("Bot.Conversation.Enabled", false);
        c.Whisper = sConfigMgr->GetBoolDefault("Bot.Conversation.Whisper.Enabled", true);
        c.Party = sConfigMgr->GetBoolDefault("Bot.Conversation.Party.Enabled", true);
        c.ChatterPct = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Conversation.Party.ChatterPct", 35), 0, 100));
        std::string const personality = sConfigMgr->GetStringDefault("Bot.Conversation.Personality", "random");
        c.RandomPersonality = !ParsePersonality(personality, c.Fixed);
        c.DelayMinMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Conversation.Delay.MinMs", 800), 0, 10000));
        c.DelayMaxMs = std::max(c.DelayMinMs, uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Conversation.Delay.MaxMs", 3500), 0, 15000)));
        c.CooldownMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("Bot.Conversation.CooldownSec", 4), 0, 600)) * 1000;
        c.LogEvents = sConfigMgr->GetBoolDefault("Bot.Conversation.LogEvents", true);
        return c;
    }();
    return cfg;
}

struct Pending
{
    uint32 DueMs = 0;
    ObjectGuid Bot;
    ObjectGuid Target;     // the player who spoke
    bool Party = false;    // party chat, else whisper
    Intent Topic = Intent::None;
    uint32 Seed = 0;
};

std::deque<Pending> s_pending;
std::unordered_map<uint64, uint32> s_lastReply;   // bot guid counter -> game time ms of its last reply

bool IsBotPlayer(Player* p) { return p && p->GetSession() && p->GetSession()->IsBot() && p->GetSession()->GetBotAI(); }

Personality PersonalityOf(Player* bot)
{
    Cfg const& cfg = Config();
    return cfg.RandomPersonality ? PersonalityFromSeed(bot->GetGUID().GetCounter()) : cfg.Fixed;
}

// A chat command ("follow", "g1 stay", "tank pull") belongs to BotChat, even from a player who may not give it.
bool CommandShaped(std::string_view text)
{
    auto next = [&text]() -> std::string_view
    {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        size_t const end = text.find(' ');
        std::string_view const tok = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
        return tok;
    };
    BotChat::RoleMasks const roles;
    BotChat::Selector sel;
    std::string_view tok = next();
    if (tok.empty())
        return false;
    if (BotChat::ParseSelector(tok, roles, sel))
        tok = next();
    return BotChat::ParseVerb(tok, true) != BotChat::Verb::None;
}

bool CoolingDown(Player* bot, uint32 now)
{
    auto it = s_lastReply.find(bot->GetGUID().GetCounter());
    return it != s_lastReply.end() && now - it->second < Config().CooldownMs;
}

void Queue(Player* bot, Player* target, bool party, Intent topic)
{
    uint32 const now = GameTime::GetGameTimeMS();
    if (s_pending.size() >= PENDING_CAP || CoolingDown(bot, now))
        return;
    for (Pending const& p : s_pending)
        if (p.Bot == bot->GetGUID())
            return;   // one line in flight per bot
    if (s_lastReply.size() >= COOLDOWN_MAP_CAP)
        s_lastReply.clear();
    s_lastReply[bot->GetGUID().GetCounter()] = now;

    Pending p;
    p.Bot = bot->GetGUID();
    p.Target = target->GetGUID();
    p.Party = party;
    p.Topic = topic;
    p.Seed = urand(0, 0x7FFFFFFF);
    Cfg const& cfg = Config();
    p.DueMs = now + TypingDelayMs(40, p.Seed, cfg.DelayMinMs, cfg.DelayMaxMs);
    s_pending.push_back(p);
}

Facts FactsOf(Player* bot, Player* target)
{
    Facts f;
    f.Player = target->GetName();
    f.Bot = bot->GetName();
    f.Class = BotMgr::ClassName(bot->GetClass());
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetZoneId()))
        f.Zone = area->AreaName[DEFAULT_LOCALE];
    if (f.Zone.empty())
        f.Zone = "the wild";
    f.Level = bot->GetLevel();
    f.HealthPct = uint32(std::clamp(bot->GetHealthPct(), 0.0f, 100.0f));
    f.Activity = ActivityText(!bot->IsAlive(), bot->IsInCombat(), f.HealthPct, bot->GetGroup() != nullptr);
    return f;
}

// Plain chat from a player: not a command, no item/quest links (BotTradeLink and others read those).
bool Chatty(Player* sender, std::string_view text)
{
    return Config().Enabled && sender && sender->GetSession() && !sender->GetSession()->IsBot() && !text.empty() &&
        text.find("|H") == std::string_view::npos && !CommandShaped(text);
}
}

void OnWhisper(Player* sender, Player* receiver, std::string_view text)
{
    if (!Config().Whisper || !IsBotPlayer(receiver) || receiver == sender || !Chatty(sender, text))
        return;
    Queue(receiver, sender, false, Classify(text));
}

void OnPartyChat(Player* sender, std::string_view text)
{
    if (!Config().Party || !Chatty(sender, text))
        return;
    Group* group = sender->GetGroup();
    if (!group)
        return;

    std::vector<Player*> bots;
    uint8 const sub = group->GetMemberGroup(sender->GetGUID());
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == sender->GetGUID() || (group->isRaidGroup() && group->GetMemberGroup(slot.guid) != sub))
            continue;
        Player* p = ObjectAccessor::FindConnectedPlayer(slot.guid);
        if (IsBotPlayer(p))
            bots.push_back(p);
    }
    if (bots.empty())
        return;

    Intent const topic = Classify(text);
    uint32 answered = 0;
    for (Player* bot : bots)
        if (Addresses(text, bot->GetName()) && answered < 2)
        {
            Queue(bot, sender, true, topic);
            ++answered;
        }
    if (answered || !IsChatter(topic) || urand(0, 99) >= Config().ChatterPct)
        return;
    Queue(bots[urand(0, uint32(bots.size()) - 1)], sender, true, topic);
}

void Update(uint32 /*diff*/)
{
    if (s_pending.empty())
        return;
    uint32 const now = GameTime::GetGameTimeMS();
    for (size_t i = 0; i < s_pending.size();)
    {
        Pending const p = s_pending[i];
        if (int32(now - p.DueMs) < 0)
        {
            ++i;
            continue;
        }
        s_pending.erase(s_pending.begin() + i);

        Player* bot = ObjectAccessor::FindConnectedPlayer(p.Bot);
        Player* target = ObjectAccessor::FindConnectedPlayer(p.Target);
        if (!IsBotPlayer(bot) || !target)
            continue;
        Group* group = bot->GetGroup();
        if (p.Party && (!group || !group->IsMember(target->GetGUID())))
            continue;

        Personality const personality = PersonalityOf(bot);
        std::string const line = Reply(personality, p.Topic, FactsOf(bot, target), p.Seed);
        if (p.Party)
        {
            WorldPackets::Chat::Chat packet;
            packet.Initialize(CHAT_MSG_PARTY, LANG_UNIVERSAL, bot, nullptr, line);
            group->BroadcastPacket(packet.Write(), false, group->GetMemberGroup(bot->GetGUID()));
        }
        else
            bot->Whisper(line, LANG_UNIVERSAL, target);

        if (Config().LogEvents)
            bot->GetSession()->GetBotAI()->EmitEvent(bot, "conversation", BOTLOG_INFO, "CONV_REPLY",
                Trinity::StringFormat("{} answered {} ({}, {})", bot->GetName(), target->GetName(), p.Party ? "party" : "whisper", PersonalityName(personality)),
                Trinity::StringFormat("{{\"player\":\"{}\",\"channel\":\"{}\",\"personality\":\"{}\",\"intent\":{}}}",
                    target->GetName(), p.Party ? "party" : "whisper", PersonalityName(personality), uint32(p.Topic)));
    }
}
}
