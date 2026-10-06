# Player bots: next implementation plan (proposed 2026-10-06)

Starts after the pending changes (Phase 3 navigation fixes, basic combat pass) are verified and committed.
Milestone (owner, 2026-10-05): unattended bots that log in, level, quest and roam. Phase numbers follow feature-plan.md.

| Step | What | Owner | Done when (verified on the sim) |
|---|---|---|---|
| A | Close Phase 3: independent check of nav fixes and basic combat; full link and run check of commit 72a9e61e67 | bot-qa | 180 bots, goto arrival above 90%, no unexplained stuck rows |
| A2 | Chat command layer (Phase 4, pulled forward), see below | bot-control (proposed) or class-ai | scripted raid of 10-40 bots obeys subgroup and role prefixes; unauthorized issuers ignored and logged |
| A3 | Player alts as bots: log in/out own-account alts as bots, controlled by their group leader | bot-control (proposed) or plumber | alt logs in as bot, obeys leader, saves correctly on logout and restart, refused for other accounts and for online characters |
| B | Combat core (Phase 5, minimal): threat and assist, no over-pulling, elite and level-gap avoidance, flee, rest; then combat chat commands | class-ai | bots fight at-or-below level mobs without dying trivially; bot-analyst death post-mortem works |
| C | Classes, levels 1-10 (Phase 6), warrior first, one class at a time, fork spell data, level 30 cap | class-ai | each class reaches level 10 by grinding, dealt/taken profile logged |
| D | Factory (Phase 7, minimal): create, gear, spells, talents by level | class-ai | 10 bots per class and faction created reproducibly |
| E | Grind and quests (Phase 8): quest pick by level/zone, travel, kill/collect/talk, turn-in, vendor sell/repair, blocked reasons | bot-nav | level 1 to 10 by questing with blocked-reason breakdown |
| F | Population (Phase 9): login rotation, level/zone spread, throttling, tick budget | plumber | 180 bots stable 24 h, log DB within retention |
| G | Knowledge layer (Phase 9b): priorities, aura and mob blacklists from tables; rules proposed by bot-analyst, approved by owner | plumber, bot-analyst | rule changes behaviour with no rebuild, rollback works |
| H | Deploy and soak on the main realm after backup (restart drops players: owner approval) | server-ops, db-keeper, bot-sim | bots run on main realm without harming players |

Order: A first; A2 right after A (non-combat commands only); B/C/D before E; E's non-combat parts may run beside C once B works.

## A2: chat commands

- Channels: party, raid, whisper. Parsed on the map thread at the chat packet hook; cheap prefix match, no DB access.
- Addressing (decided by owner):
  - `g1`, `g2`, `g3-g4` (raid subgroups, single, list or range), also `g1,g3`.
  - by role/class: `tank`, `healer`, `dps`, `warrior`, ... (exact set to be defined with the class roles).
  - `all` = every bot in the group/raid; no prefix in party chat = all bots in that party.
  - Subgroup membership is read live, so moving a bot changes who hears what.
  - Example: `g3-g4 follow`, `healers rest`, `warriors attack`.
- Authorization (owner, 2026-10-06): the party or raid leader controls the bots in that group. Everyone else is ignored and logged. Raid assistants: open question.
- Player alts as bots (owner, 2026-10-06): a player can log in their own alts (same account) as bots, who then obey that group's leader. Needs: a command to log an alt in/out as a bot (e.g. `.bot alt add|remove <name>`), only characters of the issuer's own account, never while that character is online as a player, and a bot cannot be logged in as a player while it is a bot. Alt bots follow the same engine and are logged with `source = alt`. Data-loss care: same save path as Phase 1, tested on throwaway characters first; a cap per account to protect the tick budget. Added as step A3 after A2.
- Verbose: `verbose on` / `verbose off` (per issuer or global, to be fixed in design) turns acknowledgements and refusals on or off. Default: to decide. When on, replies are bounded (one aggregated line per subgroup) so 40 bots do not flood chat.
- Commands first: follow, stay, goto, rest, release, status, strategy, verbose. Combat commands (attack, assist, flee, pull, tank, heal) arrive with step B.
- Logging: every command to bot_event with issuer, matched subgroup/role, accepted/refused and reason.
- Test: sim gets a fake issuer (GM command or console button) so raids can be scripted without a client.

