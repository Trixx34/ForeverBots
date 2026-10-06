# Player bots — feature inventory and implementation plan

**Date:** 2026-10-05
**Builds on:** [feasibility-report.md](feasibility-report.md) (native implementation, borrow design from the
reference projects), [implementation-plan.md](implementation-plan.md) (Phase 0 scaffold, Phase 1 login) and
[fork-context.md](fork-context.md).

**Evidence base, and its limits.** This inventory comes from the repository tree and docs of
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) (file layout of ~1530 files, `AGENTS.md`,
`.agents/docs/ai-engine.md`, `conf/playerbots.conf.dist` with 886 options, the SQL table list) and a summary of
[cmangos/playerbots](https://github.com/cmangos/playerbots). The source files themselves were **not** read yet, so the
descriptions below say what exists and how it is organised, not how each piece is implemented. Read the relevant
source before building each feature. Both projects are GPLv2, as is TrinityCore: ideas and structure can be reused,
copied code keeps its notices and credits.

## 1. What mod-playerbots contains

### 1.1 Engine (the part worth copying in structure)
- One `PlayerbotAI` per bot, ticked from `Player::Update` on the **map thread**; bots on different maps run in parallel.
- Three engines per bot: **combat, non-combat, dead**. State changes only in one place (`DoNextAction`).
- **Strategy → Trigger → Action** model plus **Values** and **Multipliers**:
  - a *strategy* contributes triggers, actions and multipliers; several are active at once;
  - a *trigger* is a cheap condition (must be O(1), reads cached values);
  - an *action* does one job and gates expensive work behind `isUseful()` / `isPossible()`;
  - a *value* is a cached calculation (`checkInterval`), e.g. attackers, party member to heal, nearest NPCs, loot;
  - a *multiplier* scales or zeroes action relevance (used for boss mechanics, passive modes).
  The highest-relevance possible-and-useful action in one merged queue runs. Relevance uses named bands
  (default 5, normal 10, high 20, move 30, interrupt 40, dispel 50, raid 60, emergency 90, pull 105-107).
- Name-based registries ("creator tables" for actions, triggers, strategies, values), class-specific contexts, and
  `AiFactory` choosing the default strategy sets per class/spec/state.
- Rules the maintainers enforce, worth adopting: triggers O(1); **no synchronous DB queries on map threads**; shared
  code never branches on strategy names; one action one job; expensive features opt-in via config.

### 1.2 Runtime and management
- `PlayerbotMgr` (bots owned by a master player, alt-login), `RandomPlayerbotMgr` (world population),
  `RandomBotLevelMgr`, `PlayerbotFactory` / `RandomPlayerbotFactory` (create, level, gear, talents, enchant),
  `Talentspec` (premade specs, 262 `Premade*` config options), `BisListMgr`, `RandomItemMgr`, `StatsWeightCalculator`
  (gear upgrade by stat weights), `FleeManager`, `PlayerbotSecurity`, `PlayerbotTextMgr` (translatable bot chat),
  `GuildTaskMgr` and `PlayerbotGuildMgr`, `PerfMonitor`, a command server, and a world-thread operation queue for
  cross-thread work.
- Its own database (`playerbots`): random-bot registry, account links/keys, custom strategies, item/enchant/equip
  caches, travel node graph (`travelnode`, `travelnode_link`, `travelnode_path`), speech and text tables, tele cache.

### 1.3 Behaviors (by area)
| Area | What exists |
|---|---|
| Party and master control | follow/stay/guard/flee, invite/leave, pass leadership, ready check, formations, chat-command interface, RTI marking, focus-heal targets, loot rules and roll |
| Combat, generic | threat, tank/DPS assist, pull sequence, kite, aggressive/passive, mana conservation, potions/food, racials, interrupts, dispels, CC targets, duels, wipe handling |
| Combat, per class | one directory per class with rotations: Warrior, Paladin, Hunter (+pet), Priest, Shaman, Mage, Warlock, Rogue, Druid (+DK in WotLK) |
| Survival and recovery | dead engine: release spirit, revive from corpse, accept resurrect, eat/drink/rest, repair, stuck detection |
| World (non-party) | "new RPG" behavior: choose a target (NPC, object, outdoor PvP), travel there, interact; grind; fishing; travel graph (`TravelMgr`/`TravelNode`), taxi/flightpath, teleports, zone distribution |
| Quests | accept, talk to giver, share, complete, reward, drop, quest strategies, area triggers, gossip |
| Items and economy | buy/sell, repair, bank and guild bank, mail, trade, auction (WTS), inventory/outfits, equip/unequip, destroy/use items, unlock |
| Professions and crafting | training, crafting orders, gathering reveal, enchanting |
| Social | guild create/accept/manage, petition signing, greetings, emotes, "say" strategies, toxic chat options, broadcast chat |
| Group content | per-instance dungeon and raid strategies with boss multipliers and aura triggers (WotLK/TBC set; Classic set in this fork's scope: MC, Onyxia, BWL, AQ20 and AQ40 exist for the reference core) |
| PvP | battleground join and per-BG tactics, arena actions (arena does not exist in Classic) |
| Operations | performance monitor, debug switches, per-bot log level, 886 config options (levels, activity, RPG, teleports, timing, per-class tuning, database) |

## 2. What applies to Forever (vanilla Classic 1.60, vanilla world from VMaNGOS)
- **Keep as design:** the engine (1.1), factory + premade specs + stat-weight gearing, travel graph, RPG behavior,
  command vocabulary, random-bot manager, security, translatable text.
- **Drop or re-author:** WotLK talent/glyph/dual-spec/DK/vehicle/arena/LFG-tool/emblem content; all per-class spell
  and talent data is re-authored from this fork's Classic spell and talent data; thresholds re-tuned against this
  fork's rewritten combat and stat code (`Unit::CalculateMeleeDamage`, `Player::UpdateStats`).
- **Skyborne classes (confirmed by the owner):** six classes are available to Skyborne: Warrior, Hunter, Rogue, Druid,
  plus Shaman (Horde only) and Mage (Alliance only). Class AI and quest paths must work for those on Zephras Isle too.
  Still to confirm: which classes the other races use for the "10 per class per faction" test target (plain Classic is
  9 classes, about 180 bots) and whether Skyborne bots count as a separate group.
- **Fork difference that shapes the architecture:** mod-playerbots hooks `Player::Update` through an AzerothCore
  hook and edits the core in about 133 files; here we edit TrinityCore directly (as `BotMgr` already does) and keep
  the surface small by funnelling through `BotMgr` and a per-bot `BotAI`.

## 3. Forever-specific requirement: the log is part of the engine
Reference projects log for developers. Here the log drives bot scripting, so the engine records, per bot, in
`forever_botlog` (plumber's `BotMgr::LogEvent`): which strategy/trigger/action ran and why (`reason` code, alternatives
considered), state changes between combat/non-combat/dead, deaths in full (killer, damage taken, position, last
decisions), quest progress and the specific blocker when a quest cannot complete (`quest_blocked`), `stuck`,
`path_fail`, flee/wipe events. Rules: log on state change not per tick; severity levels; per-bot trace switch; never
a synchronous DB call from a map thread (the logger is already buffered and batched).

## 4. Phased plan
Each phase ends with a verify step run on a live worldserver with throwaway characters; do not start the next phase
until it is confirmed (not "compiles"). Owners: `plumber` = infrastructure, `class-ai` = behavior, `server-ops`,
`db-keeper`, `data-extractor` as named.

| # | Phase | Deliverable | Verify | Needs |
|---|---|---|---|---|
| 0 | Scaffold (done) | `BotMgr`, `.bot hello`, `Bot.Enabled`, RBAC perm | `.bot hello` | build ok |
| 0b | Bot log (written, unbuilt) | `BotLogDatabase` pool, batched `LogEvent`, `.bot logtest` | rows appear in `bot_event` | plumber builds and tests |
| 1 | Socket-less session and login | `LoginBot`/`LogoutBot`, trimmed login | bot visible, survives restart and shutdown with no data loss | `dbc`/`gt`/`maps` (done), worldserver running |
| 2 | Engine core | Strategy/Trigger/Action/Value/Multiplier classes, three engines, relevance bands, name registries, action queue, `BotAI` tick from the map thread | unit-style test strategy fires and logs; tick cost measured with 180 idle bots | Phase 1 |
| 3 | Non-combat basics | follow/stay/guard, loot, eat/drink/rest, release/revive, stuck detection | follows a GM through a zone, recovers from death | vmaps/mmaps for pathing |
| 4 | Command and control | whisper/party-chat vocabulary, security (who may command), custom strategy editing, `.bot` GM commands | commands work, unauthorized ignored | 3 |
| 5 | Combat core (generic) | target selection, threat/aggro, assist, pull, flee, potions, interrupts, wipe handling | bot does not over-pull, does not die trivially | 3 |
| 6 | First class end to end | Warrior (melee, no mana) against this fork's formulas, then the other 8 classes one by one | sane damage/threat vs level and gear per class | 5, per-class agents (see 7) |
| 7 | Factory and gearing | create bots, level, Classic talent premades, gear by stat weights/BiS, spell training, consumables | 10 per class per faction created and equipped reproducibly | 6 |
| 8 | Travel, grind and quests | travel node graph generated from our mmaps/maps, RPG target choice, grind, quest accept/complete/reward, taxi, fishing | bot levels 1-10 by questing; blocked quests are logged with a reason | mmaps/vmaps, quest data |
| 9 | Random-bot manager | login/logout rotation, level and zone distribution, teleports, activity throttling, performance limits | 180 bots stable, tick budget respected | 7, 8 |
| 9b | Knowledge layer | strategy priorities, aura lists, mob/quest blacklists read from tables (not hard-coded); richer log events (encounter start/end, aura applied/removed, damage timeline, who could dispel/interrupt); `bot-analyst` post-mortems and proposed rules with owner approval, versioned with rollback | a rule changes bot behavior without a rebuild and can be rolled back; a death post-mortem is produced from log data | 9, plumber extends the schema |
| 10 | Economy and social | vendor/repair, bank, mail, trade, auction, guilds, professions, chat/emote text | bots trade and sell without duplicating items | 9 |
| 11 | Group content | Classic dungeon and raid strategies (RFC to Scholomance, MC, Onyxia, BWL, ZG, AQ, Naxx), battlegrounds (WSG, AB, AV) | clear a dungeon with a mixed party | 6, 10 |
| 11b | Simulation harness | separate sim worldserver and databases, auto-formed parties, instance reset, run ids and outcomes in the log, batch runner, A/B batches (`bot-sim`, results read by `bot-analyst`) | 100 unattended runs complete and are all visible in the log; an A/B batch yields a comparable result | 11, bots able to clear content |
| 12 | Polish and tuning | config surface, perf monitor, text translation, docs | soak test | all |

Cross-cutting: a per-phase validation note appended to `docs/playerbots/progress.md`; new SQL only as dated files in
`sql/custom/*`; opt-in config for anything expensive.

## 5. Risks and open decisions
1. **Data-loss risk in Phase 1** (session/login internals): test on throwaway characters and a database snapshot.
2. **Map-thread safety:** engine code runs on map threads; shared state needs the same discipline as the logger
   (mutex or world-thread queue); no DB access from them.
3. **Per-tick cost at 180 bots** (and any later scale-up): measure from Phase 2 on; keep triggers O(1).
4. **Pathing quality** depends on `mmaps`, which are not generated yet (`vmap4extractor`, `vmap4assembler`,
   `mmaps_generator` pending).
5. **Class list** (Skyborne additions) and vanilla spell/talent data accuracy must be checked per class.
6. **Scope control:** phases 8-11 are each months of work in the reference projects; consider cutting Phase 11 and
   parts of 10 for a first release.
7. **Decided (owner, 2026-10-05): the first milestone is world population** (phases 1-9: bots that log in, level,
   quest and roam on their own), not party fillers. Consequences: phase 8 (travel, grind, quests, `bot-nav`) and
   phase 9 (random-bot manager) move from "later" to the critical path, `mmaps` quality becomes a hard dependency,
   and phase 4 (command/control) shrinks to GM commands plus a minimal vocabulary until population works. The engine
   must be cheap enough for ~180 bots acting independently from phase 2 on, and the log must make it possible to see
   why an unattended bot is stuck. Group-play features (parts of 5, 10, 11) follow after the milestone.

## 5. Later development notes (owner)

- **Quest log (in scope now, Phase 2):** per bot, log which quest was accepted, progressed, completed, rewarded (chosen reward, xp, money), abandoned or failed, as `quest` events with `quest_id`, so questing can be reviewed per bot and per quest.
- **Group quests (later, Phase 8 or 11):** when a bot has a quest meant for a group (the log marks these with `group_quest` and `suggested_players`), it should check chat (party, say or a world channel, whatever the Phase 4 vocabulary provides) for other bots that have the same quest or are free to help, and do it together. Needs: group-quest detection (already in the log), a chat vocabulary to ask and answer ("anyone for <quest>?"), matching bots by quest and zone, forming a group, and a shared objective until all members have completed or given up. The simulation can test this with several bots holding the same quest.
