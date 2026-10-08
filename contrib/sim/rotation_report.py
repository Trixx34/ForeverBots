#!/usr/bin/env python3
"""Per-class rotation report from the bot log DB (DUMMY_SUMMARY and CAST_FAILED rows).

Usage:
  rotation_report.py plan [--seconds 60]                  print the party-chat lines for one dummy run per class
  rotation_report.py report [--since "2026-10-08 12:00"]  print the report (needs the mysql client on PATH)
        [--db forever_botlog] [--host H] [--user U] [--password P] [--worldlog Server.log] [--json out.json]

Read-only on the DB. Standard library only.
"""
import argparse, collections, json, re, statistics, subprocess, sys

CLASSES = {1: "warrior", 2: "paladin", 3: "hunter", 4: "rogue", 5: "priest", 6: "death knight", 7: "shaman",
           8: "mage", 9: "warlock", 11: "druid"}
CASTERS = {5, 8, 9}        # melee swings (spell id 0) above MELEE_CASTER_PCT mean the class is not casting enough
MELEE_CASTER_PCT = 40.0
GAP_PCT = 30.0             # longest idle gap above this share of the run
FIRST_HIT_MS = 6000
HEALER_ONLY = {5}          # priest healing is not measured, low dps is not a finding for a holy priest


def plan(seconds):
    print("# Paste into party chat, one block per class, with the bots grouped and at the level to test.")
    print("# Wait duration + ~10 s between lines. Run once with Bot.AI.Rotation.Enabled=0 and once with =1.")
    for c in CLASSES.values():
        print(f"{c.replace(' ', '')} dummy {seconds}")


def query(a, sql):
    cmd = ["mysql", "--batch", "--raw", "--skip-column-names", "-h", a.host, "-u", a.user, a.db]
    if a.password:
        cmd.insert(1, f"-p{a.password}")
    out = subprocess.run(cmd, input=sql, capture_output=True, text=True)
    if out.returncode:
        sys.exit(out.stderr.strip())
    return [l.split("\t", 3) for l in out.stdout.splitlines() if l]


def fetch(a):
    since = f"AND e.ts >= '{a.since}'" if a.since else ""
    # bot_event_all sees bot_event and bot_event_hot (decision rows may be in the hot table)
    rows = query(a, f"""
SELECT e.reason, b.class_id, e.level, CAST(e.details AS CHAR)
FROM bot_event_all e JOIN bot b ON b.guid = e.bot_guid
WHERE e.event_type = 'decision' AND e.reason IN ('DUMMY_SUMMARY','CAST_FAILED') {since}
ORDER BY e.ts;""")
    runs, fails = [], collections.defaultdict(collections.Counter)
    for reason, cls, lvl, det in rows:
        try:
            d = json.loads(det)
        except ValueError:
            continue
        cls = int(cls)
        if reason == "DUMMY_SUMMARY":
            d["class"] = cls
            runs.append(d)
        else:
            fails[cls][(d.get("spell_name", "?"), d.get("result_name", "?"))] += 1
    return runs, fails


