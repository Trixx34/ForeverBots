# 18. Notes from advocaite's `forever-bots` branch

Source: https://github.com/advocaite/TrinityCore/tree/forever-bots (read-only, shallow clone; "WoW Forever Bots" by Karrz, on agatho's
PlayerbotV2). It is an in-process bot module (about 220,000 lines), a different design from ours (out-of-process modules over Tier 1/2),
so this file only lists the facts and ideas worth taking. Nothing is copied; ids and game facts are re-checked against our own data
before use. Paths below are in `src/modules/PlayerbotV2/` unless stated.

## 1. Class spells are trained, not granted (answers an open question)

`Bot/BotSetupPipeline` says it outright: "on Classic class spells are trained, not granted by GiveLevel". After `GiveLevel` they call
three things (`Fleet/BotSetupPipeline.cpp`, `LearnLevelKit`; `Api/PlayerbotAPI.cpp`):

- `learn_trainer_spells_free()`: for every trainer in `sObjectMgr->GetTrainers()` (not Tradeskill/Pet), `Trainer::TeachAvailableSpellsFree`
  (`src/server/game/Entities/Creature/Trainer.cpp`), repeated up to 8 passes because `ReqAbility` is often the previous rank. It uses the
  normal `CanTeachSpell` checks (class mask, level, ReqAbility) but takes no money and needs no NPC. Skips `ReqSkillLine` rows and battle pets.
- `learn_class_quest_spells_free()`: spells that class quests teach (Voidwalker, Bear Form, stances, totems, hunter pet spells), found by
  scanning `quest_template` reward spells and class-quest display spells, kept only when the spell is on the bot's class skill line.
- `learn_weapon_skills_free()`: see section 2.

This matches the user's wish ("spells should be applied automatically on level-up") and is likely the missing piece if the ability dump
shows that `GiveLevel` alone does not add trainer spells. It is a core change (a free-teach helper on `Trainer`, called from our bot
level-up path), so it fits `BotMgr`. Wait for the `BotAbilities:` dump before building it.

## 2. Weapon skills, Defense and proficiencies

Bots never gain skill by use, so they set it: every weapon skill, Unarmed and Defense, `LearnDefaultSkill` (which also learns the
proficiency spell), then `SetSkill(skill, step, level*5, level*5)`. Their note: a level 63 boss crushed a warrior with 181 Defense on
80 to 95% of swings. Check our bots: if a class lacks a weapon proficiency or has weapon skill 1, it misses and glances a lot, which
fits our "long fights, weak damage" observation in run 5. Candidate core step in `BotMgr::CreateBot` and on level-up.

## 3. Rotations: same vanilla spell ids as ours, plus patterns

Their Classic rotations (`Combat/Apl/Apl_Classic_*.cpp`) use the vanilla rank-1 ids and resolve to the highest known rank at cast time.
The ids agree with our table (Charge 100, Rend 772, Heroic Strike 78, Thunder Clap 6343, Frostbolt 116, Fireball 133, Corruption 172,
Smite 585, Shadow Word: Pain 589, Mind Blast 8092, Lightning Bolt 403, Earth Shock 8042, Flame Shock 8050, Sinister Strike 1752,
Eviscerate 2098, Arcane Shot 3044, Serpent Sting 1978, Judgement 20271, Seal of Righteousness 21084, Holy Light 635).
Ideas to take:

- Warrior: stance logic (Battle for DPS, Defensive for tank), Overpower/Revenge gated by the reactive-aura state, Execute under 20%,
  Bloodrage for rage, Tactical Mastery keeps rage on a stance change.
- Hunter: Hawk as the only aspect, Auto Shot armed by the attack call (not cast), ranged dead zone 8 yd (switch to Raptor Strike and
  Wing Clip inside it), Concussive Shot to slow, Call/Mend/Revive Pet, pet sent at the victim, Freezing Trap before pack pulls.
- Priest/Mage/Warlock: wand (Shoot, 5019) as the filler when low on mana; Life Tap; Inner Fire; Shield with Weakened Soul check.
- Rogue: Stealth plus Ambush/Garrote/Cheap Shot openers, Slice and Dice, Eviscerate at combo points, Kick for casters.
- Reactive spells need an aura-state check; our snapshot has no auras, so these stay approximations (as noted in doc 14).

## 4. Dungeon: Deadmines (map 36) in the vanilla data

`Bot/Dungeon/Scripts/DeadminesScript.cpp` has a Classic block (`DeadminesClassicAdvice`) built from the same VMaNGOS-derived data our
Forever world uses. Directly useful for the Deadmines milestone:

