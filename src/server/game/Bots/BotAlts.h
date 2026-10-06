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

#ifndef TRINITY_BOT_ALTS_H
#define TRINITY_BOT_ALTS_H

// Player alts as bots (step A3 of docs/playerbots/next-plan.md): `.bot alt add|remove|list <name>` logs a character of the
// issuer's own account in as a bot through the normal bot login path (BotMgr::StartAlt). Rules: own account only, never while the
// character is online as a player, a bot character cannot log in as a player (CharacterHandler hook), at most Bot.Alt.MaxPerAccount
// active alts per account, events carry "source":"alt". The group leader controls them through BotChat like any other bot.
// Alts are remembered across a restart (table bot_alt in the characters DB, Bot.Alt.Persist, restored once at startup; a missing
// table disables persistence with one log line). Only .bot alt remove / owner despawn forget an alt, never a shutdown.

#include "Define.h"
#include <string>

class ChatHandler;
class Player;
struct BotEvent;

namespace BotAlts
{
// World thread: is this character queued, logging in or online as an alt bot right now?
TC_GAME_API bool IsLoggedInAsBot(uint64 lowGuid);

// Thread-safe (map threads): adds "source":"alt" to the JSON details of events of alt bots. Cheap no-op when no alt was ever added.
TC_GAME_API void TagEvent(BotEvent& event);

// World thread, called by BotMgr::FinishLogin: puts the alt into its owner's group when the owner leads it (or is alone).
TC_GAME_API void OnAltLoggedIn(Player* alt);

// World thread, called every BotMgr::Update: restores the remembered alts on the first call after startup.
TC_GAME_API void RestoreOnce();

// `.bot despawn <name>` for an alt bot: only the owner's own account may log an alt out (the console may, for cleanup). Returns true
// when `name` is an alt bot (the reply was sent, the caller must not despawn anything else), false for any other name.
TC_GAME_API bool DespawnAlt(ChatHandler* handler, std::string const& name);

// `bot alt add|remove|list [name] [test]`. `test` (needs the bot permission) uses the character's own account as the issuer, for
// console tests without a client. Replies go to the handler.
TC_GAME_API bool HandleCommand(ChatHandler* handler, std::string const& op, std::string const& name, bool gmTest);
}

#endif
