# Bot questing design (levels 1-10, start zones)

**Date:** 2026-10-06. Work areas: navigation (quest logic), combat AI, bot infrastructure, simulation, log analysis
(see section 9). Design only: nothing in the repo, the databases or the servers was changed to write this.

**Evidence base.** Every number below comes from SELECT queries on the sim's frozen world copy `forever_sim_world` (queries in
appendix A, re-runnable), from the sim worldserver's `DBErrors.log` of its current start (2026-10-06), from the core source in
`src/server/game` and from the bot code in `src/server/game/Bots`. Where a claim is a heuristic or could not be verified from SQL it
says so. The Python used for chain/route statistics is not part of the repo; appendix B states the algorithm so it can be redone.

## 0. Key findings

1. **The data is not ready for every race.** `playercreateinfo` starts **Troll at Echo Isles (-1171,-5264) and Gnome at (-4983,878)**,
   not in Valley of Trials / Coldridge Valley. There are 0 quest starters within 300 yd of either point (Echo Isles has only wildlife
   spawns, the Gnome point only a `Techbot`). A Troll or Gnome bot created today starts in a place with nothing to do (section 2.1).
2. **79 of the 176 new Forever quests (ID >= 90000) have neither a starter nor an ender row** (58 of them are in the level 1-10 pool).
   That includes the whole new Northshire/Elwynn book chain (91741..92124, 91723..91777), the Skyborne class quests, the "Camping 101" /
   "The Great Outdoors" series and the Tauren/Elwynn 99xxx quests. No NPC can give or take them (section 4).
3. **The Skyborne (Zephras Isle) quest data is only half usable.** (a) The sort has 63 quests for all races, 19 masked **Horde only**
   and **none masked Alliance**: the data looks sniffed from a Horde (Windshaper) character. The Alliance trunk therefore ends after
   8 quests (`The Next Step` 92472 is followed by the Horde-only `Welcome to Shen'dar Village` 92514, which is `PrevQuestID` of
   92517), so only **34 of 64** Alliance zone quests are reachable. (b) For both factions the main chain breaks at level 5:
   `Among the Faithful` (92528) has an objective creature (252078) with no `creature_template` row, `The Western Watch` (93926) has an
   ender (252155) with no spawn, `A Last Request` (93927) a starter with no spawn, and everything after (92579, 92700, ...) has 93927
   as `PrevQuestID`: **40 of 84** Horde zone quests are reachable (section 2.5).
4. **`BotQuestLog`'s `group_quest` flag can never be true on this data:** `SuggestedGroupNum` is 0 for all 4605 classic quests. Group
   quests are marked by `QuestInfoID` (1 = group: 262 quests, 81 = dungeon: 395, 62 = raid: 308, 41 = PvP: 186). A new detector is needed
   (sections 6 and 8).
5. **Outside those gaps the converted vanilla data is in good shape for levels 1-10:** of the 847-quest pool, 579 (68 %) have no detected
   defect, 128 (15 %) have a hard data defect (blacklist seed, section 4.5), 140 (17 %) are playable only with special handling. No pool
   quest has an objective item or a reward item missing from the client DB2 data; those problems exist only at level 20+ (section 4.3).
   The beta's DB2/hotfix gaps do not hurt levels 1-10 questing, the missing relation rows and creature templates do.
6. **mmaps exist** for maps 0 (736 tiles), 1 (988) and 2991 Zephras Isle (33); all 65,462 world creature spawns on those maps lie in an
   existing tile. Local pathing is feasible, but start zones are not tiny: the first hub outside the valley is 600-2200 yd away (section 5.7).

## 1. Goal and non-goals

**Goal.** A bot created at level 1 in its race's start zone finds quests, picks a sensible next quest, travels to the giver, accepts,
does every objective, turns in, picks a reward and continues, until level 10, solo, unattended, on map threads, at 180-bot scale. Every
time it cannot continue, a log row says why (`quest_blocked`, `stuck`, `path_fail`) with a stable reason code, so
log analysis can script around it.

**First target.** Levels 1-10 in the ten start zones (eight races, plus Skyborne Alliance and Horde), plus the 1-10 class quests that
sit in those zones, plus the next ring of zones only as far as needed to keep a level 9-10 bot busy.

**Non-goals for this milestone** (notes in the sections named):
- groups, party quests, dungeon quests (section 8, QuestInfo 1/81 quests are blacklisted);
- professions and their quests (sorts -24, -101, -121, -181, -182, -201, -264, -304, -324 are excluded from the pool);
- holiday, PvP, AQ-war-effort and similar sorts (-22, -284, -364..-369 and others, excluded);
- escort quests (blacklisted until a bot can follow and defend an NPC; section 4.4);
- timed quests, quests needing a skill or reputation (blacklisted by default);
- levels above 10 (the beta cap is 30; the data model here is level-agnostic, the pool and tests are not);
- gold/vendor/repair/training loops (Phase 10 / combat AI Phase 7; quests only need bags not to be full);
- the travel node graph, taxis, boats and zeppelins (Phase 8 proper; section 5.7 says what the first milestone does without them).

**Terms used throughout.**
- *Classic quest set*: the 4605 quests that have a row in `quest_classic_level` (and `quest_template_classic_level`). The world DB also
  holds 48,651 `quest_template` rows in total (retail leftovers), which have no classic min level. Bots only consider the classic set
  (exceptions: section 4.2).
- *Pool*: classic quests with `MinLevel <= 10`, minus unused/test titles, profession sorts and holiday/event sorts. **847 quests**
  (funnel in 4.1). "Available to levels 1-10" means `MinLevel <= 10`; because 286 of the 847 are exactly `MinLevel = 10`, every
  per-zone table also gives the count for `MinLevel <= 9`.
- *Zone set* of a race: quests whose `QuestSortID` is the start valley, the parent zone or the race's capital city, restricted to
  quests the race may take (`AllowableRaces`, bit = raceId-1, Skyborne bits 32 and 33 per `RaceMask::GetRaceBit`).
- *Hard defect*: a data defect that makes the quest unfinishable for any bot. *Soft*: possible but needs special handling or is
  uncertain from SQL alone.

## 2. Start zones, verified against `playercreateinfo` and the quest data

### 2.1 Start positions

`playercreateinfo` has 292 rows for 33 races (retail leftovers: Blood Elf, Draenei, Goblin, Worgen, Pandaren, allied races, Death
Knight/Monk/Demon Hunter/Evoker rows). The bot factory must take the allowed race/class set from the table of available combinations
(`race-class-combos.md`) / game data (`class_expansion_requirement`, `race_unlock_requirement` has Skyborne races 95 and 96 at
expansion 0), not from `playercreateinfo` alone.

| Race (id) | map, position in `playercreateinfo` | Start zone as intended | Verified? | Quest starters within 300 yd of the start |
|---|---|---|---|---|
| Human (1) | 0, -8914.6, -133.9, 80.5 | Northshire Valley (area 9, Elwynn zone 12) | yes | 23 (12 from sort 9) |
| Orc (2) | 1, -618.5, -4251.7, 38.7 | Valley of Trials (area 363, Durotar 14) | yes | 30 (11 from sort 363) |
| Dwarf (3) | 0, -6230.4, 330.2, 383.1 | Coldridge Valley (area 132, Dun Morogh 1) | yes | 22 (10 from sort 132) |
| Night Elf (4) | 1, 10311.3, 831.5, 1326.6 | Shadowglen (area 188, Teldrassil 141) | yes | 20 (12 from sort 188) |
| Undead (5) | 0, 1699.9, 1706.6, 135.9 | Deathknell (area 154, Tirisfal 85) | yes | 17 (10 from sort 154) |
| Tauren (6) | 1, -2915.6, -257.3, 59.3 | Camp Narache (area 220, Mulgore 215) | yes | 16 (8 from sort 220) |
| **Gnome (7)** | 0, **-4983.4, 877.7, 274.3** | should be Coldridge Valley | **NO** | **0** (only a `Techbot` spawn nearby; the nearest NPC with the questgiver flag, Namdo Bizzfizzle, is 163 yd away and starts no quest) |
| **Troll (8)** | 1, **-1171.5, -5263.7, 0.8** | should be Valley of Trials (per the design brief) | **NO** | **0** (Echo Isles: only wildlife around the point, for example Surf Crawlers, Bloodtalon Taillashers, Durotar Tigers; no NPC) |
| Skyborne Alliance (95) | 2991, 4098.1, 1849.7, 975.6 | Thendal Village, Zephras Isle (area 16635) | yes | see 2.5 |
| Skyborne Horde (96) | 2991, 4098.1, 1849.7, 975.6 | Thendal Village, Zephras Isle | yes | see 2.5 |

Gnome and Troll look like retail start points (Echo Isles is the Cataclysm troll start). Whether the 1.60 beta really starts them
elsewhere than their faction mates is unknown; the design brief says Valley of Trials for Orc and Troll and Coldridge/Anvilmar
for Dwarf and Gnome. See open question 1. Until decided, the Gnome and Troll rows in 2.2-2.4 assume the quest pool of Coldridge
Valley / Valley of Trials (same pool as Dwarf / Orc, race-masked), and the factory must not create them at the current coordinates
without a teleport. Skyborne class rows in `playercreateinfo`: 95 has classes 1,3,4,8,11 and 96 has 1,3,4,7,11, matching the
table (Mage Alliance only, Shaman Horde only).

### 2.2 Quests available per start zone (classic pool, zone set, race-eligible)

Counts are `MinLevel <= 10` (and `<= 9`). Zone set = valley + parent zone + capital sorts. Gnome and Troll equal Dwarf and Orc in the
counts (their race bits are in the same masks) once the start position is fixed. Class quests are in 2.4.

| Zone (race) | Total (<=9) | Valley | Parent zone | Capital | **Playable now** | MinLevel spread (level: count) |
|---|---|---|---|---|---|---|
| Northshire/Elwynn/Stormwind (Human) | 70 (69) | 18 | 45 | 7 | 51 | 1:19 2:5 3:3 4:11 5:11 6:3 7:14 9:3 10:1 |
| Coldridge/Dun Morogh (Dwarf, Gnome) | 38 (38) | 13 | 25 | 0 | 37 | 1:14 2:7 3:1 4:3 5:3 6:2 7:3 8:5 |
| Shadowglen/Teldrassil/Darnassus (Night Elf) | 52 (51) | 14 | 33 | 5 | 48 | 1:17 2:4 3:3 4:17 5:5 6:3 7:1 9:1 10:1 |
| Valley of Trials/Durotar/Orgrimmar (Orc, Troll) | 47 (45) | 13 | 30 | 4 | 40 | 1:14 2:1 3:4 4:10 5:4 6:2 7:4 8:1 9:5 10:2 |
| Deathknell/Tirisfal/Undercity (Undead) | 49 (47) | 13 | 33 | 3 | 43 | 1:6 2:6 3:1 4:5 5:14 6:9 7:5 9:1 10:2 |
| Camp Narache/Mulgore/Thunder Bluff (Tauren) | 47 (46) | 11 | 32 | 4 | 37 | 1:11 3:7 4:9 5:6 6:7 7:5 9:1 10:1 |
| Zephras Isle (Skyborne Alliance) | 64 (60) | 64 | - | - | **34** | 1:11 2:5 3:2 4:4 5:8 6:11 8:15 9:4 10:4 |
| Zephras Isle (Skyborne Horde) | 84 (78) | 84 | - | - | **40** | 1:13 2:6 3:5 4:4 5:9 6:17 8:17 9:7 10:6 |

"Playable now" = no hard defect (4.5) and every prerequisite quest (`PrevQuestID`, `NextQuestID` sources) is itself takeable by the race
and not hard-defective; it includes soft-defect quests. The cascade removes quests that look fine alone but sit behind a dead
prerequisite (for example 14 Alliance and 23 Horde Zephras quests).

