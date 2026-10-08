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

#include "tc_catch2.h"

#include "BotQuestClassifier.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include <cstring>
#include <map>
#include <set>

using namespace BotQuest;
using Trinity::StringFormat;

namespace
{
struct FakeWorld final : QuestWorldLookup
{
    std::set<uint32> Spawned;                            // creature entries with a spawn
    std::map<uint32, std::vector<uint32>> CreditSrc;     // kill credit -> creatures granting it
    std::map<uint32, std::vector<uint32>> ItemSrc;       // item -> dropping creatures
    std::set<uint32> ItemGoSrc;                          // items held by a chest object
    std::set<uint32> ItemGoSpawned;                      // ... of which the chest has a spawn
    std::set<uint32> UseGoSpawned;                       // game objects of use-this-object objectives that have a spawn
    std::set<uint32> EnderRows;                          // quests with an ender row
    std::set<uint32> EnderSpawned;                       // quests whose ender has a spawn

    std::set<uint32> EventOk;                            // completion-event quests the bots can finish

    bool HasSpawn(uint32 entry) const override { return Spawned.count(entry) != 0; }
    bool EventQuestSupported(uint32 questId) const override { return EventOk.count(questId) != 0; }

    std::vector<uint32> KillEntries(uint32 credit) const override
    {
        std::vector<uint32> out { credit };
        if (auto it = CreditSrc.find(credit); it != CreditSrc.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
        return out;
    }

    bool ItemCreatureSources(uint32 item, std::vector<uint32>& out) const override
    {
        auto it = ItemSrc.find(item);
        if (it == ItemSrc.end())
            return false;
        out = it->second;
        return true;
    }

    bool ItemHasSpawnedGameObject(uint32 item) const override { return ItemGoSpawned.count(item) != 0; }
    bool ItemHasGameObjectSource(uint32 item) const override { return ItemGoSrc.count(item) != 0; }
    bool UseObjects = true;                              // Bot.Quest.UseObjects
    bool UseObjectObjectives() const override { return UseObjects; }
    bool GameObjectHasSpawn(uint32 goEntry) const override { return UseGoSpawned.count(goEntry) != 0; }
    bool HasEnderRow(uint32 questId) const override { return EnderRows.count(questId) != 0; }
    bool HasEnderSpawn(uint32 questId) const override { return EnderSpawned.count(questId) != 0; }
};

constexpr uint32 QUEST = 100;

// a quest with a working ender and no objectives: plannable unless a test breaks something
QuestFacts MakeQuest(FakeWorld& world)
{
    QuestFacts q;
    q.QuestId = QUEST;
    world.EnderRows.insert(QUEST);
    world.EnderSpawned.insert(QUEST);
    return q;
}

ObjectiveFacts Obj(QuestObjectiveType type, int32 id, int32 amount = 1, bool optional = false)
{
    return { int32(type), id, amount, optional };
}

bool IsCode(ClassifierResult const& r, char const* code)
{
    return r.Code && std::strcmp(r.Code, code) == 0;
}
}

TEST_CASE("BotQuest classifier: plannable quests", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);

    SECTION("no objectives, ender present")
    {
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("kill objective with a spawned target")
    {
        world.Spawned.insert(10);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("kill credit granted by a spawned creature")
    {
        world.CreditSrc[10] = { 11 };
        world.Spawned.insert(11);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("item dropped by a spawned creature")
    {
        world.ItemSrc[500] = { 12 };
        world.Spawned.insert(12);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("item in a spawned chest")
    {
        world.ItemGoSrc.insert(500);
        world.ItemGoSpawned.insert(500);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("item handed over by the quest start item")
    {
        q.SrcItemId = 500;
        q.SrcItemCount = 2;
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500, 2));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("item handed over by the ItemDrop list")
    {
        q.ItemDrops = { { 0, 0 }, { 500, 1 } };
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500, 1));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("talk-to objective with a spawned npc")
    {
        world.Spawned.insert(20);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_TALKTO, 20));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("optional objective is ignored even when unsupported")
    {
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 99, 1, true));
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONEY, 0, 1, true));
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("auto complete quest needs no ender")
    {
        world.EnderRows.clear();
        world.EnderSpawned.clear();
        q.AutoComplete = true;
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }
}

TEST_CASE("BotQuest classifier: REPEATABLE", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.Repeatable = true;
    q.SuggestedPlayers = 5;   // earlier-checked codes win, nothing below is reached

    ClassifierResult r = ClassifyQuest(q, world);
    CHECK(IsCode(r, "REPEATABLE"));
    CHECK(r.Silent);
}

TEST_CASE("BotQuest classifier: NEEDS_GROUP", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);

    SECTION("a suggested group of one is a solo quest")
    {
        q.SuggestedPlayers = 1;
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("two or more players")
    {
        q.SuggestedPlayers = 3;
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "NEEDS_GROUP"));
        CHECK_FALSE(r.Silent);
        CHECK(r.Info == "suggested players 3");
    }
}

TEST_CASE("BotQuest classifier: TIMED_UNSUPPORTED", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.LimitTime = 600;
    CHECK(IsCode(ClassifyQuest(q, world), "TIMED_UNSUPPORTED"));
}

TEST_CASE("BotQuest classifier: SKILL_REQUIRED", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.RequiredSkill = 164;

    ClassifierResult r = ClassifyQuest(q, world);
    CHECK(IsCode(r, "SKILL_REQUIRED"));
    CHECK(r.Entry == 164);
}

TEST_CASE("BotQuest classifier: REPUTATION_REQUIRED", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.RequiredMinRepFaction = 529;

    ClassifierResult r = ClassifyQuest(q, world);
    CHECK(IsCode(r, "REPUTATION_REQUIRED"));
    CHECK(r.Entry == 529);
}

