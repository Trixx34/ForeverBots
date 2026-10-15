# Agent sudo wrappers (sim only)

These let one account (the one the coding agent runs under) control the **sim** without a password, and nothing else.
Prod is out of reach on purpose: no wrapper touches a prod unit, prod folder or prod database.

## Why wrappers

A passwordless rule on a script that the account can edit would give that account root. So the rules point at
copies under `/usr/local/sbin/forever-agent`, owned by `root:root`. Each wrapper does one fixed job, takes only
a fixed set of words (or checked numbers), and never follows symlinks in folders the account can write to.

## Install and remove

Review the files first, then:

```
sudo bash install-agent-sudo.sh --user <login>     # installs the wrappers and /etc/sudoers.d/forever-agent
sudo bash install-agent-sudo.sh --remove           # removes both
```

The sudoers text is checked with `visudo -cf` before it is installed. After changing a wrapper in the repo,
run the installer again, because the installed copies are what runs.

## What is allowed

| Command (run with `sudo /usr/local/sbin/forever-agent/...`) | What it does |
|---|---|
| `forever-sim-ctl start\|stop\|restart\|reset-failed\|status` | Controls `forever-sim` only. |
| `forever-sim-deploy conf` | Installs the sim `worldserver.conf` (refused if it, or anything in `worldserver.conf.d`, holds a `*DatabaseInfo` line, with or without leading spaces; the check and the install use the same root-owned copy) and the module settings from `/srv/forever/build/`. |
| `forever-sim-deploy modules` | Copies the staged bot module folders into `/srv/forever/sim/modules_runtime`. |
| `forever-sim-deploy binaries` | Installs the staged `worldserver`, libraries and script plugins into the sim. |
| `forever-sim-db backup` | Dumps the four `forever_test_*` databases to `/srv/forever/backups/db`. |
| `forever-sim-db reset-world\|reset-hotfixes\|reset-characters` | Drops that one test database. Refused unless the sim is stopped and an intact backup (`gzip -t`) of that database exists that is under 24 hours old and was made after the sim last stopped. |
| `forever-sim-logs export` | Copies the sim logs and bot telemetry to `/srv/forever/build/sim-logs`, readable by the calling account. |
| `forever-sim-logs tail-server` | Prints the last 300 lines of the sim's `Server.log`. |
| `forever-sim-profile [seconds]` | Samples where the sim worldserver spends CPU (`perf`, 10 to 120 seconds, default 30) and writes a report to `/srv/forever/build/sim-logs/perf-<time>.txt`. Read-only; the report is built in a root-only folder and then installed. |
| `forever-sim-soak [--bots N] [--minutes M] [--seed S] [--dry-run]` | Starts a timed bot run (`start-sim-soak.sh`). Numbers only; `--minutes` is 5 to 1380. |

## What is not allowed

- Anything on prod: units, folders, credentials, the NAS database.
- Arbitrary `mysql`, `systemctl`, `cp` or shell commands.
- Editing the wrappers or the sudoers file. Only root can.

Remove the rule when the testing is finished.
