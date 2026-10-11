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

#include "BotGuildPlan.h"
#include "BotMovePlan.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace BotGuildPlan
{
using BotMove::Mix;
using BotMove::Unit;

namespace
{
// Lower case words of letters and digits; apostrophes are dropped ("how's" -> "hows").
std::vector<std::string> Words(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text)
    {
        unsigned char const c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
            cur += char(std::tolower(c));
        else if (ch == '\'' || ch == '`')
            continue;
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

bool Has(std::vector<std::string> const& words, std::string_view w)
{
    return std::find(words.begin(), words.end(), w) != words.end();
}

template <size_t N>
bool HasAny(std::vector<std::string> const& words, std::array<std::string_view, N> const& list)
{
    for (std::string_view w : list)
        if (Has(words, w))
            return true;
    return false;
}

// The words joined with single spaces, for phrases.
std::string Joined(std::vector<std::string> const& words)
{
    std::string out;
    for (std::string const& w : words)
    {
        if (!out.empty())
            out += ' ';
        out += w;
    }
    return out;
}

bool ContainsPhrase(std::string const& joined, std::string_view phrase)
{
    size_t pos = 0;
    while ((pos = joined.find(phrase, pos)) != std::string::npos)
    {
        bool const startOk = pos == 0 || joined[pos - 1] == ' ';
        size_t const end = pos + phrase.size();
        bool const endOk = end == joined.size() || joined[end] == ' ';
        if (startOk && endOk)
            return true;
        ++pos;
    }
    return false;
}

template <size_t N>
std::string Pick(std::array<std::string_view, N> const& pool, uint64 seed)
{
    return std::string(pool[size_t(Mix(seed, 0x6c696e65) % N)]);
}

void Replace(std::string& s, std::string_view token, std::string_view with)
{
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos)
    {
        s.replace(pos, token.size(), with);
        pos += with.size();
    }
}

uint32 Pct(uint64 h) { return uint32(Unit(h) * 100.0f); }
}

char const* InviteCheck(InviteFacts const& f, Config const& cfg)
{
    if (!cfg.Enabled || !cfg.InviteEnabled)
        return "GUILD_INVITE_REFUSED_DISABLED";
    if (f.InviterIsBot)
        return "GUILD_INVITE_REFUSED_BOT_INVITER";
    if (f.BotIsAlt && !f.InviterIsOwner)
        return "GUILD_INVITE_REFUSED_NOT_OWNER";
    if (f.BotLevel < cfg.InviteMinLevel)
        return "GUILD_INVITE_REFUSED_LEVEL";
    if (!f.BotIsAlt && f.Roll >= cfg.AcceptPct)
        return "GUILD_INVITE_DECLINED";
    return nullptr;
}

Intent Classify(std::string_view text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty())
        return Intent::None;

    std::string const joined = Joined(words);
    if (words.size() <= 9)
    {
        for (std::string_view p : { "how are you", "how r u", "how are u", "hows it going", "how is it going", "how is everyone", "hows everyone", "how are you all", "how you doing", "how are things" })
            if (ContainsPhrase(joined, p))
                return Intent::HowAreYou;
    }
    if (words.size() > 5)
        return Intent::None;

    if (HasAny(words, std::array<std::string_view, 6>{ "thanks", "thx", "thank", "ty", "tyvm", "thankyou" }))
        return Intent::Thanks;
    if (HasAny(words, std::array<std::string_view, 7>{ "bye", "cya", "gn", "goodnight", "laters", "farewell", "ttyl" }) ||
        ContainsPhrase(joined, "see you") || ContainsPhrase(joined, "see ya") || ContainsPhrase(joined, "good night") ||
        ContainsPhrase(joined, "gotta go") || ContainsPhrase(joined, "got to go") || ContainsPhrase(joined, "heading out"))
        return Intent::Farewell;
    if (HasAny(words, std::array<std::string_view, 9>{ "hi", "hello", "hey", "heya", "hiya", "yo", "sup", "howdy", "greetings" }) ||
        ContainsPhrase(joined, "good morning") || ContainsPhrase(joined, "good evening") || ContainsPhrase(joined, "good afternoon") ||
        ContainsPhrase(joined, "whats up") || ContainsPhrase(joined, "wassup"))
        return Intent::Greeting;
    return Intent::None;
}