The valley sorts are small (11-18 quests) and the parent zones carry most of the content, so a level 1-10 run always leaves the valley.
Continuation after level 9-10 (classic quests with `MinLevel <= 10` in the next ring): Westfall (sort 40) 24, Loch Modan (38) 22,
Darkshore (148) 28, The Barrens (17) 53, Silverpine Forest (130) 36 (counts include the pool's level-10 quests).

### 2.3 Chain structure and objective mix per zone set

Edges used: `PrevQuestID` (positive = must be rewarded, negative = must be active), `NextQuestID` (addon, unlocks the target),
`RewardNextQuest` (the same NPC offers the next quest on turn-in; `Player::GetNextQuest` still needs the NPC to have the quest as a
starter), `ExclusiveGroup` (>0: one of the group; <0: all of the group), `BreadcrumbForQuestId`. In the pool, `PrevQuestID > 0`: 423
quests, `< 0`: 10, `RewardNextQuest`: 321, `NextQuestID`: 24, `ExclusiveGroup > 0`: 75, `< 0`: 2, breadcrumbs: 9.

| Zone set | Roots | Standalone | Chains (>=2 quests) | Longest chain | Chains >= 3 | Excl. groups |
|---|---|---|---|---|---|---|
| Human | 34 | 25 | 9 | 7 | 6 | 1 |
| Dwarf/Gnome | 20 | 12 | 8 | 6 | 4 | 0 |
| Night Elf | 23 | 10 | 13 | 9 | 6 | 0 |
| Orc/Troll | 25 | 11 | 13 | 8 | 4 | 0 |
| Undead | 17 | 7 | 10 | 9 | 5 | 0 |
| Tauren | 23 | 18 | 5 | 11 | 5 | 0 |
| Skyborne Alliance | 27 | 20 | 6 | 8 | 6 | 0 |
| Skyborne Horde | 28 | 21 | 6 | 25 | 4 | 0 |

Objective mix per zone set. A quest counts once per category it has (a quest with kill and item objectives is in both). "None" is
delivery/talk (no objective row; turning in at the ender is the whole quest, often with a quest-provided item). Objective rows per type
are in brackets (0 kill, 1 item, 2 gameobject, 3 talk, 14 criteria tree).

| Zone set | Kill | Item | GO | Talk | Explore (areatrigger) | Event flag | Escort | Criteria | None | Objective rows |
|---|---|---|---|---|---|---|---|---|---|---|
| Human | 6 | 43 | 1 | 0 | 2 | 0 | 0 | 0 | 19 | 0:8 1:48 2:1 |
| Dwarf/Gnome | 7 | 27 | 0 | 0 | 1 | 0 | 1 | 0 | 4 | 0:10 1:32 |
| Night Elf | 5 | 34 | 0 | 0 | 0 | 3 | 0 | 0 | 11 | 0:7 1:41 |
| Orc/Troll | 7 | 28 | 2 | 0 | 0 | 2 | 0 | 0 | 11 | 0:14 1:29 2:4 |
| Undead | 11 | 33 | 1 | 0 | 0 | 1 | 0 | 0 | 5 | 0:20 1:38 2:1 |
| Tauren | 5 | 28 | 0 | 0 | 0 | 3 | 0 | 0 | 13 | 0:9 1:40 |
| Skyborne Alliance | 24 | 26 | 0 | 5 | 0 | 0 | 0 | 2 | 14 | 0:37 1:30 3:5 14:2 |
| Skyborne Horde | 30 | 31 | 0 | 7 | 0 | 0 | 0 | 3 | 21 | 0:43 1:37 3:8 14:3 |

"Item" objectives are mostly not "kill X for drops": many are carried items to deliver (`StartItem`/`ItemDrop`) and quest-required
loot. Pure kill-count quests are rare in the vanilla start zones (kill rows: 8-20 per zone set). Spell-cast objectives are not a
separate type in this schema; "use item X on Y" quests appear as item/GO objectives or as `Flags & 2` (completion event) and are in the
soft list. "Escort" here is the `Flags & 2`/script-detected count: only quest 412 (`Operation Recombobulation`, Dwarf/Gnome) is detected
by a waypoint-start script; 435 `Escorting Erland` (Silverpine) and 938 `Mist` (Teldrassil, also timed) are escorts by their quest text
(heuristic: I found them by text search; the converted quests have `QuestInfoID` 0, never 84).

### 2.4 Hubs, and class quests

Top hubs by (quests started + ended) in the zone set. "Dist" is the straight line from the start point to the first parent-zone hub
(a lower bound for walking).

| Race | Valley hubs (starts/ends) | First parent-zone hub | Dist |
|---|---|---|---|
| Human | Marshal McBride (-8903,-163) 4/4, Deputy Willem (-8934,-137) 5/2, Milly Osworth 2/2 | Marshal Dughan, Goldshire (-9466,74) 7/8 | ~590 yd |
| Dwarf/Gnome | Sten Stoutarm (-6215,328) 2/1, Grelin Whitebeard (-6363,567) 3/3 | Rejold Barleybrew (-5378,315) 4/4; Kharanos hub (-5578,-501) | ~850 / ~1060 yd |
| Night Elf | Tallonkai Swiftroot (9899,985) 3/3, Athridas Bearmantle (9888,966) 3/3, Gilshalan, Dirania | Denalan (9507,714) 5/8, Corithras Moonrage (9737,956) 5/5 | ~810 yd |
| Orc/Troll | Gornek (-600,-4186) 2/5, Zureetha Fargaze (-629,-4228) 3/2, Foreman Thazz'ril | Gar'Thok, Razor Hill (275,-4709) 4/4; Master Gadrin, Sen'jin (-826,-4921) 3/3 | ~1000 yd |
| Undead | Coleman Farthing (2262,244) 3/2 ... (valley is at 1850,1600; Brill hubs below) | Executor Zygand, Brill (2278,296) 4/6; Apothecary Johaan (2259,347) 6/4 | ~1525 yd |
| Tauren | Grull Hawkwind (-2913,-258) 3/3, Chief Hawkwind (-2878,-222) 3/3 | Mull Thunderhorn, Bloodhoof (-2341,-445) 6/6; Baine Bloodhoof 3/3 | ~600 yd |
| Skyborne | Rorian the Dayseeker (4085,1892) 3/4 (Thendal), Yala Windwatcher (4144,1601) 2/2 | Constable Aonda, Shen'dar (3269,1691) 5/5 (Horde 7/6); Aamelia Windfield, Windfield Orchard (1918,1633) 6/7 | ~840 / ~2190 yd |

Class quests at levels 1-10 (class sorts: -81 Warrior, -61 Warlock, -82 Shaman, -141 Paladin, -161 Mage, -162 Rogue, -261 Hunter, -262
Priest, -263 Druid), counted **per class the race can be** (hard defect count in brackets). They are given by class trainers, mostly
in capitals and start valleys. They gate spells and pets (Hunter taming, Warlock imp) so bots will want them eventually, but they are
not needed for level 10 and are lower priority in scoring.
- Human: Hunter 6 (5), Mage 9 (0), Paladin 1 (0), Priest 11 (1), Rogue 9 (1), Warlock 13 (3), Warrior 28 (3)
- Dwarf: Hunter 13 (5), Paladin 1 (0), Priest 10 (0), Rogue 9 (1), Shaman 18 (1), Warrior 27 (2)
- Gnome: Mage 9 (0), **Priest 0**, Rogue 9 (1), Warlock 13 (3), Warrior 27 (2)
- Night Elf: Druid 9 (0), Hunter 14 (5), Priest 10 (0), Rogue 9 (1), Warrior 27 (2)
- Orc: Hunter 10 (1), Mage 8 (0), Rogue 6 (1), Shaman 19 (1), Warlock 14 (3), Warrior 27 (2)
- Troll: Hunter 10 (1), Mage 9 (0), Priest 9 (1), Rogue 6 (1), Shaman 19 (1), Warlock 10 (3), Warrior 27 (2)
- Undead: Mage 9 (0), **Paladin 0**, Priest 9 (1), Rogue 8 (1), Warlock 14 (3), Warrior 27 (2)
- Tauren: Druid 9 (0), Hunter 10 (1), Shaman 19 (1), Warrior 27 (2)
- Skyborne Alliance: Druid 3 (0), Hunter 10 (9), Mage 8 (0), Rogue 2 (1), Warrior 26 (2)
- Skyborne Horde: Druid 4 (0), Hunter 6 (5), Rogue 2 (1), Shaman 22 (5), Warrior 26 (2)

The two new combinations that have **no class quests in the data** are Gnome Priest and Undead Paladin: bots of those combos simply
have none (no error). Hunter counts include many `Taming the Beast` / `Training the Beast` quests with no starter row (item or script
started, see 4.2), which blocks Hunter pets until resolved.

### 2.5 Skyborne (Zephras Isle), separately

- Quest rows: `QuestSortID 16593` has **88 quests**, 84 of them in the classic set; the other four (94488 `The Ties That Bind`, 94489
  `The Wounds of Betrayal`, 94490 `Ripped Missive`, 94491 `The Fate of the Den`) have a starter row but **no `quest_classic_level` /
  `quest_template_classic_level` row, no ender and no objectives**: their min level and quest level are 0 and nobody can turn them in.
  They are outside the pool; list them in the blacklist (section 4.5). Skyborne class quests are in the class sorts with race mask
  bits 32/33 (e.g. 94006, 94013, 94638 Druid/Hunter at `MinLevel 7`).
- Creature data: 2148 creature spawns on map 2991, all in an existing mmap tile. The creature `areaId`/`zoneId` columns are 0 for every
  spawn on maps 0, 1 and 2991 (65,462 rows), so zone/area must be derived from position or from the map data, not from the tables. The console's
  `mapdata/2991.zones.json` names the areas (Thendal Village 16635 at about (4081,1875), Shen'dar Village, Valanaar, Windfield Orchard,
  Falaath, Rohashi Spires ...).
- Race masks: of the 84 classic-set quests in the sort, 63 are for all races, 19 are masked Horde only (including the trunk quests
  92514 `Welcome to Shen'dar Village`, 92579, 92700), 1 is Horde-Skyborne only and 1 both Skyborne; **none is masked Alliance**.
  Alliance Skyborne bots therefore get the shared quests only, and quests behind a Horde-only prerequisite are unreachable (their
  longest path is 8: 92460 `Coming of Age` ... 92472 `The Next Step`). Probable cause: the sniff came from a Horde character; the
  High Order (Alliance) variants are not in the data. (Open question 10.)
- Chain: a single 25-step main chain in the Horde zone set: `Coming of Age` 92460, `Harmony in Balance` 92461, `Elemental Unrest` 92464, `Agitators`
  92465, `Return to Rorian` 92469, `Aetheen of the Gale` 92471, `Foul Matriarch` 92470, `The Next Step` 92472, `Welcome to Shen'dar`
  92514, `The Criminal Element` 92517, `Infiltrating the Cult` 93036, `Falaath Village` 92529, **`Among the Faithful` 92528 (blocked)**,
  **`The Western Watch` 93926 (blocked)**, **`A Last Request` 93927 (blocked)**, `To Valanaar` 92579, `The Grand Skyseer` 92700, the
  `Broken Construct` trio 93735/93737/93738, `A Firm Response` 93746, `In Service of Zephras` 92871, `Tower Defense` 93320, `Breaking
  the Break` 92645, `Return to Valanaar` 92880 (title list from the path; first element 92460 is `Coming of Age`). The path stops at
  step 13 for the main chain; side chains and standalone quests (20 Alliance / 21 Horde) remain.
- Completeness check, Skyborne Alliance / Horde zone sets: clean 47 / 61, hard 16 / 21, soft 1 / 2, playable after the prerequisite
  cascade **34 / 40** (lists in 4.5). Hard causes in the Horde set (21 quests): 6 relation-less new quests (92466-92468, 93159, 93160,
  93172); 5 objectives whose creature has no `creature_template` row (252078 for 92528, 255013 for 92643, 273972 for 92708, 251684 for
  93159, 257066 for 93949); 4 more targets without a spawn (256634 `Confront Lorthuna Credit` for 92646, 252863 `Ferauu the Bludgeon`
  for 92693, 264712 `High Priestess Lorthuna` for 94568, talk target 252155 for 93926); 4 starters and 5 enders without a spawn (93927,
  94485-94487 / 93926, 94484-94487); two quests whose targets are level 20 at quest levels 6 and 11 (92595, 92640; 92640 also lists two
  elite targets). Not hard but unsupported: 3 criteria-tree objectives (92474 `Falling With Style`, 92598 `The Gift of Skysight`, 94414
  `The Anchors of Zephras`: "use Skysight near the Elemental Convergence", "view the Anchor Pylon") that need an action no bot has. The
  elite hunt `WANTED: Vulgara the Insatiable` (93318) is `QuestInfoID 1` (group).
- Verdict: **exists, mostly complete but not yet playable end to end.** The hub NPCs, vendors and most kill/collect quests are there;
  the main chain breaks at 92528/93926/93927 and the new class-quest and camping quests have no relation rows.

## 3. Data model: what decides what

### 3.1 Tables and columns (this schema, TDB 12.1 base with the classic conversion on top)

| Question | Source |
|---|---|
| Is it a classic quest, and its levels | `quest_classic_level` (`QuestLevel`, `MinLevel`, `MaxLevel`, read into `Quest::SetClassicLevelRange`); `quest_template_classic_level` (`LevelType`, `QuestLevel`, shown in the log). `Player::GetQuestMinLevel/GetQuestLevel` use them and ignore ContentTuning for classic quests. |
| Who gives it | `creature_queststarter(id, quest)`, `gameobject_queststarter`; items via `ItemSparse.StartQuestID` (client DB2, not in SQL); area triggers via `areatrigger_involvedrelation` (36 rows for the classic set); scripts (`smart_scripts`, the converted VMaNGOS start/end scripts) |
| Who ends it | `creature_questender`, `gameobject_questender`; `QUEST_FLAGS_AUTO_COMPLETE` (0x10000) quests need no NPC |
| Prerequisites | `quest_template_addon`: `PrevQuestID`, `NextQuestID`, `ExclusiveGroup`, `BreadcrumbForQuestId`; `quest_template.RewardNextQuest`; `conditions` (SourceTypeOrReferenceId 19 = accept, 20 = show mark; 113 rows for the classic set, evaluated by `SatisfyQuestConditions`) |
| Race/class/skill/rep limits | `quest_template.AllowableRaces` (bigint, `RaceMask`), addon `AllowableClasses`, `RequiredSkillID/Points`, `RequiredMinRepFaction/Value`, `RequiredMaxRepFaction/Value`, addon `MaxLevel` (unused: 0 in the pool) |
| Repeatable/daily/timed | `Flags` (0x1000 daily, 0x8000 weekly), `TimeAllowed` (6 pool quests), `SpecialFlags` (addon) |
| Group flags | `SuggestedGroupNum` (always 0 here), `QuestInfoID` (1 group, 21 life, 41 PvP, 62 raid, 81 dungeon; client QuestInfo DB2 not readable here, the meaning is the TrinityCore convention and matches the quest titles I sampled), `Flags & 0x8` sharable (set on most quests, not a group marker) |
| Rewards, fixed | `RewardItem1-4` + `RewardAmount1-4`; money `RewardMoneyDifficulty`/`RewardBonusMoney` (QuestMoneyReward.db2), xp `RewardXPDifficulty` x `RewardXPMultiplier` (QuestXP.db2), `RewardSpell`, `RewardDisplaySpell1-3`, `RewardFactionID1-5/Value`, `RewardMailTemplateID` |
| Rewards, choice | `RewardChoiceItemID1-6` + quantity (pool: 686 of 847 have no choice, 93 have 2, 51 have 3, 12 have 4, 4 have 5, 1 has 6) |
| Items given on accept | `StartItem` (+ `ProvidedItemCount`), `ItemDrop1-4` (+ quantities), `SourceSpellID` (addon, cast on accept) |
| Objectives and targets | `quest_objectives` (`Type`, `ObjectID`, `Amount`, `Flags`, `StorageIndex`, `ParentObjectiveID`); Type 0 creature (`ObjectID` = `creature_template.entry`, other creatures give credit through `creature_template.KillCredit1/2`), 1 item, 2 gameobject, 3 talk-to, 10/19/20 areatrigger, 14 criteria tree |
| Where the target is | `creature(id, map, position_x/y/z, spawntimesecs, wander_distance, spawnDifficulties, phase*)`, `gameobject(...)`; world spawns have `spawnDifficulties` `'0'` (dungeon difficulties are separate rows); `game_event_creature/gameobject` are empty here, `pool_members` pools exist |
| Target level | `creature_classic_level(entry, level_min, level_max)` (7705 rows); `creature_template.Classification` (elite), `faction` |
| Where an item comes from | `creature_template_difficulty.LootID/SkinLootID/PickPocketLootID` (DifficultyID 0) -> `creature_loot_template` (`Chance`, `QuestRequired`, `GroupId`), `skinning_loot_template`, `pickpocketing_loot_template`, `gameobject_template.Data1` (types 3, 25) -> `gameobject_loot_template`, `item_loot_template`, `reference_loot_template`, `fishing_loot_template`, `npc_vendor` |
| Retail leftovers to ignore | `creature_questitem` (166,253 invalid rows in the log), `spawn_tracking*`, `quest_poi*`, `ui_map_quest*`, `creature_quest_currency` |

### 3.2 What the core already computes: do not re-implement

All in `Player` (`src/server/game/Entities/Player/Player.cpp`), all O(small), all callable with `msg = false` for a silent answer:
- Eligibility: `CanTakeQuest(quest, false)` (status, exclusive group, class, race, min/max level, skill, reputation, previous/next/
  breadcrumb chain, timed, day/week/month/seasonal, conditions, expansion, disables), `CanSeeStartQuest`, `CanAddQuest` (log full,
  room for the source item), `SatisfyQuestLog`, and the individual `SatisfyQuest*` checks (`cs_bot.cpp` already enumerates them for the
  `bot quest add` diagnostics: use the same list to name the blocker).
- NPC offers: `GetQuestDialogStatus(giver)` returns what this NPC has for this player (available, reward ready, trivial) using the
  creature's starter/ender relations, hostility, combat state, `CanTakeQuest` and the trivial-level threshold. This is the cheapest
  "does that NPC have something for me" test and is the authoritative one.
- Progress: kills (`KilledMonsterCredit` through the XP/kill path, including `KillCredit` creatures), looting (`ItemAddedQuestCheck`),
  area triggers (`AreaExploredOrEventHappens`), talking (`TalkedToCreature`, called from the gossip-hello and quest-hello handlers),
  all through `UpdateQuestObjectiveProgress`; `CanCompleteQuest` / `CompleteQuest`; `CanRewardQuest(quest, type, itemId, false)`;
  `RewardQuest` (xp, money, reputation, spells, mail, chain, `SaveToDB(false)`); `AbandonQuest`.
- Numbers: `Quest::XPValue(player)` (full xp at `QuestLevel >= level - 1`, then `diffFactor = clamp(2*(questLevel - level) + 12, 1,
  10)`, so 10 % at six levels below), `Quest::MoneyValue`, `Player::GetQuestLevel/GetQuestMinLevel`, config `Quests.LowLevelHideDiff` /
  `HighLevelHideDiff` (sim worldserver.conf: 5 and 2; core defaults 4 and 7).
- Quest log size: `MAX_QUEST_LOG_SIZE` = **35** in this core. The 1.60 client's own limit is not verified (classic vanilla used 20).
- Relations: `ObjectMgr::GetCreatureQuestRelations(entry)`, `GetCreatureQuestInvolvedRelations`, `GetCreatureQuestInvolvedRelationReverseBounds(questId)`
  and the GO equivalents, `GetExclusiveQuestGroupBounds`, `GetAllCreatureData()` / `GetAllGameObjectData()` (spawn lists in memory),
  `GetQuestTemplates()`, `GetItemTemplate`, loot stores (`LootTemplates_Creature` etc.). No SQL is needed at runtime.

What the bot must add itself: choosing among eligible quests, walking, picking targets, interacting the way a client would (see 5.8),
reward choice, abandon/blacklist policy, and the data-defect knowledge (4.5).

### 3.3 Startup caches (world thread, read-only afterwards)

Built once after `ObjectMgr` has loaded (same place the engine registry is built), from the in-memory stores, no new DB access:

1. `QuestIndex`: per classic quest: starters (entry + spawn list), enders, objectives with resolved target spawn clusters, item sources
   (creature loot with chance, GO loot, vendor), level data, flags, computed `StaticBlockers` (the section 4 codes), `EstimatedWork`.
2. `SpawnIndex`: per creature/GO entry the spawn points on world maps (with `KillCredit` expansion); a coarse grid per map
   (for example 100 yd cells) for "nearest spawn of entry X from here" in O(1)-ish.
3. `ZoneSets`: per race the pool order and hubs (derived offline or at startup from the sort ids and positions).
4. `QuestRules` (Phase 9b): blacklist/whitelist/overrides from a table `bot_quest_rule` (to be created in a dated
   file in `sql/custom/`), loaded at startup and by an explicit reload command, versioned (rule version in every decision row).
All structures are immutable after build; per-bot state lives in the bot's AI (temporary blacklist with expiry, plan, counters). Memory
estimate: about 4605 quests x (a few hundred bytes) + 65 k spawn points: below 20 MB.

## 4. Data quality audit (levels 1-10)

### 4.1 Funnel and method

| Step | Count |
|---|---|
| Classic quests in the world DB | 4605 (of 48,651 `quest_template` rows) |
| ... with `MinLevel <= 10` | 1208 |
| ... minus unused/test titles (`<UNUSED>`, `<NYI>`, `<TXT>`, `[...]`, `Blank`) | -56 |
| ... minus profession sorts | -15 |
| ... minus holiday/event/PvP/AQ-effort sorts (-365 70, -366 62, -22 55, -367 32, -364 19, -284 16, -368 12, -369 6) | -272 |
| ... minus test/voucher quests (`Test Kill Quest`, `Hunter test quest(2)`, `REUSE`, `iCoke ...`, `Darkmoon Faire`, `(123)aa`, `Waskily/Wabbit`) | -18 |
| **Pool** | **847** (reproduced in SQL, appendix A1) |

Checks use world spawns only (`spawnDifficulties` `'0'`), `KillCredit1/2` expansion for kill targets, reference loot counted as "has a
source" without expanding it. The whole-DB `DBErrors.log` totals (166 k `creature_questitem` lines, 36 k ContentTuning lines ...) are
retail leftovers; only the classic-set counts below matter.

### 4.2 Defects in the pool (847 quests), with SQL-reproducible counts

| Defect | Quests | of which Forever (ID >= 90000) | Meaning |
|---|---|---|---|
| No starter row (no creature/GO starter) | 94 | 58 | Not necessarily unobtainable: item-started and script-started quests have no row here; the item link (`ItemSparse.StartQuestID`) is in the client DB2 which SQL cannot read (the hotfix table has only 2 such rows). Split below. |
| Starter rows exist but none has a world spawn | 10 | 4 | giver absent from the world |
| No ender row (and not auto-complete) | 73 | 58 | cannot be turned in |
| Ender rows exist but none has a world spawn | 11 | 5 | |
| Objective target (kill/GO/talk) without any world spawn | 21 | 12 | includes 7 creature entries (and 1 GO) missing from the template tables, see 4.3 |
| Item objective with no loot/vendor/container source in the DB | 61 | 18 | **uncertain**: the item may be created by a spell/script (for example `Filled Crystal Phial` 5184 for 921/929/933/7383) or the loot row was lost; needs a runtime probe |
| Starter or ender only in an instance | 2 | 0 | 6661, 6662 (Deeprun Tram) |
| Kill target level far above the quest | 2 | 2 | 92595 (target level 20 at quest level 6), 92640 (level 20) |
| Elite target | 1 | 1 | 92640 |
| `RewardNextQuest` points to a missing quest | 4 | 4 | 93160, 93172, 95350, 98424: chain breaks, quest itself fine |
| Criteria-tree objective (type 14) | 19 | 19 | 3 Zephras + 16 "Camping 101"/"The Great Outdoors" (sit near a campfire, gain a buff ...): needs a dedicated action |
| Escort by script / by text (heuristic) | 1 + 2 | 0 | 412 by waypoint start; 435, 938 by quest text |
| Timed | 6 | 1 | 812, 853, 938, 3364, 3522, 97257 |
| Skill required | 7 | 5 | 768, 769 (Tauren leatherworking), "Camping 101" ones |
| Completion event flag (`Flags & 2`) | 39 | 6 | quest completes by a script event, objective unknown to the bot |
| Completion area-trigger flag (`Flags & 4`) | 7 | 0 | needs the bot to walk into the trigger (supportable) |
| VMaNGOS start/end script attached | 57 | - | converted `quest_start_scripts`/`quest_end_scripts` (timed action lists 9500000xx in `smart_scripts`): usually flavor, sometimes the objective |
| `QuestInfoID` 1 / 81 / 41 | 14 / 11 / 16 | 4 / 0 / 0 | group / dungeon / PvP |
| `SuggestedGroupNum > 1` | 0 | 0 | the field is unused (all 4605 are 0) |

Summary of severities: **hard defect 128 of 847 (15.1 %)**, **soft-only 140 (16.5 %)**, **clean 579 (68.4 %)** using the lists in 4.5
(hard = no starter/no ender/no spawn for starter, ender or target/instance-only/target far too strong/elite; soft = the rest of the table).
A quest with no starter row but a known item start (for example `Nibbled-On Book` 91741, `StartItem` or `SourceSpellID` set) is counted
hard until it is probed at runtime.

Split of the 94 "no starter row" quests (the likely cause, not proof):
- 58 Forever-range quests (91723..99143): new beta content whose NPC relations were never sniffed or converted (all 58 also have no
  ender). Includes the Northshire/Elwynn "kobold and library" chain 91741 -> 91743 -> 91745 -> 91752 -> 91758 -> 91772 -> 91775 ->
  91777 (91741 `Nibbled-On Book` is item-started, the rest follow through `RewardNextQuest`, but with no relation rows the next quest is
  not offered: `GetNextQuest` requires `questGiver->hasQuest(next)`), 92124, the Skyborne class quests 92466-92468, 92479, 92482-92484,
  92532, 94013, 94050, 94774, 94792-94979, 97243-97257, the camping series 95998-97923, the Tauren/Elwynn 99079-99143 group and 98024,
  98424, 98435, 92742/92744, 93159/93160/93172.
- 23 vanilla quests with `StartItem` or `SourceSpellID` set or tutorial quests: probably item/spell/script-started (for example
  `Welcome!` 5805, 5841-5844, 5847, 123, 184, 361, 770, 781, 830, 832, 883-885, 897, 927, 968, 3111, 7810, 7908, 8856).
- 12 other vanilla quests (136, 785, 912, 999, 1005, 1006, 1099, 1174, 1500, 8001, 8290, 8295) and 1 class quest (5659): the starter was
  an item, an event or a removed NPC in VMaNGOS.

### 4.3 Missing items and creatures (DB2 / template gaps), classic quests only

From the sim worldserver's `DBErrors.log`, counted per distinct classic quest (all 4605, then the pool):

| Log message | Classic quests | In the pool |
|---|---|---|
| `RewardChoiceItemIdN` ... item does not exist | 57 | **0** |
| `RewardItemIdN` ... item does not exist | 79 | **0** |
| objective item does not exist ("quest can't be done") | 20 | **0** |
| `SourceItemId` item does not exist | 2 | 0 |
| objective creature entry does not exist (no `creature_template` row) | 8 | **7** (all Forever/Zephras and 97257/97485) |
| objective gameobject entry does not exist | 1 | **1** (91772, entry 563442) |
| `RewardNextQuest` target does not exist | 4 (classic) | 4 |

Examples of missing item ids: quest 592 choice item 6723; quest 1048 choice items 10711, 6802, 6803; quest 1053 choice items
6829, 6830; objective items 2665 (quest 90), 9243 (quest 2868), 10454/10455 (quests 3373/3374), 12731 (quests 5063/5067/5068), 12871
(quest 5166). Pool quests are unaffected because the affected quests are above level 10 (14 quests with `MinLevel <= 10` have a
missing reward item: 7881, 7882, 7889, 7890, 7894, 7895, 7899, 7900 (Warsong Gulch marks), 7905, 7926 (Darkmoon), 9411-9414; none is in
the pool). 2542 `creature_loot_template` rows are skipped at load because their item is missing from the client data; none of them is
an objective item of a pool quest (all pool item objectives exist as items). **The beta's item DB2 gap is therefore a level 20+ concern,
not a level 1-10 one; the creature_template gaps for the Zephras objectives are real blockers.**

### 4.4 Quests that need an escort, an event or a script (pool)

- Escort: 412 `Operation Recombobulation` (script waypoint start), 435 `Escorting Erland`, 938 `Mist` (both likely, by text).
- Completion by event (`Flags & 2`, 39 quests): pool lists in 4.5 (soft). Typical: "talk to X and wait", NPC-spawned fights.
- Start/end scripts (57 quests) include summons and waypoint starts (action 53 appears in script lists for quests 155, 648, 836, 976,
  1103, 1191, 1273, 1955, 1957, 2767, 3182, 3367, 4961, 5087, 5158, 5944, 6523, 7642: none of them is in the pool except 412).
- Group (`QuestInfoID` 1, 14 quests in the pool): 99, 176 `Wanted: "Hogger"` (Elwynn), 314 `Protecting the Herd` (Dun Morogh), 442, 450,
  999, 1005, 1006, 1500, 2499 `Oakenscowl` (Teldrassil), 91752, 91775, 91777 (the new Northshire/Elwynn book chain, also relation-less)
  and 93318 `WANTED: Vulgara the Insatiable` (Zephras). 11 dungeon quests with `QuestInfoID` 81 (914, 971, 1489, 1490, 4295, 5722-5725,
  5728, 5761).

### 4.5 Seed blacklist (initial `quest_blocked` knowledge)

Reason codes are the ones defined in section 6. Each list is the complete set in the pool; "static" means the bot's startup audit can
produce the row without any run. Quests marked **probe** might be fine and need a one-time runtime check
(`bot quest add` + giver interaction on the sim) before a permanent rule.

**HARD (128 quests, blacklist from day one):**

- `NO_STARTER_ROW` (94): 123, 136, 184, 361, 770, 781, 785, 830, 832, 883, 884, 885, 897, 912, 927, 968, 999, 1005, 1006, 1099, 1174,
  1500, 3111, 5659, 5805, 5841, 5842, 5843, 5844, 5847, 7810, 7908, 8001, 8290, 8295, 8856 (probe: item/spell/tutorial started),
  and the 58 Forever quests 91723, 91725, 91733, 91741, 91743, 91745, 91752, 91758, 91772, 91775, 91777, 92124, 92466, 92467, 92468,
  92479, 92482, 92483, 92484, 92532, 92742, 92744, 93159, 93160, 93172, 94013, 94050, 94774, 94792, 94793, 94863, 94864, 94978, 94979,
  95998, 96605, 96626, 96627, 96659, 97243, 97244, 97245, 97257, 97923, 98024, 98424, 98435, 99079, 99080, 99081, 99082, 99101,
  99127, 99128, 99129, 99130, 99131, 99143. (91741 is item-started: the chain behind it is the only probe candidate there.)
- `NO_STARTER_SPAWN` (10): 2399, 4295, 5724, 6846, 6901, 9029, 93927, 94485, 94486, 94487.
- `NO_ENDER_ROW` (73): the 58 Forever quests above plus 785, 912, 999, 1005, 1006, 1099, 1174, 1500, 3111, 5659, 7908, 8001, 8290, 8295, 8856.
- `NO_ENDER_SPAWN` (11): 2399, 4295, 5722, 6846, 6901, 9029, 93926, 94484, 94485, 94486, 94487.
- `NO_TARGET_SPAWN` (21): 409, 1471, 1504, 1689, 1819, 2118, 5723, 5728, 9422, 91772, 92528, 92643, 92646, 92693, 92708, 93159, 93926,
  93949, 94568, 97257, 97485.
- `STARTER_ONLY_IN_INSTANCE` / `ENDER_ONLY_IN_INSTANCE`: 6661, 6662.
- `TARGET_LEVEL_TOO_HIGH`: 92595, 92640. `ELITE_TOO_STRONG`: 92640.
- Not in the pool but also blocked (no level data, no ender, no objectives): 94488, 94489, 94490, 94491 (Zephras), 94911 (`Child of Nature`,
  druid, starter only).

**SOFT (140 more quests, handle or blacklist with a reason until supported):** all pool quests with any of the following and no
hard defect. The ones in start zones are listed by race below; the full set is the SQL in appendix A.
- `MISSING_ITEM_SOURCE` (61, probe): 410, 437, 579, 593, 619, 746, 812, 881, 882, 914, 921, 929, 933, 1099, 1174, 1861, 3924, 4295,
  5725, 5761, 6846, 6901, 7383, 8001, 8266, 8268, 8289, 8290, 8292, 8293, 8295, 8296, 8368, 8372, 8386, 8389, 8565-8570, 8856, 91725,
  91733, 91743, 91752, 91775, 91777, 92466, 92742, 92744, 93737, 93927, 97244, 97245, 98424, 99080, 99127, 99130, 99143.
- `OBJECTIVE_UNSUPPORTED` (criteria tree, 19): 92474, 92598, 94414, 95998, 96101, 96605, 96626, 96646, 97923, 97963, 97964, 97965,
  97967-97972, 98284.
- `NEEDS_ESCORT` (3): 412, 435, 938. `TIMED_UNSUPPORTED` (6): 812, 853, 938, 3364, 3522, 97257. `SKILL_REQUIRED` (7): 768, 769, 97923,
  97965, 97967, 97969, 97971. `NEEDS_GROUP` (QuestInfoID 1/81/41): the 41 quests above plus Warsong Gulch ones in sort 3277.
- `NEEDS_EVENT` (`Flags & 2`, 39) and `needs script` (57): supportable per quest later; blacklist until a bot completed one on the sim.

**Hard and soft defects per start zone (zone sets):**

| Zone set | Clean | Hard | Soft | Playable (prerequisites reachable) | Hard ids | Soft ids |
|---|---|---|---|---|---|---|
| Human | 44 | 18 | 8 | 51 | 123, 91723, 91725, 91733, 91743, 91745, 91752, 91758, 91772, 91775, 91777, 92124, 99127-99131, 99143 | 54, 62, 76, 112, 114, 176, 333, 579 |
| Dwarf/Gnome | 31 | 1 | 6 | 37 | 5841 | 287, 308, 314, 412, 3364, 5541 |
| Night Elf | 36 | 3 | 13 | 48 | 927, 2399, 5842 | 489, 921, 929, 930, 931, 933, 938, 997, 2499, 2520, 2561, 3522, 7383 |
| Orc/Troll | 34 | 4 | 9 | 40 | 785, 830, 832, 5843 | 804, 806, 808, 812, 823, 827, 829, 924, 5727 |
| Undead | 41 | 3 | 5 | 43 | 361, 409, 5847 | 407, 410, 411, 492, 590 |
| Tauren | 24 | 10 | 13 | 37 | 770, 781, 97485, 98424, 98435, 99079-99082, 99101 | 746, 748, 753, 754, 756, 758-760, 768, 769, 771, 772, 854 |
| Skyborne Alliance | 47 | 16 | 1 | 34 | 92466, 92528, 92640, 92643, 92693, 93159, 93160, 93172, 93926, 93927, 93949, 94484-94487, 94568 | 93318 |
| Skyborne Horde | 61 | 21 | 2 | 40 | the Alliance list plus 92467, 92468, 92595, 92646, 92708 | 93318, 93737 |

Playable counts come from the prerequisite cascade (`PrevQuestID` and `NextQuestID` sources, race masks, hard defects, appendix B).
The old-world zones have 37-51 playable quests (whether that is enough xp for level 10 is checked by the step-1 xp table in section 9);
the weak ones are Tauren (24 clean of 47: the 98xxx/99xxx Forever quests are hard) and especially Skyborne (34 of 64 Alliance and 40 of 84 Horde
quests are reachable, the Horde main chain is cut at step 13 and the Alliance one at step 8).

### 4.6 Two data gaps not visible in per-quest counts

- Gnome and Troll start positions (section 2.1).
- The Skyborne start trunk (section 2.5).
- Possible additional gap: `AllowableRaces` of 55 classic quests is `255` (races 1-8 only), so Skyborne bots cannot take them; with the
  mask used for new Skyborne content (`6130900294268439629` Alliance incl. bit 32, `12261800583900083122` Horde incl. bit 33,
  `12884901888` both) this looks intentional, not a conversion error.

## 5. The quest loop as engine parts

The Phase 2 engine (`BotEngine.h`, `BotAI.h`, `engine-design.md`) gives three engines (NonCombat, Combat, Dead), one action per AI tick
per bot (`Bot.AI.TickMs`, default 500 ms), O(1) triggers over cached values, relevance bands (Default 5, Normal 10, High 20, Move 30,
Interrupt 40, Dispel 50, Raid 60, Emergency 90, Pull 105-107), multipliers (0 forbids), names in registries, `decision` rows with
alternatives. The quest logic is implemented as new registry entries; the quest files are separate from the engine hooks (section 9). Names use the existing lower_snake style.

### 5.1 State machine (per bot; the plan is derived from the player's quest log, so a relog or crash loses nothing)

```
NEED_QUEST -> CHOOSE -> TRAVEL_GIVER -> INTERACT_ACCEPT --(hub batching: accept every eligible quest of this giver)--+
   ^                                                                                                            |
   |   +---------------------------- per objective (cheapest first) ----------------------------+              v
   |   | CHOOSE_TARGET -> TRAVEL_TARGET -> ENGAGE (kill/loot) | USE_OBJECT | TALK | EXPLORE -> CHECK_PROGRESS |<-+
   |   +---------------------------------------------------------------------------------------+
   |                              all objectives done (QUEST_COMPLETE)                                          |
   |                                      v                                                                     |
   +-- NEXT (chain unlock / re-evaluate) <- TURN_IN (choose reward) <- TRAVEL_ENDER <-----------------------------+
BLOCKED(quest, code) -> temp-blacklist + quest_blocked row -> NEED_QUEST      DEAD -> (Dead engine) -> resume plan
```

State lives in values, not in a separate state variable: `quest_plan` (quest id, step, target, since) is recomputed from the quest log
when it is empty or stale. Transitions are logged on change only (a `decision` row per change of quest or step, never per tick).

### 5.2 Strategies (names and engines)

| Strategy | Engine | Purpose |
|---|---|---|
| `quest` | NonCombat | umbrella: choose, accept, turn in, re-evaluate. Default on for levels 1-30 once Phase 8 ships. |
| `quest_objective` | NonCombat | do the current objective: travel to target, loot, use object, talk, explore. |
| `quest_recover` | NonCombat | stuck / path_fail / blocked handling ladder (5.6). |
| `quest_combat` | Combat | quest-aware target preference and leash for the combat code (hooks via multipliers), loot priority. |
| `quest_dead` | Dead | keep the plan across death, remember where it happened, back off from a target that killed the bot. |

### 5.3 Triggers (all O(1), read values; interval in ms for rate limiting)

| Trigger | Active when | Action | Band |
|---|---|---|---|
| `quest_need` (1000) | no executable quest in the log and `quest_plan` empty | `quest_choose` | Normal 10 |
| `quest_giver_near` (500) | plan step = ACCEPT and the giver creature/GO is within 4.5 yd (interaction distance is 5) | `quest_accept` | Normal 10 |
| `quest_hub_extra` (500) | right after an accept: the same giver still has `Quest`-status entries | `quest_accept` (batch) | Normal 10 |
| `quest_ready_turn_in` (500) | a quest is `QUEST_STATUS_COMPLETE` and the ender is within 4.5 yd | `quest_turn_in` | High 20 |
| `quest_travel_giver` / `quest_travel_ender` (500) | plan step needs a giver/ender not yet near | `nav_move_to` (target = spawn) | Move 30 |
| `quest_objective_pending` (500) | a quest has an unfinished objective | `quest_choose_target` then `nav_move_to` | Move 30 |
| `quest_target_near` (500) | a valid target is visible and within pull range | `quest_engage` (hands over to the combat pull actions, band Pull) / `quest_use_object` / `quest_talk` | High 20 |
| `quest_loot_ready` (250) | a corpse of a quest target (or a quest GO) is lootable | `loot_target` (Phase 3 loot action) | High 20 |
| `quest_area_trigger` (500) | explore objective: reached the trigger position | `nav_move_to` onto the trigger centre | Move 30 |
| `quest_complete_event` (event) | `QUEST_COMPLETE` row seen (value change) | `quest_choose` (re-plan) | Normal 10 |
| `quest_progress_stalled` (2000) | no objective change and no position change for `Bot.Quest.StallSec` (60) while a plan is active | `quest_recover` | High 20 |
| `quest_level_up` / `quest_zone_changed` (event) | `OnLevelUp` hook / `GetZoneId()` or map changed | invalidate `quest_eligible`, `quest_choose` | Normal 10 |
| `quest_log_full` (1000) | no free slot (`SatisfyQuestLog`) and a candidate waits | `quest_abandon_worst` | Normal 10 |
| `bag_full` (2000) | `CanStoreNewItem` fails for the reward or source item | `vendor_or_destroy` (Phase 10; until then: log and block) | High 20 |

### 5.4 Actions (one job each, `SetResult(reason, summary, detailsJson)`)

`quest_choose` (scores eligible quests, picks a plan, logs the decision with alternatives), `quest_choose_target` (nearest valid spawn
of the objective entry, with `KillCredit` sources, loot sources for items), `nav_move_to` (7.5: PathGenerator + `MotionMaster::MovePoint`,
hop chaining, no-progress detection), `quest_accept` (5.8), `quest_turn_in` (5.8, reward choice 5.9), `quest_engage` (selects the
target and issues the pull through the combat attack action), `quest_use_object` (GO use for quest GOs), `quest_talk` (gossip hello on a
talk-to NPC), `quest_abandon` / `quest_abandon_worst`, `quest_block` (writes the `quest_blocked` row and the temporary blacklist),
`quest_recover` (ladder), `quest_reevaluate`.

### 5.5 Values (cached, intervals in ms)

| Value | Content | Refresh |
|---|---|---|
| `quest_log` | the bot's active quests and objective progress (reads `Player` once per refresh) | 1000, and on quest events |
| `quest_eligible` | pool quests passing the eligibility filter (5.10) with scores | invalidated by level up, zone change, quest reward, abandon; fallback 60 s |
| `quest_plan` | current quest id, step (accept/objective n/turn-in), target entry, target position, since | on change |
| `quest_nearest_giver` / `quest_nearest_ender` | spawn position and path hint for the plan's giver/ender | on plan change |
| `quest_target_spawns` | up to 8 candidate spawn points near the bot for the current objective, sorted | 2000, and on arrival |
| `quest_blacklist_tmp` | per-bot temporary blocks with expiry (quest, code, until) | on block / 30 s sweep |
| `quest_progress_ms` | AI clock of the last objective change or accepted/turned-in event | on change |
| `quest_reward_choice` | chosen reward item for the quest being turned in | on turn-in |
| `quest_dist_to_plan` | straight-line distance to the next waypoint/target (feeds triggers) | 500 |

### 5.6 Multipliers

| Multiplier | Effect |
|---|---|
| `quest_busy` | reduces (does not forbid) wander/idle/grind-style actions' relevance while a plan exists (x0.2) so questing wins ties |
| `quest_no_new_pull_when_hurt` | forbids `quest_engage` below a health/mana threshold (x0), lets rest/eat win |
| `quest_skip_elite` | forbids engage on `Classification` >= elite targets of the current quest unless `Bot.Quest.AllowElite` |
| `quest_leash` | forbids `quest_engage` if the target is further than `Bot.Quest.LeashYards` (60) from the straight route to the objective area |
| `quest_travel_over_loot_of_others` | forbids looting trivially-valued bodies far from the route while traveling (keeps `loot_target` for quest corpses) |
| `quest_dead_backoff` | after a death at target T, x0 for engaging T's spawn area for 10 min (and a `TARGET_DANGEROUS` temp block after the second death) |
| `quest_log_pressure` | at >= soft cap (12 active), x0 on `quest_accept` for non-chain quests |

Relevance summary: survival (Emergency 90) and combat actions always outrank quest actions. Within quest actions: turn-in 20
> objective work 20 > travel 30 (movement band wins over idle/rest only when nothing urgent) > choose/accept 10. Quest actions never use
a band above `Move` 30 except `quest_engage`, which delegates to the pull bands.

### 5.7 Navigation: what exists, what the first milestone needs

Exists in the repo and the data:
- `PathGenerator` (Detour over mmaps): `CalculatePath` returns a path type (`PATHFIND_NORMAL`, `INCOMPLETE` partial path, `SHORT`,
  `NOPATH`, `NOT_USING_PATH` on a map without mmaps, `FARFROMPOLY`), capped at `MAX_PATH_LENGTH` 74 polygons / 74 smoothed points
  (`SMOOTH_PATH_STEP_SIZE` 4 yd). `MotionMaster::MovePoint(id, pos, generatePath=true)`, `MoveFollow`, `MoveChase`, `MovePath`.
- mmaps: map 0 736 tiles, map 1 988, map 2991 33; every one of the 28,004 + 35,310 + 2148 world creature spawns lies in a tile. No mmaps
  for map 530 or 609 (not needed for levels 1-10).
- The bot log already has `stuck` / `path_fail` as event types; the position sampler and the console's movement trails exist.

The first milestone (**local pathing only**) needs only:
1. `nav_move_to(position)`: `CalculatePath`; on `NOPATH` -> `path_fail` row; on `INCOMPLETE`/`SHORT` follow the partial path and re-issue
   from the end point (**hop chaining**: one call cannot cross a 1000-2000 yd valley-to-hub trip; a hop is at most about 296 yd
   (74 points x 4 yd), usually fewer polygons); track distance-to-goal per hop, and if it has not shrunk by 5 % over 3 hops emit
   `path_fail` reason `NO_PROGRESS`.
2. Arrival test: within interaction distance (4.5 yd) of the giver/ender, within attack range of a target, on the area trigger.
3. Stuck detection (5.11) and a fallback target.
4. No nav graph, no taxis, no boats. Distances to cover are the table in 2.4 (590 to 2200 yd to the first parent-zone hub; straight-line
   quest legs have a median of 390-920 yd per quest, p90 1000-2070 yd, for the zone sets; these are lower bounds). That is a walk
   of 1-3 minutes per leg at 7 yd/s, which is acceptable.
5. Cross-zone moves (for example Elwynn -> Westfall, Teldrassil -> Darkshore needs a boat) are out of scope for the first milestone:
   the bot stays in its zone set and the capital. The Phase 8 node graph (generated from mmaps/maps) is needed for: leaving the start
   zone to the next ring, boats/zeppelins/transports, flight paths, instance portals, and for corridor routing that avoids water and
   cliff traps found in the first runs.

### 5.8 Interacting like a client

The `bot quest ...` test aids bypass interaction on purpose. The real flow must respect: the NPC is alive, has `UNIT_NPC_FLAG_QUESTGIVER`
(`CanInteractWithQuestGiver` -> `GetNPCIfCanInteractWith`, 5 yd), is not hostile, `object->hasQuest(id)` (or `hasInvolvedQuest` for
turn-in), `CanTakeQuest(quest, true)`, `CanAddQuest(quest, true)`. The most faithful and cheapest way is to expose
helpers that **synthesize the client packets and call the existing handlers** (`HandleQuestgiverHelloOpcode`,
`HandleQuestgiverAcceptQuestOpcode`, `HandleQuestgiverChooseRewardOpcode`, `HandleGossipHelloOpcode` for talk-to objectives,
`HandleLootOpcode` / `HandleAutostoreLootItemOpcode` for loot, `GameObject::Use` for GOs), so every core side effect (`TalkedToCreature`,
`SaveToDB`, scripts, reputation, chain offers) is the real one. Open check: `SendPacket` on a socket-less bot session must be
a cheap no-op, and no handler may run a synchronous DB query on the map thread (`RewardQuest` ends with `SaveToDB(false)`: verify it
stays asynchronous; 180 bots can turn in many quests per minute).

### 5.9 Reward choice

At turn-in with a non-empty `RewardChoiceItemID*` (pool: 161 of 847 quests have a choice, 2-6 options):
1. Drop options the bot cannot use (`Player::CanUseItem`, class/armor/weapon proficiency, required level, `AllowableClass`).
2. For each usable option compute the upgrade value = stat-weighted score of the item minus the score of the item in the same slot
   (empty slot = full score), using the per-class stat weights (Phase 7 table). Until Phase 7 exists, use
   a simple proxy: armor class match (cloth/leather/mail/plate for the class) first, then item level / required level, then main stat.
3. If no option is an upgrade or weights are unknown: **vendor price fallback**, the highest `ItemTemplate::GetSellPrice()`.
4. Log `choice_item` (the existing QUEST_REWARDED field) plus a `decision` row `QUEST_REWARD_CHOICE` with the scored alternatives.
5. If the chosen item cannot be stored (bag full): block with `BAG_FULL` rather than losing the reward; try the next best that fits.

### 5.10 Eligibility filter and scoring

Filter (cheap checks first; the expensive ones use cached static data):
1. In the classic set and not in `QuestIndex` static blockers or `QuestRules` blacklist (section 4.5) and not in the bot's temporary blacklist.
2. `Player::CanTakeQuest(quest, false)` and `CanSeeStartQuest` (race/class/skill/rep/prereq/exclusive/breadcrumb/level, core code).
3. Level fit: `QuestLevel <= level + 4` (chain continuation may exceed it by 2), not trivial (`level > QuestLevel + 6` gives 10 % xp; the
   default skips quests whose xp factor is below 40 %).
4. Not group/dungeon/PvP/raid (`QuestInfoID` 1/81/41/62, `SuggestedGroupNum > 1`), not timed, not elite-target (configurable).
5. Reachable: starter has a world spawn on a map with an mmap tile; for each unfinished objective at least one reachable target spawn;
   objective types supported in this build (kill, item from loot/GO, GO, talk, area trigger).
6. Quest log has room (or an abandon candidate, 5.12).

Score (higher is better), computed per candidate from cached geometry; `Quest::XPValue(bot)` for xp:
```
t_travel = 1.4 * (d(bot, giver) + d(giver, objectives) + d(objectives, ender)) / 7.0          # seconds, straight line x detour
t_work   = sum(kill: amount * 20 s; item: amount / chance * 20 s capped at 600 s; talk/explore/GO: 10 s)
score    = xp / (t_travel + t_work)                                  # xp per second
         * 2.0  if the quest is already in the log                    # finish what was started
         * 1.5  if it continues a chain whose previous quest this bot turned in at the same giver
         * 1.3  if its objective area overlaps the area of another quest in the log (bundling)
         * level_fit  (1.0 for QuestLevel in [level-1, level+2]; 0.7 at +3/+4; xp factor handles lower)
         * 0.5  class quests until level 6; * 0.0 blacklisted
```
Hub batching: at a giver, accept every eligible quest of that giver up to the soft cap (`Bot.Quest.MaxActive` 12; core hard limit 35),
because the cost of coming back is higher than the cost of carrying them. Keep an `explorer` tie-breaker: prefer quests whose giver
is within 150 yd of the bot, to avoid ping-ponging between hubs.

Re-evaluation triggers: level up, new zone/map, a quest turned in (chain unlock: ask the same giver with `GetQuestDialogStatus`),
quest blocked/abandoned/failed, 60 s timer, a blacklist reload. Never re-evaluate mid-objective unless the plan became invalid.

### 5.11 Stuck handling

Definitions: *stuck* = a movement was requested and the position has changed by less than 2 yd in 10 s, or no progress (position,
objective, accepted quest) in `Bot.Quest.StallSec` (60 s) outside combat/rest/dead. The ladder (one rung per 10 s, state in the AI):
1. re-issue `MovePoint` to the same goal; 2. path again from the current position with `forceDest = false` and a nearer intermediate
point; 3. step aside (a short random movement) and retry; 4. pick the next target spawn of the same objective; 5. `quest_block` with
`UNREACHABLE` (details: last position, goal, path type, hops) and move to the next quest; 6. after 3 blocked quests in 10 minutes,
switch to a grind-near-here fallback (Phase 8 grind) and emit one `stuck` row `NO_PROGRESS_FALLBACK`. Log one `stuck` row when the
ladder starts and one `decision` row when it ends; never per tick.

### 5.12 Quest log capacity and abandon rules

Core capacity is 35 (`MAX_QUEST_LOG_SIZE`); the bot uses a soft cap `Bot.Quest.MaxActive` (default 12) so objectives stay close
together. At the soft cap non-chain quests are not accepted. A quest is abandoned (`AbandonQuest` through the normal API, which logs
`QUEST_ABANDONED`) only when: (a) it is blocked with a permanent code and has made no progress; (b) every remaining objective is trivial
(quest level more than 6 below the bot, 10 % xp); (c) the log is full and a better candidate waits (`quest_abandon_worst` picks the
lowest-score non-chain quest with no progress); (d) it failed (`COMPLETION_NO_DEATH` quests fail on death in the core). A quest
with progress or in a chain is never abandoned for space.

### 5.13 Death and recovery

The engine already moves to the Dead engine on death (`DIED`, `REVIVED`, recent decisions in the row). The quest layer does not run in
the Dead engine except `quest_dead` (keeps the plan; core fails `COMPLETION_NO_DEATH` quests by itself and logs `QUEST_FAILED cause =
died`). On revival: re-run `quest_choose` (the plan from the quest log may have changed). Death accounting: after one death at target
entry T, a 10-minute back-off from T's spawn area; after two deaths, a temporary `TARGET_DANGEROUS` block on the quest (30 min) and a
`quest_blocked` row; after the death run (corpse retrieval is a Phase 3/5 combat feature) the bot resumes at the plan's last
waypoint, not at the target. Combined with the `Bot.AI` death rows it is visible which quest killed whom.

## 6. Logging

### 6.1 Existing events reused as they are (event_type `quest`)

`QUEST_ACCEPTED`, `QUEST_PROGRESS` (first change, completing change, everything under trace), `QUEST_COMPLETE`, `QUEST_REWARDED`
(choice item, xp, money, `accept_to_reward_s`), `QUEST_ABANDONED`, `QUEST_FAILED` (cause), all from `BotQuestLog` with `quest_id` set.
`details.source` stays `"bot"` for real bot activity and `"test_command"` for `bot quest ...` aids (analysts filter
`details->>'$.source' = 'bot'`). New: `details.strategy` (the strategy that caused it, for example `quest`), added to the log schema; and
`details.run_id` for the sim (Phase 11b). No new event types are needed for the quest events.

Changes needed in the log schema: (1) the `group_quest` flag must use a new detector, `group_quest =
(SuggestedGroupNum > 1) OR QuestInfoID in {1, 62, 81} OR any objective target is an elite`, and add `details.group_reason`; today it
is false for every classic quest. (2) `QUEST_REWARDED` should carry the choice options and scores when the bot chose
(`details.choices[]`); (3) a startup audit writes one `quest_blocked` row per static blocker with `source = "audit"` (a pseudo bot
guid 0 or a reserved system guid; the console panel groups by reason/zone/quest, so a system bot is acceptable).

### 6.2 `quest_blocked` reason codes

Event type `quest_blocked`, severity WARN (2) for a permanent block, INFO (1) for a temporary one; `Reason` = the code below (upper
snake, like the existing codes; the lower-case working names map one to one: `no_starter_spawn` = `NO_STARTER_SPAWN` ...). Columns:
`quest_id` always; `target_entry` when the blocker names an NPC/creature/GO/item; position/zone/map/level as for every event.
Logged **once per bot, quest and code** and again only when the code changes, or when a temporary block expires and recurs; static
blockers are logged once per boot by the audit. Common `details` fields on every row: `source`, `strategy`, `title`, `quest_level`,
`min_level`, `step` (`accept` / `objective:<id>` / `turn_in`), `permanent` (bool), `blocked_s` (time since the plan began),
`alternatives` (what was chosen instead), `rule_version`.

| Code | When | Extra `details` fields |
|---|---|---|
| `QUEST_PREREQ` | `SatisfyQuestDependentQuests` / previous / breadcrumb / exclusive group failed | `check` (which `Satisfy*`), `prev_quest`, `exclusive_group` |
| `QUEST_LEVEL` | `SatisfyQuestMinLevel/MaxLevel` failed (informational, not blacklisted) | `bot_level`, `min`, `max` |
| `QUEST_RACE` / `QUEST_CLASS` | race/class mask fails (a filter bug if it ever appears) | `race`, `class`, `mask` |
| `NO_STARTER_ROW` | no creature/GO starter and no known item/script start | `probe` (bool) |
| `NO_STARTER_SPAWN` | starter rows exist, none with a world spawn | `starter_entries[]` |
| `NO_ENDER_ROW` / `NO_ENDER_SPAWN` | same for the ender | `ender_entries[]` |
| `NO_TARGET_SPAWN` | an objective's creature/GO has no spawn (credit sources included) | `objective_id`, `type`, `object_id` |
| `TARGET_LEVEL_TOO_HIGH` | best target spawn above the bot by more than the cap | `target_levels`, `bot_level` |
| `ELITE_TOO_STRONG` | target is elite and elites are disabled | `target_entry`, `classification` |
| `MISSING_ITEM_SOURCE` | item objective with no loot/vendor/container source | `item`, `probe` |
| `ITEM_NOT_DROPPING` | runtime: N kills of valid sources without the item | `item`, `kills`, `expected_chance`, `window_s` |
| `NEEDS_GROUP` | `QuestInfoID` 1/62/81/41, `SuggestedGroupNum > 1` or elite objective | `group_reason`, `suggested_players` |
| `NEEDS_ESCORT` | escort script or text heuristic | `heuristic` (script/text) |
| `NEEDS_EVENT` | `Flags & 2` or a start/end script that drives completion | `flags`, `scripts[]` |
| `OBJECTIVE_UNSUPPORTED` | type 14 criteria tree, area trigger exit, currency, spell cast ... | `objective_type` |
| `SKILL_REQUIRED` / `REPUTATION_REQUIRED` / `TIMED_UNSUPPORTED` | from the addon columns / `TimeAllowed` | `skill`, `value` / `faction`, `value` / `time_limit_s` |
| `DATA_ERROR` | chain target missing, objective entry missing from the templates, quest without level data | `kind`, `ids[]` |
| `GIVER_NOT_INTERACTABLE` | at the NPC: hostile, dead, in combat, no questgiver flag, quest status `None` | `entry`, `reason` |
| `BAG_FULL` | `CanAddQuest` source item or reward item does not fit | `item` |
| `QUEST_LOG_FULL` | no free slot and no abandon candidate | `active`, `cap` |
| `UNREACHABLE` | stuck ladder exhausted or `NOPATH` for giver/ender/target | `goal`, `last_path_type`, `hops`, `position` |
| `TARGET_DANGEROUS` | two deaths at the same target in 10 minutes | `target_entry`, `deaths` |
| `BLACKLISTED` | `QuestRules` entry (links to the analyst's rule id) | `rule_id`, `rule_version` |

`path_fail` rows (separate event type) keep `NO_PATH`, `NO_PROGRESS`, `MMAP_MISSING`, `PARTIAL_PATH_LIMIT` with `details` {from, to,
path_type, hops, distance, remaining}; `stuck` rows keep `NO_PROGRESS`, `GEOMETRY_TRAP`, `UNREACHABLE_TARGET`, `NO_PROGRESS_FALLBACK`.
New codes are proposals; the console's demo reason lists (`QUEST_PREREQ`, `TARGET_ELITE`, `OBJECT_NOT_FOUND`) are
placeholders, map `TARGET_ELITE` to `ELITE_TOO_STRONG` and `OBJECT_NOT_FOUND` to `NO_TARGET_SPAWN`.

#### 6.2.1 Implemented codes (BotQuest.cpp, registered)

`quest_blocked` reasons currently emitted by `BotQuest.cpp` (meanings confirmed against the code):

| Code | Meaning in the implementation |
|---|---|
| `NO_QUEST_AVAILABLE` | pick pass found no takeable quest within `FarRadius` at the bot's level (quest_id 0) |
| `NO_PATH` | giver/ender/target path check returned no path (`info.NoPath`); drop for a cool-down |
| `PATH_PARTIAL_FAR` | path is partial and the goal is far from a walkable poly (same code as the `path_fail` row, see engine-design) |
| `UNREACHABLE` | gave up walking to a giver/ender after 15 min, or the pre-check dropped the target |
| `TARGET_UNREACHABLE` | could not reach the target NPC, or no progress on an objective for `StallSec` with no better explanation |
| `ELITE_TOO_STRONG` | stalled objective: every live target seen was too strong and at least one was elite |
| `TARGET_LEVEL_TOO_HIGH` | stalled objective: every live target seen was too strong, none elite |
| `ITEM_NOT_DROPPING` | stalled item objective after at least 6 kills of valid sources |
| `MISSING_ITEM_SOURCE` | item objective with no creature/GO source |
| `NO_TARGET_SPAWN` | objective creature/GO (or item source) has no spawn, or the NPC has no spawn on this map |
| `OBJECTIVE_UNSUPPORTED` | area-trigger completion, item from a game object, or another unsupported objective type |
| `NEEDS_GROUP` | `SuggestedPlayers` above 1 (details: suggested players) |
| `GIVER_NOT_INTERACTABLE` | at the NPC: not at its spawn point, seen but never interactable, or `CanTakeQuest` passes but the NPC will not give it |
| `TIMED_UNSUPPORTED` | timed quest |
| `SKILL_REQUIRED` | required skill (target_entry = skill id) |
| `REPUTATION_REQUIRED` | minimum reputation faction (target_entry = faction id) |
| `NEEDS_EVENT` | quest completes through an event/script |
| `NO_ENDER_ROW` | no creature/GO takes the quest, or the NPC does not take it at turn-in |
| `NO_ENDER_SPAWN` | ender rows exist but none has a spawn (also used for a complete quest nobody can take) |
| `DATA_ERROR` | quest marked complete but cannot be rewarded |
| `QUEST_LEVEL` | bot level outside the quest's level window (informational) |
| `QUEST_PREREQ` | earlier quest or other `CanTakeQuest` prerequisite missing |
| `QUEST_LOG_FULL` | no free slot in the quest log |
| `BAG_FULL` | no room for the source item or the reward (details: `reward_item`) |

Also emitted: `NO_STARTER_SPAWN` (giver NPC has no spawn on this map). `REPEATABLE` is a silent block (never logged).

`decision` reasons (BotQuest.cpp):

| Code | Severity | Meaning |
|---|---|---|
| `QUEST_PICK` | info | picked a quest from a giver found in the near pass |
| `QUEST_PICK_FAR` | info | picked a quest from a giver found in the far pass (pass 1) |
| `QUEST_WORK` | info | chose which open quest to work next (summary says N of M open quests) |
| `QUEST_TURNIN_PLAN` | info | heading to an ender to turn in a completed quest |
| `QUEST_TURNED_IN` | info | quest turned in |
| `QUEST_REWARD_EQUIPPED` | info | the chosen quest reward was equipped |
| `QUEST_PULL` | trace | bot pulls a quest target mob (summary: name and level) |

#### 6.2.2 Economy, hub and survival codes (emitted by BotQuest.cpp / BotBehavior.cpp, registered)

Formerly reserved; all now emitted. `blk` = `quest_blocked` row (WARN unless noted), `dec` = `decision` row (goes to `bot_event_hot` when HotSplit is on).

| Code | Type | Meaning |
|---|---|---|
| `QUEST_NO_LOCAL` | dec | no takeable quest within FarRadius at the bot's level |
| `QUEST_HUB_TRAVEL` | dec | walking to the next quest hub (hub idx, takeable count, distance, zone) |
| `QUEST_HUB_NONE` | blk | no hub on the map fits the level, grinding instead |
| `QUEST_GRIND` | dec | no quest to do, grinding mobs of the bot's level |
| `NO_GRIND_TARGET` | blk | no hostile spawn within 500 yd to grind |
| `TRAIN_TRIP` | dec | walking to the class trainer |
| `TRAINED` | dec | learned spells (trainer entry, copper spent) |
| `TRAIN_NO_MONEY` | blk | spells available, none affordable (or nothing learned at the trainer) |
| `TRAIN_NO_TRAINER` | blk | no reachable class trainer within 4000 yd, or the NPC has no trainer data |
| `TRAIN_UNREACHABLE` | blk | trainer known but the walk failed (quarantine path, Svc=1) |
| `VENDOR_TRIP` | dec | walking to a vendor (free slots, repair cost, bag upgrade) |
| `SOLD_ITEMS` | dec | sold grey/unusable stacks |
| `REPAIRED` | dec | repaired equipment |
| `BAG_BOUGHT` | dec | bought (and maybe equipped) a bag |
| `BAG_NO_MONEY` | blk | cannot afford a bag (spell reserve counted) |
| `BAG_BUY_FAILED` | blk | bag purchase refused or no inventory space |
| `VENDOR_NONE` | blk | no reachable vendor within range |
| `VENDOR_UNREACHABLE` | blk | vendor known but the walk failed (quarantine path, Svc!=1) |
| `REWARD_ITEM_MISSING` | blk | reward item not in item data, quest turned in without it |
| `QUEST_QUARANTINED` | blk | quest failed N times for this bot (UNREACHABLE/TARGET_UNREACHABLE etc.), skipped 6 h |
| `QUEST_QUARANTINED_GLOBAL` | blk | quest failed across bots (DEAD_FAILS drops), skipped by all bots 1 h |
| `CORPSE_RUN_GAVE_UP` | dec WARN | corpse run failed repeatedly, falls back to the spirit healer |
| `SPIRIT_HEAL_GAVE_UP` | dec WARN | no reachable spirit healer, resurrects in place |
| `QUEST_WALK_START` | dec | quest travel leg started (quest_id, goal, path_length, path_us); at most one per bot per 10 s |
| `QUEST_WALK_ARRIVE` | dec | quest travel leg arrived (only after a logged start) |
| `QUEST_WALK_ABORT` | dec | quest travel leg failed in BotMotion (`abort_reason` = the path_fail/stuck code, quest_id); a stop requested by BotQuest is not logged |
| `IDLE_WAIT_SPAWN` | dec | idle at an empty grind spawn point; details `idle_reason` WAIT_SPAWN; at most one per bot per 60 s |
| `LOG_SUPPRESSED` | dec | counter row (total, per type/reason/quest/target counts) written when the per-login repeat cap dropped rows, at the next different row and at logout |

R5 fields (2026-10-07): `quest_id` column and `target_entry` are set on quest-travel NO_PROGRESS, UNREACHABLE_TARGET, path_fail and stuck rows (also `quest_id` in details). `details.path_us` (microseconds of the path query, steady_clock) is on GOTO_START, motion path_fail/stuck rows, QUEST_WALK_START and BotQuest NO_PATH/PATH_PARTIAL_FAR drops (plus `hop_us` for FindHop); `details.select_us` on QUEST_HUB_TRAVEL (hub selection time; the hub path query itself is logged on the following walk rows). `details.idle_reason` on idle rows: TEST_STRATEGY (TEST_IDLE*), NO_GRIND_TARGET, WAIT_SPAWN.

#### 6.2.3 Social event types (BotSocial.cpp, BotAlts.cpp)

These types are not in `Bot.Log.HotTypes`, so they stay in `bot_event` (14 days). `success`/refusal is carried in `outcome`/severity; refusal reason is the code.

| event_type | Codes |
|---|---|
| `invite` | `INVITE_ACCEPTED`, `INVITE_REFUSED_DISABLED`, `INVITE_REFUSED_NOT_OWNER`, `INVITE_REFUSED_ALREADY_GROUPED` |
| `trade` | `TRADE_ACCEPTED`, `TRADE_REFUSED_DISABLED`, `_WORLD_BOT`, `_NOT_OWNER`, `_BUSY` (in combat), `_EMPTY`, `_CORE_FAILED`, `_TIMEOUT` |
| `quest_share` | `QUEST_SHARE_SENT`, `QUEST_SHARE_ACCEPTED`, `QUEST_SHARE_REFUSED_` + `BUSY, DEAD, DONE, ON_QUEST, LOG_FULL, BAG_FULL, LEVEL_LOW, LEVEL_HIGH, CLASS, RACE, REP_LOW, REP_HIGH, PREREQ, EXPANSION, NOT_ELIGIBLE, REPEATABLE` |
| `alt_command` | `ALT_ADDED`, `ALT_REMOVED`, `ALT_RESTORED` (startup), `ALT_REFUSED_` + `NOT_OWNER, ONLINE, LOADING, ALREADY, CAP, BOT, NOT_OWNER_DESPAWN` |

Chat `share` verb failure reasons (returned to the chat handler, not logged as rows): `NOT_GROUPED`, `NOT_ON_QUEST`, `NOT_SHAREABLE`.

Consumers: query `bot_event_all` (view over `bot_event` + `bot_event_hot`), never `bot_event` alone, for decision/state_change/trace rows; the social types above are only in `bot_event` but the view is still correct for them.

### 6.3 Choices and alternatives

`decision` rows (the engine already writes `engine, trigger, action, relevance, band, effective, alternatives[]`) with the action's own
code and a `detail` object:
- `QUEST_CHOOSE`: `{candidates:[{quest_id, score, xp, t_travel_s, t_work_s, chain, level_fit}] (top 5), rejected:[{quest_id, code}] (top
  5 plus counts per code), chosen, reason (chain/xp/proximity/in_log)}`; logged only when the chosen quest changes.
- `QUEST_TARGET`: `{objective_id, target_entry, spawn_guid, distance, alternatives:[{spawn_guid, distance}]}` on change of target.
- `QUEST_REWARD_CHOICE`: the scored options (5.9).
- `QUEST_RECOVER`: the ladder rung and what was tried.
Everything logs on state change, not per tick; the per-bot trace switch adds `TRACE_EVAL` rows as today.

### 6.4 Console

The why-stuck panel (`api_stuck` in the sim console app) counts `event_type IN ('stuck','path_fail','quest_blocked')` in a time
window and groups by `reason`, `zone_id` and `quest_id`, with distinct bot counts. It does not read `details`. Therefore: (1) each code
above is one `reason` value, so the by-reason table is the first answer; (2) `zone_id` must be set on every row (the engine fills
`GetZoneId()`), (3) `quest_id` must be set (the by-quest table shows the most-blocked quests), and (4) the detail JSON is for the live
feed and for log analysis queries. `source` is not filtered by the panel today; since audit rows and test-command rows would
inflate the counts, the console should add `details.source = 'bot'` to the panel's default filter (or the audit uses a
distinct event type `quest_audit`).

## 7. Metrics and success criteria for the first milestone

Reference measurements: about 1.3 ms CPU per bot per second idle without AI at 180 bots (measured earlier); `Bot.AI.TickMs` 500;
sim worldserver `MapUpdate.Threads = 4`; the sim runs in real time (no time acceleration).

XP reference: cumulative XP from `gt/xp.txt` to reach level 5 is 4800 and level 10 is 27,600 (400, 900, 1400, 2100, 2800, 3600, 4500,
5400, 6500 per level for levels 1-9). Quest XP per quest is not queryable from SQL (QuestXP.db2 is binary and `quest_xp` in the
hotfix DB is empty): the scorer calls `Quest::XPValue(bot)` and the first deliverable (step 1 in section 9) tabulates it per zone set so
the criteria below can be set from facts. **Numbers marked (provisional) are estimates until that table and a first measured run exist.**

| Metric | Target for the milestone |
|---|---|
| Completion | per start zone and class/faction group, 10 bots from the sim: at least 90 % reach level 5 with the valley chain turned in within the scenario duration (30 min wall for L1-5), and at least 80 % reach level 10 within 120 min wall (provisional) |
| Blocked | at most 10 % of the quest attempts end in `quest_blocked` for a non-static reason (static blacklist hits excluded and counted separately); every block has a code from 6.2 (0 rows with an empty or `UNKNOWN` reason) |
| Idle | no bot without an action, movement, fight or rest for more than 60 s (provisional); `stuck` rows per bot-hour <= 2 |
| Deaths | at most 1 death per bot per level on average; no bot dies more than 3 times at the same target |
| Quest throughput | median accept-to-reward under 8 minutes for kill/collect quests (provisional; from `accept_to_reward_s`) |
| Quest log | no bot above the soft cap of 12 active quests; no `QUEST_LOG_FULL` before level 10 |
| Tick cost | AI tick (including quest triggers, excluding path calculation and combat) at most 0.2 ms CPU per bot per second at 180 bots (= 36 ms/s = about 4 % of one core) and 99th percentile of one tick under 1 ms; path calculation at most 2 calls per bot per 10 s on average; total AI cost reported next to the 1.3 ms baseline from `BotAIStats` |
| Data | the startup audit reproduces the SQL counts of appendix A within 0 difference; the seed blacklist rows are the only static `quest_blocked` rows |

Mapping to the sim console scenarios (table `scenario` in `forever_sim_log`, ids 11-17 as seen today):
- `Quests L1-5: Northshire Valley (Alliance)` (id 11, 10 bots, classes 1,5,8,4, 1800 s) and `Valley of Trials (Horde)` (id 12, classes
  1,7,3,9): success = the completion and blocked rows above for L5. **Gap:** the scenario params have no race list, so the Alliance
  bots do not all start in Northshire (factory picks races); both scenarios need a `races` parameter (Human only for 11; Orc only
  for 12 until Troll/Gnome starts are fixed).
- `Quests L1-10: Zephras Isle (Skyborne start)` (id 13, classes 1,3,4,11, 3600 s, no faction): matches this section's L10 criteria only if
  the data is fixed; today it will measure the 92528 stall. It omits Mage (Alliance Skyborne only) and Shaman (Horde only); add them as
  faction-specific variants.
- `Zone: Elwynn Forest L5-10` (id 14): the continuation test (hub batching, parent zone). `Load test: idle population` (id 17): the tick
  cost baseline, rerun with the quest strategy on.
New scenarios to add (simulation):
1. one `L1-10` scenario per start zone (Coldridge, Shadowglen, Deathknell, Camp Narache, Valley of Trials, Elwynn, Zephras Alliance and
   Horde), 10 bots with the zone's allowed classes;
2. `Class matrix L1-5`: 10 bots per allowed race/class combination, short, to find class-specific failures (new combos Gnome Priest,
   Undead Paladin, Human Hunter, Orc Mage, Dwarf Shaman, Troll Warlock);
3. `Blacklist audit`: one bot per seed-blacklist quest, `bot quest add` (source `test_command`) to prove or clear each entry, results go
   to log analysis;
4. `Recovery`: forced deaths mid-quest (`bot kill`), checks the plan resumes and the back-off works;
5. `Log pressure`: give a bot 35 quests, checks `QUEST_LOG_FULL`/abandon rules;
6. `Stuck injection`: bots placed behind a known geometry trap, checks the ladder and `UNREACHABLE`;
7. `Load L1-10 x 180`: tick cost with the quest strategy on, 10 bots per class/faction.

## 8. Group quests (later)

Idea: a bot holding a group quest asks for help in chat; other bots that have the same quest, or are free, join
and do it together.

What exists: `BotQuestLog` marks `group_quest` and `suggested_players` on `QUEST_ACCEPTED/COMPLETE/...` events, **but the flag is
false for all 4605 classic quests** (`SuggestedGroupNum` is 0 everywhere). The real markers are `QuestInfoID`: 1 = group (262 classic
quests, 14 in the pool: 176 Hogger, 314 Protecting the Herd, 2499 Oakenscowl, 93318 Vulgara and Silverpine/Wailing Caverns ones), 81 =
dungeon (395, 11 in the pool), 62 = raid (308), 41 = PvP (186). In start zones the group quests are few (176, 314, 2499, 93318), so the
first milestone loses little by blacklisting them (`NEEDS_GROUP`).

What is needed later:
1. the new detector in 6.1 so the log and the matcher agree what a group quest is;
2. detection: the bot reaches a group objective (elite target, `QuestInfoID` 1) and logs `NEEDS_GROUP` with the quest and zone;
3. the chat vocabulary from Phase 4 (party/say/zone channel): "anyone for <quest>?" and the answers; a bot-to-bot message layer is
   preferable to parsing text (a small registry of "open requests" in a mutex-protected shared structure, the same discipline as the
   logger), with the chat line as the visible side;
4. matching: other bots with the same quest in the log and within a distance, or bots without a plan nearby with the right level;
   `CanShareQuest` and the existing `PushQuestToParty` path for bots that do not have it;
5. forming a group (the core `Group` API, `IsInSameRaidWith` is checked by the accept handler), a shared objective until all members
   completed or gave up, and loot rules for quest items;
6. scenarios: several bots holding the same quest (the sim can test this).
Dungeon quests (`QuestInfoID` 81) come with Phase 11 and need party/instance logic, not chat alone.

## 9. Work breakdown, risks and open questions

### 9.1 Steps (ordered; each ends with a verify on the sim)

| # | Step | Area | Verify (sim) |
|---|---|---|---|
| 0 | Data fixes proposed as dated `sql/custom/*` files to apply: Troll/Gnome start rows; relation rows for quests with item/script starts once probed; 94488-94491; creature templates for the Zephras objectives (252078 ...) and the broken Zephras chain (92528/93926/93927); only after it is decided what the beta really does (open questions 1, 2) | Database (SQL), navigation | the audit counts in appendix A drop by the fixed ids; `playercreateinfo` row for race 7/8 points at the expected valley |
| 1 | `QuestIndex`/`SpawnIndex` startup caches and the static-blocker audit as a GM command (`bot quest audit`) with a table of `Quest::XPValue` per zone set and level 1-10 | navigation (code), bot infrastructure (hook, log source `audit`) | the command output equals appendix A (94/10/73/11/21/61, 847 pool); load time and memory logged; no DB access from map threads |
| 2 | Quest values, triggers, actions skeleton, `quest` strategy registered, not default; trace rows | combat AI (engine hooks), navigation | `bot strategy <name> +quest` on one bot; `decision` rows show `quest_choose` with candidates and rejects; zero cost when off |
| 3 | Navigation: `nav_move_to` with hop chaining, no-progress detection, stuck ladder, `path_fail` / `stuck` rows | navigation | a bot walks Northshire start -> Goldshire (~590 yd) and Valley of Trials -> Razor Hill (~1000 yd) on the sim; a deliberately blocked goal produces `path_fail NO_PATH` with details |
| 4 | Interaction helpers (synthesized client packets): hello, accept, reward, loot, GO use | bot infrastructure | a bot accepts quest 783 `A Threat Within` and turns in 7 `Kobold Camp Cleanup` through the real handlers; `QUEST_ACCEPTED/REWARDED source=bot` rows |
| 5 | Kill/loot/GO/talk/area-trigger objectives with target choice, leash and combat hand-off | navigation + combat AI (pull actions) | the Northshire chain 783 -> 7 -> 15 -> 21 -> 54 (verified in the data: each has the previous as `PrevQuestID`/`RewardNextQuest`) completed by a Warrior bot, `QUEST_PROGRESS` and `QUEST_COMPLETE` rows, level 5 reached |
| 6 | Scoring, hub batching, chain continuation, re-evaluation triggers, reward choice, soft cap and abandon rules | navigation | scenario 11/12 variant with races fixed: completion/blocked metrics from section 7 for L1-5 |
| 7 | Death/recovery integration and the back-off multipliers | combat AI + navigation | `Recovery` scenario |
| 8 | Seed blacklist as `bot_quest_rule` rows + reload; `BLACKLISTED` rows; first analyst loop | bot infrastructure/database (table), log analysis (rules from the log), reviewed before adoption | a rule added at runtime changes a bot's behavior without a rebuild and can be rolled back |
| 9 | L1-10 per start zone incl. parent-zone hubs; Zephras specifics (criteria objectives, area triggers) | navigation | per-zone L1-10 scenarios meet the section 7 criteria, `quest_blocked` summary per zone from the console panel |
| 10 | 180-bot load run, tick-cost report, log volume check | simulation, bot infrastructure | `Load L1-10 x 180` within the tick-cost budget |
| 11 | Group quest layer (section 8) | navigation, bot infrastructure, combat AI | later |

### 9.2 Risks

- **Data:** the relation-less Forever quests and the Zephras chain are the biggest blockers and are data work, not code; item-started
  quests cannot be identified from SQL (a runtime probe in step 1 or 4 is required).
- **Navigation:** hop chaining over 1000-2000 yd valley-to-hub legs, water/cliff traps and partial paths are unproven on this data;
  mmaps for Zephras (33 tiles) have not been exercised by any bot.
- **Dependencies on other phases:** looting (Phase 3), pull/combat quality and survivability at level 1-10 for six-to-nine classes
  (Phases 5-6, combat), resting/eating, release/revive. Questing is only as good as the weakest of these.
- **Beta vs vanilla:** respawn rates were raised in several zones, escort bugs fixed, XP changes unknown (beta notes); the converted data
  follows VMaNGOS 1.12 plus sniffed Forever additions. Kill-credit and loot behavior may differ from VMaNGOS (multi-drop quest items
  were fixed in the beta).
- **Competition:** 180 bots in the same start zone share spawns; respawn timers (`spawntimesecs`) will starve quest mobs. Needs a
  per-spawn reservation or fast target switching (and it is a population-manager question, Phase 9).
- **Load:** `RewardQuest` does `SaveToDB(false)` per turn-in; with 180 bots turning in quests in bursts this may need batching.
- **Log volume:** hub batching and `QUEST_PROGRESS` can spam; mitigated by logging first/last change only and state-change rules.

### 9.3 Open questions

1. **Gnome and Troll start positions.** Is (-4983,878) / Echo Isles really what the beta uses, or should they match Dwarf (Coldridge) and
   Orc (Valley of Trials) like the 1.12 start? Until decided, the factory must not create Gnome/Troll bots at these coordinates.
2. **How closely to follow vanilla leveling vs the beta's changes?** The pool is VMaNGOS 1.12 plus sniffed Forever additions; the 79
   relation-less Forever quests suggest the beta moved content (new Northshire chain, Skyborne class quests, camping series). Should
   bots wait for a data fix, or follow the vanilla pool only and ignore anything new?
3. **May bots skip unobtainable quests** (blacklist and move on, with a logged reason)? This design assumes yes.
4. **Dungeon and group quests:** keep them out until Phase 11 (this design) or allow elite hunts such as Hogger with a bot party?
5. **XP rates and time acceleration:** the sim runs real time; a level 1-10 run is hours. Is a rate multiplier allowed in the sim config
   (only for sim scenarios) to shorten runs, and what is the real-server rate?
6. **Class quests** (Hunter taming, Warlock imp ...): in scope for level 10 or later? They are the only way to pets/summons.
7. **Where do bots go at level 10?** The pool is per start zone; Phase 9 needs a rule for the next ring (Westfall 24 quests, Loch Modan
   22, Darkshore 28, Barrens 53, Silverpine 36 at `MinLevel <= 10`) including boats (Darkshore) and taxis.
8. **Quest log limit:** should the bot soft cap follow the real client limit (unverified, 20 in classic vanilla, 35 in the core), and is
   12 acceptable?
9. **Real players and bots sharing a zone:** should bots avoid or compete for quest mobs/NPC spawns with a real player nearby?
10. **Skyborne chain and class quests:** is the 92528/93926/93927 gap a known beta bug, or missing sniff data? Same for the 79
    relation-less Forever quests (can the sniff be extended to capture questgiver/ender relations?).
11. **New combinations without class quests:** Gnome Priest and Undead Paladin have none; fine to leave them without?
12. **Level cap 30 in the beta:** should the pool and scenarios extend to 30 after this milestone, and is the Excavation Site: Wetlands
    dungeon (new content) a target?

## 10. What surprised me

- Troll and Gnome start in places with no quests (retail start coordinates in a classic-converted world).
- 79 of 176 Forever quests are relation-less; the Zephras main chain stops at level 5.
- The log's `group_quest` flag cannot fire: the DB has no `SuggestedGroupNum` data at all; group quests are `QuestInfoID`.
- The beta's DB2 gap (missing items) does not touch levels 1-10; it is a level 20+ problem. The real blockers are missing relations and
  creature templates.
- Creature `areaId`/`zoneId` are 0 for every spawn: zone and area must come from positions or map data.
- The quest log is 35 in this core, far above classic vanilla's 20.
- `RewardNextQuest` does not bypass the starter rows: chains behind relation-less quests are dead even though the link exists.
- The first parent-zone hub is 590-2200 yd from the start in every race; "start zones are small" holds only for the valleys themselves.

---

## Appendix A. Queries (re-runnable, SELECT only)

Run with `mysql --defaults-file=<client.cnf> forever_sim_world < file.sql` (the client reads
the credentials from the file; they are not reproduced here). Appending a query to the pool CTE `pool` (A1) makes it a runnable file.
Expected results are the numbers quoted in this document (frozen copy of 2026-10-06).

### A0. Universe

```sql
SELECT COUNT(*) AS quest_template_all FROM quest_template;                          -- 48651
SELECT COUNT(*) AS classic_level_rows FROM quest_classic_level;                     -- 4605
SELECT COUNT(*) AS classic_min_le10 FROM quest_classic_level WHERE MinLevel <= 10;  -- 1208
SELECT QuestInfoID, COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID GROUP BY 1; -- 0:3452 1:262 21:1 41:186 62:308 81:395 82:1
SELECT COUNT(*) FROM creature;      -- 78674 spawn rows (world difficulty '0' and dungeon rows)
SELECT COUNT(*) FROM gameobject;    -- 46513
```

### A1. The pool CTE (847 quests; add `SELECT COUNT(*) FROM pool;` to check)

```sql
WITH pool AS (
  SELECT q.*, c.MinLevel, c.QuestLevel AS QL
  FROM quest_template q JOIN quest_classic_level c ON c.ID = q.ID
  WHERE c.MinLevel <= 10
    AND q.LogTitle NOT LIKE '<%' AND UPPER(q.LogTitle) NOT LIKE '%UNUSED%' AND q.LogTitle NOT LIKE '[%' AND q.LogTitle NOT LIKE 'Blank%'
    AND q.LogTitle NOT IN ('Test Kill Quest','Hunter test quest','Hunter test quest2','REUSE')
    AND q.LogTitle NOT LIKE '%iCoke%' AND q.LogTitle NOT LIKE '%(123)%' AND q.LogTitle NOT LIKE '%Darkmoon%'
    AND q.LogTitle NOT LIKE '%Waskily%' AND q.LogTitle NOT LIKE '%Wabbit%'
    AND q.QuestSortID NOT IN (-24,-101,-121,-181,-182,-201,-264,-304,-324)
    AND q.QuestSortID NOT IN (-22,-284,-364,-365,-366,-367,-368,-369,-370,-371,-372,-373,-374,-375,-376,-377,-1001,-1002,-1003,-1004)
)
```

### A2. Starter / ender defects (pool; expected 847, 94, 10, 73, 11)

```sql
WITH pool AS (
  SELECT q.*, c.MinLevel, c.QuestLevel AS QL
  FROM quest_template q JOIN quest_classic_level c ON c.ID = q.ID
  WHERE c.MinLevel <= 10
    AND q.LogTitle NOT LIKE '<%' AND UPPER(q.LogTitle) NOT LIKE '%UNUSED%' AND q.LogTitle NOT LIKE '[%' AND q.LogTitle NOT LIKE 'Blank%'
    AND q.LogTitle NOT IN ('Test Kill Quest','Hunter test quest','Hunter test quest2','REUSE')
    AND q.LogTitle NOT LIKE '%iCoke%' AND q.LogTitle NOT LIKE '%(123)%' AND q.LogTitle NOT LIKE '%Darkmoon%'
    AND q.LogTitle NOT LIKE '%Waskily%' AND q.LogTitle NOT LIKE '%Wabbit%'
    AND q.QuestSortID NOT IN (-24,-101,-121,-181,-182,-201,-264,-304,-324)
    AND q.QuestSortID NOT IN (-22,-284,-364,-365,-366,-367,-368,-369,-370,-371,-372,-373,-374,-375,-376,-377,-1001,-1002,-1003,-1004)
),
cs AS (SELECT s.quest, s.id, c.guid FROM creature_queststarter s LEFT JOIN creature c ON c.id = s.id AND c.spawnDifficulties IN ('0','')),
gs AS (SELECT s.quest, s.id, g.guid FROM gameobject_queststarter s LEFT JOIN gameobject g ON g.id = s.id AND g.spawnDifficulties IN ('0','')),
ce AS (SELECT s.quest, s.id, c.guid FROM creature_questender s LEFT JOIN creature c ON c.id = s.id AND c.spawnDifficulties IN ('0','')),
ge AS (SELECT s.quest, s.id, g.guid FROM gameobject_questender s LEFT JOIN gameobject g ON g.id = s.id AND g.spawnDifficulties IN ('0',''))
SELECT
  (SELECT COUNT(*) FROM pool) AS pool,
  (SELECT COUNT(*) FROM pool p WHERE NOT EXISTS (SELECT 1 FROM cs WHERE cs.quest=p.ID) AND NOT EXISTS (SELECT 1 FROM gs WHERE gs.quest=p.ID)) AS no_starter_row,
  (SELECT COUNT(*) FROM pool p WHERE (EXISTS (SELECT 1 FROM cs WHERE cs.quest=p.ID) OR EXISTS (SELECT 1 FROM gs WHERE gs.quest=p.ID))
      AND NOT EXISTS (SELECT 1 FROM cs WHERE cs.quest=p.ID AND cs.guid IS NOT NULL) AND NOT EXISTS (SELECT 1 FROM gs WHERE gs.quest=p.ID AND gs.guid IS NOT NULL)) AS starter_without_spawn,
  (SELECT COUNT(*) FROM pool p WHERE NOT EXISTS (SELECT 1 FROM ce WHERE ce.quest=p.ID) AND NOT EXISTS (SELECT 1 FROM ge WHERE ge.quest=p.ID) AND (p.Flags & 0x10000)=0) AS no_ender_row,
  (SELECT COUNT(*) FROM pool p WHERE (EXISTS (SELECT 1 FROM ce WHERE ce.quest=p.ID) OR EXISTS (SELECT 1 FROM ge WHERE ge.quest=p.ID))
      AND NOT EXISTS (SELECT 1 FROM ce WHERE ce.quest=p.ID AND ce.guid IS NOT NULL) AND NOT EXISTS (SELECT 1 FROM ge WHERE ge.quest=p.ID AND ge.guid IS NOT NULL)) AS ender_without_spawn;
```

### A3. Objective targets without a spawn (expected 21 quests)

```sql
WITH pool AS (
  SELECT q.*, c.MinLevel, c.QuestLevel AS QL
  FROM quest_template q JOIN quest_classic_level c ON c.ID = q.ID
  WHERE c.MinLevel <= 10
    AND q.LogTitle NOT LIKE '<%' AND UPPER(q.LogTitle) NOT LIKE '%UNUSED%' AND q.LogTitle NOT LIKE '[%' AND q.LogTitle NOT LIKE 'Blank%'
    AND q.LogTitle NOT IN ('Test Kill Quest','Hunter test quest','Hunter test quest2','REUSE')
    AND q.LogTitle NOT LIKE '%iCoke%' AND q.LogTitle NOT LIKE '%(123)%' AND q.LogTitle NOT LIKE '%Darkmoon%'
    AND q.LogTitle NOT LIKE '%Waskily%' AND q.LogTitle NOT LIKE '%Wabbit%'
    AND q.QuestSortID NOT IN (-24,-101,-121,-181,-182,-201,-264,-304,-324)
    AND q.QuestSortID NOT IN (-22,-284,-364,-365,-366,-367,-368,-369,-370,-371,-372,-373,-374,-375,-376,-377,-1001,-1002,-1003,-1004)
)
, spawned AS (SELECT DISTINCT id FROM creature WHERE spawnDifficulties IN ('0','')),
  credit AS (SELECT t.KillCredit1 AS target FROM creature_template t JOIN spawned s ON s.id = t.entry WHERE t.KillCredit1 > 0
             UNION SELECT t.KillCredit2 FROM creature_template t JOIN spawned s ON s.id = t.entry WHERE t.KillCredit2 > 0),
  gspawned AS (SELECT DISTINCT id FROM gameobject WHERE spawnDifficulties IN ('0',''))
SELECT 'objective_target_without_spawn' AS check_name, COUNT(DISTINCT o.QuestID) AS quests FROM quest_objectives o JOIN pool p ON p.ID = o.QuestID
WHERE (o.Type IN (0,3) AND o.ObjectID NOT IN (SELECT id FROM spawned) AND o.ObjectID NOT IN (SELECT target FROM credit))
   OR (o.Type = 2 AND o.ObjectID NOT IN (SELECT id FROM gspawned));
```

### A4. Item objectives without any source (expected 61 quests)

```sql
WITH pool AS (
  SELECT q.*, c.MinLevel, c.QuestLevel AS QL
  FROM quest_template q JOIN quest_classic_level c ON c.ID = q.ID
  WHERE c.MinLevel <= 10
    AND q.LogTitle NOT LIKE '<%' AND UPPER(q.LogTitle) NOT LIKE '%UNUSED%' AND q.LogTitle NOT LIKE '[%' AND q.LogTitle NOT LIKE 'Blank%'
    AND q.LogTitle NOT IN ('Test Kill Quest','Hunter test quest','Hunter test quest2','REUSE')
    AND q.LogTitle NOT LIKE '%iCoke%' AND q.LogTitle NOT LIKE '%(123)%' AND q.LogTitle NOT LIKE '%Darkmoon%'
    AND q.LogTitle NOT LIKE '%Waskily%' AND q.LogTitle NOT LIKE '%Wabbit%'
    AND q.QuestSortID NOT IN (-24,-101,-121,-181,-182,-201,-264,-304,-324)
    AND q.QuestSortID NOT IN (-22,-284,-364,-365,-366,-367,-368,-369,-370,-371,-372,-373,-374,-375,-376,-377,-1001,-1002,-1003,-1004)
)
, oi AS (SELECT DISTINCT o.ObjectID AS item FROM quest_objectives o JOIN pool p ON p.ID = o.QuestID WHERE o.Type = 1),
  spawned AS (SELECT DISTINCT id FROM creature WHERE spawnDifficulties IN ('0','')),
  gspawned AS (SELECT DISTINCT id FROM gameobject WHERE spawnDifficulties IN ('0','')),
  src AS (
    SELECT oi.item FROM oi JOIN creature_loot_template l ON l.Item = oi.item AND l.ItemType = 0
         JOIN creature_template_difficulty d ON d.LootID = l.Entry AND d.DifficultyID = 0 JOIN spawned s ON s.id = d.Entry
    UNION SELECT oi.item FROM oi JOIN skinning_loot_template l ON l.Item = oi.item AND l.ItemType = 0
         JOIN creature_template_difficulty d ON d.SkinLootID = l.Entry AND d.DifficultyID = 0 JOIN spawned s ON s.id = d.Entry
    UNION SELECT oi.item FROM oi JOIN pickpocketing_loot_template l ON l.Item = oi.item AND l.ItemType = 0
         JOIN creature_template_difficulty d ON d.PickPocketLootID = l.Entry AND d.DifficultyID = 0 JOIN spawned s ON s.id = d.Entry
    UNION SELECT oi.item FROM oi JOIN gameobject_loot_template l ON l.Item = oi.item AND l.ItemType = 0
         JOIN gameobject_template t ON t.type IN (3,25) AND t.Data1 = l.Entry JOIN gspawned s ON s.id = t.entry
    UNION SELECT oi.item FROM oi JOIN item_loot_template l ON l.Item = oi.item AND l.ItemType = 0
    UNION SELECT oi.item FROM oi JOIN reference_loot_template l ON l.Item = oi.item AND l.ItemType = 0   -- counted as a source, not expanded
    UNION SELECT oi.item FROM oi JOIN fishing_loot_template l ON l.Item = oi.item AND l.ItemType = 0
    UNION SELECT oi.item FROM oi JOIN npc_vendor v ON v.item = oi.item JOIN spawned s ON s.id = v.entry
  )
SELECT 'item_objective_without_source' AS check_name, COUNT(DISTINCT o.QuestID) AS quests
FROM quest_objectives o JOIN pool p ON p.ID = o.QuestID
WHERE o.Type = 1 AND o.ObjectID NOT IN (SELECT item FROM src)
  AND o.ObjectID <> p.StartItem AND o.ObjectID NOT IN (p.ItemDrop1, p.ItemDrop2, p.ItemDrop3, p.ItemDrop4);
```

### A5. Zone sets (expected: Human 70/69, Dwarf 38/38, Gnome 38/38, NightElf 52/51, Orc 47/45, Troll 47/45, Undead 49/47, Tauren 47/46, SkyborneAlliance 64/60, SkyborneHorde 84/78)

Race bit = raceId - 1 for races 1-11; Skyborne Alliance 95 = bit 32, Horde 96 = bit 33 (`RaceMask::GetRaceBit`).

```sql
WITH pool AS (
  SELECT q.*, c.MinLevel, c.QuestLevel AS QL
  FROM quest_template q JOIN quest_classic_level c ON c.ID = q.ID
  WHERE c.MinLevel <= 10
    AND q.LogTitle NOT LIKE '<%' AND UPPER(q.LogTitle) NOT LIKE '%UNUSED%' AND q.LogTitle NOT LIKE '[%' AND q.LogTitle NOT LIKE 'Blank%'
    AND q.LogTitle NOT IN ('Test Kill Quest','Hunter test quest','Hunter test quest2','REUSE')
    AND q.LogTitle NOT LIKE '%iCoke%' AND q.LogTitle NOT LIKE '%(123)%' AND q.LogTitle NOT LIKE '%Darkmoon%'
    AND q.LogTitle NOT LIKE '%Waskily%' AND q.LogTitle NOT LIKE '%Wabbit%'
    AND q.QuestSortID NOT IN (-24,-101,-121,-181,-182,-201,-264,-304,-324)
    AND q.QuestSortID NOT IN (-22,-284,-364,-365,-366,-367,-368,-369,-370,-371,-372,-373,-374,-375,-376,-377,-1001,-1002,-1003,-1004)
)
, zs AS (
  SELECT 'Human' z, 0 bit, 9 s UNION ALL SELECT 'Human',0,12 UNION ALL SELECT 'Human',0,1519
  UNION ALL SELECT 'Dwarf',2,132 UNION ALL SELECT 'Dwarf',2,1 UNION ALL SELECT 'Dwarf',2,1537
  UNION ALL SELECT 'Gnome',6,132 UNION ALL SELECT 'Gnome',6,1 UNION ALL SELECT 'Gnome',6,1537
  UNION ALL SELECT 'NightElf',3,188 UNION ALL SELECT 'NightElf',3,141 UNION ALL SELECT 'NightElf',3,1657
  UNION ALL SELECT 'Orc',1,363 UNION ALL SELECT 'Orc',1,14 UNION ALL SELECT 'Orc',1,1637
  UNION ALL SELECT 'Troll',7,363 UNION ALL SELECT 'Troll',7,14 UNION ALL SELECT 'Troll',7,1637
  UNION ALL SELECT 'Undead',4,154 UNION ALL SELECT 'Undead',4,85 UNION ALL SELECT 'Undead',4,1497
  UNION ALL SELECT 'Tauren',5,220 UNION ALL SELECT 'Tauren',5,215 UNION ALL SELECT 'Tauren',5,1638
  UNION ALL SELECT 'SkyborneAlliance',32,16593 UNION ALL SELECT 'SkyborneHorde',33,16593)
SELECT zs.z AS zone_set, COUNT(*) AS total, SUM(p.MinLevel <= 9) AS le9
FROM zs JOIN pool p ON p.QuestSortID = zs.s AND ((p.AllowableRaces >> zs.bit) & 1) = 1
GROUP BY zs.z ORDER BY zs.z;
```

### A6. Forever content, group markers, retail leakage

```sql
SELECT 'forever_quests_ID>=90000_in_classic_set' k, COUNT(*) v FROM quest_classic_level WHERE ID >= 90000
UNION ALL SELECT 'of_those_with_no_starter_and_no_ender', COUNT(*) FROM quest_classic_level c WHERE c.ID >= 90000
  AND NOT EXISTS (SELECT 1 FROM creature_queststarter s WHERE s.quest=c.ID) AND NOT EXISTS (SELECT 1 FROM gameobject_queststarter s WHERE s.quest=c.ID)
  AND NOT EXISTS (SELECT 1 FROM creature_questender s WHERE s.quest=c.ID) AND NOT EXISTS (SELECT 1 FROM gameobject_questender s WHERE s.quest=c.ID)
UNION ALL SELECT 'classic_quests_with_SuggestedGroupNum>0', COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE q.SuggestedGroupNum > 0
UNION ALL SELECT 'classic_quests_QuestInfoID_1_group', COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE q.QuestInfoID = 1
UNION ALL SELECT 'classic_quests_QuestInfoID_81_dungeon', COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE q.QuestInfoID = 81
UNION ALL SELECT 'classic_quests_QuestInfoID_62_raid', COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE q.QuestInfoID = 62
UNION ALL SELECT 'classic_quests_QuestInfoID_41_pvp', COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE q.QuestInfoID = 41
UNION ALL SELECT 'zephras_sort_16593_total', COUNT(*) FROM quest_template WHERE QuestSortID = 16593
UNION ALL SELECT 'zephras_sort_16593_without_classic_level_row', COUNT(*) FROM quest_template q WHERE QuestSortID = 16593 AND NOT EXISTS (SELECT 1 FROM quest_classic_level c WHERE c.ID=q.ID)
UNION ALL SELECT 'non_classic_quests_with_a_creature_starter_row', COUNT(DISTINCT s.quest) FROM creature_queststarter s WHERE NOT EXISTS (SELECT 1 FROM quest_classic_level c WHERE c.ID=s.quest)
UNION ALL SELECT 'quest_log_core_constant_MAX_QUEST_LOG_SIZE', 35;
```

Other single-purpose checks used above:

```sql
-- start position checks (section 2.1)
SELECT race, class, map, position_x, position_y, position_z FROM playercreateinfo WHERE race IN (1,2,3,4,5,6,7,8,95,96) AND class = 1;
-- race masks of the Zephras sort (63 all-races, 19 Horde mask 12261800583900083122, 0 Alliance mask 6130900294268439629)
SELECT AllowableRaces, COUNT(*) FROM quest_template q JOIN quest_classic_level c ON c.ID=q.ID WHERE QuestSortID = 16593 GROUP BY 1;
-- quest starters near a point (here: the Gnome start; 0 rows expected within 300 yd)
SELECT s.quest, s.id FROM creature_queststarter s JOIN creature c ON c.id = s.id
WHERE c.map = 0 AND SQRT(POW(c.position_x + 4983, 2) + POW(c.position_y - 878, 2)) <= 300;
-- Zephras objective creatures missing from creature_template (7 in the pool)
SELECT o.QuestID, o.ObjectID FROM quest_objectives o WHERE o.Type = 0 AND o.QuestID BETWEEN 90000 AND 99999
  AND NOT EXISTS (SELECT 1 FROM creature_template t WHERE t.entry = o.ObjectID);
```

Log-file checks (sim worldserver `DBErrors.log`, current start):

```
grep -E "^Quest [0-9]+ .*(objective [0-9]+ has non existing|RewardItemId|RewardChoiceItemId|SourceItemId|RewardNextQuest)" DBErrors.log
grep "item does not exist - skipped" DBErrors.log          # 2542 creature_loot_template rows with items missing from the client data
```
Then restrict to quest ids in the pool (the counts in 4.3 are per distinct classic quest).

## Appendix B. Method notes for the numbers not in SQL

The chain statistics, hubs, route lengths and the prerequisite cascade were computed offline from TSV dumps of the same tables (Python, kept in
a throwaway script). To redo them:
- **Race eligibility:** `(AllowableRaces >> bit) & 1` with the bits above; **class eligibility:** `AllowableClasses = 0` or bit (class-1).
- **Chain edges** inside a zone set: `PrevQuestID` (abs value) -> quest; `RewardNextQuest` and `NextQuestID` quest -> next. Roots = quests with
  no incoming edge inside the set; "longest chain" = longest path from a root (cycle guarded); components via undirected traversal.
- **Hubs:** group `creature_queststarter`/`creature_questender` (and the GO tables) rows of the zone set by NPC; first spawn position shown.
- **Objective categories:** kill = Type 0; item = Type 1; GO = Type 2; talk = Type 3; explore = areatrigger objective (10/19/20) or a row
  in `areatrigger_involvedrelation` or `Flags & 4`; event flag = `Flags & 2`; criteria = Type 14; none = no objective row.
- **Hard defect** = any of: no starter row (no hotfix item start), starter or ender without a world spawn, ender row missing without
  `Flags & 0x10000`, objective target without a spawn (KillCredit expanded), starter/ender only in an instance, target level more than 4
  above the quest level and 6 above the minimum, elite target. **Soft** = missing item source, criteria-tree objective, area-trigger flag,
  completion-event flag, start/end script, timed, skill/reputation requirement, group `QuestInfoID`, broken `RewardNextQuest` target,
  heuristic escort (quest ids 435 and 938 found by text search of the quest descriptions).
- **Playable (cascade):** a quest is dead if its race mask excludes the race, it is hard-defective, its `PrevQuestID` quest is dead, or all
  quests that list it in `NextQuestID` are dead. A quest outside the pool counts as dead as a prerequisite.
- **Route lengths (section 2.4, 5.7):** per quest: nearest starter spawn -> nearest spawn of each objective target from the previous point
  (loot sources included, quest-provided items skipped) -> nearest ender spawn, straight-line yards on one map; quests whose starter,
  ender or any target has no spawn are "not routable" (51 of 70 Human, 36 of 38 Dwarf, 44 of 52 Night Elf, 42 of 47 Orc, 43 of 49 Undead, 35 of 47
  Tauren, 49 of 64 Skyborne Alliance, 63 of 84 Skyborne Horde are routable). Median / mean / p90 per zone set: Human 390/689/1298, Dwarf
  424/615/1017, Night Elf 506/862/1910, Orc 921/925/1687, Undead 816/870/2067, Tauren 718/854/1740, Skyborne A 351/487/1011, Skyborne H
  383/541/1035 yards. All are straight-line lower bounds.
- **mmap coverage:** tile = (floor(32 - x/533.3333), floor(32 - y/533.3333)) in the file name `MMMM_XX_YY.mmtile`; every world creature spawn of maps
  0, 1 and 2991 has a tile.
- **Pool sort exclusions:** professions -24, -101, -121, -181, -182, -201, -264, -304, -324; events/PvP/AQ and others -22, -284, -364 to -377
  and -1001 to -1004 (only -22, -284, -364, -365, -366, -367, -368, -369 occur at `MinLevel <= 10`).
