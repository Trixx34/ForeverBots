# 14. Implementation status

The design documents (00 to 12) are the records of what was decided. Their status lines were written before the code,
so they still say "not yet implemented". This page says what exists now. Where the code differs from a design doc,
the code is the reference and the difference is listed.

## Implemented

| Area | What exists | Where |
|------|-------------|-------|
| Process host (04) | Discovery, spawn, Tier 1 handshake, heartbeat, restart policy, approval store (06) | `src/server/game/Modules/ModuleMgr.*`, `ModuleApproval.*` |
| Tier 1 transport | Length-prefixed proto2 frames over Unix sockets (AF_UNIX on Windows 10 1803+) | `ModuleSocket.*`, `ModuleSession.*`, `ModuleWire.*` |
| Events (03) | `PlayerScript.OnLootItem` notify; chat commands via `CommandScript` | `ModuleMgr::DispatchItemLooted`, `RouteCommand` |
| Module actions | `SellItem`, `SendPlayerMessage`, `ShareQuestItem` (gives a looted quest item to the party members who need it; the core finds who, within range, with a quest that needs it and room in the bags) | `ModuleActions.cpp` |
| Module config | `Module.<name>.<Key>` from `modules.d/<name>.conf`, sent in `ModuleConfig` after accept | `ModuleMgr::LoadModuleConfig` |
| Tier 2 region | Shared-memory region per module, double-buffered | `ModuleTier2.*`, `ModuleTier2Layout.h` (copy of the SDK header) |
| Tier 2 snapshots | Owned, online bots: position, map, health, power, level, combat target, the result of the last action, cooldowns, party state, the nearest vendor on the bot's map, and nearby units (creatures first; hostile means the bot may fight it) | `ModuleBotSnapshot.*`, `ModuleTier2::PublishSnapshot` |
| Tier 2 actions | `stop`, `move`, `follow` (players and creatures), `attack`, `cast` (spells the bot knows), `rest`, `vendor` (sell grey, repair, equip), `train`, `group` (invite, accept, leave), `loot` (a corpse, through the bot's own loot handlers) | `ModuleBotActions.*`, `ModuleTier2::ReadNewActions` |
| Unit references | Type, entry and 32-bit counter in one 64-bit value | SDK `tier2_layout.h` (`MakeUnitRef`) |
| Bot control | `RequestControl`, `ReleaseControl`, `ControlRevoked`; one owner per bot; `max_bots` | `ModuleBotOwnership.*`, `ModuleMgr::ExecuteControlRequest` |
| Bot lifecycle | Create, log in and out through a queue, relogin delay; at shutdown, logins in flight are finished (bounded by the login timeout) before logout | `src/server/game/Bots/BotMgr.*` |
| Spawn level | `SpawnParams.level` honoured; 0 is the starting level; above max level refused | `BotModuleBackend::Spawn`, `BotMgr::CreateBot` |
| Spawn team | 0 from race, 1 Alliance, 2 Horde; a team that disagrees with the race is refused | `BotModuleBackend::Spawn` |
| Spell cooldowns | Up to 8 active cooldowns per bot in each snapshot, as spell id and remaining ms, closest to expiring first | `ModuleBotSnapshot.cpp` (`FillCooldowns`) |
| Module store | Per-module key/value rows in the world database, through the action path; the module's own rows only | `ModuleStore.*`, `ModuleStoreRules.*`, `sql/updates/world/master/2026_10_12_00_world.sql`, doc 15 |
| Bot limits | `Bot.Login.*`, `Bot.Limit.*`, `Bot.Run.StopTime` (clean shutdown at a local time), `Bot.Spread.*`; `Bot.Dump.Abilities` logs what each race and class has at each level | `BotMgr.*`, `worldserver.conf.dist` |
| Restart reclaim | The module keeps its bot guids as store rows (`bot/<guid>`) and takes them back on start | module repo `src/bot_store.*`; core store (doc 15) |

## Differs from the design documents

- **Tier 2 actions.** Design 11 had `PublishActions(ActionQueue const&)`. The SDK has
  `bool PublishActions(std::vector<BotAction> const&, std::string& error)`. It refuses more actions than `MaxActions`
  and any unknown action type, and publishes nothing on refusal.
- **Tier 2 targets.** Units are referenced by a 64-bit unit reference (type, entry, 32-bit counter), not a bare guid.
  Units whose counter does not fit are refused.
- **Team.** `SpawnParams.team`: 0 takes the race's faction; 1 is Alliance; 2 is Horde. Any other value, or a team that
  disagrees with the race, is refused before any account is created.
- **Spell cooldowns.** Each cooldown carries its spell id (`SpellCooldown`), so the module knows which spell it is.
- **Protocol and layout.** Protocol is 4 and the Tier 2 layout is 7. Each layout change is breaking: a module built against another layout is refused at region attach. The changes are listed in the SDK's `CHANGELOG.md`. Adding an action type or a Tier 1 action does not change the layout.
- **Revocation and state.** A `ControlRevoked` does not remove a bot from the module's state file, because the core
  revokes all bots at shutdown. Only a `character not found` reply removes a guid.
- **Action timing.** A module publishes at most one action set per snapshot tick. The core applies the newest set it
  sees; two publishes between two core ticks leave only the second visible.

## Still open

- Config is sent once, after accept. A reload does not re-send it (design 03 says it should).
- Resource limits (`Modules.ResourceLimits.*`) are declared but not enforced.
- Core unit tests for the action and snapshot code need a build; only syntax checks have run for them.
- Run on a dedicated sim worldserver with up to 1000 bots (spawn, login, the ramp, the safety valves, telemetry); sustained combat, levelling, looting, vendor and hub walks and parties are not yet verified there. See doc 13.
- The core builds on Windows only here: the changes since the first sim runs (layouts 5 to 7, the loot and share actions, PR 80) have been checked by reading, not compiled.
