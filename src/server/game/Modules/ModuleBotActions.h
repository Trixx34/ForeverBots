/*
 * Applies one bot action a module published through Tier 2 (doc/module-system/03, Tier 2 section).
 *
 * The core only carries out what the module asks, for a bot the module controls. Deciding what a bot does is the
 * module's job. Core checks the owner and that the bot is online (ModuleMgr); this file checks the action itself.
 *
 * Targets are unit references (forever_module::MakeUnitRef): a player by its database guid, or a creature by entry
 * and counter on the bot's map. An attack needs a live target that is hostile to the bot; other actions need only a
 * unit on the bot's map. Summoned creatures are reachable too, since the reference carries the entry and counter.
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
