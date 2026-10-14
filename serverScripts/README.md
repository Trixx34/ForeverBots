# Forever server scripts

Setup scripts for the Forever prod and sim servers on Ubuntu 26.04. Run them in the order below.
The layout they create is described in the Forever Linux migration plan (Phase 4).

## Order

| # | Script | Run as | What it does |
|---|---|---|---|
| 1 | `install-tools.sh` | root | Installs build and runtime packages (apt). MySQL 8.4 server is installed separately from the MySQL APT repo. |
| 2 | `setup-directories.sh` | root | Creates the users, groups, directories and access rights. Run with `--check` first. `--admin <login>` adds an admin to `forever-ops`. |
| 3 | `create-bnet-certs.sh` | your account | Creates the self-signed root CA and the server certificate for `trinity.actual.battle.net`. See below. |
| 4 | `install-services.sh` | root | Checks the prerequisites, then installs the systemd units (`forever-prod`, `forever-prod-bnet`, `forever-sim`). Enables and starts nothing. Run with `--check` first. |
| 5 | `start-sim-soak.sh` | root | Starts a timed bot run on the **sim**: `--bots N` (default 200), `--minutes M` (default 90), `--seed S`. Ramps bots up (50, then +50 every 5 minutes), sets the core's `Bot.Run.StopTime` so the sim shuts down cleanly at the end, and restarts `forever-sim`. `--dry-run` shows what it would write and changes nothing. Prod is never touched. |
| 6 | `copy-account-to-sim.py` | root | Copies one account (login, password hash, GM access) from the prod auth database to the sim, with new ids there. Prod is only read. Dry run unless you add `--apply`. |

Start services by hand with `systemctl start`. Prod stays off until the owner approves the start window.

## Systemd units (`systemd/`)

- `forever-prod.service`: prod worldserver. `Restart=no` until the owner approves an auto-restart policy.
- `forever-prod-bnet.service`: prod Battle.net login server. Runs as `forever-prod`. Starts after the prod worldserver.
- `forever-sim.service`: sim worldserver, bot test world. Uses the system MySQL on 127.0.0.1:3306.
- `deferred/`: not installed. `forever-sim-mysql.service` is a separate MySQL 8.4 instance on port 3307. Kept for later.

Database logins come from `/etc/forever/credentials/*.env`, not from the server configs. The core reads each value from an environment variable named `TC_<KEY>` (for example `TC_LOGIN_DATABASE_INFO`).

## Battle.net certificates (`create-bnet-certs.sh`)

The prod Battle.net server needs a certificate for `trinity.actual.battle.net`. The script creates a self-signed CA and a server certificate signed by it.

```
bash create-bnet-certs.sh              # writes to ~/forever-certs (mode 0700)
bash create-bnet-certs.sh --out DIR    # writes to DIR instead
```

It writes four files:

| File | Goes to | Notes |
|---|---|---|
| `root.crt` | Clients (Windows trust store) | Public. Install it as a trusted root on each client. |
| `root.key` | Nowhere | CA signing key. Keep it off the server and out of backups. |
| `server.key` | Server: `/srv/forever/prod/conf/bnetserver.key.pem` | Private key. Owner `forever-prod`, mode 0640. |
| `fullchain.pem` | Server: `/srv/forever/prod/conf/bnetserver.fullchain.pem` | Server certificate, then the CA. |

- The script refuses to replace an existing `root.key`. Replacing the CA breaks every client that trusts the old `root.crt`. Use `--force` only on purpose.
- The CA is limited by a name constraint to `trinity.actual.battle.net`, so it can't sign for other names.
- Clients must resolve `trinity.actual.battle.net` to the prod server's LAN address (hosts file or DNS).

## Firewall

`ufw` is active. Current rules:

- `22/tcp` (SSH): anywhere.
- `8085/tcp` (prod worldserver): `192.168.34.0/24` only.
- `9085`, `4443`, `8878` (sim game, admin, SOAP): denied. The sim binds to 127.0.0.1 as well.
- Battle.net, `1119/tcp` and `8081/tcp`: to be opened for `192.168.34.0/24` when prod's login server goes live.

## Removed

- `create-users.sh`: replaced by `setup-directories.sh`. It reset the game data permissions and must not be run on this server.
