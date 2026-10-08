# Sim verification checklist and A2/A3 review (2026-10-08)

Status: desk review only. Nothing here was run; the sim was not available. Every item below is UNVERIFIED until the steps are run.

## Review result (code read against progress.md and next-plan.md)

Defects fixed in `BotChat.*` (unit-tested, parser only):
1. `g1,tank` with an empty role class mask (for example `Bot.Chat.Role.Tank = ""`) matched every bot of subgroup 1, because an empty class mask meant "no class filter". The selector now remembers that a role/class part was given (`HasClass`), so an empty role matches nobody.
2. Plural selectors from the A2 design (`healers rest`, `warriors follow`, `tanks stay`) were not parsed and the message was silently ignored. Plurals are now accepted (single trailing `s`, `dps` unchanged).
3. `goto nan nan`, `goto inf 0` and absurd coordinates passed `StringTo<float>` and went to the pathing code. `goto` now answers BAD_ARGS for non finite values and for |x|,|y| > 17100 or |z| > 5000.

The parser pieces (`Selector`, `RoleMasks`, `ParseSelector`, `GotoArgs`, `ParseGotoArgs`) moved to `BotChat.h` as pure functions; tests in `tests/game/BotChatParse.cpp` (target `tests`, run `tests --reporter compact "[BotChat]"`). The pure code was also compiled and exercised with a stub harness (g++ -std=c++20), the full tree was not built.

