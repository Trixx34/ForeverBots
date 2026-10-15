"""Tests for the pure parts of the server scripts. Run:  python3 -m unittest serverScripts/test_scripts.py -v  (nothing here touches the servers)."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
METRICS = os.path.join(HERE, "telegraf", "forever-bots-live-metrics")
SOAK = os.path.join(HERE, "start-sim-soak.sh")


def run_metrics(payload=None, raw=None, age_s=0):
    """Runs the Telegraf exec script on a live file and returns its one output line."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "bots-live.json")
        if payload is not None:
            payload.setdefault("time_ms", int((time.time() - age_s) * 1000))
            with open(path, "w") as f:
                json.dump(payload, f)
        elif raw is not None:
            with open(path, "w") as f:
                f.write(raw)
        out = subprocess.run([sys.executable, "-I", METRICS, path, "sim"], capture_output=True, text=True, check=True)
        return out.stdout.strip()


def fields(line):
    return dict(kv.split("=") for kv in line.split(" ", 1)[1].split(","))


class BotsLiveMetrics(unittest.TestCase):
    def test_counts_and_types(self):
        line = run_metrics({"bots": [{"level": 10, "hp": 5, "combat": True}, {"level": 20, "hp": 0}, {"level": 1}], "tick": 7})
        f = fields(line)
        self.assertEqual(f["online"], "3i")
        self.assertEqual(f["dead"], "1i")            # only a bot with an hp key of 0; one without hp is not counted
        self.assertEqual(f["in_combat"], "1i")
        self.assertEqual(f["max_level"], "20i")
        self.assertEqual(f["level1"], "1i")
        self.assertEqual(f["tick"], "7i")
        self.assertNotIn("i", f["avg_level"])         # always a float, never an integer field

    def test_empty_list_keeps_float_type(self):
        f = fields(run_metrics({"bots": [], "tick": 1}))
        self.assertEqual(f["online"], "0i")
        self.assertEqual(f["avg_level"], "0.0")

    def test_bad_level_reports_stale_instead_of_failing(self):
        line = run_metrics({"bots": [{"level": "?"}], "tick": 1})
        self.assertIn("online=0i,stale=1i", line)

    def test_missing_file_and_old_file(self):
        self.assertIn("online=0i,stale=1i", run_metrics())
        self.assertIn("stale=1i", run_metrics({"bots": [{"level": 5}]}, age_s=120))


class SoakDryRun(unittest.TestCase):
    def dry(self, *args):
        return subprocess.run(["bash", SOAK, "--dry-run", *args], capture_output=True, text=True)

    def test_accepts_a_normal_run(self):
        r = self.dry("--bots", "50", "--minutes", "90")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("stop time:", r.stdout)

    def test_minutes_limits(self):
        self.assertNotEqual(self.dry("--minutes", "4").returncode, 0)
        self.assertEqual(self.dry("--minutes", "1380").returncode, 0)
        self.assertNotEqual(self.dry("--minutes", "1381").returncode, 0)
        self.assertNotEqual(self.dry("--minutes", "1500").returncode, 0)

    def test_rejects_bad_numbers(self):
        self.assertNotEqual(self.dry("--bots", "0").returncode, 0)
        self.assertNotEqual(self.dry("--bots", "1501").returncode, 0)
        self.assertNotEqual(self.dry("--seed", "abc").returncode, 0)


if __name__ == "__main__":
    unittest.main()
