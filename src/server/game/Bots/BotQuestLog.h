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

#ifndef TRINITY_BOT_QUEST_LOG_H
#define TRINITY_BOT_QUEST_LOG_H

#include "Define.h"

class Object;
class Player;
class Quest;
struct QuestObjective;
enum class LootItemType : uint8;

// Quest step logging for bots (event_type 'quest'). Called from small IsBot-guarded hooks in Player quest code; works with or
// without the AI engine. Builds events on the calling (map) thread and queues them through BotMgr::LogEvent: no DB access.
// Reason codes: QUEST_ACCEPTED, QUEST_PROGRESS, QUEST_COMPLETE, QUEST_REWARDED, QUEST_ABANDONED, QUEST_FAILED.
namespace BotQuestLog
{
    // Marks the code run inside it (on this thread only) so quest events can say why they happened. `cause` is used for
    // QUEST_FAILED (timeout, died, logout, escort_failed, event_failed, test_command); `test` tags every quest event of the
    // scope with details.source = "test_command" (real bot activity says "bot"). Nesting restores the previous values.
    class TC_GAME_API Scope
    {
    public:
        explicit Scope(char const* cause, bool test = false);
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;
    private:
        char const* _prevCause;
        bool _prevTest;
    };

    TC_GAME_API void OnAccepted(Player* bot, Quest const* quest, Object* giver);
    TC_GAME_API void OnObjectiveChange(Player* bot, Quest const* quest, QuestObjective const& objective, int32 oldAmount, int32 newAmount);
    TC_GAME_API void OnComplete(Player* bot, Quest const* quest);
    TC_GAME_API void OnRewarded(Player* bot, Quest const* quest, LootItemType rewardType, uint32 rewardId, Object* giver, uint32 xp, int32 money);
    TC_GAME_API void OnAbandoned(Player* bot, Quest const* quest);
    TC_GAME_API void OnFailed(Player* bot, Quest const* quest);
    // The bot logs out: forgets its remembered accept times (quests that left the log another way never erased theirs).
    TC_GAME_API void OnLogout(uint64 botGuid);
}

#endif
