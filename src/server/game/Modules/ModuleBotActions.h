/*
 * Applies one bot action a module published through Tier 2 (doc/module-system/03, Tier 2 section).
 *
 * The core only carries out what the module asks, for a bot the module controls. Deciding what a bot does is the
 * module's job. Core checks the owner and that the bot is online (ModuleMgr); this file checks the action itself.
 *
 * Targets are players only for now. A creature's guid needs its map and entry, and the 64-bit guid on the wire does
 * not carry them yet. Actions aimed at a creature are refused with a reason.
 */

#ifndef TRINITYCORE_MODULE_BOT_ACTIONS_H
#define TRINITYCORE_MODULE_BOT_ACTIONS_H

#include "Define.h"
#include "ModuleTier2Layout.h"

#include <string>

class Player;

// Returns true if the action was applied. On false, reason says why it was refused.
bool ApplyBotAction(Player& bot, forever_module::BotAction const& action, std::string& reason);

#endif // TRINITYCORE_MODULE_BOT_ACTIONS_H