def analyse(runs, fails):
    out = []
    groups = collections.defaultdict(list)
    for r in runs:
        groups[(r["class"], bool(r.get("rotation")))].append(r)
    for cls in sorted({k[0] for k in groups}):
        entry = {"class": CLASSES.get(cls, str(cls)), "class_id": cls, "modes": {}, "hints": []}
        for rot in (False, True):
            g = groups.get((cls, rot))
            if not g:
                continue
            ok = [r for r in g if r.get("end") in ("done", "stopped")]
            dps = [r["dps"] for r in ok] or [0.0]
            spells = collections.defaultdict(lambda: [0, 0])
            for r in ok:
                for s in r.get("spells", []):
                    key = "melee" if s["spell"] == 0 else (s["name"] or str(s["spell"]))
                    spells[key][0] += s["hits"]
                    spells[key][1] += s["damage"]
            total = sum(v[1] for v in spells.values()) or 1
            entry["modes"]["on" if rot else "off"] = {
                "runs": len(g), "valid_runs": len(ok), "levels": sorted({r["level"] for r in g}),
                "dps_mean": round(statistics.mean(dps), 1), "dps_median": round(statistics.median(dps), 1),
                "dps_min": min(dps), "dps_max": max(dps),
                "gap_pct": round(100 * statistics.mean([r["longest_gap_ms"] / max(r["duration_ms"], 1) for r in ok]), 1) if ok else 0,
                "first_hit_ms": round(statistics.mean([r["first_hit_ms"] for r in ok])) if ok else 0,
                "ended_early": len(g) - len(ok),
                "spells": sorted(((n, h, d, round(100 * d / total, 1)) for n, (h, d) in spells.items()), key=lambda x: -x[2]),
            }
        on, off = entry["modes"].get("on"), entry["modes"].get("off")
        hints = entry["hints"]
        if on and off and off["dps_mean"] > 0:
            delta = 100 * (on["dps_mean"] - off["dps_mean"]) / off["dps_mean"]
            entry["dps_delta_pct"] = round(delta, 1)
            if delta < 5:
                hints.append(f"rotation on is {delta:+.0f}% vs off: the table adds little, check dropped/failed spells")
        m = on or off
        if m and cls not in HEALER_ONLY:
            melee = sum(p for n, _, _, p in m["spells"] if n == "melee")
            if cls in CASTERS and melee > MELEE_CASTER_PCT:
                hints.append(f"{melee:.0f}% of damage is melee swings: spells are not firing (see CAST_FAILED below)")
            if m["gap_pct"] > GAP_PCT:
                hints.append(f"idle {m['gap_pct']:.0f}% of the run (longest gap): cooldown gaps or out of resources")
            if m["first_hit_ms"] > FIRST_HIT_MS:
                hints.append(f"first hit after {m['first_hit_ms']/1000:.1f} s: approach or opener is slow")
            if m["spells"] and m["spells"][0][3] > 80 and len(m["spells"]) > 1:
                hints.append(f"{m['spells'][0][0]} is {m['spells'][0][3]:.0f}% of damage: other rows rarely fire")
        if m and m["ended_early"]:
            hints.append(f"{m['ended_early']} run(s) ended early (bot_died or dummy_gone)")
        entry["cast_failed"] = [(s, r, n) for (s, r), n in fails.get(cls, collections.Counter()).most_common(8)]
        out.append(entry)
    return out


def dropped_lines(path):
    pat = re.compile(r"class spells validated, dropped:(.*)")
    last = None
    with open(path, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                last = m.group(1).strip()
    return last


def render(res, dropped):
    print("# Rotation report\n")
    if dropped is not None:
        print(f"Spells dropped at startup (fix these ids first): {dropped}\n")
    if not res:
        print("No DUMMY_SUMMARY rows found. Check Bot.AI.Dummy.Enabled, the world SQL, and --since.")
    for e in res:
        print(f"## {e['class']}")
        for mode in ("off", "on"):
            m = e["modes"].get(mode)
            if m:
                print(f"- rotation {mode}: {m['valid_runs']}/{m['runs']} runs, levels {m['levels']}, dps mean {m['dps_mean']} "
                      f"(median {m['dps_median']}, {m['dps_min']}-{m['dps_max']}), idle {m['gap_pct']}%, first hit {m['first_hit_ms']} ms")
        if "dps_delta_pct" in e:
            print(f"- rotation on vs off: {e['dps_delta_pct']:+}% dps")
        m = e["modes"].get("on") or e["modes"].get("off")
        if m and m["spells"]:
            print("- damage by spell (name, hits, damage, %): " + "; ".join(f"{n} {h}/{d}/{p}%" for n, h, d, p in m["spells"][:8]))
        if e["cast_failed"]:
            print("- cast failures: " + "; ".join(f"{s}: {r} x{n}" for s, r, n in e["cast_failed"]))
        for h in e["hints"] or ["nothing flagged"]:
            print(f"- TUNE: {h}")
        print()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    pl = sub.add_parser("plan")
    pl.add_argument("--seconds", type=int, default=60)
    r = sub.add_parser("report")
    r.add_argument("--db", default="forever_botlog")
    r.add_argument("--host", default="127.0.0.1")
    r.add_argument("--user", default="forever_bot")
    r.add_argument("--password", default="")
    r.add_argument("--since", default="", help="only rows from this time, e.g. '2026-10-08 12:00'")
    r.add_argument("--worldlog", default="", help="worldserver log file, to show the 'dropped:' line")
    r.add_argument("--json", default="", help="also write the result as JSON")
    a = p.parse_args()
    if a.cmd == "plan":
        return plan(a.seconds)
    if not re.fullmatch(r"[0-9: \-]*", a.since):
        sys.exit("bad --since")
    runs, fails = fetch(a)
    res = analyse(runs, fails)
    render(res, dropped_lines(a.worldlog) if a.worldlog else None)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1)


if __name__ == "__main__":
    main()
