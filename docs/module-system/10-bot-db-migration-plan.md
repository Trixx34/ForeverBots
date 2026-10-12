# Module System — Bot DB Migration Plan

Status: plan drafted, not yet implemented.
Resolves the DB access model decision from `08-forever-bot-migration-gap-analysis.md` with concrete steps, based on direct inspection of `forever` branch SQL (`sql/bots/*.sql`, `sql/custom/auth/*rbac_bot*.sql`, `sql/custom/world/*bot_dummy*.sql`).

## Classification of every bot-related SQL artifact found

| Artifact | Database | Classification | Action |
|---|---|---|---|
| `forever-botlog-setup.sql` → `bot`, `bot_event`, `bot_event_hot`, `bot_event_hourly`, `bot_death_daily`, `bot_pos`, `bot_event_rollup` | `forever_botlog` (already its own dedicated DB!) | **Module-private** | Keep schema as-is; change which *process* connects (§1) |
| `forever-botlog-migrate-1.sql`, `-2.sql` | `forever_botlog` | Module-private | Keep as module-owned manual migration scripts (§3) |
| `forever-characters-bot-alt.sql` → `bot_alt` table | `forever_characters` (core `CharacterDatabase`!) | **Module-private data, wrong database today** | Migrate out (§2) |
| `2026_10_05_00_auth_rbac_bot.sql` / `..._rbac_bot_alt.sql` → `rbac_permissions`/`rbac_default_permissions` rows | `forever_auth` (core `AuthDatabase`) | **Core-owned** (RBAC is a generic core mechanism; this just grants the `.bot` command permission) | No migration — already compatible (confirms `08`'s "core-owned data via Tier 1" framing) |
| `2026_10_08_04_world_forever_bot_dummy.sql` → `creature_template` entry `9900001` | `forever_world` (core `WorldDatabase`) | **Core-owned content** (core's spawning/combat engine must resolve this entry regardless of which module summons it) | No migration — ships as world content like any other creature template; module references it by entry ID via a Tier 1 action (e.g. `SUMMON_CREATURE entry=9900001`) |
| `forever-db-setup.sql` | `forever_auth`/`forever_characters`/`forever_world`/`forever_hotfixes` | **Not bot-specific** — this is the fork's general server DB/user provisioning (misfiled under `sql/bots/`, unrelated to the bot subsystem itself) | Out of scope for this migration entirely |

## Important clarification on the isolation principle

Re-reading the Phase 0 trust decision: the restriction was never "modules can't do SQL at all" — it's "modules can't reach **core's** live `DatabaseWorkerPool`/connection handles" (the ones holding `LoginDatabase`/`CharacterDatabase`/`WorldDatabase` credentials). A module connecting to **its own** dedicated database, with its own credentials, using whatever client library it likes (including a plain MySQL client linked into the module executable) is fully consistent with process isolation — it's not reaching into core's address space or credentials at all. `forever_botlog` already follows this exact pattern today (see §1) — the SQL-level design was already correct; only the *connecting process* needs to change.

## 1. `forever_botlog` — already isolated, minimal migration needed

Current state (`forever-botlog-setup.sql`):
```sql
CREATE DATABASE IF NOT EXISTS forever_botlog ...;
CREATE USER IF NOT EXISTS 'forever_bot'@'<client-host>' IDENTIFIED BY 'CHANGE_ME';
GRANT ALL PRIVILEGES ON forever_botlog.* TO 'forever_bot'@'<client-host>';
```

This is **already** a dedicated DB + dedicated least-privilege user, exactly matching the Phase 0/`08` decision. No schema change needed. The only change: the connecting process moves from core's in-process `BotMgr` (today) to the module executable (post-migration).

**Steps:**
1. Operator sets `Module.<name>.Log.DB.Host`/`Port`/`User`/`Password`/`Database` in `modules.d/<name>.conf` (core-side, same plaintext-at-rest practice already used for `worldserver.conf`'s own DB credentials per the Phase 0 investigation's security touchpoints — not a new weaker link, consistent with existing precedent).
2. Core's `ConfigMgr::LoadAdditionalDir` loads it, strips the `Module.<name>.` prefix, forwards as part of the `ModuleConfig` message (Phase 1 addendum) at handshake accept.
3. Module's own code opens its own connection pool directly to `forever_botlog` using these credentials — no core code involved in bot-log writes at all after this point.
4. `BotMgr`'s current in-process bot-log write calls get removed as that logic migrates into the module (this is part of the broader "port `Bots/` subsystem code" effort from `08`, not a DB-specific step).

## 2. `bot_alt` — must move out of `CharacterDatabase`

Current state: lives in `forever_characters` (core's `CharacterDatabase`), referencing `characters.guid`. This is genuinely module-private bookkeeping ("which characters should be remembered as bots across restarts") sitting in the wrong database — a module must not have write access to `CharacterDatabase` under the process-isolation model.

**Steps:**
1. Create an equivalent `bot_alt` table in the module's own database (either `forever_botlog` or a new dedicated `forever_botmodule` schema — naming choice, not architecturally significant) with the same columns (`guid`, `account_id`, `added_at`).
2. One-time data migration: copy existing rows from `forever_characters.bot_alt` to the new module-owned table.
3. Drop `bot_alt` from `forever_characters` once copied and verified.
4. **Behavioral change**: today, core's `BotMgr` presumably queries this table directly at startup to decide which alts to revive. Post-migration, the **module** queries its own DB at startup (after handshake) for its remembered GUIDs, then issues a `ControlRequest(bot_guid)` for each one — reusing the dynamic bot-ownership flow already designed in Phase 2 §6, rather than needing any new core-side lookup logic.
5. **Edge case**: a remembered GUID's character may have been deleted since last seen. Core's `ControlResponse` should return `granted=false` with a reason (e.g. `"character not found"`) for such GUIDs; the module should then delete that stale row from its own `bot_alt` table rather than retry it indefinitely. (This reason value is a small addition to the `ControlResponse.reason` cases already specified in Phase 2 §6 — not a new message type.)

## 3. Module-owned schema versioning

`forever_botlog` is **deliberately excluded** from core's auto-updater today (confirmed by `forever-db-setup.sql`'s own comment: *"TrinityCore's auto-updater creates tables/applies SQL updates... on these 4 DBs only"* — `forever_botlog` isn't one of them). It already uses manual, hand-written `migrate-N.sql` scripts. **Keep this exact pattern post-migration** — the module ships its own numbered migration scripts, applied manually by the operator, same as today. No need to build a module-specific auto-updater; reuse the existing convention rather than inventing a new one.

## 4. RBAC and world content — explicitly confirmed, no action needed

- `COMMAND_BOT` RBAC permission (id `1000`) stays in `AuthDatabase` exactly as-is — this is core's generic permission-granting mechanism being used for one specific command, same as any other GM command. Already compatible with the Tier 1 `CommandScript` pattern (`08` §"already compatible" table).
- The training-dummy `creature_template` row (entry `9900001`) stays in `WorldDatabase` exactly as-is — it's core world content that core's own spawning/combat systems must resolve regardless of which module requests it. A module references it by entry ID through a Tier 1 action; no module-side copy or ownership needed.

## Net result

Of the 4 real SQL artifacts investigated, **2 need no changes at all** (RBAC, world dummy — already correctly core-owned), **1 needs essentially no schema change** (`forever_botlog` — already correctly isolated, just needs the connecting process to change), and **1 needs an actual migration** (`bot_alt`, ~10 lines of schema + a one-time data copy + reusing the already-designed `ControlRequest` flow for revival). This is a small, well-scoped piece of the overall migration effort — confirms the DB access model decision from `08` was the right call and is cheap to execute, unlike the larger C++ rewrite effort for the bot logic itself.

## Next action

With hooks (`09`) and DB migration (this doc) both resolved at the design level, the remaining open items from `08`'s recommended path are: (1) port the low-frequency subsystems (quest/loot/auction/gear/chat) as Tier 1 modules, and (2) execute Phase 5's reference-module acceptance test before committing to porting combat/movement. Both are implementation work, not further design — this is a reasonable point to consider the design phase complete and start actual coding, starting with whichever of (1)/(2) you want to tackle first.
