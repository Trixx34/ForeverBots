# Use-this-object quests (suggestion 5, part 1)

Quests with an objective of type "use a game object" (`QUEST_OBJECTIVE_GAMEOBJECT`: levers, goobers, clickable props) were blocked as
`OBJECTIVE_UNSUPPORTED`. `Bot.Quest.UseObjects` (default 0) lets the bots work them.

- The quest index keeps the spawns of the objective's object next to the chest spawns (`g.GoSpawns`).
- The classifier asks `QuestWorldLookup::UseObjectObjectives()` and `GameObjectHasSpawn()`: switch off keeps `OBJECTIVE_UNSUPPORTED`, no spawn
  gives `NO_TARGET_SPAWN`.
- The work step is the existing chest loot task (`Kind::Loot`, `RunLoot`): spawn choice, shared claims between bots, danger check, approach,
  respawn wait and failure budget are unchanged. For this objective type the success test is the objective counter rising after `Use()`;
  a loot window is not required. Logged as `QUEST_LOOT_GO` / `QUEST_LOOT_OBJECT` like the chests.

Not done: escort quests, quests that need a group (`NEEDS_GROUP`, see `design-group-dungeon-20261008.md`), and "use item on target" quests
(a quest item with a spell that credits a kill objective).
