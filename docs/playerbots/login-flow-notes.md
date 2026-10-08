# Phase 1 de-risking: how real player login actually works here

Read directly from `src/server/game/Handlers/CharacterHandler.cpp` and
`src/server/game/Server/WorldSession.{h,cpp}` on this fork, to figure out
exactly which parts of TrinityCore's login path a bot needs to replicate vs.
can skip entirely. No code changes yet — this is the research Phase 1 will be
built on.

## The real call chain (client-driven parts, all skippable for a bot)

1. `WorldSession::HandlePlayerLoginOpcode` (`CharacterHandler.cpp:1232`) —
   validates the character belongs to the account, then sends
   `SMSG_CONNECT_TO_INSTANCE` and waits.
2. A connect-to-instance network round trip (this fork is TrinityCore master,
   which retained the modern multi-process-style handshake even though it
   serves a Classic client) ends with the client reconnecting and triggering:
3. `WorldSession::HandleContinuePlayerLogin` (`CharacterHandler.cpp:1255`) —
   builds a `LoginQueryHolder(accountId, guid)`, calls `Initialize()`, sends
   `SMSG_RESUME_COMMS`, registers a time-sync counter, and queues the holder
   on `CharacterDatabase.DelayQueryHolder(holder)`. When that async DB query
   completes, it calls `HandlePlayerLogin(holder)`.

**None of this is needed for a bot.** There's no real client, so there's
nothing to hand a connect-to-instance ticket to and nothing to wait on. A bot
login function can skip straight to building and running the
`LoginQueryHolder` itself.

## The part that actually matters: `HandlePlayerLogin` (`CharacterHandler.cpp:1298`)

This is the one function to study closely, because it mixes two very
different kinds of calls:

**World-state calls — must be replicated (not packet-dependent, this is what
actually makes the character real):**
- `new Player(this)` then `pCurrChar->LoadFromDB(playerGuid, holder)` (line
  1302, 1307) — the core DB load, driven by the `LoginQueryHolder` built
  above.
- `pCurrChar->SetVirtualPlayerRealm(...)` (1322)
- `pCurrChar->GetMotionMaster()->Initialize()` (1327)
- This fork's own Classic spawn-height fix for sniff-imported start positions
  (1424-1430) — relevant since bot characters may well be freshly created
  with the same -15000 Z placeholder.
- `pCurrChar->GetMap()->AddPlayerToMap(pCurrChar)` (1432) — the actual
  "enters the world" call; falls back to homebind/go-back-trigger teleport if
  the stored location is invalid.
- `ObjectAccessor::AddObject(pCurrChar)` (1440) — registers the character so
  other systems (lookups, `.lookup player`, etc.) find it.
- Guild field restore/clear (1370-1383) and `guild->SendLoginInfo(this)`
  (1443-1453) — only matters if the bot is meant to show as a guild member.
- `pCurrChar->RemoveAurasWithInterruptFlags(SpellAuraInterruptFlags::Login)`
  (1455)
- `pCurrChar->UpdateClassicLegacyUnlock()` (1460) — **fork-specific**, keep it.
- Two DB writes marking the character/account online: `CHAR_UPD_CHAR_ONLINE`
  and `LOGIN_UPD_ACCOUNT_ONLINE` (1466-1471) — worth keeping so "is this
  character online" stays correct for friends lists, `.lookup`, guild online
  status, etc.

**Packet/client-communication calls — skip these entirely, don't just rely on
falling through:**
`SendAccountDataTimes`, `SendTutorialsData`, `SendDungeonDifficulty`,
`SendRaidDifficulty`, the `LoginVerifyWorld` packet, `LoadAccountData`,
`SendFeatureSystemStatus`, the MOTD packet, `SendSetTimeZoneInformation`, the
`SeasonInfo` packet, `SendAuctionFavoriteList`, `BattlePetMgr`'s
`SendJournalLockStatus`, `SendInitialPacketsBeforeAddToMap` /
`SendInitialPacketsAfterAddToMap`, and the first-login cinematic/movie calls.

## Important finding: don't just reuse the stock function unmodified

`WorldSession::SendPacket` (`WorldSession.cpp:223`) already null-checks the
socket (`if (!m_Socket[conIdx])`) and returns safely — so a session built with
a null socket **will not crash** if something calls `SendPacket` on it. That's
good news in one sense (it confirms the "linkdead" session mechanism this
fork already relies on is forgiving of a missing socket), but:

> Every skipped `SendPacket` call logs a `TC_LOG_ERROR("network.opcode",
> "Prevented sending of ... to non existent socket ...")` line.

If a bot login reused `HandlePlayerLogin` unmodified, every bot would spam
that error dozens of times per login, and potentially again on an ongoing
basis for any gameplay code path that calls `SendPacket` during normal ticks
(aura updates, spell visuals, etc. — not yet audited, but worth assuming many
exist). **Conclusion: Phase 1 needs its own trimmed bot-login function that
omits the client-only calls above, not a thin wrapper around the stock
function.** This also means the "Phase 2+" combat/movement work should budget
time to audit which other frequently-called functions assume a live socket
and would need the same treatment, rather than assuming the null-check alone
makes it safe.

## Constructing the session itself

`WorldSession`'s constructor (`WorldSession.h:985`) takes
`std::shared_ptr<WorldSocket>&& sock` — a plain empty/null `shared_ptr` is a
valid, first-class argument; no special "headless session" constructor
overload is needed. The rest of the constructor args (account id, name,
security level, expansion, locale, build, etc.) are just the bot's own
account/character data, same as any real session.

## What Phase 1 should actually build, concretely

1. A `BotMgr::LoginBot(ObjectGuid guid)` that: looks up the bot's account id,
   constructs a `WorldSession` with a null socket, builds + initializes a
   `LoginQueryHolder` for that guid, runs it through
   `CharacterDatabase.DelayQueryHolder` (or a synchronous equivalent — worth
   checking whether a sync query path is cleaner for bots than threading
   through the async callback machinery built for real clients), and on
   completion runs a **new, trimmed** function — not `HandlePlayerLogin`
   itself — that performs only the "must replicate" list above.
2. A symmetric `BotMgr::LogoutBot(...)` doing the inverse (remove from map,
   `ObjectAccessor::RemoveObject`, mark offline in both DBs, clean up the
   session) — not researched yet, do this before writing `LoginBot` so login
   and logout are designed together.
3. Decide now how `WorldSession::Update()` (the per-session packet-processing
   tick every real session gets) applies to a bot session — it likely needs
   to run (session-level timers, aura/spell processing may be driven through
   it) but with its packet-queue-draining logic short-circuited since there's
   no socket feeding it packets. Not yet researched — do this next, before
   writing any Phase 1 code.
