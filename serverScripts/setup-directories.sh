#!/usr/bin/env bash
# Set up the Forever directory tree, users, groups and access rights, as in layout.md.
# Run as root:  sudo bash setup-directories.sh [--check] [--admin <login>]
#
#   --check          report what differs from the layout, change nothing
#   --admin <login>  add a login to forever-ops (admins run service actions through sudo)
#
# Safe to re-run. It sets owner, group and mode only; it never changes file contents,
# and it never writes passwords. It does not replace create-users.sh, which resets the
# data folder to 0755 and must not be run on this server.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

ROOT=/srv/forever
CHECK=0
ADMIN=""
while (( $# )); do
  case "$1" in
    --check) CHECK=1 ;;
    --admin) ADMIN="${2:?--admin needs a login name}"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

changes=0
note() { echo "$*"; }

# --- users and groups -------------------------------------------------------

ensure_group() {
  local grp="$1"
  if getent group "$grp" >/dev/null; then
    note "ok       group $grp"
  else
    changes=$((changes + 1))
    if (( CHECK )); then note "MISSING  group $grp"; return; fi
    groupadd --system "$grp"
    note "created  group $grp"
  fi
}

ensure_user() {
  local name="$1" comment="$2"
  if id -u "$name" >/dev/null 2>&1; then
    note "ok       user $name"
  else
    changes=$((changes + 1))
    if (( CHECK )); then note "MISSING  user $name"; return; fi
    useradd --system --no-create-home --home-dir "$ROOT" --shell /usr/sbin/nologin \
      --comment "$comment" "$name"
    note "created  user $name"
  fi
}

ensure_member() {
  local name="$1" grp="$2"
  if [[ " $(id -nG "$name" 2>/dev/null) " == *" $grp "* ]]; then
    note "ok       $name in $grp"
  else
    changes=$((changes + 1))
    if (( CHECK )); then note "DIFFERS  $name is not in $grp"; return; fi
    usermod -aG "$grp" "$name"
    note "added    $name to $grp"
  fi
}

ensure_group forever-data
ensure_group forever-ops
ensure_group forever-prod
ensure_group forever-sim
ensure_group forever-backup
ensure_group forever-build

ensure_user forever-build  "Forever build (source and compile only)"
ensure_user forever-prod   "Forever prod worldserver and its modules"
ensure_user forever-sim    "Forever sim worldserver and its modules"
ensure_user forever-backup "Forever DB backups (read-only DB user, see notes)"

ensure_member forever-build forever-data
ensure_member forever-prod forever-data
ensure_member forever-sim forever-data
ensure_member forever-backup forever-ops

if [[ -n "$ADMIN" ]]; then
  id -u "$ADMIN" >/dev/null 2>&1 || { echo "no such login: $ADMIN" >&2; exit 1; }
  ensure_member "$ADMIN" forever-ops
fi

# --- directories ------------------------------------------------------------

# ensure_dir <owner> <group> <mode> <path>...
# Creates the directory if missing, or fixes owner, group and mode if they differ.
ensure_dir() {
  local owner="$1" group="$2" mode="$3"
  shift 3
  local path
  for path in "$@"; do
    local want="$owner:$group ${mode#0}"
    if [[ ! -d "$path" ]]; then
      changes=$((changes + 1))
      if (( CHECK )); then note "MISSING  $path (want $want)"; continue; fi
      install -d -o "$owner" -g "$group" -m "$mode" "$path"
      note "created  $path ($want)"
      continue
    fi
    local cur
    cur="$(stat -c '%U:%G %a' "$path")"
    if [[ "$cur" == "$want" ]]; then
      note "ok       $path ($want)"
    else
      changes=$((changes + 1))
      if (( CHECK )); then note "DIFFERS  $path is $cur, want $want"; continue; fi
      chown "$owner:$group" "$path"
      chmod "$mode" "$path"
      note "fixed    $path ($cur -> $want)"
    fi
  done
}

