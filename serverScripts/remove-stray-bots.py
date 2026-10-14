#!/usr/bin/python3 -I
"""List, and on request remove, the bot characters the bots module created on the PROD realm.

Run as root:
    sudo python3 remove-stray-bots.py                       list them (changes nothing)
    sudo python3 remove-stray-bots.py --keep Name1,Name2    ... leaving the bots with these character names alone
    sudo python3 remove-stray-bots.py --delete              remove them (asks you to type DELETE BOTS first)

What counts as a bot: a game account named BOT0001, BOT0002 ... with the email bot@forever.invalid, that is not linked to a
Battle.net account and has no GM access. Characters on any other account (yours, your alts) are never touched.

Removing goes through the running worldserver's own `account delete` command (Remote Access), which removes every table row of the
characters properly. Nothing is deleted with raw SQL except the bots module's own saved rows (`bot/<guid>` in module_store).
If Remote Access is not reachable the commands are written to a file for you to paste into a GM session or the console.
Prod is only read (SELECT) except for those module_store rows, and only after the accounts are really gone.
"""
import argparse
import os
import re
import socket
import sys
import time
from pathlib import Path

for vendor in ("/opt/forever-dashboard", "/srv/forever/src/forever-dashboard"):
    if os.path.isdir(vendor + "/vendor"):
        sys.path.insert(0, vendor + "/vendor")
        sys.path.insert(0, vendor)
        break
try:
    import pymysql
except ImportError:
    sys.exit("pymysql not found (it ships in the dashboard's vendor folder)")

BOT_NAME = re.compile(r"^BOT\d{4}$")
BOT_EMAIL = "bot@forever.invalid"
PROD_ENV = "/etc/forever/credentials/prod-db.env"
RA_ENV = "/etc/forever-dashboard/credentials/prod-ra.env"


def dsn(env_file: str, key: str) -> dict:
    for line in Path(env_file).read_text().splitlines():
        if line.startswith(key + "="):
            host, port, user, password, database = line.split("=", 1)[1].strip().strip('"').split(";")
            return dict(host=host, port=int(port), user=user, password=password, database=database)
    sys.exit(f"no {key} in {env_file}")


def connect(params: dict):
    return pymysql.connect(charset="utf8mb4", cursorclass=pymysql.cursors.DictCursor, connect_timeout=5, **params)


def is_bot_account(row: dict) -> bool:
    """The rule for 'this account belongs to the bots module'. Every part must hold."""
    return (bool(BOT_NAME.match(str(row.get("username", "")))) and row.get("email") == BOT_EMAIL
            and not row.get("battlenet_account") and not row.get("gm_level"))


def pick_accounts(accounts: list[dict], characters_by_account: dict[int, list[dict]], keep_names: set[str]):
    """(to_remove, kept): bot accounts split by whether one of their characters is on the keep list (case-insensitive)."""
    keep = {n.lower() for n in keep_names}
    remove, kept = [], []
    for acc in accounts:
        if not is_bot_account(acc):
            continue
        chars = characters_by_account.get(acc["id"], [])
        (kept if any(c["name"].lower() in keep for c in chars) else remove).append(acc)
    return remove, kept


def delete_command(account_name: str) -> str:
    if not BOT_NAME.match(account_name):
        raise ValueError(f"refusing to build a delete command for {account_name!r}")
    return f"account delete {account_name}"


