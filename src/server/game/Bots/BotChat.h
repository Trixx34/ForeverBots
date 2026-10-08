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

#ifndef TRINITY_BOT_CHAT_H
#define TRINITY_BOT_CHAT_H

// Chat commands for the native player bots (step A2 of docs/playerbots/next-plan.md).
// The party/raid leader controls the bots of the group through party chat, raid chat or a whisper to one bot:
//   [selector] <verb> [args]      selector: all | g1 | g2 | g3-g4 | g1,g3 | tank | healer | dps | <class>   (comma lists: same kind = union, subgroup + role/class = intersection, e.g. g1,tank)
//   verbs (non-combat): follow [off] | stay [off] | goto here|<x> <y> [z] | rest [off] | release | status | strategy [+a,-b] | verbose on|off
// Everyone else is ignored and logged (rate limited). Handle() is the single entry point: the ChatHandler hooks and the
// `bot say` test command both call it. It never changes or consumes the chat message and does no database access.

#include "Define.h"
#include <functional>
#include <string>
#include <string_view>

class ChatHandler;
class Player;

namespace BotChat
{
enum class Channel : uint8 { Party, Raid, Whisper };

// Target selector of a chat command. Subgroup parts are a union among themselves, role/class parts a union among themselves, and when both
// kinds are present they intersect (g1,tank = tanks inside subgroup 1). A role/class part whose class mask is empty matches nothing.
struct Selector
{
    bool All = false;
    uint8 SubMask = 0;     // bit n = raid subgroup n+1
    bool HasClass = false; // a role/class part was given (even when its mask is empty)
    uint32 ClassMask = 0;  // bit classId

    bool Matches(uint8 slotGroup, uint8 classId) const
    {
        if (All)
            return true;
        if (!SubMask && !HasClass)
            return false;
        return (!SubMask || (SubMask & (1u << slotGroup))) && (!HasClass || (ClassMask & (1u << classId)));
    }
};

struct RoleMasks { uint32 Tank = 0, Healer = 0, Dps = 0; };   // class masks behind the tank / healer / dps selectors (Bot.Chat.Role.*)

// Pure parser (no config, no world state): all | g1 | g3-g4 | g1,g3 | tank | healer | dps | <class>, each also in the plural (healers, warriors).
TC_GAME_API bool ParseSelector(std::string_view token, RoleMasks const& roles, Selector& out);

struct GotoArgs { bool Here = false; bool HasZ = false; float X = 0, Y = 0, Z = 0; };

// Pure parser for the goto arguments: "here" or "<x> <y> [z]". Returns nullptr when valid, else the refusal code (BAD_ARGS).
// Non finite or out of range coordinates are refused.
TC_GAME_API char const* ParseGotoArgs(std::string_view args, GotoArgs& out);

// Receives the reply lines instead of the issuer's client (test path).
using ReplySink = std::function<void(std::string const&)>;

// World thread, outside map updates. `whisperTarget` is the whispered player (Channel::Whisper only).
// Returns true when the text was command-shaped (a command attempt: accepted, refused or ignored as unauthorized).
TC_GAME_API bool Handle(Player* issuer, Channel channel, std::string_view text, Player* whisperTarget = nullptr, ReplySink const* sink = nullptr);

// Any player logs out (WorldSession::LogoutPlayer, world thread): forgets the per-issuer verbose setting and unauthorized-command record.
TC_GAME_API void OnPlayerLogout(uint64 guidCounter);

// `bot say <issuer> <party|raid|whisper> <text>` (for whisper the text starts with the bot's name); replies are printed to the handler.
TC_GAME_API bool TestSay(ChatHandler* handler, std::string const& issuerName, std::string const& channelName, std::string_view text);

// `bot group form|move|disband|disbandall|list ...`: builds scripted parties/raids of bots for tests.
TC_GAME_API bool TestGroup(ChatHandler* handler, std::string const& op, std::string_view args);
}

#endif
