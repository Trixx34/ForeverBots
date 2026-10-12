# Module System — SDK Scope

Status: scoping drafted, not yet implemented.
Builds on all prior docs (`00`-`10`). Supersedes the single `sdk_version` field used in `05` with the `protocol_version`/`sdk_package_version` split below.

## Versioning model

Two independent axes:

- **`protocol_version`** (coarse integer/string, e.g. `"1"`) — the Tier 1 wire schema + Tier 2 shared-memory layout. Bumps **only** on breaking changes to either. This is what core's handshake exact-matches against (Phase 0 §3) — a module declaring `protocol_version: "1"` against a core compiled for protocol `"2"` is rejected regardless of SDK package version.
- **SDK package version** (semver, e.g. `"0.3.1"`) — the SDK repo's own release tag: client library code, build scripts, examples, docs. Can bump freely for bugfixes/improvements without changing `protocol_version`, as long as the release still implements the same protocol. Multiple SDK package versions can legitimately implement the same `protocol_version`.
- A given SDK package release embeds exactly one `protocol_version` it implements (a compile-time constant, `include/forever_module/protocol_version.h`) — a module doesn't declare `protocol_version` by hand, it's derived from whichever SDK package version it pinned.

## Repo location

**Separate standalone repo** (e.g. `github.com/Trixx34/forever-module-sdk`), independently tagged — matches the Phase 0 §2b decision (pinned versioned package, not submodule-sourced from core). Module authors `FetchContent` a specific tag without needing the core source tree present for the SDK itself (though per Phase 0 §2, the *module's* source still lives in `modules/<name>/` inside a core checkout at build time — only the SDK dependency is fetched externally).

## Repo layout

```
forever-module-sdk/
  CMakeLists.txt
  proto/
    envelope.proto        # Envelope oneof wrapping everything below
    handshake.proto        # HandshakeRequest/Response, ModuleConfig, Heartbeat, Shutdown
    events.proto           # EventNotify, EventRequest, EventReply, AuraChangeEvent + other event payloads
    control.proto          # ControlRequest, ControlResponse, ReleaseControl, ControlRevoked
  include/forever_module/
    protocol_version.h      # compile-time PROTOCOL_VERSION constant
    tier2_layout.h           # POD structs: Tier2RegionHeader, BotSnapshot, NearbyUnit, WorldTickSnapshot, BotAction, ActionType, ActionQueue
    module_client.h          # ModuleClient — Tier 1 client
    tier2_session.h          # Tier2Session — Tier 2 shared-memory wrapper
  src/
    module_client.cpp
    tier2_session.cpp
  cmake/
    GenerateProto.cmake      # protoc invocation, wired into the SDK's own CMakeLists
  examples/
    minimal-module/          # smallest possible working module using this SDK — not the full Phase 5 reference-bot, just a connect+handshake+one-hook skeleton
  CHANGELOG.md                # tracks sdk_package_version bumps and explicitly calls out when protocol_version changes
  README.md
```

## Public API surface (`ModuleClient`)

```cpp
class ModuleClient {
public:
    explicit ModuleClient(ModuleManifest manifest);  // name, declared tier1 hooks, tier2 request bounds

    bool Connect(std::string const& endpoint);  // named pipe / unix socket path, passed via argv at spawn (Phase 2 §2)
    bool Handshake(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    void OnConfig(std::function<void(std::unordered_map<std::string, std::string> const&)> cb);
    void OnNotify(std::string const& hookName, std::function<void(EventNotify const&)> cb);
    void OnRequest(std::string const& hookName, std::function<EventReply(EventRequest const&)> cb);
    void OnControlRevoked(std::function<void(uint64_t botGuid, std::string const& reason)> cb);

    ControlResult RequestControl(uint64_t botGuid /* 0 = spawn new */, SpawnParams const& params = {});
    void ReleaseControl(uint64_t botGuid);

    Tier2Session* AttachTier2();  // only valid if manifest.tier2.participates was granted at handshake

    void Run();       // blocking main loop: polls io_context, dispatches heartbeats + callbacks
    void Shutdown();  // graceful disconnect
};

class Tier2Session {
public:
    WorldTickSnapshot const& ReadLatestSnapshot();        // non-blocking, reads whichever buffer is currently published (Phase 1 §Tier 2 sync)
    void PublishActions(ActionQueue const& actions);      // writes to the module's unpublished buffer, flips the atomic index
};
```

- `Run()` owns the module's event loop — hides the `boost::asio`/`boost::interprocess` details from module authors entirely; they only write callbacks and read/write POD structs.
- `ControlResult` carries `granted`/`bot_guid`/`reason`, mirroring `ControlResponse` from Phase 2 §6.
- Deliberately **no** raw socket/shared-memory accessors exposed publicly — keeps module authors from needing protocol knowledge to write a module, and keeps the wire format free to evolve inside `protocol_version` without breaking the public API shape (only the implementation behind `ModuleClient`/`Tier2Session` would need to change on a protocol bump, ideally not the call sites in module code, though a `protocol_version` bump may still require module-code changes if the actual data model changed — the API stability goal is about the *shape*, not a guarantee of zero required module changes across protocol bumps).

## Build output & dependencies

- SDK compiles to a static library target (e.g. `forever_module_sdk`) that a module's `CMakeLists.txt` links via `target_link_libraries(my-module PRIVATE forever_module_sdk)` — gets the client lib, generated protobuf code, and transitively the `Boost`/`Protobuf` dependencies without the module author needing to `find_package` them separately.
- SDK's own `CMakeLists.txt` handles `find_package(Protobuf)`, `find_package(Boost COMPONENTS system)` (needed by `boost::asio` in some configs), and the `protoc` codegen step via `cmake/GenerateProto.cmake`.
- Generated protobuf C++ code: generated at **SDK build time** (not committed to the repo) — keeps the repo clean and avoids generated-code drift from the `.proto` sources; requires `protoc` available wherever a module is built (reasonable assumption for a C++ build toolchain, consistent with `boost`/`protobuf` already being core build dependencies per the investigation).

## Examples shipped with the SDK

- `examples/minimal-module/`: smallest possible skeleton — connect, handshake, one `OnNotify` hook (e.g. `WorldScript.OnStartup` logging a line), no Tier 2. This is the actual starting point the Phase 3 "Hello Module" walkthrough template repo forks from.
- The fuller Phase 5 reference-bot module (attack/follow/stop decision loop, Tier 2 participation) is a **separate** repo built *using* this SDK, not shipped inside the SDK itself — keeps the SDK itself minimal and generic, not bot-specific.

## Open items deferred from this scoping pass

- Exact `protoc`/`Protobuf` minimum version pin — an implementation-time detail once the SDK's `CMakeLists.txt` is actually written, not a scoping decision.
- Whether `ModuleClient`/`Tier2Session` should be header-only (simpler for `FetchContent` consumers, no separate link step) vs. compiled static lib (faster module rebuilds, smaller module build times) — a build-ergonomics tradeoff to settle during implementation, not architecturally significant either way.
- CI/release process for the SDK repo itself (tagging, changelog discipline) — operational setup, not a design question.

## Next action

This completes the design/scoping arc. Remaining work is implementation, roughly in this order: (1) SDK repo scaffolding per this doc, (2) core-side `ModuleMgr` (doc `04`), (3) `BuildModules.cmake` + template repo (doc `05`), (4) Phase 5's reference-bot module (doc `07`) as the acceptance test, (5) the bot subsystem migration itself (docs `08`-`10`) once Phase 5 passes.
