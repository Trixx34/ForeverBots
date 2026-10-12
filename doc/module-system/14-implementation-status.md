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
| Module actions | `SellItem`, `SendPlayerMessage` | `ModuleActions.cpp` |
| Module config | `Module.<name>.<Key>` from `modules.d/<name>.conf`, sent in `ModuleConfig` after accept | `ModuleMgr::LoadModuleConfig` |
| Tier 2 region | Shared-memory region per module, double-buffered | `ModuleTier2.*`, `ModuleTier2Layout.h` (copy of the SDK header) |
| Tier 2 snapshots | Owned, online bots: position, health, mana, combat target, nearby units | `ModuleBotSnapshot.*`, `ModuleTier2::PublishSnapshot` |
| Tier 2 actions | `stop`, `move`, `follow` (players and creatures), `attack` (hostile units), `cast` (spells the bot knows) | `ModuleBotActions.*`, `ModuleTier2::ReadNewActions` |
| Unit references | Type, entry and 32-bit counter in one 64-bit value | SDK `tier2_layout.h` (`MakeUnitRef`) |
| Bot control | `RequestControl`, `ReleaseControl`, `ControlRevoked`; one owner per bot; `max_bots` | `ModuleBotOwnership.*`, `ModuleMgr::ExecuteControlRequest` |
| Bot lifecycle | Create, log in and out through a queue, relogin delay; at shutdown, logins in flight are finished (bounded by the login timeout) before logout | `src/server/game/Bots/BotMgr.*` |
| Spawn level | `SpawnParams.level` honoured; 0 is the starting level; above max level refused | `BotModuleBackend::Spawn`, `BotMgr::CreateBot` |
| Restart reclaim | The module keeps its bot guids in a state file and takes them back on start | module repo `src/bot_state_file.*`; core needs no change |

## Differs from the design documents

- **Tier 2 actions.** Design 11 had `PublishActions(ActionQueue const&)`. The SDK has
  `bool PublishActions(std::vector<BotAction> const&, std::string& error)`. It refuses more actions than `MaxActions`
  and any unknown action type, and publishes nothing on refusal.
- **Tier 2 targets.** Units are referenced by a 64-bit unit reference (type, entry, 32-bit counter), not a bare guid.
  Units whose counter does not fit are refused.
- **Team.** `SpawnParams.team` is not used. Team follows from race.
- **Revocation and state.** A `ControlRevoked` does not remove a bot from the module's state file, because the core
  revokes all bots at shutdown. Only a `character not found` reply removes a guid.
- **Action timing.** A module publishes at most one action set per snapshot tick. The core applies the newest set it
  sees; two publishes between two core ticks leave only the second visible.

## Still open

- Spell cooldowns are not in snapshots (`spell_cooldowns` reads zero).
- Config is sent once, after accept. A reload does not re-send it (design 03 says it should).
- Resource limits (`Modules.ResourceLimits.*`) are declared but not enforced.
- Team in `SpawnParams` is ignored; a request for a team that disagrees with the race is not refused.
- The auto-vendor module (`mod-auto-vendor-trash`) still uses its defaults and does not read its config.
- Core unit tests for the action and snapshot code need a build; only syntax checks have run for them.
- Not yet run end to end on a live worldserver. See doc 13.
