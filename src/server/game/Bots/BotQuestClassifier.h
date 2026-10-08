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

#ifndef TRINITY_BOT_QUEST_CLASSIFIER_H
#define TRINITY_BOT_QUEST_CLASSIFIER_H

// Static quest blocker classification for the bot quest pipeline (BotQuest.cpp). The classifier is a pure function over plain
// facts about the quest plus a lookup of what the world index knows, so it can be unit tested without a database.

#include "Define.h"
#include <string>
#include <vector>

namespace BotQuest
{
    struct ObjectiveFacts
    {
        int32 Type = 0;            // QuestObjectiveType
        int32 ObjectID = 0;
        int32 Amount = 0;
        bool Optional = false;
    };

    struct QuestFacts
    {
        uint32 QuestId = 0;
        bool Repeatable = false;   // daily, weekly, monthly or repeatable
        uint32 SuggestedPlayers = 0;
        int32 LimitTime = 0;
        uint32 RequiredSkill = 0;
        uint32 RequiredMinRepFaction = 0;
        bool CompletionEvent = false;
        bool CompletionAreaTrigger = false;
        bool AutoComplete = false;
        uint32 SrcItemId = 0;
        uint32 SrcItemCount = 0;
        std::vector<std::pair<uint32, uint32>> ItemDrops;   // (item, quantity) the quest hands over
        std::vector<ObjectiveFacts> Objectives;
    };

    // What the world index knows. Production implementation lives in BotQuest.cpp.
    class QuestWorldLookup
    {
    public:
        virtual ~QuestWorldLookup() = default;
        virtual bool HasSpawn(uint32 creatureEntry) const = 0;
        // the credit entry itself plus every creature entry that grants it
        virtual std::vector<uint32> KillEntries(uint32 creditEntry) const = 0;
        // creature entries that drop the quest item; false when the item has no known creature source
        virtual bool ItemCreatureSources(uint32 item, std::vector<uint32>& out) const = 0;
        // chest game objects with a spawn that hold the item
        virtual bool ItemHasSpawnedGameObject(uint32 item) const = 0;
        // the item sits in a chest game object, spawned or not
        virtual bool ItemHasGameObjectSource(uint32 item) const = 0;
        virtual bool HasEnderRow(uint32 questId) const = 0;
        virtual bool HasEnderSpawn(uint32 questId) const = 0;
    };

    struct ClassifierResult
    {
        char const* Code = nullptr;   // null = plannable
        uint32 Entry = 0;
        bool Silent = false;          // not worth a log row (daily, repeatable)
        std::string Info;
    };

    TC_GAME_API ClassifierResult ClassifyQuest(QuestFacts const& quest, QuestWorldLookup const& world);
}

#endif