def ra_ready(env_file: str):
    """(user, password) from the Remote Access credentials file, or None if it is missing or still the template."""
    try:
        values = dict(line.split("=", 1) for line in Path(env_file).read_text().splitlines() if "=" in line and not line.startswith("#"))
    except OSError:
        return None
    user, password = values.get("RA_USER", "").strip(), values.get("RA_PASSWORD", "").strip()
    if not user or user.upper().startswith("CHANGE_ME") or password.upper().startswith("CHANGE_ME"):
        return None
    return user, password


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--delete", action="store_true")
    ap.add_argument("--keep", default="", help="comma-separated character names to keep")
    ap.add_argument("--yes", action="store_true", help="do not ask for the typed confirmation")
    ap.add_argument("--ra-port", type=int, default=3443)
    ap.add_argument("--commands-file", default="/root/stray-bots-commands.txt")
    ap.add_argument("--prod-env", default=PROD_ENV)
    ap.add_argument("--ra-env", default=RA_ENV)
    args = ap.parse_args()
    keep_names = {n.strip() for n in args.keep.split(",") if n.strip()}

    auth = connect(dsn(args.prod_env, "TC_LOGIN_DATABASE_INFO"))
    chars = connect(dsn(args.prod_env, "TC_CHARACTER_DATABASE_INFO"))
    try:
        with auth.cursor() as cur:
            cur.execute("SELECT a.id, a.username, a.email, a.battlenet_account, a.online,"
                        " (SELECT MAX(SecurityLevel) FROM account_access x WHERE x.AccountID = a.id) AS gm_level"
                        " FROM account a WHERE a.username LIKE 'BOT%' ORDER BY a.username")
            accounts = cur.fetchall()
        bots = [a for a in accounts if is_bot_account(a)]
        by_account: dict[int, list[dict]] = {}
        if bots:
            with chars.cursor() as cur:
                cur.execute("SELECT guid, account, name, level, online FROM characters WHERE account IN (%s) ORDER BY account, guid"
                            % ",".join(["%s"] * len(bots)), tuple(b["id"] for b in bots))
                for c in cur.fetchall():
                    by_account.setdefault(c["account"], []).append(c)
    finally:
        auth.close()
        chars.close()

    remove, kept = pick_accounts(accounts, by_account, keep_names)
    skipped = [a for a in accounts if not is_bot_account(a)]
    print(f"Bot accounts on prod: {len(bots)} ({len(remove)} to remove, {len(kept)} kept)")
    for acc in remove + kept:
        tag = "KEEP  " if acc in kept else "remove"
        names = ", ".join(f"{c['name']} (L{c['level']}{', online' if c['online'] else ''})" for c in by_account.get(acc["id"], [])) or "no characters"
        print(f"  {tag} {acc['username']:<8} id {acc['id']:<6} {names}")
    for acc in skipped:
        print(f"  NOT A BOT: account {acc['username']} (id {acc['id']}) starts with BOT but does not match the bot rule; left alone")
    if not args.delete or not remove:
        if not args.delete:
            print("\nNothing was changed. Add --delete to remove the accounts marked 'remove'.")
        return 0

    if not args.yes:
        print(f"\nThis removes {len(remove)} account(s) and all their characters from the LIVE realm. There is no undo besides a database backup.")
        if input("Type DELETE BOTS to continue: ").strip() != "DELETE BOTS":
            print("Cancelled.")
            return 1

    commands = [delete_command(a["username"]) for a in remove]
    creds = ra_ready(args.ra_env)
    from dashboard.ra import RaClient, RaError          # noqa: E402  (path set above)
    failed = []
    if creds:
        client = RaClient("127.0.0.1", args.ra_port, creds[0], creds[1], timeout=20)
        for cmd in commands:
            try:
                reply = client.run(cmd)
                print(f"  {cmd}: {reply or 'sent'}")
            except RaError as e:
                print(f"  {cmd}: FAILED ({e})")
                failed.append(cmd)
            time.sleep(0.3)
    else:
        failed = commands
        print("\nRemote Access is not set up for prod (no filled-in " + args.ra_env + ").")
    if failed:
        Path(args.commands_file).write_text("\n".join(failed) + "\n")
        os.chmod(args.commands_file, 0o600)
        print(f"\nRun these in a GM session (put a dot in front) or the worldserver console. They are also in {args.commands_file}:")
        for cmd in failed:
            print("   ." + cmd)

    # Verify against the database, and only then clear the module's saved rows for the characters that are really gone.
    time.sleep(1)
    auth = connect(dsn(args.prod_env, "TC_LOGIN_DATABASE_INFO"))
    try:
        with auth.cursor() as cur:
            cur.execute("SELECT username FROM account WHERE username IN (%s)" % ",".join(["%s"] * len(remove)), tuple(a["username"] for a in remove))
            left = {r["username"] for r in cur.fetchall()}
    finally:
        auth.close()
    gone = [a for a in remove if a["username"] not in left]
    print(f"\nRemoved: {len(gone)} of {len(remove)}." + (f" Still there: {', '.join(sorted(left))}" if left else ""))
    guids = [c["guid"] for a in gone for c in by_account.get(a["id"], [])]
    if guids:
        try:
            world = connect(dsn(args.prod_env, "TC_WORLD_DATABASE_INFO"))
            with world.cursor() as cur:
                cur.execute("SELECT COUNT(*) AS n FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'module_store'")
                if cur.fetchone()["n"]:
                    n = cur.execute("DELETE FROM module_store WHERE module = 'forever-bots' AND `key` IN (%s)" % ",".join(["%s"] * len(guids)),
                                    tuple(f"bot/{g}" for g in guids))
                    world.commit()
                    print(f"Cleared {n} saved bot row(s) of the bots module.")
                else:
                    print("module_store does not exist on prod: nothing to clear.")
            world.close()
        except pymysql.MySQLError as e:
            print(f"Could not clear the module's saved rows ({e.args[1] if len(e.args) > 1 else e}). Run on the prod world database:\n"
                  "   DELETE FROM module_store WHERE module = 'forever-bots' AND `key` LIKE 'bot/%';")
    return 1 if left else 0


if __name__ == "__main__":
    if os.geteuid() != 0 and "--prod-env" not in sys.argv:
        sys.exit("run as root (it reads the database login files in /etc/forever/credentials)")
    sys.exit(main())
