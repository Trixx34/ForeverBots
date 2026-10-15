#!/usr/bin/env bash
# Adds the bots' live numbers to Telegraf: installs the exec script, lets the telegraf account read the sim's log folder (read-only, an ACL),
# writes /etc/telegraf/telegraf.d/forever-bots.conf and restarts Telegraf. Run as root:  sudo bash install-telegraf-bots.sh
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v setfacl >/dev/null || { echo "setfacl is missing (apt install acl)" >&2; exit 1; }
id telegraf >/dev/null 2>&1 || { echo "no telegraf account: install Telegraf first" >&2; exit 1; }

install -o root -g root -m 0755 "$HERE/forever-bots-live-metrics" /usr/local/bin/forever-bots-live-metrics

# read access to the folder (new files too), and to the file that exists now
setfacl -m u:telegraf:rx /var/log/forever/sim
setfacl -d -m u:telegraf:rx /var/log/forever/sim
[[ -f /var/log/forever/sim/bots-live.json ]] && setfacl -m u:telegraf:r /var/log/forever/sim/bots-live.json || true

cat > /etc/telegraf/telegraf.d/forever-bots.conf <<'CONF'
# The sim's bots from the live file (see /usr/local/bin/forever-bots-live-metrics).
[[inputs.exec]]
  commands = ["/usr/local/bin/forever-bots-live-metrics /var/log/forever/sim/bots-live.json sim"]
  timeout = "5s"
  data_format = "influx"
CONF

telegraf --test --config /etc/telegraf/telegraf.conf --config-directory /etc/telegraf/telegraf.d 2>&1 | grep -E "forever_bots|^E!" | head -3 || true
systemctl restart telegraf
sleep 3
systemctl is-active telegraf
curl -s localhost:9273/metrics | grep -m3 "forever_bots_" || echo "no forever_bots metrics yet (is the sim running?)"