bool Mentions(std::string_view text, std::string_view botName)
{
    if (botName.empty())
        return false;
    std::string name;
    for (char ch : botName)
        name += char(std::tolower(static_cast<unsigned char>(ch)));
    for (std::string const& w : Words(text))
        if (w == name)
            return true;
    return false;
}

bool WantsInvite(std::string_view text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty() || words.size() > 8)
        return false;
    if (HasAny(words, std::array<std::string_view, 5>{ "inv", "invite", "ginv", "ginvite", "invit" }))
        return true;
    return Has(words, "guild") && Has(words, "join");
}

std::string PickLine(Intent intent, uint64 seed, std::string_view name, uint32 level, std::string_view guild)
{
    using SV = std::string_view;
    std::string line;
    switch (intent)
    {
        case Intent::Greeting:
            line = Pick(std::array<SV, 8>{ "hey {n}!", "hello {n}", "hi {n}, how's it going?", "o/ {n}", "yo {n}", "heya {n}", "hey hey", "hi there" }, seed);
            break;
        case Intent::Farewell:
            line = Pick(std::array<SV, 7>{ "cya {n}!", "bye {n}, take care", "see you later {n}", "gn {n}", "later {n}!", "safe travels {n}", "bye bye" }, seed);
            break;
        case Intent::Thanks:
            line = Pick(std::array<SV, 6>{ "np {n}", "no problem!", "anytime {n}", "you're welcome {n}", "happy to help", "no worries" }, seed);
            break;
        case Intent::HowAreYou:
            line = Pick(std::array<SV, 7>{ "doing good {n}, you?", "not bad, just questing around", "pretty good, busy day", "can't complain {n}", "all good here, thanks for asking", "tired of the grind but good", "great now that I got a break" }, seed);
            break;
        case Intent::Mention:
            line = Pick(std::array<SV, 8>{ "yes {n}?", "hm? what's up {n}", "I'm here {n}", "what's up?", "you called {n}?", "hey {n}, need something?", "yep?", "listening {n}" }, seed);
            break;
        case Intent::Login:
            line = Pick(std::array<SV, 8>{ "hey {n}, welcome back!", "wb {n}", "welcome back {n}!", "o/ {n}", "{n}! good to see you", "hi {n}, glad you're here", "look who's online, hey {n}", "hey {n}!" }, seed);
            break;
        case Intent::LevelUp:
            line = Pick(std::array<SV, 8>{ "grats {n}!", "gz {n}, level {l}!", "congrats {n}, level {l} nice", "nice one {n}!", "gratz {n}", "level {l}! well done {n}", "gz {n}!", "woo level {l}, congrats {n}" }, seed);
            break;
        case Intent::Joined:
            line = Pick(std::array<SV, 7>{ "hello everyone!", "hi all, glad to be here", "thanks for the invite {n}!", "hey guild!", "o/ everyone", "happy to join {g}", "thanks {n}, hello all" }, seed);
            break;
        case Intent::InviteSent:
            line = Pick(std::array<SV, 6>{ "sure {n}, sending an invite to {g}", "invite on its way {n}, welcome to {g}!", "okay {n}, check your invites", "{n} you got it, invite sent", "inviting you to {g} now", "inviting you {n}, join us in {g}" }, seed);
            break;
        case Intent::NoGuild:
            line = Pick(std::array<SV, 6>{ "I'm not in a guild right now {n}", "sorry {n}, I have no guild at the moment", "no guild here {n}, sorry", "I'm guildless myself {n}", "can't help with that {n}, I'm not in a guild", "I'm between guilds {n}, sorry" }, seed);
            break;
        case Intent::NoRights:
            line = Pick(std::array<SV, 6>{ "I can't invite in {g}, {n}, ask an officer", "sorry {n}, I'm not allowed to invite in {g}", "{n}, only the officers of {g} can invite", "no invite rights in {g}, sorry {n}", "ask the guild master of {g} {n}, I can't invite", "can't invite you {n}, I'm just a member of {g}" }, seed);
            break;
        case Intent::AlreadyInGuild:
            line = Pick(std::array<SV, 6>{ "you're already in a guild {n}", "{n}, you're in a guild already", "leave your guild first {n}", "you already have a guild {n}", "can't, you're guilded already {n}", "{n} you already belong to a guild" }, seed);
            break;
        case Intent::Declined:
            line = Pick(std::array<SV, 6>{ "sorry {n}, I'm not looking for a guild right now", "thanks {n}, but I'll pass", "no thanks {n}, I'm good for now", "maybe another time {n}", "sorry {n}, not now", "I'll stay where I am {n}, thanks" }, seed);
            break;
        case Intent::None:
            break;
    }
    Replace(line, "{n}", name);
    Replace(line, "{l}", std::to_string(level));
    Replace(line, "{g}", guild);
    return line;
}