Read and found consistent, no change: authorization (leader of the bot's group only, rate limited unauthorized log, `NOT_IN_GROUP` for whispers), raid needs a prefix, party chat inside a raid addresses the issuer's own subgroup, per-issuer maps erased on logout (`WorldSession.cpp:602`), A3 own-account rule (same answer for unknown names and foreign characters), online-as-player and account-mid-login refusals, per-account cap, `CharacterHandler` refusal while an alt is a bot, persistence via `bot_alt` with table probe, queued/logging-in/online `StopAlt` states, duplicate login-queue entries after remove+add are skipped by the `State != BOT_QUEUED` check.

Open risks, not changed (need the sim):
- `OnAltLoggedIn`: if the owner's group exists, `AddMember` failure on a freshly created one-person group is not cleaned up.
- `RestoreOnce` runs once; a row skipped because the owner is logging in is only retried after the next restart.
- Hybrids are not split by spec in `tank|healer|dps`; `rest` only toggles the strategy.
- Alt bot sessions share the owner's account id; saving, mail and account level side effects on logout are only reasoned about, not seen.
- Unauthorized attempts are logged against the first bot of the group.

"Not committed" claims in progress.md: no longer true. `git log` shows BotChat/BotQuest (3c8db604), BotAlts (cdd8c2d6), BotCombat (a02f86e3), BotBehavior (9d551bd2) and BotAI (72a9e61e) on `forever`, all 2026-10-06. The headings were changed to "code is committed on `forever`". The A2 section still says NOT verified, which remains true.

## Common setup

1. Deploy the build from this branch, sim worldserver with `Bot.Enabled`, bot log DB reachable, `Bot.Chat.LogEvents = 1`.
2. Apply `forever-characters-bot-alt.sql` (A3) and check the log has no "table bot_alt not found" warning.
3. Query helper: `SELECT ts, bot_name, type, reason, summary FROM bot_event WHERE type='chat_command' ORDER BY ts DESC LIMIT 50;` (adapt the column names to the schema in use). Alt rows: `type='alt_command'`.
4. Run each section below with 0 or only the needed bots online (see progress.md: `bot spawn` only with 0 online).

## A2: chat commands (`BotChat.*`)

Setup: `bot spawn 40`, then `bot group form <botLeader> all raid` (leader is a bot, so replies come through `bot say` output). Use `bot group list <botLeader>` and `bot group move <member> <1-8>` to place members in subgroups. For client tests use a real character as raid leader.

1. Unit tests: build target `tests`, run `[BotChat]`. All pass.
2. Routing by subgroup: `bot say <leader> raid g1 status`, then `g3-g4 status`, `g1,g3 status`. Reply line "Status N bot(s)" matches the member count of those subgroups from `bot group list`. Move one bot with `bot group move` and repeat: counts change (live membership).
3. Routing by role/class: `raid tank status`, `raid healers status`, `raid dps status`, `raid mage status`. Counts match the classes in the raid. Then `raid g1,tank status`: only warriors in subgroup 1. Set `Bot.Chat.Role.Tank = ""` (restart) and confirm `g1,tank` matches nobody (NO_MATCH event), not the whole subgroup.
4. Prefix rules: `bot say <leader> raid follow` (no prefix) logs NEEDS_PREFIX. `bot say <leader> party follow` inside the raid hits only the leader's own subgroup (match `party:gN`). Plain party (5 bots, no raid): `party stay` hits all.
5. Verbs: `follow` (bots move to the issuer, strategy follow shown by `bot state`), `follow off`, `stay` (goal cleared), `goto here` (bots spread on the spiral, no stacking on one point), `goto <x> <y>` and with z (GOTO_START/GOTO_ARRIVED rows), `rest` / `rest off`, `release` on dead bots (ghost within a tick, ALREADY_GHOST / NOT_DEAD on others), `strategy` (listing), `strategy +combat,-quest`, `strategy +bogus` (UNKNOWN_STRATEGY for every bot), `share <questId>`.
6. Bad input: `goto nan 5`, `goto 1e9 0`, `goto 5`, `follow maybe` -> BAD_ARGS for every matched bot, nobody moves.
7. Combat refusal: put a bot in combat (`bot aggro`), `goto here` -> IN_COMBAT for it, OK for the rest. One aggregated reply line only (verbose on).
8. Authorization: from a real client a non-leader member says `all follow`; bot_event shows UNAUTHORIZED, severity warn, once per 30 s with a `suppressed` count; nothing changes. A non-grouped player whispers a grouped bot `stay` -> NOT_IN_GROUP.
9. Whisper: leader whispers one bot `follow`, then `tank follow` to a non-warrior bot (NO_MATCH).
10. Verbose: default off (no replies, events still written); `verbose on` -> one line per command, bounded by `Bot.Chat.MaxReplyLines`; `verbose off`; logout/login of the issuer resets it.
11. Cost: with 180 bots and 40 in a raid, `bot ai status` tick average before/after a burst of 20 commands; chat hook cost on ordinary party chat with no bots must be negligible (no allocation before the verb check).

Pass: every expected event row exists with outcome accepted/refused and the right reason, no server error log lines, tick average within noise.

## A3: alt bots (`BotAlts.*`)

Needs a test account with 5+ throwaway characters (never real ones). Not online as players unless stated.

1. `.bot alt add <alt>` from the client of the owning account: alt logs in as a bot, `alt_command ALT_ADDED`, events of the alt carry `"source":"alt"`, alt joins the owner's group (owner alone: new party; owner leads: added; owner is a member: message that the leader must invite).
2. Group control: owner says `follow`, `stay`, `goto here` in party chat; the alt obeys. A second player in the group cannot command it (UNAUTHORIZED).
3. Refusals (each logged as `alt_command` with the ALT_REFUSED_* code, same wording for unknown and foreign characters): another account's character, unknown name, character online as a player (ALT_REFUSED_ONLINE), add twice (ALREADY), over `Bot.Alt.MaxPerAccount` (CAP), a reserved bot character (BOT), add while another character of the account is logging in (LOADING).
4. Player login of an active alt is refused at the character list (CharacterHandler hook); works again after remove.
5. `.bot alt list` shows names and `n of cap`. `.bot alt remove <alt>` and `.bot despawn <alt>` by the owner log it out and save; `.bot despawn <alt>` by a GM of another account is refused; from the console it is allowed. `.bot despawn all` must not touch alts.
6. Data safety: before add, note the alt's level, XP, money, bag contents, position; after remove and after a worldserver restart, compare (characters table). Then log the alt in as a normal player and confirm nothing was lost.
7. Persistence: add 2 alts, restart worldserver: both restored (`ALT_RESTORED`, log line "restored 2 alt bot(s)"). Remove one, restart: only the other returns. Delete a character, restart: row dropped. Set the cap to 1, restart: one restored, one skipped, row kept.
8. Console test mode: `.bot alt add <name> test` uses the character's own account.
9. Owner logs out while alts are online: behavior must be defined (observe and record; the plan says alts keep running until removed or the owner despawns them).

## BAG_BOUGHT (E4 vendor, `BotQuest.cpp` vendor trip)

Preconditions in code: bot level >= 3, a free target bag slot or a smaller bag to replace, bag vendor indexed at startup, money >= spell reserve + price.
1. Pick a bot of level >= 3 in a town with a bag vendor; set its money to 2 gold and empty its extra bag slots (`bot level`, `bot tele`, GM modify money as available).
2. Make its bags nearly full (fill with greys) so a VENDOR_TRIP triggers, or wait for the periodic trip.
3. Expect in order: VENDOR_TRIP, SOLD_ITEMS, optional REPAIRED, then BAG_BOUGHT with `equipped: true`, `bag_slot`, `money_left`. Verify in game or DB: new bag in the slot, free slot count increased.
4. Negative cases: money below price -> BAG_NO_MONEY once per level; full inventory and no free bag slot -> BAG_BUY_FAILED; level < vendor requirement -> nothing.
5. Watch: bag bought but left in backpack (`equipped:false`), repeated purchases every trip, a quest item sold.

## Gnome start (race 7, start (-4983, 878, 274))

Known: 22-24 race-7 bots idle at level 1 (NO_GRIND_TARGET, NO_PATH, QUEST_QUARANTINED, QUEST_HUB_NONE); no complete path in 16 headings at 108 yd; goals on mesh holes. Cause not investigated.
1. Spawn one gnome (`bot spawn 1 mage`, check race 7) at its start point.
2. `bot path <gnome> <x> <y> <z>` to 6 targets (10, 30, 60, 108 yd in different headings, and the Kharanos/Anvilmar quest giver positions). Record path type and `start_off_navmesh`.
3. `bot goto` to the nearest valid target; note GOTO_START details (`end_gap_3d`, `goal_far_from_poly`) and the outcome.
4. Check mmaps for map 0 tile around the start; compare with a dwarf at (-6094, 818.8).
5. Let the gnome run 20 min with quests on: record blocked codes. Pass: it picks and does a quest, or the log names one concrete cause (no navmesh, no starter quest data).

## Path type 0x0A on long paths

Known: paths of 400-500 yd return 0x0A (NOPATH|SHORTCUT, the 74 point buffer); Travel hops via FindHop for routes above about 300 yd.
1. `bot path` from one bot to 5 targets at 300, 400, 500, 700, 1000 yd. Record type, point count and whether the end is within 7 yd of a poly.
2. `bot goto` to the same targets. Expect GOTO_START with hops (FindHop) or a clean NO_PATH; fail if the bot walks a straight line off the mesh or hangs (NO_PROGRESS).
3. Count: of 20 long gotos, how many ARRIVED, NO_PATH, PATH_PARTIAL_FAR. Compare with the old numbers in progress.md.
4. Note any 0x0A where a hop exists (that is a FindHop gap).