## Reordered (owner, 2026-10-06): leveling test run comes before raid/command tests
Target: before the A2 raid/command verification and A3, show bots leveling 1-5 by themselves through questing on the sim.
Sequence: class-ai basic combat lands -> bot-nav quest pipeline (pick, accept, travel, kill/collect, turn-in, reasons logged) -> 1-5 leveling run (e.g. 10 per race/class start zone, outcome per bot, XP/level timeline, blocked-reason breakdown) -> A2 verification -> A3.
Professions (new step E2, after the 1-5 run): bots, alt bots included, pick and level primary and secondary professions where possible (gathering: mining/herbalism/skinning; crafting; first aid, cooking, fishing), with trainer visits and skill-up logged. Scope confirmed by owner (2026-10-06): gathering (mining, herbalism, skinning) + first aid + cooking first; fishing and crafting afterwards.

Owner decisions (2026-10-06): bots auto-equip quest rewards (to build). Gnome and troll starts: wait until the owner can log in and verify which quests exist there (not today); exclude them from the 1-5 run for now.

Owner request (2026-10-06): on level-up, bots go to their class trainer and learn the new spells (new step E1, right after the 1-5 run, before professions; profession trainers reuse the same visit). Needs: nearest class trainer lookup per class/faction from forever_world (cached at startup), walk and interact via `RunNpcVisit`, learn all affordable available spells (copper is limited at level 1-5: log TRAIN_NO_MONEY), log TRAINED / TRAIN_NO_TRAINER / TRAIN_UNREACHABLE; then class-ai extends the per-class spell table to use trained spells (Serpent Sting, Rend, Frostbolt, Judgement, ...).

Owner request (2026-10-06): when a quest chain ends or nothing is available nearby, bots must find new quests themselves (step E3, bot-nav): widen the search to the next quest hub (areas with many givers matching level/faction/class), follow chain successors and zone-to-zone progression, travel there (taxi/flight paths later), and when no hub fits for the level, fall back to grinding mobs of the right level. Needs a hub index built at startup alongside the quest index (givers clustered by area, with min/max quest level), a per-bot "no quests left here" state with a reason code (QUEST_NO_LOCAL / QUEST_HUB_TRAVEL / QUEST_HUB_NONE), and logging of each hub switch.

Owner request (2026-10-06): bags (step E4, bot-nav): when bags are full (or nearly), bots travel to the nearest town/hub vendor, sell junk (greys, then unusable items), repair if needed, then resume (replaces the plain BAG_FULL log). Bots also upgrade bags: when they can afford a bigger bag and have an empty bag slot or a smaller bag to replace, they buy one from a vendor (bag vendors indexed at startup like trainers) and equip it. Codes planned: VENDOR_TRIP, SOLD_ITEMS, BAG_BOUGHT, BAG_NO_MONEY.

## Decisions (owner, 2026-10-06)
- Selector `g1,tank` = intersection (tanks inside group 1); `g1,g3` stays a union of subgroups.
- Leader only; raid assistants do not command bots.
- `verbose` default off; status on request. Alt-bot cap: 4 per account.
- Sim restart onto the new exe happens after class-ai hands back; then A2 verification.

## Risks
- Commit 72a9e61e67 is only syntax-checked; step A links and runs it.
- Main realm still runs the pre-bot binary; deploying is step H and needs owner approval.
- New hooks run on map threads: same review discipline as the logger.
- Sim runs without Hardcore; test Hardcore behaviour before deploy.

## Open
1. Raid assistants allowed to command too, or leader only (default: leader only). Per-account cap for alt bots.
2. Default for verbose (default proposed: off, status on request).
3. ~~Create the `bot-control` subagent~~ Done 2026-10-06 (`~/.claude/agents/bot-control.md`); A2 was started by a general-purpose agent before it existed, A3 and later control work go to `bot-control`.