std::string IdleLine(uint64 seed)
{
    using SV = std::string_view;
    return Pick(std::array<SV, 12>{
        "anyone up for some questing?", "quiet in here today", "the auction house prices are wild today", "what's everyone working on?",
        "I could use a good dungeon run", "who's online?", "just finished a long quest chain, finally", "anyone need help with a quest?",
        "found a great spot to grind, if anyone wants company", "my bags are full again", "need to repair, again...", "hope everyone's doing well" }, seed);
}

std::vector<Responder> PickResponders(std::vector<Candidate> const& bots, Intent intent, Config const& cfg, uint64 seed)
{
    std::vector<Responder> out;
    if (!cfg.Enabled || !cfg.ChatEnabled || bots.empty())
        return out;

    uint32 cap = 1, pct = 0;
    bool named = false;
    std::vector<Candidate const*> pool;
    switch (intent)
    {
        case Intent::Login:
            cap = 2; pct = cfg.GreetPct;
            for (Candidate const& c : bots)
                if (c.SinceSpokeSec >= cfg.CooldownSec)
                    pool.push_back(&c);
            break;
        case Intent::LevelUp:
            cap = 2; pct = cfg.CongratsPct;
            for (Candidate const& c : bots)
                if (c.SinceSpokeSec >= cfg.CooldownSec)
                    pool.push_back(&c);
            break;
        case Intent::Mention:
        case Intent::Greeting:
        case Intent::Farewell:
        case Intent::Thanks:
        case Intent::HowAreYou:
            for (Candidate const& c : bots)
                if (c.Named && c.SinceSpokeSec >= cfg.CooldownSec / 2)
                    pool.push_back(&c);
            named = !pool.empty();
            if (named)
            {
                cap = 2; pct = cfg.ReplyPct;
            }
            else if (intent == Intent::Mention || std::any_of(bots.begin(), bots.end(), [](Candidate const& c) { return c.Named; }))
                return out;   // named, but every named bot is on cooldown
            else
            {
                pct = cfg.SmallTalkPct;
                for (Candidate const& c : bots)
                    if (c.SinceSpokeSec >= cfg.CooldownSec)
                        pool.push_back(&c);
            }
            break;
        default:
            return out;
    }
    if (pool.empty())
        return out;

    std::stable_sort(pool.begin(), pool.end(), [seed](Candidate const* a, Candidate const* b) { return Mix(seed, a->Key, 1) < Mix(seed, b->Key, 1); });
    std::vector<uint64> chosen;
    for (Candidate const* c : pool)
    {
        if (chosen.size() >= cap)
            break;
        if (Pct(Mix(seed, c->Key, 2)) < pct)
            chosen.push_back(c->Key);
    }
    uint32 const lo = cfg.DelayMinMs, hi = std::max(cfg.DelayMaxMs, cfg.DelayMinMs);
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        float const slot = (float(i) + Unit(Mix(seed, chosen[i], 3))) / float(chosen.size());
        out.push_back({ chosen[i], lo + uint32(slot * float(hi - lo)) });
    }
    return out;
}

