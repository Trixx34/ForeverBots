# Escort and group quests (2026-10-08)

Requested by Trixx34. Three things, all off by default. Settings are in `docs/playerbots/botserver-conf-escort-group-quests.dist.txt` (append to `botserver.conf.dist` once PR #50 is in; the code reads the keys either way).

## 1. Bots help players on group and escort quests (`Bot.Quest.Assist.*`)

A player who carries an escort quest (quest info "escort") or a group quest (suggested players 2 or more, or quest info "group") gets idle bots from the area:

1. Every `ScanEverySec` the glue looks at each connected player. Dungeon, raid, PvP and world event quests are skipped (their own features), as are players in instances, in raid groups or in a group led by somebody else.
2. `HelpersWanted`: a group quest wants `SuggestedPlayers - 1` companions (at most `MaxHelpers`), an escort wants `MaxHelpers`; people already in the party count, a party never goes past five.
3. `PickHelpers`: free bots (alive, no group, no combat, not an alt, not busy with dungeon runs, travel, taming or a bot party) on the player's map within `RecruitRadius`, level within `MaxLevelGap`, closest level first.
4. The bots join the player's group (a group is created when the player has none), follow the player, and take the quest through the normal quest share path (`BotSocial::OnQuestPushed`), so the player sees the usual "accepted" answers. The player gets a system message. Log rows `QUEST_ASSIST_STARTED` / `QUEST_ASSIST_JOINED`.
5. **Escort guard**: each pass the escorted NPC (a friendly creature within 80 yards whose entry is an objective or ender of the quest) is checked; the creatures attacking it are shared out to the free helpers (`AssignGuards`: attackers on the escort first, one helper each before a second joins, only attackers within `GuardRadius`). Row `QUEST_ESCORT_GUARD` (trace).
6. The quest AI of a helper stands back (`BotQuestAssist::Busy`), like for bot parties.

The session ends (`Evaluate`) when the quest is no longer open for `DoneGraceSec`, the player is dead/away for 30 s, after `LifetimeMin`, or when no helper is left. A helper is let go when dead 60 s or farther than `LeashYards` for `LeashSec`. **The player sends a bot away by removing it from the group**: it is not asked again for that quest. After a lifetime end the same quest is offered again after five minutes.

## 2. Solo bots and escort quests (classifier)

`ClassifyQuest` has a new fact `Escort` and block code `NEEDS_ESCORT` (quest info "escort", unless `QuestWorldLookup::EscortQuestSupported` says otherwise; none today). A bot on its own no longer takes or works escort quests and logs the block once. This is not behind a switch: a bot alone could not finish those quests before either. `NEEDS_GROUP` is unchanged and still comes first.

## 3. Use item on target (`Bot.Quest.UseItems`)

Kill objectives whose quest hands over (start item, `ItemDrop`) or asks for (item objective) an item with an on-use spell that needs a target are worked with the item:
* the target scan also accepts friendly creatures;
* the bot walks to 90 percent of the spell range (3 to 30 yards) in line of sight and casts the item on it (`Step` from `BotQuestUse::Decide`: approach, cast, wait, done on credit, abort after `MaxCasts` casts without credit or `TimeoutSec`);
* a hostile target gets one opening cast and is fought afterwards; a friendly one that gave no credit is ignored for two minutes.
Rows `QUEST_USE_ITEM`, `QUEST_USE_ITEM_FAILED`, `QUEST_USE_ITEM_GIVEUP`.

## Code and tests

* `BotQuestAssistPlan.h/.cpp`, `BotQuestUsePlan.h/.cpp`: pure decisions. Tests: `tests/game/BotQuestAssistPlan.cpp` (`[BotQuestAssist]`), `BotQuestUsePlan.cpp` (`[BotQuestUse]`), `BotQuestClassifier.cpp` (`NEEDS_ESCORT`).
* `BotQuestAssist.h/.cpp`: world-thread glue, called from `BotMgr::Update`. `BotQuest.cpp`: use-item step in `RunKill`. `BotWatchdog.cpp`: helpers are not stuck.

## Not covered / to check on a sim

* Nothing has run on a sim server. Built here: all changed game objects compile; the two plan test files pass standalone; the full test binary was not linked.
* The escorted NPC is found by entry (objective or ender) and friendliness; an escort whose NPC has neither is not guarded. Escort NPC creation, waypoints and fail conditions stay with the core scripts, helpers do not talk to the NPC.
* Use-item detection looks only at the spell (on use, needs a unit target). A quest whose credit needs a condition on the target (health below X) will use up `MaxCasts` and give the creature up.
* First checks with the switches on: a level 10 player takes a 3-player quest near idle bots (`QUEST_ASSIST_JOINED`, quest in the bots' logs), removes a bot from the group (no new offer), an escort start with attackers on the NPC (`QUEST_ESCORT_GUARD`), a solo bot with a "use item on" quest (`QUEST_USE_ITEM` then credit).
