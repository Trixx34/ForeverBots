#!/usr/bin/env bash
# Give an account passwordless sudo for the sim wrappers only (see README.md in this folder).
# Run as root:  sudo bash install-agent-sudo.sh [--user <login>] [--remove]
#
#   --user <login>  the account that may use the wrappers (default: the account that ran sudo)
#   --remove        delete the sudoers rule and the installed wrappers
#
# The wrappers are copied to /usr/local/sbin/forever-agent, owned by root, so the account that uses
# them cannot change what runs as root. After editing a wrapper in the repo, run this script again.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST=/usr/local/sbin/forever-agent
SUDOERS=/etc/sudoers.d/forever-agent
USER_NAME="${SUDO_USER:-}"
REMOVE=0

while (( $# )); do
  case "$1" in
    --user)   USER_NAME="${2:?--user needs a login}"; shift ;;
    --remove) REMOVE=1 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if (( REMOVE )); then
  rm -f "$SUDOERS"
  rm -rf "$DEST"
  echo "removed $SUDOERS and $DEST"
  exit 0
fi

[[ -n "$USER_NAME" && "$USER_NAME" != root ]] || { echo "give the account with --user <login>" >&2; exit 1; }
id -u "$USER_NAME" >/dev/null 2>&1 || { echo "no such account: $USER_NAME" >&2; exit 1; }
command -v visudo >/dev/null || { echo "visudo not found" >&2; exit 1; }
command -v rsync >/dev/null || echo "WARN: rsync is not installed; forever-sim-deploy will not work until it is" >&2

WRAPPERS=(forever-sim-ctl forever-sim-deploy forever-sim-db forever-sim-logs forever-sim-soak forever-sim-profile)
SOAK="$HERE/../start-sim-soak.sh"

for f in "${WRAPPERS[@]}" ; do
  [[ -f "$HERE/$f" && ! -L "$HERE/$f" ]] || { echo "missing or a link: $HERE/$f" >&2; exit 1; }
done
[[ -f "$SOAK" && ! -L "$SOAK" ]] || { echo "missing or a link: $SOAK" >&2; exit 1; }
[[ -f "$HERE/forever-agent.sudoers" ]] || { echo "missing: $HERE/forever-agent.sudoers" >&2; exit 1; }

# Check the sudoers text before it goes anywhere near /etc/sudoers.d.
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
sed "s/AGENT_USER/$USER_NAME/" "$HERE/forever-agent.sudoers" > "$tmp"
visudo -cf "$tmp" >/dev/null || { echo "sudoers check failed; nothing installed" >&2; exit 1; }

install -d -o root -g root -m 0755 "$DEST"
for f in "${WRAPPERS[@]}"; do
  install -o root -g root -m 0755 "$HERE/$f" "$DEST/$f"
done
install -o root -g root -m 0755 "$SOAK" "$DEST/start-sim-soak.sh"
install -o root -g root -m 0440 "$tmp" "$SUDOERS"

echo "installed for $USER_NAME:"
ls -l "$DEST"
echo
echo "sudo -l -U $USER_NAME shows the rules:"
sudo -l -U "$USER_NAME" | sed -n '/may run/,$p'
