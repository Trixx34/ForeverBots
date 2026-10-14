#!/usr/bin/env bash
# Install the Forever prod and sim worldserver systemd units.
# Run as root:  sudo bash install-services.sh [--check]
#
# What it does:
#   1. checks the users, groups, credential files, binaries and configs the units need
#   2. creates the log folders (owned by the service users) if they are missing
#   3. copies systemd/*.service to /etc/systemd/system (an existing, different unit is kept as *.bak)
#   4. runs systemd-analyze verify and daemon-reload
#
# It does NOT enable or start anything. Prod stays off until the owner approves the start window.
# With --check it only runs the checks (step 1) and changes nothing.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

HERE="$(cd "$(dirname "$0")" && pwd)"
UNIT_SRC="$HERE/systemd"
UNIT_DST=/etc/systemd/system
CRED_DIR=/etc/forever/credentials
ROOT=/srv/forever

problems=0
warn() { echo "WARN: $*" >&2; problems=$((problems + 1)); }
fail() { echo "FAIL: $*" >&2; problems=$((problems + 1)); }
ok()   { echo "ok:   $*"; }

# --- 1. prerequisites -------------------------------------------------------

for grp in forever-prod forever-sim forever-data; do
  getent group "$grp" >/dev/null && ok "group $grp" || fail "group $grp missing (see layout)"
done
for u in forever-prod forever-sim; do
  id "$u" >/dev/null 2>&1 && ok "user $u" || fail "user $u missing (see layout)"
done

# Credential files: root:<service group>, mode 0640 (the layout's rule).
check_env() {
  local file="$1" grp="$2"
  if [[ ! -f "$CRED_DIR/$file" ]]; then
    fail "$CRED_DIR/$file missing"
    return
  fi
  local owner mode
  owner="$(stat -c '%U:%G' "$CRED_DIR/$file")"
  mode="$(stat -c '%a' "$CRED_DIR/$file")"
  if [[ "$owner" == "root:$grp" && "$mode" == "640" ]]; then
    ok "$file ($owner, $mode)"
  else
    warn "$file is $owner mode $mode, expected root:$grp mode 640"
  fi
  # The core reads DB strings from TC_<KEY> environment variables (Config.cpp, IniKeyToEnvVarKey).
  for key in TC_LOGIN_DATABASE_INFO TC_WORLD_DATABASE_INFO TC_CHARACTER_DATABASE_INFO TC_HOTFIX_DATABASE_INFO; do
    grep -q "^$key=" "$CRED_DIR/$file" || warn "$file has no $key line"
  done
}
check_env prod-db.env forever-prod
check_env sim-db.env forever-sim

# Binaries and configs.
for svc in prod sim; do
  if [[ -x "$ROOT/$svc/bin/worldserver" ]]; then
    ok "$ROOT/$svc/bin/worldserver"
  else
    warn "$ROOT/$svc/bin/worldserver missing: the unit will fail until it is installed"
  fi
  if [[ -r "$ROOT/$svc/conf/worldserver.conf" ]]; then
    ok "$ROOT/$svc/conf/worldserver.conf"
  else
    warn "$ROOT/$svc/conf/worldserver.conf missing or not readable by root"
  fi
done

# Game data must be there for both servers.
[[ -d "$ROOT/data/dbc" ]] && ok "$ROOT/data/dbc" || warn "$ROOT/data/dbc missing"

# Prod Battle.net: binary, config, and the certificate pair from create-bnet-certs.sh.
for f in bin/bnetserver conf/bnetserver.conf conf/bnetserver.fullchain.pem conf/bnetserver.key.pem; do
  if [[ -e "$ROOT/prod/$f" ]]; then
    ok "$ROOT/prod/$f"
  else
    warn "$ROOT/prod/$f missing: forever-prod-bnet will not start until it is installed"
  fi
done
if [[ -f "$ROOT/prod/conf/bnetserver.key.pem" ]]; then
  owner="$(stat -c '%U:%G %a' "$ROOT/prod/conf/bnetserver.key.pem")"
  [[ "$owner" == "forever-prod:forever-prod 640" || "$owner" == "forever-prod:forever-prod 600" ]] \
    && ok "bnetserver.key.pem ($owner)" || warn "bnetserver.key.pem is $owner, expected forever-prod 640"
fi

if (( CHECK_ONLY )); then
  echo
  echo "check finished: $problems problem(s). Nothing was changed."
  exit $(( problems > 0 ))
fi

# --- 2. log folders ---------------------------------------------------------

for svc in prod sim; do
  dir="/var/log/forever/$svc"
  install -d -o "forever-$svc" -g "forever-$svc" -m 0750 "$dir"
  ok "$dir (forever-$svc, 0750)"
done

# --- 3. copy units ----------------------------------------------------------

for unit in forever-prod.service forever-prod-bnet.service forever-sim.service; do
  src="$UNIT_SRC/$unit"
  dst="$UNIT_DST/$unit"
  if [[ -e "$dst" ]] && ! cmp -s "$src" "$dst"; then
    cp -a "$dst" "$dst.bak"
    echo "kept existing $dst as $dst.bak"
  fi
  install -m 0644 -o root -g root "$src" "$dst"
  ok "installed $dst"
done

# --- 4. verify and reload ---------------------------------------------------

# Missing binaries are reported by the check above; verify here only looks at unit syntax.
systemd-analyze verify "$UNIT_DST/forever-prod.service" "$UNIT_DST/forever-prod-bnet.service" "$UNIT_DST/forever-sim.service" 2>&1 \
  | grep -v -E 'CPUAccounting|is not executable|No such file or directory' || true

systemctl daemon-reload
ok "systemctl daemon-reload"

echo
echo "installed. Nothing is enabled or started. Next steps:"
echo "  sim start test:   sudo systemctl start forever-sim && journalctl -u forever-sim -f"
echo "  sim on boot:      sudo systemctl enable forever-sim   (only when you want it to start at boot)"
echo "  prod:             leave off until the owner approves the window: sudo systemctl start forever-prod"
echo
echo "problems noted: $problems"