bool CongratsLevel(uint32 level, Config const& cfg)
{
    return cfg.CongratsEvery > 0 && level >= cfg.CongratsEvery && level % cfg.CongratsEvery == 0;
}

bool IdleDue(uint32 sinceSpokeSec, uint64 botKey, Config const& cfg)
{
    uint32 const lo = cfg.IdleMinMin, hi = std::max(cfg.IdleMaxMin, cfg.IdleMinMin);
    uint32 const minutes = lo + uint32(Unit(Mix(botKey, 19)) * float(hi - lo));
    return sinceSpokeSec >= minutes * 60;
}

std::vector<Action> PlanAuto(std::vector<BotInfo> const& bots, std::vector<GuildInfo> const& guilds, Config const& cfg, uint64 salt)
{
    std::vector<Action> out;
    if (!cfg.Enabled || !cfg.AutoEnabled || cfg.AutoMaxActions == 0)
        return out;

    std::vector<BotInfo const*> free;
    for (BotInfo const& b : bots)
        if (!b.InGuild && b.Free && b.Level >= cfg.AutoMinLevel)
            free.push_back(&b);
    std::stable_sort(free.begin(), free.end(), [salt](BotInfo const* a, BotInfo const* b) { return Mix(salt, a->Guid) < Mix(salt, b->Guid); });

    std::vector<GuildInfo> work = guilds;
    uint32 botLed = uint32(std::count_if(work.begin(), work.end(), [](GuildInfo const& g) { return g.BotLed; }));
    bool founded[2] = { false, false };

    for (BotInfo const* b : free)
    {
        if (out.size() >= cfg.AutoMaxActions)
            break;
        GuildInfo* best = nullptr;
        for (GuildInfo& g : work)
        {
            if (!g.BotLed || g.Faction != b->Faction || g.Members >= cfg.AutoMaxMembers)
                continue;
            if (!best || g.Members < best->Members || (g.Members == best->Members && g.Id < best->Id))
                best = &g;
        }
        if (best)
        {
            if (Pct(Mix(salt, b->Guid, 1)) < cfg.AutoJoinPct)
            {
                out.push_back({ Action::Join, b->Guid, best->Id });
                ++best->Members;
            }
            continue;
        }
        uint8 const f = b->Faction & 1;
        if (!founded[f] && botLed < cfg.AutoMaxGuilds && b->Level >= cfg.AutoFoundMinLevel && Pct(Mix(salt, b->Guid, 2)) < cfg.AutoFoundPct)
        {
            out.push_back({ Action::Found, b->Guid, 0 });
            founded[f] = true;
            ++botLed;
        }
    }
    return out;
}

std::string GuildName(uint64 seed, uint32 attempt)
{
    using SV = std::string_view;
    static constexpr std::array<SV, 20> adjectives = { "Silver", "Crimson", "Iron", "Golden", "Shadow", "Storm", "Ember", "Frost", "Wild", "Silent",
        "Bold", "Ancient", "Hollow", "Radiant", "Grim", "Swift", "Emerald", "Twilight", "Burning", "Lost" };
    static constexpr std::array<SV, 20> nouns = { "Wolves", "Ravens", "Blades", "Lanterns", "Vanguard", "Wardens", "Pilgrims", "Hammers", "Banners", "Foxes",
        "Drakes", "Hawks", "Reavers", "Sentinels", "Wanderers", "Oaks", "Embers", "Anvils", "Harbingers", "Stags" };
    static constexpr std::array<SV, 6> places = { "Dawn", "Dusk", "North", "Vale", "Mist", "Hearth" };

    uint64 const h = Mix(seed, attempt, 0x6e616d65);
    SV const adj = adjectives[size_t(Mix(h, 1) % adjectives.size())];
    SV const noun = nouns[size_t(Mix(h, 2) % nouns.size())];
    SV const place = places[size_t(Mix(h, 3) % places.size())];
    switch (Mix(h, 4) % 3)
    {
        case 0:  return "The " + std::string(adj) + " " + std::string(noun);
        case 1:  return std::string(adj) + " " + std::string(noun);
        default: return std::string(noun) + " of the " + std::string(place);
    }
}
}
