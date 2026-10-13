/*
 * Group loot rolls for bots and alts. Bots have no client, so the roll window of a group loot item never gets an answer from them and
 * runs into its timeout. With Bot.Loot.Roll.Enabled (default on) a bot votes the moment it sees the roll: Need for a usable upgrade (an
 * item level above what it wears in that slot), else Pass, or Greed with Bot.Loot.Roll.GreedOnOther. A bot with full bags passes.
 */

#ifndef TRINITY_BOT_LOOT_ROLL_H
#define TRINITY_BOT_LOOT_ROLL_H

#include "Define.h"
#include "Optional.h"

struct ItemTemplate;
class Player;
enum class RollVote;

namespace BotLootRoll
{
// The vote of a bot player for an item whose roll allows the votes in `voteMask` (RollMask), or nothing when the feature is off or the
// player is not a bot.
TC_GAME_API Optional<RollVote> ChooseVote(Player* player, ItemTemplate const* item, uint32 voteMask);
}

#endif
