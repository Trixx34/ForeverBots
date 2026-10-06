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

#include "BotAlts.h"
#include "BotMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

namespace BotAlts
{
namespace
{
// Every character that was an alt bot since startup (add-only): used to tag events from map threads, hence the mutex.
std::mutex AltSetMutex;
std::set<uint64> AltGuids;
std::atomic<bool> AnyAlt{false};

std::string JsonEscape(std::string const& s)
{
    std::string out;
    for (char c : s)
    {
        if (uint8(c) < 0x20)
            continue;
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

// bot_event of type alt_command: one per add/remove attempt, accepted or refused (reason codes ALT_*).
void LogAlt(uint64 guid, char const* reason, bool accepted, std::string const& issuer, uint32 accountId, std::string const& charName)
{
    if (!guid)
        return;
    BotEvent event;
    event.BotGuid = guid;
    event.Type = "alt_command";
    event.Severity = accepted ? BOTLOG_INFO : BOTLOG_WARN;
    event.Reason = reason;
    event.Summary = Trinity::StringFormat("alt {} by {}: {}", charName, issuer, reason);
    event.Details = Trinity::StringFormat(R"({{"issuer":"{}","account_id":{},"outcome":"{}"}})", JsonEscape(issuer), accountId, accepted ? "accepted" : "refused");
    sBotMgr->LogEvent(std::move(event)); // LogEvent tags the source
}

uint32 MaxPerAccount()
{
    return uint32(std::max<int32>(1, sConfigMgr->GetIntDefault("Bot.Alt.MaxPerAccount", 4)));
}
}

bool IsLoggedInAsBot(uint64 lowGuid)
{
    if (!AnyAlt.load(std::memory_order_relaxed))
        return false;
    return sBotMgr->IsActiveAlt(lowGuid);
}

void TagEvent(BotEvent& event)
{
    if (!AnyAlt.load(std::memory_order_relaxed))
        return;
    {
        std::lock_guard<std::mutex> lock(AltSetMutex);
        if (!AltGuids.contains(event.BotGuid))
            return;
    }
    if (event.Details.empty())
        event.Details = R"({"source":"alt"})";
    else if (event.Details.front() == '{' && event.Details.find(R"("source":"alt")") == std::string::npos)
        event.Details.insert(1, event.Details.size() > 2 ? R"("source":"alt",)" : R"("source":"alt")");
}

void OnAltLoggedIn(Player* alt)
{
    WorldSession* ownerSession = sWorld->FindSession(alt->GetSession()->GetAccountId());
    Player* owner = ownerSession ? ownerSession->GetPlayer() : nullptr;
    if (!owner || owner == alt || !owner->IsInWorld() || alt->GetGroup())
        return;

    Group* group = owner->GetGroup();
    if (!group)
    {
        group = new Group();
        if (!group->Create(owner))
        {
            delete group;
            return;
        }
        sGroupMgr->AddGroup(group);
    }
    else if (group->GetLeaderGUID() != owner->GetGUID())
    {
        ChatHandler(ownerSession).PSendSysMessage("%s is online as a bot but you do not lead your group: its leader must invite it.", alt->GetName().c_str());
        return;
    }
    else if (group->IsFull())
    {
        ChatHandler(ownerSession).PSendSysMessage("%s is online as a bot but your group is full.", alt->GetName().c_str());
        return;
    }

    group->AddMember(alt);
}

bool HandleCommand(ChatHandler* handler, std::string const& op, std::string const& rawName, bool gmTest)
{
    WorldSession* session = handler->GetSession();
    std::string const issuer = session && session->GetPlayer() ? session->GetPlayer()->GetName() : std::string("console");

    if (!sWorld->getBoolConfig(CONFIG_BOT_ENABLED) || !sConfigMgr->GetBoolDefault("Bot.Alt.Enabled", true))
    {
        handler->PSendSysMessage("%s", "Alt bots are disabled.");
        return false;
    }

    if (op == "list")
    {
        if (!session)
        {
            handler->PSendSysMessage("%s", "Use this from a game client (the list is per account).");
            return false;
        }
        std::vector<std::string> names = sBotMgr->GetActiveAltNames(session->GetAccountId());
        std::string text;
        for (std::string const& n : names)
            text += (text.empty() ? "" : ", ") + n;
        handler->PSendSysMessage("Alt bots (%zu of %u): %s", names.size(), MaxPerAccount(), text.empty() ? "none" : text.c_str());
        return true;
    }

    bool const add = op == "add";
    if (!add && op != "remove")
    {
        handler->PSendSysMessage("%s", "Use: .bot alt add|remove|list <character name>");
        return false;
    }

    if (gmTest && !handler->HasPermission(rbac::RBAC_PERM_COMMAND_BOT))
        gmTest = false;
    if (!session && !gmTest)
    {
        handler->PSendSysMessage("%s", "From the console add the word test after the name (the character's own account is used).");
        return false;
    }

    std::string name = rawName;
    if (name.empty() || !normalizePlayerName(name))
    {
        handler->PSendSysMessage("%s", "Give the exact character name.");
        return false;
    }

    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByName(name);
    uint32 const accountId = gmTest && entry ? entry->AccountId : (session ? session->GetAccountId() : 0);
    // same answer for unknown names and other accounts' characters: do not reveal who owns what
    if (!entry || entry->IsDeleted || entry->AccountId != accountId)
    {
        if (entry)
            LogAlt(entry->Guid.GetCounter(), "ALT_REFUSED_NOT_OWNER", false, issuer, accountId, entry->Name);
        TC_LOG_INFO("server.worldserver", "BotAlts: {} refused for '{}' by {} (not a character of the account)", op, name, issuer);
        handler->PSendSysMessage("%s", "That is not one of your characters.");
        return false;
    }

    uint64 const guid = entry->Guid.GetCounter();

    if (!add)
    {
        if (!sBotMgr->StopAlt(guid))
        {
            handler->PSendSysMessage("%s is not logged in as a bot.", entry->Name.c_str());
            return false;
        }
        LogAlt(guid, "ALT_REMOVED", true, issuer, accountId, entry->Name);
        handler->PSendSysMessage("%s is being logged out and saved.", entry->Name.c_str());
        return true;
    }

    char const* refuse = nullptr;
    char const* text = nullptr;
    Player* online = ObjectAccessor::FindConnectedPlayer(entry->Guid);
    if (online && !(online->GetSession() && online->GetSession()->IsBot()))
    {
        refuse = "ALT_REFUSED_ONLINE";
        text = "is online as a player: log it out first.";
    }
    else
    {
        // a real session of the account that is in the middle of a character login could be loading this very character
        for (auto const& [id, s] : sWorld->GetAllSessions())
            if (s && s->GetAccountId() == accountId && !s->IsBot() && s->PlayerLoading())
            {
                refuse = "ALT_REFUSED_LOADING";
                text = "cannot be added while a character of your account is logging in.";
                break;
            }
    }
    if (!refuse && sBotMgr->IsActiveAlt(guid))
    {
        refuse = "ALT_REFUSED_ALREADY";
        text = "is already logged in as a bot.";
    }
    if (!refuse && sBotMgr->CountActiveAlts(accountId) >= MaxPerAccount())
    {
        refuse = "ALT_REFUSED_CAP";
        text = "cannot be added: the alt bot limit of your account is reached.";
    }

    std::string error;
    if (!refuse && !sBotMgr->StartAlt(guid, accountId, session ? session->GetAccountName() : std::string("alt"),
        entry->Name, entry->Race, entry->Class, entry->Sex, entry->Level, error))
    {
        refuse = "ALT_REFUSED_BOT";
        text = "cannot be added: it is a reserved bot character.";
    }

    if (refuse)
    {
        LogAlt(guid, refuse, false, issuer, accountId, entry->Name);
        handler->PSendSysMessage("%s %s", entry->Name.c_str(), text);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(AltSetMutex);
        AltGuids.insert(guid);
    }
    AnyAlt.store(true, std::memory_order_relaxed);
    LogAlt(guid, "ALT_ADDED", true, issuer, accountId, entry->Name);
    handler->PSendSysMessage("%s is logging in as a bot (it joins your group when you lead it or are alone).", entry->Name.c_str());
    return true;
}
}
