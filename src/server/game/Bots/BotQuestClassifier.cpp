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

#include "BotQuestClassifier.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include <algorithm>

using Trinity::StringFormat;

namespace BotQuest
{
namespace
{
// the quest hands the item over itself (start item, or the ItemDrop list), at least in the amount the objective needs
bool QuestSuppliesItem(QuestFacts const& q, ObjectiveFacts const& obj)
{
    uint32 const item = uint32(obj.ObjectID);
    uint32 const need = uint32(std::max<int32>(1, obj.Amount));
    if (q.SrcItemId == item && std::max<uint32>(1, q.SrcItemCount) >= need)
        return true;
    for (auto const& [dropItem, dropCount] : q.ItemDrops)
        if (dropItem == item && std::max<uint32>(1, dropCount) >= need)
            return true;
    return false;
}
}

ClassifierResult ClassifyQuest(QuestFacts const& q, QuestWorldLookup const& world)
{
    ClassifierResult b;
    if (q.Repeatable)
    {
        b.Code = "REPEATABLE"; b.Silent = true;
        return b;
    }
    if (q.SuggestedPlayers > 1)
    {
        b.Code = "NEEDS_GROUP"; b.Info = StringFormat("suggested players {}", q.SuggestedPlayers);
        return b;
    }
    if (q.LimitTime > 0)
    {
        b.Code = "TIMED_UNSUPPORTED";
        return b;
    }
    if (q.RequiredSkill)
    {
        b.Code = "SKILL_REQUIRED"; b.Entry = q.RequiredSkill;
        return b;
    }
    if (q.RequiredMinRepFaction)
    {
        b.Code = "REPUTATION_REQUIRED"; b.Entry = q.RequiredMinRepFaction;
        return b;
    }
    if (q.CompletionEvent && !world.EventQuestSupported(q.QuestId))
    {
        b.Code = "NEEDS_EVENT";
        return b;
    }
    if (q.CompletionAreaTrigger)
    {
        b.Code = "OBJECTIVE_UNSUPPORTED"; b.Info = "area trigger completion";
        return b;
    }

    for (ObjectiveFacts const& obj : q.Objectives)
    {
        if (obj.Optional)
            continue;
        switch (obj.Type)
        {
            case QUEST_OBJECTIVE_MONSTER:
            {
                bool any = false;
                for (uint32 e : world.KillEntries(uint32(obj.ObjectID)))
                    any = any || world.HasSpawn(e);
                if (!any)
                {
                    b.Code = "NO_TARGET_SPAWN"; b.Entry = uint32(obj.ObjectID);
                    return b;
                }
                break;
            }
            case QUEST_OBJECTIVE_ITEM:
            {
                std::vector<uint32> sources;
                bool const hasCreatureSource = world.ItemCreatureSources(uint32(obj.ObjectID), sources);
                bool any = QuestSuppliesItem(q, obj);   // delivery / report-to quests: the item comes with the quest
                for (uint32 e : sources)
                    any = any || world.HasSpawn(e);
                any = any || world.ItemHasSpawnedGameObject(uint32(obj.ObjectID));
                if (!any)
                {
                    b.Entry = uint32(obj.ObjectID);
                    if (world.ItemHasGameObjectSource(uint32(obj.ObjectID)))
                    {
                        b.Code = "OBJECTIVE_UNSUPPORTED"; b.Info = "item comes from a game object without a spawn";
                    }
                    else if (hasCreatureSource)
                        b.Code = "NO_TARGET_SPAWN";
                    else
                        b.Code = "MISSING_ITEM_SOURCE";
                    return b;
                }
                break;
            }
            case QUEST_OBJECTIVE_GAMEOBJECT:
                if (!world.UseObjectObjectives())
                {
                    b.Code = "OBJECTIVE_UNSUPPORTED"; b.Entry = uint32(obj.ObjectID);
                    b.Info = StringFormat("objective type {}", uint32(obj.Type));
                    return b;
                }
                if (!world.GameObjectHasSpawn(uint32(obj.ObjectID)))
                {
                    b.Code = "NO_TARGET_SPAWN"; b.Entry = uint32(obj.ObjectID);
                    return b;
                }
                break;
            case QUEST_OBJECTIVE_TALKTO:
                if (!world.HasSpawn(uint32(obj.ObjectID)))
                {
                    b.Code = "NO_TARGET_SPAWN"; b.Entry = uint32(obj.ObjectID);
                    return b;
                }
                break;
            default:
                b.Code = "OBJECTIVE_UNSUPPORTED"; b.Entry = uint32(obj.ObjectID);
                b.Info = StringFormat("objective type {}", uint32(obj.Type));
                return b;
        }
    }

    if (!q.AutoComplete)
    {
        if (!world.HasEnderRow(q.QuestId))
            b.Code = "NO_ENDER_ROW";
        else if (!world.HasEnderSpawn(q.QuestId))
            b.Code = "NO_ENDER_SPAWN";
    }
    return b;
}
}
