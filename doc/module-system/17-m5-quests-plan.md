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

## Order of work

1. Column listing, then the starter quest export (data).
2. Core: the two actions and the layout 8 fields; SDK copy; unit tests where they can run.
3. Module: chooser and objective handling, driven by the export.
4. A sim run with a few bots on one race's start zone, then more.

## Open questions

- Quest rewards that are items: equip the best, or keep for selling (the vendor step already does both).
- Group quests and elite quests are out of scope until parties (M8) are verified on the sim.