# Game data: written by the extractor (forever-build), read by prod and sim through forever-data.
ensure_dir root root 0755 "$ROOT"
ensure_dir forever-build forever-data 2750 "$ROOT/data" \
  "$ROOT/data/dbc" "$ROOT/data/maps" "$ROOT/data/vmaps" "$ROOT/data/mmaps" "$ROOT/data/gt"

# Game data files: group-readable only, so forever-data members can read them and nobody else can.
if [[ -d "$ROOT/data" ]]; then
  bad_group=$(find "$ROOT/data" -type f ! -group forever-data | wc -l)
  bad_mode=$(find "$ROOT/data" -type f ! -perm 0640 | wc -l)
  if (( bad_group + bad_mode == 0 )); then
    note "ok       game data files (group forever-data, 0640)"
  elif (( CHECK )); then
    changes=$((changes + 1))
    note "DIFFERS  game data files: $bad_group wrong group, $bad_mode wrong mode"
  else
    changes=$((changes + 1))
    find "$ROOT/data" -type f ! -group forever-data -exec chgrp forever-data {} +
    find "$ROOT/data" -type f ! -perm 0640 -exec chmod 0640 {} +
    note "fixed    game data files: $bad_group group, $bad_mode mode"
  fi
fi

# Source checkouts and build trees: forever-build writes, the setgid bit keeps the group.
ensure_dir forever-build forever-build 2775 "$ROOT/src" "$ROOT/build"

# Servers: each tree belongs to its own user, and nobody else can read it.
ensure_dir forever-prod forever-prod 0750 "$ROOT/prod" "$ROOT/prod/conf" "$ROOT/prod/bin" \
  "$ROOT/prod/bin/scripts" "$ROOT/prod/lib" "$ROOT/prod/modules_runtime" \
  "$ROOT/prod/conf/worldserver.conf.d" "$ROOT/prod/conf/worldserver.conf.d/modules.d"
ensure_dir forever-sim  forever-sim  0750 "$ROOT/sim"  "$ROOT/sim/conf"  "$ROOT/sim/bin" \
  "$ROOT/sim/bin/scripts" "$ROOT/sim/lib" "$ROOT/sim/modules_runtime" \
  "$ROOT/sim/conf/worldserver.conf.d" "$ROOT/sim/conf/worldserver.conf.d/modules.d"
# Sim working folder: the worldserver writes its temp SQL here and reads the TDB base files from here.
ensure_dir forever-sim  forever-sim  0750 "$ROOT/sim/run"

# Backups: written by forever-backup, admins read through forever-ops.
ensure_dir forever-backup forever-ops 0750 "$ROOT/backups" "$ROOT/backups/db" \
  "$ROOT/backups/configs" "$ROOT/backups/manifests"

# Logs: one folder per server, owned by the server's user.
ensure_dir root root 0755 /var/log/forever
ensure_dir forever-prod forever-prod 0750 /var/log/forever/prod
ensure_dir forever-sim  forever-sim  0750 /var/log/forever/sim

# Secrets: outside the tree, root-owned. Each env file belongs to one service group.
ensure_dir root root 0700 /etc/forever
ensure_dir root root 0750 /etc/forever/credentials

check_secret() {
  local file="$1" grp="$2"
  local path="/etc/forever/credentials/$file"
  if [[ ! -f "$path" ]]; then
    note "WARN     $path missing (create it with the values for this server)"
    return
  fi
  local want="root:$grp 640" cur
  cur="$(stat -c '%U:%G %a' "$path")"
  if [[ "$cur" == "$want" ]]; then
    note "ok       $path ($want)"
  else
    changes=$((changes + 1))
    if (( CHECK )); then note "DIFFERS  $path is $cur, want $want"; return; fi
    chown "root:$grp" "$path"
    chmod 0640 "$path"
    note "fixed    $path ($cur -> $want)"
  fi
}
check_secret prod-db.env forever-prod
check_secret sim-db.env forever-sim
check_secret backup-db.env forever-backup

echo
if (( CHECK )); then
  echo "check finished: $changes difference(s). Nothing was changed."
  (( changes == 0 ))
else
  echo "done: $changes change(s) made."
fi
