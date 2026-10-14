#!/usr/bin/python3 -I
"""Copy one account (login, password and GM access) from the prod auth database to the sim auth database.

Run as root:
    sudo python3 copy-account-to-sim.py --bnet you@example.com           look at what would be copied (changes nothing)
    sudo python3 copy-account-to-sim.py --bnet you@example.com --apply   copy it
    sudo python3 copy-account-to-sim.py --account NAME [--apply]         a game account (and the Battle.net account it belongs to)
    options: --no-gm      do not copy the GM access rows
             --list TEXT  list prod accounts whose name or email contains TEXT, then stop

Prod is only READ (SELECT). Nothing is written there. The sim gets new rows with new ids, so it can never collide with the sim's
own accounts (bots). The password hash is copied as it is, so the same password works on the sim; hashes are never printed.
Characters are not copied: the sim has its own world. Database logins come from /etc/forever/credentials/{prod,sim}-db.env.
"""
import argparse
import os
import sys
from pathlib import Path

for vendor in ("/opt/forever-dashboard/vendor", "/srv/forever/src/forever-dashboard/vendor"):
    if os.path.isdir(vendor):
        sys.path.insert(0, vendor)
        break
try:
    import pymysql
except ImportError:
    sys.exit("pymysql not found (it ships in the dashboard's vendor folder: /opt/forever-dashboard/vendor)")

BNET_COLUMNS = ["email", "srp_version", "salt", "verifier", "joindate", "locale"]
ACCOUNT_COLUMNS = ["username", "salt", "verifier", "email", "reg_mail", "joindate", "expansion", "locale", "battlenet_index"]


def dsn(env_file: str) -> dict:
    """The auth database connection from TC_LOGIN_DATABASE_INFO=host;port;user;password;database."""
    for line in Path(env_file).read_text().splitlines():
        if line.startswith("TC_LOGIN_DATABASE_INFO="):
            host, port, user, password, database = line.split("=", 1)[1].strip().strip('"').split(";")
            return dict(host=host, port=int(port), user=user, password=password, database=database)
    sys.exit(f"no TC_LOGIN_DATABASE_INFO in {env_file}")


def connect(params: dict):
    return pymysql.connect(charset="utf8mb4", cursorclass=pymysql.cursors.DictCursor, connect_timeout=5, **params)