TEST_CASE("BotQuest classifier: NEEDS_EVENT", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.CompletionEvent = true;
    CHECK(IsCode(ClassifyQuest(q, world), "NEEDS_EVENT"));
}

TEST_CASE("BotQuest classifier: a supported completion-event quest is not blocked as NEEDS_EVENT", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.CompletionEvent = true;
    world.EventOk.insert(q.QuestId);
    CHECK_FALSE(IsCode(ClassifyQuest(q, world), "NEEDS_EVENT"));

    // another quest id stays blocked
    FakeWorld other;
    QuestFacts q2 = MakeQuest(other);
    q2.CompletionEvent = true;
    other.EventOk.insert(q2.QuestId + 1);
    CHECK(IsCode(ClassifyQuest(q2, other), "NEEDS_EVENT"));
}

TEST_CASE("BotQuest classifier: OBJECTIVE_UNSUPPORTED", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);

    SECTION("area trigger completion")
    {
        q.CompletionAreaTrigger = true;
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "OBJECTIVE_UNSUPPORTED"));
        CHECK(r.Info == "area trigger completion");
    }

    SECTION("objective type the bots cannot work")
    {
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONEY, 0));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "OBJECTIVE_UNSUPPORTED"));
        CHECK(r.Info == StringFormat("objective type {}", uint32(QUEST_OBJECTIVE_MONEY)));
    }

    SECTION("item held by a chest that has no spawn")
    {
        world.ItemGoSrc.insert(500);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "OBJECTIVE_UNSUPPORTED"));
        CHECK(r.Entry == 500);
        CHECK(r.Info == "item comes from a game object without a spawn");
    }
}

TEST_CASE("BotQuest classifier: NO_TARGET_SPAWN", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);

    SECTION("kill target without a spawn")
    {
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "NO_TARGET_SPAWN"));
        CHECK(r.Entry == 10);
    }

    SECTION("kill credit whose granting creatures have no spawn")
    {
        world.CreditSrc[10] = { 11, 12 };
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
        CHECK(IsCode(ClassifyQuest(q, world), "NO_TARGET_SPAWN"));
    }

    SECTION("item whose droppers have no spawn")
    {
        world.ItemSrc[500] = { 12 };
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "NO_TARGET_SPAWN"));
        CHECK(r.Entry == 500);
    }

    SECTION("talk-to npc without a spawn")
    {
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_TALKTO, 20));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "NO_TARGET_SPAWN"));
        CHECK(r.Entry == 20);
    }

    SECTION("the first unplannable objective is reported")
    {
        world.Spawned.insert(10);
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_TALKTO, 20));
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_TALKTO, 21));
        CHECK(ClassifyQuest(q, world).Entry == 20);
    }
}

TEST_CASE("BotQuest classifier: MISSING_ITEM_SOURCE", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);

    SECTION("item with no known source at all")
    {
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500));
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "MISSING_ITEM_SOURCE"));
        CHECK(r.Entry == 500);
    }

    SECTION("the quest supplies fewer items than the objective needs")
    {
        q.SrcItemId = 500;
        q.SrcItemCount = 1;
        q.Objectives.push_back(Obj(QUEST_OBJECTIVE_ITEM, 500, 4));
        CHECK(IsCode(ClassifyQuest(q, world), "MISSING_ITEM_SOURCE"));
    }
}

TEST_CASE("BotQuest classifier: NO_ENDER_ROW", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    world.EnderRows.clear();
    world.EnderSpawned.clear();
    CHECK(IsCode(ClassifyQuest(q, world), "NO_ENDER_ROW"));
}

TEST_CASE("BotQuest classifier: NO_ENDER_SPAWN", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    world.EnderSpawned.clear();
    CHECK(IsCode(ClassifyQuest(q, world), "NO_ENDER_SPAWN"));
}

TEST_CASE("BotQuest classifier: check order", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    world.EnderRows.clear();
    world.EnderSpawned.clear();
    q.Objectives.push_back(Obj(QUEST_OBJECTIVE_MONSTER, 10));
    q.RequiredSkill = 164;
    q.LimitTime = 60;

    // quest-level blockers come before objectives, and objectives before the ender checks
    CHECK(IsCode(ClassifyQuest(q, world), "TIMED_UNSUPPORTED"));
    q.LimitTime = 0;
    CHECK(IsCode(ClassifyQuest(q, world), "SKILL_REQUIRED"));
    q.RequiredSkill = 0;
    CHECK(IsCode(ClassifyQuest(q, world), "NO_TARGET_SPAWN"));
    q.Objectives.clear();
    CHECK(IsCode(ClassifyQuest(q, world), "NO_ENDER_ROW"));
}

TEST_CASE("BotQuest classifier: use-this-object objectives", "[BotQuest]")
{
    FakeWorld world;
    QuestFacts q = MakeQuest(world);
    q.Objectives.push_back(Obj(QUEST_OBJECTIVE_GAMEOBJECT, 700));

    SECTION("object with a spawn is plannable")
    {
        world.UseGoSpawned.insert(700);
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }

    SECTION("object without a spawn")
    {
        ClassifierResult r = ClassifyQuest(q, world);
        CHECK(IsCode(r, "NO_TARGET_SPAWN"));
        CHECK(r.Entry == 700);
    }

    SECTION("switch off: unsupported as before")
    {
        world.UseGoSpawned.insert(700);
        world.UseObjects = false;
        CHECK(IsCode(ClassifyQuest(q, world), "OBJECTIVE_UNSUPPORTED"));
    }

    SECTION("an optional object objective is not checked")
    {
        q.Objectives.back().Optional = true;
        CHECK(ClassifyQuest(q, world).Code == nullptr);
    }
}
