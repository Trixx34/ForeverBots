# Environment setup — what you need to provide

What's needed to build this fork and actually run the Phase 0 `.bot hello`
test (and later phases). This is **not** something to run on this machine —
it describes what to set up on whichever machine will build/run the server.

## To build

- **A C++ build toolchain** matching this fork's TrinityCore master baseline:
  Visual Studio 2022 (MSVC) on Windows, CMake ≥ 3.24 (`cmake_minimum_required`
  in the top-level `CMakeLists.txt`), and Git.
- **OpenSSL** — not vendored; discovered via `cmake/macros/FindOpenSSL.cmake`.
  Needs a system install (or a prebuilt OpenSSL you point `OPENSSL_ROOT_DIR`
  at) matching your compiler/architecture.
- **MySQL** — the client library discovery lives in `dep/mysql`, but the
  **MySQL/MariaDB server itself is not vendored** — install MySQL Server 8.0+
  (or MariaDB) separately; it needs to be running before you create the
  databases below.
- Everything else (Boost, fmt, g3dlite, CascLib, recastnavigation, jemalloc,
  readline, zlib, SFMT, utf8cpp, etc. — see `dep/PackageList.txt`) is vendored
  under `dep/` and builds as part of the project; nothing to install for
  those.
- Build the `tools` target too (`-DTOOLS=1` or equivalent CMake option) if you
  need `mapextractor`/`vmap4extractor`/`vmap4assembler`/`mmaps_generator` —
  only required once you want a real client to see/walk around a bot, not for
  the Phase 0 command test itself.

## To run (databases)

Per [README.md](../../README.md), **not** stock TrinityCore setup — this fork
auto-applies everything through `worldserver` itself:

1. A running MySQL/MariaDB server, with credentials `worldserver`/`authserver`
   will connect with (set in their `.conf` files — `LoginDatabaseInfo` /
   `WorldDatabaseInfo` / `CharacterDatabaseInfo`).
2. Two TDB SQL dumps placed in the `worldserver` working directory:
   - `TDB_full_world_1210.26091_2026_09_09.sql`
   - `TDB_full_hotfixes_1210.26091_2026_09_09.sql`
3. `Updates.EnableDatabases = 15` (the default in this fork's configs) — on
   first start, `worldserver` applies the TDB dumps, then every file under
   `sql/custom` **automatically**, including the ~65 MB vanilla-world baseline
   converted from VMaNGOS (`sql/custom/world/2026_09_27_00_world_forever_baseline_*.sql`).
   First start takes a few minutes; nothing manual needed beyond having the
   two TDB files present and MySQL reachable.
4. If you already have a database from an older checkout (including one
   built against `vmangos_world`), you don't need to rebuild — just pull and
   start `worldserver`; it layers the new files on top. `vmangos_world` can be
   dropped afterwards.

## Config (only what differs from TrinityCore defaults)

`worldserver.conf`:
```
RealmID = 70
Expansion = 0
Network.SkipBuildAuthKeyCheck = 1
Network.EnterEncryptedModeRegionGroup = 8
```
(If worldserver stops right after "Realm running as realm ID 1", `RealmID`
isn't actually set to 70.)

`bnetserver.conf` — **only needed if you intend to log in with a real game
client**, not for the Phase 0 console/GM-command test:
```
Realm.CfgContentSetID = 137
LoginREST.ExternalAddress = trinity.actual.battle.net
LoginREST.LocalAddress = trinity.actual.battle.net
CertificatesFile = "./<your cert chain>.pem"
PrivateKeyFile = "./<your key>.pem"
```

## Only needed for visual/client testing (not for Phase 0)

The `.bot hello` command (`Console::Yes` in `cs_bot.cpp`) can be run from the
`worldserver` console with **no game client and no map/vmap/mmap data** —
it only touches the new `BotMgr` singleton. Everything below is for later
phases (an actual bot character walking around) or for verifying the fork
in-game generally:

- **A legitimate copy of the WoW Classic Beta client, build 1.60.1.70170**
  (`_classic_beta_\WowB.exe`). Needed to:
  - run this branch's `mapextractor` / `vmap4extractor` / `vmap4assembler` /
    `mmaps_generator` against it (CASC product `wow_classic_beta` — this
    fork's extractors default to that; stock TrinityCore tools default to
    `wow`/retail and will fail with "No locales detected"),
  - move the resulting `dbc`, `gt`, `maps`, `vmaps`, `mmaps` into
    `worldserver`'s `DataDir`,
  - actually log in and see a bot.
- To log in with that client: a hosts-file entry pointing
  `trinity.actual.battle.net` at the server, a self-signed CA + certificate
  for that hostname (restricted with
  `nameConstraints = critical,permitted;DNS:actual.battle.net` — never share
  the CA's private key), and the
  [Forever Launcher](https://github.com/advocaite/foreverlauncher) to point
  the client at the right portal/public key. The repack's `setup/Setup.ps1`
  has the exact `openssl` commands if you go this route.

## Minimal path to just validate Phase 0

If the only goal is confirming `.bot hello` works (the current state of this
plan), you need just: a buildable toolchain + OpenSSL, a running
MySQL/MariaDB server, the two TDB SQL files, and the `RealmID`/`Expansion`
config lines above. No game client, no map data, no certificates — connect to
the `worldserver` console directly (or a remote-console client) and run
`.bot hello`.