- Bosses (entry): Rhahk'Zor 644, Sneed's Shredder 642, Gilnid 1763, Mr. Smite 646, Captain Greenskin 647, Edwin VanCleef 639, Cookie 645.
- Kill first: Defias Evoker 1729, Wizard 4418, Squallshaper 1732, Goblin Engineer 622, Magician 1726, Overseer 634.
- Interrupt: Fireball 9053, Flamestrike 11829, Chains of Ice 113. Crowd-control targets: the casters above (not the Goblin Engineer).
- **The Iron Clad Door** (object 16397 at -100.5 -668.8 7.4) between the foundry and the ship does not open by itself: use the **Defias
  Cannon** (object 16398 at -107.6 -659.7 7.2) once. Rhahk'Zor, Sneed and Gilnid open their own doors on death (doors 13965, 16400, 16399).
  Without this a party walks up to a shut door and stalls before Mr. Smite.
- Progression waypoints: Rhahk'Zor (-192.9 -448.2 54.4), Sneed (-289.5 -513.0 49.7), Gilnid (-177.4 -574.5 19.3), Smite (-22.8 -797.3 20.4),
  Greenskin (-69.0 -808.0 40.8), VanCleef (-87.4 -819.9 39.3), Cookie (-67.6 -853.7 17.1).
- They keep dungeon routes in a shared DB table (`sql/shared/0001_dungeon_route_waypoints.sql`, `tools/gen_dungeon_routes.py` builds dense
  navmesh-safe chains). Their lesson: long legs fail past a poly cap, so chunk routes into short hops through on-mesh stepping stones.
- Ignore lists matter: passive/cutscene units (bunnies, props) that bots otherwise attack forever.

These need a Tier 1/2 way to use an object (`use game object`), which neither tier has. Add it with the M5/M9 core work.

## 5. Questing design lessons

- `Travel/QuestHubDatabase`: DBSCAN over quest-giver spawns (75 yd radius, 2+ givers) into hundreds of hubs; picks the nearest level-
  appropriate hub; when a bot has exhausted a zone it relocates to another hub. Same idea as our `Hub<map>_<n>` list but data-derived
  instead of hand-placed. We could generate our hub list the same way from the quest export queries in doc 17.
- `Bot/QuestDoable`: one shared predicate "can this bot still take this quest" (not rewarded, not repeatable, no uncompletable objective,
  skill known, `CanTakeQuest` ok). Grey quests (quest level at or below the bot's grey level) are skipped, except class/profession quests.
  Use the same rules in our offer filter.
- Accept/turn-in reach: accept only within about 4.5 yd (the server gate is 5 yd, bots drift); walk the last yards to the giver
  themselves; skip accept when bags are full (a quest with a start item fails and loops forever); add a short "reading" delay (1.5 to 6 s)
  so the bot does not accept five quests in 250 ms.
- Quest rewards: the reward choice is picked by an item scorer (`Api/BotItemScorer`).
- They also auto-complete the starter quests for non-vanilla realms (`StarterQuestAutocomplete`); on vanilla data they deliberately do
  not, because faction-masked quests would complete the whole log.

## 6. Robustness features we lack

- `Diagnostics/WedgeWatchdog` classifies stuck bots into root causes, one log line per episode; `Bot/DeathHotspots` notes where bots
  die (100 yd cells, 3 deaths in 20 min, killer level) and quest pursuit then avoids that cell for under-levelled bots. Both map onto our
  open question "where do bots die" and would add a `deaths` location log to telemetry plus a hotspot avoid-list in the module.
- `Api/NoMotionGuard`, `PartialRouteStall`, `PathLiquid`: motion/path failure handling (partial paths, water). Worth reading when we see
  stalled movement.
- `Api/ClassicSupplies`: vanilla supplies per class (food, water, bandages, potions, reagents, totems, poison, Symbol of Kings) topped up
  by item id. We need food/drink for rest speed once rest is the bottleneck.
- Starter kit: 30-slot bags (`EnsureBags`), gold per level (2.5g per level in their vanilla mode), city reputation pre-seeded to Friendly
  so vendors do not refuse.
- Dead state (`Bot/States/State_Dead.cpp`, 1000+ lines): release, run back, resurrect. We have no corpse-run yet; 43 deaths in run 5 means
  it matters.

## 7. Possible actions for us, in order

1. After the `BotAbilities:` dump: if trainer spells are missing, add the free-teach helper (section 1) to level-up.
2. Check weapon skills/proficiencies/Defense of created bots against section 2; set them to 5 per level if short.
3. Deadmines: object-use action plus the cannon step; take boss, kill-first and interrupt lists into the module's dungeon data.
4. Telemetry: death positions (hotspots), idle seconds, refusal reasons per action type.
5. Bot death handling (release and corpse run) in the module or core.
6. Generate hubs from quest-giver clusters rather than by hand.

## Licence note

advocaite's tree is GPL-2.0 (like TrinityCore) with parts ported from other GPL-2.0 bot projects; one project it only read (AzerothCore
mod-playerbots, AGPL) was reference only. This file records facts and ideas, not code. If anything is ever copied, check the licence
and keep the credits.