def columns_present(cur, table: str, wanted: list[str]) -> list[str]:
    cur.execute("SELECT column_name AS c FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = %s", (table,))
    have = {r["c"] for r in cur.fetchall()}
    return [c for c in wanted if c in have]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    who = ap.add_mutually_exclusive_group()
    who.add_argument("--bnet", metavar="EMAIL")
    who.add_argument("--account", metavar="NAME")
    ap.add_argument("--list", metavar="TEXT")
    ap.add_argument("--no-gm", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--prod-env", default="/etc/forever/credentials/prod-db.env")
    ap.add_argument("--sim-env", default="/etc/forever/credentials/sim-db.env")
    args = ap.parse_args()
    if not (args.bnet or args.account or args.list):
        ap.error("give --bnet EMAIL, --account NAME, or --list TEXT")

    prod = connect(dsn(args.prod_env))
    try:
        with prod.cursor() as cur:
            if args.list:
                like = "%" + args.list.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
                cur.execute("SELECT a.id, a.username, COALESCE(b.email, a.email) AS email FROM account a LEFT JOIN battlenet_accounts b "
                            "ON b.id = a.battlenet_account WHERE a.username LIKE %s OR a.email LIKE %s OR b.email LIKE %s ORDER BY a.id LIMIT 40",
                            (like, like, like))
                for r in cur.fetchall():
                    print(f"  account {r['id']:>6}  {r['username']:<20} {r['email']}")
                return 0

            bnet = None
            if args.bnet:
                cur.execute("SELECT * FROM battlenet_accounts WHERE email = %s", (args.bnet,))
                bnet = cur.fetchone()
                if not bnet:
                    sys.exit(f"no Battle.net account {args.bnet} on prod")
                cur.execute("SELECT * FROM account WHERE battlenet_account = %s ORDER BY battlenet_index", (bnet["id"],))
                games = cur.fetchall()
            else:
                cur.execute("SELECT * FROM account WHERE username = %s", (args.account.upper(),))
                games = cur.fetchall()
                if not games:
                    sys.exit(f"no game account {args.account.upper()} on prod")
                if games[0]["battlenet_account"]:
                    cur.execute("SELECT * FROM battlenet_accounts WHERE id = %s", (games[0]["battlenet_account"],))
                    bnet = cur.fetchone()
            access = []
            if games and not args.no_gm:
                cur.execute("SELECT AccountID, SecurityLevel, RealmID, Comment FROM account_access WHERE AccountID IN (%s)"
                            % ",".join(["%s"] * len(games)), tuple(g["id"] for g in games))
                access = cur.fetchall()
    finally:
        prod.close()

    sim = connect(dsn(args.sim_env))
    try:
        with sim.cursor() as cur:
            conflicts = []
            if bnet:
                cur.execute("SELECT id FROM battlenet_accounts WHERE email = %s", (bnet["email"],))
                if cur.fetchone():
                    conflicts.append(f"the sim already has a Battle.net account {bnet['email']}")
            for g in games:
                cur.execute("SELECT id FROM account WHERE username = %s", (g["username"],))
                if cur.fetchone():
                    conflicts.append(f"the sim already has a game account {g['username']}")

            print("From prod:")
            if bnet:
                print(f"  Battle.net account  {bnet['email']}   (prod id {bnet['id']})")
            for g in games:
                lvl = [f"level {a['SecurityLevel']} realm {a['RealmID']}" for a in access if a["AccountID"] == g["id"]]
                print(f"  game account        {g['username']:<12} (prod id {g['id']})   GM: {', '.join(lvl) or 'none'}")
            if conflicts:
                print("\nNOT copied:\n  " + "\n  ".join(conflicts))
                return 1
            if not args.apply:
                print("\nDry run: nothing was changed. Add --apply to copy this to the sim (new ids there, same password).")
                return 0

            new_bnet = None
            if bnet:
                cur.execute("SELECT COALESCE(MAX(id), 0) + 1 AS n FROM battlenet_accounts")
                new_bnet = cur.fetchone()["n"]
                cols = columns_present(cur, "battlenet_accounts", BNET_COLUMNS)
                cur.execute(f"INSERT INTO battlenet_accounts (id, {', '.join(cols)}) VALUES (%s, {', '.join(['%s'] * len(cols))})",
                            (new_bnet, *[bnet[c] for c in cols]))
            id_map = {}
            cols = columns_present(cur, "account", ACCOUNT_COLUMNS)
            for g in games:
                cur.execute("SELECT COALESCE(MAX(id), 0) + 1 AS n FROM account")
                new_id = cur.fetchone()["n"]
                id_map[g["id"]] = new_id
                cur.execute(f"INSERT INTO account (id, {', '.join(cols)}, battlenet_account) VALUES (%s, {', '.join(['%s'] * len(cols))}, %s)",
                            (new_id, *[g[c] for c in cols], new_bnet or 0))
            for a in access:
                cur.execute("INSERT INTO account_access (AccountID, SecurityLevel, RealmID, Comment) VALUES (%s, %s, %s, %s)",
                            (id_map[a["AccountID"]], a["SecurityLevel"], a["RealmID"], (a["Comment"] or "")[:255]))
        sim.commit()
    except Exception:
        sim.rollback()
        raise
    finally:
        sim.close()

    print(f"\nCopied to the sim: {len(games)} game account(s)" + (", 1 Battle.net account" if bnet else "") + f", {len(access)} GM access row(s).")
    for old, new in id_map.items():
        print(f"  prod account {old} -> sim account {new}")
    print("Log in on the sim with the same name/email and password as on prod.")
    return 0


if __name__ == "__main__":
    if os.geteuid() != 0 and "--prod-env" not in sys.argv:
        sys.exit("run as root (it reads the database login files in /etc/forever/credentials)")
    sys.exit(main())
