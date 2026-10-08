# Player bots — implementation plan

Builds on [feasibility-report.md](feasibility-report.md): native implementation
on this fork's TrinityCore core, using `cmangos/playerbots` (ike3's Bot AI Core)
as a design reference rather than porting mod-playerbots' source.

## Architecture direction

A "real" player bot (shows in `/who`, can be grouped/traded/inspected like any
other character, wears gear through normal equipment slots) is a `Player`
object owned by a `WorldSession` that has **no real network socket**. This
fork's `WorldSession` already tolerates a null socket — that's exactly the
"linkdead" state TrinityCore uses when a client disconnects but the character
stays in world for a grace period. Bots are the same mechanism made permanent:
create the session, skip the packet handshake, drive the `Player` from
server-side AI calls instead of incoming opcodes.

This is a different, bigger-surface approach than "NPCBots" (spawning a
`Creature` dressed up to look like a player) — it's more work up front but is
what makes bots tradeable, groupable, and lootable the same way a real player
is, with no special-casing elsewhere in the codebase.

## Phases

### Phase 0 — Hello world (done, see below)

Prove the subsystem is wired into the build and reachable, before touching any
session/login internals. No bots exist yet.

### Phase 1 — Fake session + login

Research done, see [login-flow-notes.md](login-flow-notes.md) for the full
breakdown of `HandlePlayerLogin` (which calls must be replicated vs. skipped)
and the concrete reasons a trimmed bot-specific login function is needed
rather than reusing the stock one. Summary of what's left to build:

- Pre-create bot characters normally (existing character-creation flow, a
  reserved account range, or a SOAP/console helper) — no new DB schema yet.
- Add `BotMgr::LoginBot(ObjectGuid guid)`: a `WorldSession` built with a null
  socket (confirmed safe — `WorldSession::SendPacket` already null-checks the
  socket), a `LoginQueryHolder` run the same way `HandleContinuePlayerLogin`
  does, then a **new, trimmed** login function that performs only the
  world-state-building calls from `HandlePlayerLogin` (player load,
  `AddPlayerToMap`, `ObjectAccessor::AddObject`, etc.) and skips every
  client-packet call — reusing the stock function unmodified would spam
  `TC_LOG_ERROR` once per skipped packet per bot.
- Still open, research before writing code: a symmetric `LogoutBot`, and how
  `WorldSession::Update()` (per-session tick) should behave for a socket-less
  session.
- Verify: bot character appears in world, visible to real clients, survives a
  `.server shutdown`/restart cycle, and cleanly logs out on worldserver
  shutdown (no corrupted character data — check this exhaustively before
  moving on, this is the step with real data-loss risk).

### Phase 2 — Movement & presence

- Tick each bot's `Player` from `BotMgr::Update` (already hooked into
  `World::Update`, see Phase 0): basic follow-a-master movement using existing
  `MotionMaster` waypoint/chase APIs (no new pathing code — reuse what
  `Creature` AI already uses).
- Verify: bot follows a GM around a zone, handles normal terrain, doesn't fall
  through the world or desync on grid unload.

### Phase 3 — Group integration

- Bot responds to `Group::AddMember` normally (it's a real `Player`, this
  should mostly just work) and leaves/joins groups via a GM-issued command
  standing in for the "whisper the bot" command interface mod-playerbots
  users are used to.
- Verify: group frames, raid warnings, loot rules all behave normally with a
  bot in the party.

### Phase 4 — Combat basics (single class)

- Pick one class (suggest Warrior — simplest kit, melee-only, no mana
  management) and implement a minimal combat loop: auto-attack nearest hostile
  target when in combat, use 1-2 abilities on cooldown.
- This is where **this fork's combat/stat rewrites matter directly** — tune
  against *this fork's* `Unit::CalculateMeleeDamage` and `Player::UpdateStats`
  (see [fork-context.md](fork-context.md)), not stock TrinityCore numbers.
- Verify: bot doesn't pull aggro it shouldn't, doesn't die trivially, damage
  output is sane for its level/gear.

