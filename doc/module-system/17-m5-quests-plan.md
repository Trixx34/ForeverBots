# 17. M5 plan: quests 1 to 10

Status: plan. Nothing is built yet. M5 is the step after combat, looting and travel (doc 16): a bot takes the starter quests of
its race, does what they ask, hands them in, and so levels 1 to 10 by questing as well as by killing what it meets.

## What a bot needs to quest

1. **Know the quests.** Who gives each starter quest, where, what it asks, who takes it back, and the reward. This is data in the
   world database (below), not something the bot sees: a bot sees only the units within 40 yards.
2. **Accept and hand in.** Two core actions on the bot's own session, the way the client does it: accept a quest from a giver in range,
   and turn one in to the ender in range, choosing a reward.
3. **See its quest log.** The snapshot has no quest state today. It needs the bot's active quests with their objective progress, so the
   module can tell what is left (a layout change, so a new Tier 2 layout version).
4. **Recognise givers and enders.** The nearby list does not say which unit has a quest for the bot. The core can add a flag per unit
   (has a quest the bot can take, or can hand one in).
5. **Do the objectives.** Kill (the bot already fights), collect (quest items drop from creatures, so looting must work, and a party
   member's copy is covered by the quest-group-loot module), and walk to a place or talk to a unit.

## Core changes (new)

- `ActionType::kQuestAccept` and `kQuestComplete`: target = the NPC, `spell_id` = the quest id, and for the second a reward choice.
  Implemented by calling the session's own quest handlers, as `kLoot` does for loot, so the core's rules apply: level, race and class
  requirements, prerequisites, distance, bag space. A refusal says why.
- Layout 8: per bot, up to N active quests, each with the quest id, status, and per-objective counts; per nearby unit, a quest flag.
- Both are additive to the module protocol; the layout bump follows the usual rule (SDK `CHANGELOG.md`, core header copy).

## Module changes (new)

- A quest chooser: from the quests the nearby givers offer (the flag), take those the bot can do now, nearest first, avoiding ones whose
  objectives it cannot do yet.
- Objective handling per quest type; drop a quest that does not progress after a time (the same give-up rule the combat target has).
- Travel: walk to the ender when the quest is complete, and to the next giver when none is near (the hub list already moves an idle bot).

## Data to extract first

The world database has the quest tables (the baseline SQL in `sql/custom/world/` only holds changes on top of the TDB dump). Step 0 is to
see this fork's real column names, since it has custom tables such as `quest_template_classic_level`. Run read-only on the sim world
database:

    SHOW COLUMNS FROM quest_template;
    SHOW COLUMNS FROM quest_template_addon;
    SHOW COLUMNS FROM quest_template_classic_level;
    SHOW COLUMNS FROM quest_objectives;
    SHOW COLUMNS FROM creature_queststarter;
    SHOW COLUMNS FROM creature_questender;

With the columns, the next step is one export per race start: the quests that start in the start zone at levels 1 to 10, with their giver
and ender (name, map, position), their objectives (kind, target, count) and their rewards. That list is the first data file the module
reads, and the test set for the accept and hand-in actions.

### Export queries (columns confirmed on the sim)

Run on `forever_sim_world`, read-only. Quest level comes from `quest_template_classic_level` (`QuestLevel`, 0 = unset). `QuestSortID`
below 0 is a class or profession sort; the filters keep quests with a level of 1 to 10, or no level and a giver under that range.

    -- 1. The quests: id, title, level, chain links, race/class limits, rewards.
    SELECT q.ID, q.LogTitle, c.QuestLevel, a.MaxLevel, q.AllowableRaces, a.AllowableClasses,
           a.PrevQuestID, a.NextQuestID, q.RewardNextQuest, a.ExclusiveGroup, a.BreadcrumbForQuestId,
           q.StartItem, q.RewardItem1, q.RewardAmount1, q.RewardChoiceItemID1, q.RewardChoiceItemID2, q.RewardChoiceItemID3,
           q.RewardXPDifficulty, q.RewardMoneyDifficulty, q.Flags, a.SpecialFlags
    FROM quest_template q
    LEFT JOIN quest_template_addon a ON a.ID = q.ID
    LEFT JOIN quest_template_classic_level c ON c.ID = q.ID
    WHERE c.QuestLevel BETWEEN 1 AND 10 AND q.QuestSortID >= 0
    ORDER BY c.QuestLevel, q.ID;

    -- 2. The objectives (Type 0 = kill creature, 1 = item to collect, 2 = go to location/object, others: ignore at first).
    SELECT o.QuestID, o.`Order`, o.StorageIndex, o.Type, o.ObjectID, o.Amount, o.Flags, o.Description
    FROM quest_objectives o
    JOIN quest_template_classic_level c ON c.ID = o.QuestID
    WHERE c.QuestLevel BETWEEN 1 AND 10
    ORDER BY o.QuestID, o.`Order`;

    -- 3. Givers and enders with name, map and position (first spawn of each creature).
    SELECT s.quest AS QuestID, 'start' AS Role, s.id AS Creature, t.name AS Name, cr.map AS Map,
           ROUND(cr.position_x,1) AS X, ROUND(cr.position_y,1) AS Y, ROUND(cr.position_z,1) AS Z
    FROM creature_queststarter s
    JOIN creature_template t ON t.entry = s.id
    JOIN creature cr ON cr.id = s.id
    JOIN quest_template_classic_level c ON c.ID = s.quest AND c.QuestLevel BETWEEN 1 AND 10
    UNION ALL
    SELECT e.quest, 'end', e.id, t.name, cr.map, ROUND(cr.position_x,1), ROUND(cr.position_y,1), ROUND(cr.position_z,1)
    FROM creature_questender e
    JOIN creature_template t ON t.entry = e.id
    JOIN creature cr ON cr.id = e.id
    JOIN quest_template_classic_level c ON c.ID = e.quest AND c.QuestLevel BETWEEN 1 AND 10
    ORDER BY QuestID, Role;

    -- 4. Kill and collect targets: where the creatures spawn (needed to walk to them).
    SELECT o.QuestID, o.ObjectID AS Creature, t.name, cr.map, ROUND(AVG(cr.position_x),1) AS X, ROUND(AVG(cr.position_y),1) AS Y,
           COUNT(*) AS Spawns
    FROM quest_objectives o
    JOIN creature_template t ON t.entry = o.ObjectID
    JOIN creature cr ON cr.id = o.ObjectID
    JOIN quest_template_classic_level c ON c.ID = o.QuestID AND c.QuestLevel BETWEEN 1 AND 10
    WHERE o.Type = 0
    GROUP BY o.QuestID, o.ObjectID, t.name, cr.map;

    -- 5. Collect objectives: which creature drops the item.
    SELECT o.QuestID, o.ObjectID AS Item, lt.Entry AS Creature
    FROM quest_objectives o
    JOIN creature_loot_template lt ON lt.Item = o.ObjectID
    JOIN quest_template_classic_level c ON c.ID = o.QuestID AND c.QuestLevel BETWEEN 1 AND 10
    WHERE o.Type = 1;

Results of 1 to 3 are enough to choose the accept/hand-in actions; 4 and 5 are for the objective walking. Send each as a text table.

## Order of work

1. Column listing, then the starter quest export (data).
2. Core: the two actions and the layout 8 fields; SDK copy; unit tests where they can run.
3. Module: chooser and objective handling, driven by the export.
4. A sim run with a few bots on one race's start zone, then more.

## Open questions

- Quest rewards that are items: equip the best, or keep for selling (the vendor step already does both).
- Group quests and elite quests are out of scope until parties (M8) are verified on the sim.
