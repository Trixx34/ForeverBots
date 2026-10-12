# Module System — Phase 1 Protocol Design

Status: design drafted, not yet implemented.
Builds on `00-investigation.md`, `02-phase0-decisions.md`, `01-first-steps-plan.md`.

## Tier 1 — event hook protocol

Transport: `boost::asio` local stream socket (Windows named pipe / Unix domain socket), protobuf-framed messages.

### Message envelope

```proto
message Envelope {
  uint64 message_id = 1;  // correlates EventRequest -> EventReply
  oneof payload {
    HandshakeRequest handshake_request = 10;
    HandshakeResponse handshake_response = 11;
    Heartbeat heartbeat = 12;
    ModuleConfig module_config = 13;  // core -> module, sent once right after accept
    EventNotify event_notify = 20;    // fire-and-forget, core does not wait
    EventRequest event_request = 21;  // core blocks (bounded timeout) for a reply
    EventReply event_reply = 22;
    Shutdown shutdown = 30;
  }
}
```

### Handshake

`HandshakeRequest` (module → core): module name, version, SDK version (exact-match required per Phase 0 §3), declared Tier 1 hook subscriptions, Tier 2 participation flag + `requested_max_bots` / `requested_max_nearby_units` / `requested_max_actions` / `requested_tick_interval` (only meaningful if Tier 2 flag set).

`HandshakeResponse` (core → module): accepted (bool) + reason if rejected (version mismatch, hook not available, resource limits exceeded), assigned `module_id`, Tier 2 shared-memory region name (if accepted + Tier 2 requested).

### Config delivery

Immediately after a successful `HandshakeResponse`, core sends one `ModuleConfig` message: flat key/value pairs loaded from `modules.d/<name>.conf` via `ConfigMgr::LoadAdditionalDir` (Phase 0 §Config), already stripped of the `Module.<Name>.` prefix enforced on the core side. Module never reads core's config files directly — this message is its only config source, preserving process isolation. If the module needs to react to a config reload at runtime, core re-sends `ModuleConfig` on reload (module should treat each receipt as the full current config, not a diff).

### Notify vs. Request/Reply split

- **`EventNotify`** (fire-and-forget): `PlayerScript::OnLogin/OnLogout`, `AccountScript::OnAccountLogin`, `WorldScript::OnStartup/OnShutdown`. Core does not wait for or expect a reply.
- **`EventRequest`/`EventReply`** (core needs a result): `CommandScript` (chat command → result text/success-fail is the entire point of the hook). Bounded timeout **200ms default** — on timeout, core treats it as a module failure, returns a generic "module did not respond" error to the triggering player/system, and logs it. (Revisit the exact number once real latency is measured; not a hard requirement to finalize now.)

### Initial hook subset (v1)

`PlayerScript::OnLogin`, `PlayerScript::OnLogout`, `CommandScript`, `WorldScript::OnStartup`, `WorldScript::OnShutdown`, `AccountScript::OnAccountLogin`. Deliberately small — proves the end-to-end path (Phase 2/3) before widening to more `ScriptMgr` categories.

### Heartbeat / lifecycle

- Module sends `Heartbeat` on an interval; core's module host (Phase 2) tracks last-seen time per module, applies restart policy on silence.
- `Shutdown` sent by core before terminating a module process cleanly (config reload, server shutdown); module should flush/exit promptly.

## Tier 2 — per-tick shared-memory loop

Transport: `boost::interprocess::managed_shared_memory`, one named region per module (`trinity_module_<module_id>_tier2`), allocated at handshake accept time, sized from the negotiated bounds in `HandshakeRequest`.

### Region layout (conceptual, POD only — no pointers/STL)

```
struct Tier2RegionHeader {
  uint32 max_bots;
  uint32 max_nearby_units;
  uint32 max_actions;
  uint32 tick_interval;          // module-declared cadence, in server ticks
  std::atomic<uint64> published_snapshot_tick;
  std::atomic<uint8>  published_snapshot_buffer;  // 0 or 1
  std::atomic<uint64> published_action_tick;
  std::atomic<uint8>  published_action_buffer;    // 0 or 1
};
// Followed in memory by, each sized from max_bots/max_nearby_units/max_actions:
//   WorldTickSnapshot snapshot_buffers[2];
//   ActionQueue       action_buffers[2];
```

```
struct NearbyUnit {
  uint64 guid;
  float pos_x, pos_y, pos_z;
  uint32 health, max_health;
  bool is_hostile;
};

struct BotSnapshot {
  uint64 guid;
  float pos_x, pos_y, pos_z, orientation;
  uint32 health, max_health, mana, max_mana;
  uint64 threat_target_guid;
  uint32 spell_cooldowns[MAX_TRACKED_COOLDOWNS];  // fixed small cap, ms remaining
  uint32 nearby_count;
  // nearby[] lives in the flexible region sized by max_nearby_units, offset-addressed
};

struct WorldTickSnapshot {
  uint64 tick_number;
  uint32 bot_count;
  // bots[] sized by max_bots, offset-addressed from region header
};

enum ActionType { CAST_SPELL, MOVE_TO, ATTACK_TARGET, FOLLOW, STOP };

struct BotAction {
  uint64 bot_guid;
  ActionType type;
  uint32 spell_id;
  uint64 target_guid;
  float dest_x, dest_y, dest_z;
};

struct ActionQueue {
  uint64 tick_number;  // which snapshot this responds to (may lag — see below)
  uint32 action_count;
  // actions[] sized by max_actions, offset-addressed
};
```

### Synchronization — lock-free double buffer, non-blocking

- **Core → module (snapshot)**: core writes a full `WorldTickSnapshot` into the *unpublished* buffer slot (`1 - published_snapshot_buffer`), then atomically updates `published_snapshot_tick` and flips `published_snapshot_buffer`. Module always reads whichever buffer is currently published — never a half-written one. Core never waits on the module here.
- **Module → core (actions)**: same pattern in reverse. Module writes into its unpublished action buffer, flips the atomic index + tick. **Core's tick loop never blocks waiting for a fresh one** — it just reads whatever `ActionQueue` is currently published and applies it. If the module is slow, `published_action_tick` lags behind the current server tick; core applies stale-but-valid actions rather than stalling (per Phase 0 non-blocking decision). No semaphores, no condition variables needed on the hot path — pure atomic publish/consume.
- **Cadence**: core's module host only performs a snapshot write for a given module every `tick_interval` server ticks (module-declared at handshake) — not every single tick, reducing shared-memory write volume for modules that don't need maximum precision.

### Staleness handling

- If `published_action_tick` is older than some operator-configurable threshold (e.g. N ticks behind current), core's module host should log a warning (module may be hung/overloaded) but still **not** block — continues applying the latest available actions or treats the module as idle for that bot if nothing has ever been published.
- Heartbeat (Tier 1) is the actual liveness signal for restart decisions, not Tier 2 staleness — a module could be Tier-2-slow but still alive and heartbeating; only sustained heartbeat silence triggers the Phase 2 restart policy.

## Deferred from this phase

- Exact `tick_interval` units/default value, exact heartbeat interval/timeout numbers, and `MAX_TRACKED_COOLDOWNS` cap — pick concrete defaults during Phase 2 implementation once real measurements are available; not blocking for the protocol shape itself.
- Any additional Tier 1 hook categories beyond the initial 6 — widen only after Phase 3's end-to-end walkthrough proves the pattern.

## Next action

Phase 2: implement the core-side `ModuleMgr`/module host using this protocol shape — process spawn/handshake/heartbeat/restart policy, Tier 1 socket server, Tier 2 shared-memory allocation per the header layout above.