### Phase 5 — Class/spec AI expansion

- Repeat Phase 4 per class, referencing `cmangos/playerbots`' strategy/action
  structure for *organization* (how it separates "strategy" from "action" from
  "trigger") without copying WotLK-specific spell data — vanilla Classic spell
  IDs and talent layout are already correct in this fork's DBC/DB data, so
  this is re-authoring data tables, not porting code.

### Phase 6 — Polish

- Death/resurrection handling, bot-specific chat command vocabulary, loot
  need/greed behavior, equipment auto-upgrade logic, world-population bots
  (idle bots roaming zones, not just party fillers) if desired.

Each phase should get its own short validation note (what was tested, what
broke) appended to this file or a new `docs/playerbots/progress.md` — don't
start Phase N+1 until Phase N's verify step is actually confirmed on a running
server, not just "compiles."

## Phase 0 — what's implemented now

Minimal scaffold, no bot behavior yet — proves the subsystem builds, links,
and is reachable, and that a tick hook into the world loop works, before any
of the risky session/login work in Phase 1:

- [`src/server/game/Bots/BotMgr.h`](../../src/server/game/Bots/BotMgr.h) /
  [`BotMgr.cpp`](../../src/server/game/Bots/BotMgr.cpp) — a Meyers-singleton
  manager (`sBotMgr`), modeled on `AuctionHouseBot`'s singleton pattern
  (`src/server/game/AuctionHouseBot/AuctionHouseBot.h`/`.cpp`). `Update(diff)`
  just counts ticks and uptime for now; `GetStatus()` reports them.
- [`src/server/scripts/Commands/cs_bot.cpp`](../../src/server/scripts/Commands/cs_bot.cpp) —
  a `.bot hello` GM command, modeled on `cs_ahbot.cpp`'s `CommandScript`
  pattern. Gated by a dedicated `rbac::RBAC_PERM_COMMAND_BOT` (id 1000, see
  `src/server/game/Accounts/RBAC.h` and
  `sql/custom/auth/2026_10_05_00_auth_rbac_bot.sql`), granted by default to
  Gamemaster and Administrator.
- Registered in `src/server/scripts/Commands/cs_script_loader.cpp` (forward
  declaration + `AddCommandsScripts()` call, alphabetically placed between
  `bg` and `cast`).
- Hooked into `World::Update` in `src/server/game/World/World.cpp`, right
  after the existing `sAuctionBot->Update()` call, gated by a `Bot.Enabled`
  config option (`CONFIG_BOT_ENABLED` in `World.h`/`World.cpp`, documented in
  `worldserver.conf.dist`) — defaults to enabled, set to `0` to disable the
  subsystem entirely without recompiling.
- New `Bots/` subdirectory under `src/server/game` needs **no CMakeLists.txt
  edit** — `src/server/game/CMakeLists.txt` uses `CollectAndAddSourceFiles`
  (see `cmake/macros/AutoCollect.cmake`), which recursively globs every
  subdirectory. A CMake **reconfigure** (re-run cmake, not just rebuild) is
  required to pick up the new files, same as adding any new source file here.

### How to test it

1. Reconfigure and rebuild `worldserver` (reconfigure needed — new files).
2. Start worldserver, log in as a GM.
3. Run `.bot hello` in-game (or from the console).
4. Expect: `BotMgr alive: N update ticks, Ms uptime, 0 bots tracked (Phase 0 -
   no bots implemented yet)`, with `N` growing and uptime increasing each time
   you re-run the command. That's the whole test — it proves the manager is
   instantiated exactly once, is being ticked from the real world loop, and is
   reachable from a chat command, with zero risk to character/world data since
   nothing here touches `Player`, sessions, or the database yet.

**Not yet implemented, do not assume working:** anything involving an actual
bot character, login, movement, combat, or persistence. Phase 1 is the next
real step and the first one with meaningful risk (session/login internals) —
test it on a throwaway character/DB snapshot, not production data.
