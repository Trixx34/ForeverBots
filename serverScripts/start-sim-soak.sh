#!/usr/bin/env bash
# Start a timed bot soak run on the sim.
# Run as root:  sudo bash start-sim-soak.sh [--bots 200] [--minutes 90] [--seed 20261014] [--dry-run]
#
#   --bots N       bots to reach (default 200). They are added in steps, see below.
#   --minutes M    length of the run (default 90). The core shuts the sim down cleanly at the end
#                  (Bot.Run.StopTime), so bots are saved and logged out.
#   --seed S       seed for the random race/class picks (default 20261014): the same seed gives the same bots.
#   --data-dir D   folder with starter_quests.txt (or world_quests.txt) and grind_spots.txt, and rotations.txt if present (default: the bots repo clone).
#   --dry-run      print what would be written and the stop time, change nothing (works without root).
#
# What it changes (sim only, never prod):
#   - /srv/forever/sim/conf/worldserver.conf.d/modules.d/forever-bots.conf  (the previous file is kept as .bak)
#   - Bot.Run.StopTime in /srv/forever/sim/conf/worldserver.conf
#   - copies the data files to /srv/forever/sim/modules_runtime/forever-bots/data
#   - moves the old telemetry file aside, then restarts forever-sim
#
# Ramp: starts with 50 bots, adds 50 every 5 minutes, up to --bots.
set -euo pipefail

BOTS=200
MINUTES=90
SEED=20261014
DATA_DIR=/srv/forever/build/clean/bots/data
DRY=0

while (( $# )); do
  case "$1" in
    --bots)     BOTS="${2:?--bots needs a number}"; shift ;;
    --minutes)  MINUTES="${2:?--minutes needs a number}"; shift ;;
    --seed)     SEED="${2:?--seed needs a number}"; shift ;;
    --data-dir) DATA_DIR="${2:?--data-dir needs a folder}"; shift ;;
    --dry-run)  DRY=1 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

[[ "$BOTS" =~ ^[0-9]+$ && "$MINUTES" =~ ^[0-9]+$ && "$SEED" =~ ^[0-9]+$ ]] || { echo "numbers only" >&2; exit 2; }
(( BOTS >= 1 && BOTS <= 1500 )) || { echo "--bots must be 1..1500" >&2; exit 2; }
(( MINUTES >= 5 && MINUTES <= 1380 )) || { echo "--minutes must be 5..1380 (the stop time is a clock time, so a run stays under one day)" >&2; exit 2; }

SIM=/srv/forever/sim
CONF=$SIM/conf/worldserver.conf
MODCONF=$SIM/conf/worldserver.conf.d/modules.d/forever-bots.conf
MODDIR=$SIM/modules_runtime/forever-bots
LOGDIR=/var/log/forever/sim
TELEMETRY=$LOGDIR/forever-bots-telemetry.csv

# The whole-world quests and the rotations file are used when they are in the data folder (tools/prepare_soak.sh makes them);
# without them the run keeps the starter quests and the built-in rotations.
QUEST_NAME=starter_quests.txt
[[ -f "$DATA_DIR/world_quests.txt" ]] && QUEST_NAME=world_quests.txt
DATA_FILES=("$DATA_DIR/$QUEST_NAME" "$DATA_DIR/grind_spots.txt")
ROTATION_LINE=""
if [[ -f "$DATA_DIR/rotations.txt" ]]; then
  DATA_FILES+=("$DATA_DIR/rotations.txt")
  ROTATION_LINE="Module.forever-bots.RotationFile = $MODDIR/data/rotations.txt"
fi

END="$(date -d "+${MINUTES} minutes" +%H:%M)"
INITIAL=$(( BOTS < 50 ? BOTS : 50 ))

MODULE_SETTINGS="# Written by start-sim-soak.sh on $(date '+%Y-%m-%d %H:%M'): $BOTS bots, $MINUTES minutes, stop $END.
Module.forever-bots.MaxBots = $BOTS
Module.forever-bots.Race = random
Module.forever-bots.Class = random
Module.forever-bots.RandomSeed = $SEED
Module.forever-bots.BotsInitial = $INITIAL
Module.forever-bots.BotsStep = 50
Module.forever-bots.BotsStepMinutes = 5
Module.forever-bots.BotsCap = $BOTS
Module.forever-bots.BotsStopTime = $END
Module.forever-bots.TelemetryFile = $TELEMETRY
Module.forever-bots.TelemetrySummarySec = 60
Module.forever-bots.ScenarioResultFile = $LOGDIR/scenario-results.log
Module.forever-bots.QuestFile = $MODDIR/data/$QUEST_NAME
Module.forever-bots.GrindFile = $MODDIR/data/grind_spots.txt
${ROTATION_LINE}
Module.forever-bots.AutoGearing = 1
Module.forever-bots.ServiceEverySec = 600
Module.forever-bots.TrainEverySec = 600"

if (( DRY )); then
  echo "stop time: $END (in $MINUTES minutes)"
  echo "--- $MODCONF would contain:"
  echo "$MODULE_SETTINGS"
  echo "--- $CONF: Bot.Run.StopTime = $END"
  exit 0
fi

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

for f in "$CONF" "$SIM/modules_runtime/forever-bots/forever-bots" "${DATA_FILES[@]}"; do
  [[ -e "$f" ]] || { echo "missing: $f" >&2; exit 1; }
done
# This script runs as root; the data files may be written by a normal user. Copy them to a root-only folder (links are copied as
# links, then refused) and use only the copies from here on.
STAGE="$(mktemp -d /tmp/forever-sim-soak.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
STAGED=()
for f in "${DATA_FILES[@]}"; do
  cp -P --no-preserve=all "$f" "$STAGE/" 2>/dev/null || { echo "cannot copy: $f" >&2; exit 1; }
  c="$STAGE/$(basename "$f")"
  [[ -f "$c" && ! -L "$c" ]] || { echo "not a regular file: $f" >&2; exit 1; }
  STAGED+=("$c")
done
grep -qE '^Modules\.Enabled *= *1' "$CONF" || { echo "Modules.Enabled is not 1 in $CONF" >&2; exit 1; }

systemctl stop forever-sim

# Data files for the module.
install -d -o forever-sim -g forever-sim -m 0750 "$MODDIR/data"
install -o forever-sim -g forever-sim -m 0640 "${STAGED[@]}" "$MODDIR/data/"

# Module settings.
[[ -f "$MODCONF" ]] && cp -a "$MODCONF" "$MODCONF.bak"
printf '%s\n' "$MODULE_SETTINGS" > "$MODCONF"
chown forever-sim:forever-sim "$MODCONF"
chmod 0640 "$MODCONF"

# Core stop time.
sed -i -E "s/^Bot\.Run\.StopTime *=.*/Bot.Run.StopTime = $END/" "$CONF"
grep -qE '^Bot\.Run\.StopTime' "$CONF" || echo "Bot.Run.StopTime = $END" >> "$CONF"
grep -E '^Bot\.Run\.StopTime' "$CONF"

# Keep the last run's telemetry.
if [[ -s "$TELEMETRY" ]]; then
  mv "$TELEMETRY" "$TELEMETRY.$(date +%Y%m%d-%H%M%S)"
fi

systemctl start forever-sim

echo
echo "soak started: $BOTS bots, ends $END (about $MINUTES minutes). The sim shuts itself down at the end."
echo "watch:   journalctl -u forever-sim -f"
echo "report:  sudo python3 -I /srv/forever/build/clean/bots/tools/soak_report.py --telemetry $TELEMETRY --server-log $LOGDIR/Server.log --hours 1.5"
